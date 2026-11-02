/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * RX/TX journal, after the F4HWN RF Log (app/rxtx_log.c): same external-flash
 * area, same 32-byte records, same sequence/commit/CRC rules. The screen is
 * the Falcon history page (app/spectrum.c). Only the session tracking stays
 * resident: start-up scan, record writing, row lookup and clearing run from
 * the journal module.
 */

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "app/rxtx_log.h"
#include "apps/app_overlay.h"
#include "driver/bk4819.h"
#include "driver/py25q16.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
#include "ui/multiboot.h"
#endif

#define RXTX_LOG_FLASH_BASE         0x018000u
#define RXTX_LOG_FLASH_SECTOR_SIZE  0x1000u
#define RXTX_LOG_FLASH_SECTOR_COUNT 8u
#define RXTX_LOG_FLASH_SIZE         (RXTX_LOG_FLASH_SECTOR_SIZE * RXTX_LOG_FLASH_SECTOR_COUNT)
#define RXTX_LOG_FLASH_END          (RXTX_LOG_FLASH_BASE + RXTX_LOG_FLASH_SIZE)
#define RXTX_LOG_SLOT_COUNT         (RXTX_LOG_FLASH_SIZE / sizeof(RXTX_LogFlashEntry_t))
#define RXTX_LOG_ENTRY_COMMIT       0xA5u
// battVolt stores centivolts above 6.00 V, saturating at 8.54 V (254).
#define RXTX_LOG_BATT_UNKNOWN 0xFFu
#define RXTX_LOG_BATT_OFFSET  600u

typedef struct __attribute__((packed)) {
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t flags;
    uint8_t sMeter;
    uint8_t battVolt;
    uint8_t pad; // written as 0xFF like reserved; keeps sequence aligned
    uint32_t sequence;
    uint8_t reserved[10];
    uint8_t crc;
    uint8_t commit;
} RXTX_LogFlashEntry_t;

static_assert(sizeof(RXTX_LogFlashEntry_t) == 32);
static_assert(offsetof(RXTX_LogFlashEntry_t, sequence) % 4 == 0);
static_assert(RXTX_LOG_FLASH_END == 0x020000u);

#define RXTX_LOG_ENTRY_COPY_SIZE offsetof(RXTX_LogFlashEntry_t, pad)
static_assert(RXTX_LOG_ENTRY_COPY_SIZE ==
              offsetof(RXTX_LogEntry_t, battVolt) + sizeof(((RXTX_LogEntry_t *)0)->battVolt));

static bool gLogReady; // set by the start-up scan; nothing is written before it
static bool gSuspended;
static uint32_t gNextSequence;
static uint32_t gNextTrafficSequence;
static uint32_t gNextFlashAddress;

static bool gSessionActive;
static uint8_t gSessionFlags;
static uint8_t gSessionSMeter;
static uint8_t gSessionBattVolt;
static uint16_t gSessionChannel;
static uint32_t gSessionFrequency;
static uint32_t gSessionStartTick;
static uint8_t gSessionProfile;
static uint8_t gSessionCodeType;
static uint8_t gSessionCode;

static void RXTX_LOG_EndActiveNative(void);

static void RXTX_LOG_UpdateSessionMeters(void)
{
    if (!gSessionActive)
        return;

    // Track the lowest battery voltage seen during the session: under TX
    // load this is the sag, which rebounds as soon as the PA keys off.
    const uint16_t volt = gBatteryVoltageAverage;
    const uint8_t battVolt = volt <= RXTX_LOG_BATT_OFFSET ? 0 : (uint8_t)MIN(volt - RXTX_LOG_BATT_OFFSET, 254u);
    if (battVolt < gSessionBattVolt)
        gSessionBattVolt = battVolt;

    if ((gSessionFlags & RXTX_LOG_FLAG_TX) != 0)
        return;

    const int16_t rssiDbm = BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band];
    const uint8_t sMeter = rssiDbm >= -93 ? (uint8_t)(9u + MIN((uint8_t)(rssiDbm + 93), 40u))
                                          : (rssiDbm < -141 ? 0 : (uint8_t)((rssiDbm + 147) / 6));

    if (gSessionSMeter == RXTX_LOG_SMETER_UNKNOWN || sMeter > gSessionSMeter)
        gSessionSMeter = sMeter;
}

static void RXTX_LOG_CaptureSession(uint8_t flags, const VFO_Info_t *vfo)
{
    if (!gLogReady || gSuspended || vfo == NULL)
        return;

    const uint32_t frequency = (flags & RXTX_LOG_FLAG_TX) ? vfo->pTX->Frequency : vfo->pRX->Frequency;
    const uint16_t channel = IS_MR_CHANNEL(vfo->CHANNEL_SAVE) ? vfo->CHANNEL_SAVE : RXTX_LOG_CHANNEL_NONE;

    if (gSessionActive && gSessionFlags == flags && gSessionFrequency == frequency &&
        gSessionChannel == channel)
        return;

    RXTX_LOG_EndActive();

    gSessionActive = true;
    gSessionFlags = flags;
    gSessionFrequency = frequency;
    gSessionChannel = channel;
    gSessionStartTick = gGlobalSysTickCounter;
    // TX sessions repurpose the sMeter byte to store the TX power level
    // (OUTPUT_POWER); RX sessions track the S-meter.
    gSessionSMeter = (flags & RXTX_LOG_FLAG_TX) ? vfo->OUTPUT_POWER : RXTX_LOG_SMETER_UNKNOWN;
    gSessionBattVolt = RXTX_LOG_BATT_UNKNOWN;
    // Settings handed back to the VFO by long 1 on the journal row.
    const FREQ_Config_t *config = (flags & RXTX_LOG_FLAG_TX) ? vfo->pTX : vfo->pRX;
    gSessionProfile = (uint8_t)(vfo->Modulation | (vfo->CHANNEL_BANDWIDTH << 2));
    gSessionCodeType = config->CodeType;
    gSessionCode = config->Code;
    RXTX_LOG_UpdateSessionMeters();
}

void RXTX_LOG_BeginRx(const VFO_Info_t *vfo, FUNCTION_Type_t function)
{
    RXTX_LOG_CaptureSession(function == FUNCTION_MONITOR ? RXTX_LOG_FLAG_MONITOR : 0, vfo);
}

void RXTX_LOG_BeginTx(const VFO_Info_t *vfo)
{
    RXTX_LOG_CaptureSession(RXTX_LOG_FLAG_TX, vfo);
}

void RXTX_LOG_EndActive(void)
{
    if (!gSessionActive)
        return;

    gSessionActive = false;
    RXTX_LOG_UpdateSessionMeters();
    if (APP_NativeBegin(NATIVE_JOURNAL)) {
        RXTX_LOG_EndActiveNative();
        APP_NativeEnd();
    }
}

void RXTX_LOG_Tick500ms(void)
{
    RXTX_LOG_UpdateSessionMeters();
}

void RXTX_LOG_Suspend(void)
{
    RXTX_LOG_EndActive();
    gSuspended = true;
}

void RXTX_LOG_Resume(void)
{
    gSuspended = false;
}

// ---- Journal module ---------------------------------------------------------

static NATIVE_CODE(journal) uint8_t RXTX_LOG_Crc8(const void *data, uint16_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t crc = 0x5Au;

    while (size-- > 0) {
        crc ^= *p++;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
    }

    return crc;
}

static NATIVE_CODE(journal) bool RXTX_LOG_IsBlankFlashEntry(const RXTX_LogFlashEntry_t *entry)
{
    const uint8_t *p = (const uint8_t *)entry;

    for (uint8_t i = 0; i < sizeof(*entry); i++)
        if (p[i] != 0xFFu)
            return false;

    return true;
}

static NATIVE_CODE(journal) bool RXTX_LOG_SlotIsBlank(uint32_t address)
{
    RXTX_LogFlashEntry_t entry;

    PY25Q16_ReadBuffer(address, &entry, sizeof(entry));

    return RXTX_LOG_IsBlankFlashEntry(&entry);
}

static NATIVE_CODE(journal) void RXTX_LOG_PrepareNextSlot(void)
{
    if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
        gNextFlashAddress = RXTX_LOG_FLASH_BASE;

    if (!RXTX_LOG_SlotIsBlank(gNextFlashAddress)) {
        if ((gNextFlashAddress % RXTX_LOG_FLASH_SECTOR_SIZE) != 0) {
            gNextFlashAddress +=
                RXTX_LOG_FLASH_SECTOR_SIZE - (gNextFlashAddress % RXTX_LOG_FLASH_SECTOR_SIZE);
            if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
                gNextFlashAddress = RXTX_LOG_FLASH_BASE;
        }

        if (!RXTX_LOG_SlotIsBlank(gNextFlashAddress))
            PY25Q16_SectorErase(gNextFlashAddress);
    }
}

static NATIVE_CODE(journal) void RXTX_LOG_WriteEntry(const RXTX_LogEntry_t *src)
{
    RXTX_LogFlashEntry_t entry;
    const uint8_t commit = RXTX_LOG_ENTRY_COMMIT;

    memset(&entry, 0xFF, sizeof(entry));
    memcpy(&entry, src, RXTX_LOG_ENTRY_COPY_SIZE);
    entry.reserved[0] = src->profile;
    entry.reserved[1] = src->codeType;
    entry.reserved[2] = src->code;
    entry.sequence = gNextSequence++;
    entry.crc = RXTX_LOG_Crc8(&entry, sizeof(entry) - 2);

    RXTX_LOG_PrepareNextSlot();

    PY25Q16_WriteBuffer(gNextFlashAddress, &entry, sizeof(entry), false);
    PY25Q16_WriteBuffer(gNextFlashAddress + sizeof(entry) - 1u, &commit, 1, false);
    gNextFlashAddress += sizeof(entry);
    if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
        gNextFlashAddress = RXTX_LOG_FLASH_BASE;
}

static NATIVE_CODE(journal) void RXTX_LOG_EndActiveNative(void)
{

    // 10 ms system ticks; a squelch opening shorter than one second is noise.
    const uint32_t seconds = (gGlobalSysTickCounter - gSessionStartTick) / 100u;
    if (!seconds && !(gSessionFlags & RXTX_LOG_FLAG_TX))
        return;

    RXTX_LogEntry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.trafficSeq = gNextTrafficSequence++;
    entry.frequency = gSessionFrequency;
    entry.durationSeconds = seconds ? (uint16_t)MIN(seconds, 0xFFFFu) : 1;
    entry.channel = gSessionChannel;
    entry.flags = gSessionFlags;
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    entry.flags |= (uint8_t)((MB_GetActiveBank() + 1u) << RXTX_LOG_CONFIG_BANK_SHIFT);
#endif
    entry.sMeter = gSessionSMeter;
    entry.battVolt = gSessionBattVolt;
    entry.profile = gSessionProfile;
    entry.codeType = gSessionCodeType;
    entry.code = gSessionCode;

    RXTX_LOG_WriteEntry(&entry);
}

static NATIVE_CODE(journal) bool RXTX_LOG_IsValidFlashEntry(const RXTX_LogFlashEntry_t *entry)
{
    if (entry->commit != RXTX_LOG_ENTRY_COMMIT || entry->sequence == 0xFFFFFFFFu ||
        entry->frequency == 0xFFFFFFFFu)
        return false;

    return entry->crc == RXTX_LOG_Crc8(entry, sizeof(*entry) - 2);
}

static NATIVE_CODE(journal) void RXTX_LOG_InitNative(void)
{
    uint32_t maxSequence = 0, maxAddress = RXTX_LOG_FLASH_BASE, maxTrafficSeq = 0;
    uint8_t lastEntryFlags = 0;
    bool found = false, foundTraffic = false;

    for (uint32_t address = RXTX_LOG_FLASH_BASE; address < RXTX_LOG_FLASH_END;
         address += sizeof(RXTX_LogFlashEntry_t)) {
        RXTX_LogFlashEntry_t flashEntry;

        PY25Q16_ReadBuffer(address, &flashEntry, sizeof(flashEntry));
        if (!RXTX_LOG_IsValidFlashEntry(&flashEntry))
            continue;

        if ((flashEntry.flags & RXTX_LOG_FLAG_SESSION) == 0 &&
            (!foundTraffic || flashEntry.trafficSeq > maxTrafficSeq)) {
            foundTraffic = true;
            maxTrafficSeq = flashEntry.trafficSeq;
        }

        if (!found || flashEntry.sequence > maxSequence) {
            found = true;
            maxSequence = flashEntry.sequence;
            maxAddress = address;
            lastEntryFlags = flashEntry.flags;
        }
    }

    gNextFlashAddress = RXTX_LOG_FLASH_BASE;
    gNextSequence = 0;
    if (found) {
        gNextSequence = maxSequence + 1u;
        gNextFlashAddress = maxAddress + sizeof(RXTX_LogFlashEntry_t);
        if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
            gNextFlashAddress = RXTX_LOG_FLASH_BASE;
    }
    gNextTrafficSequence = foundTraffic ? maxTrafficSeq + 1u : 0;
    gLogReady = true;

    // Skip the marker if the log already ends with one (e.g. repeated
    // reboots with no RX/TX in between) to avoid stacking empty separators.
    if (!found || (lastEntryFlags & RXTX_LOG_FLAG_SESSION) == 0) {
        RXTX_LogEntry_t entry;

        memset(&entry, 0, sizeof(entry));
        // Markers share the ordinal of the next traffic row.
        entry.trafficSeq = gNextTrafficSequence;
        entry.channel = RXTX_LOG_CHANNEL_NONE;
        entry.flags = RXTX_LOG_FLAG_SESSION;
        entry.sMeter = RXTX_LOG_SMETER_UNKNOWN;
        entry.battVolt = RXTX_LOG_BATT_UNKNOWN;
        entry.profile = entry.codeType = entry.code = RXTX_LOG_PROFILE_NONE;
        RXTX_LOG_WriteEntry(&entry);
    }
}

void RXTX_LOG_Init(void)
{
    if (APP_NativeBegin(NATIVE_JOURNAL)) {
        RXTX_LOG_InitNative();
        APP_NativeEnd();
    }
}

// Walks the view newest first: copies its rows [first, first + count) and
// invalidates row `remove` (its commit byte only loses bits: no erase).
static NATIVE_CODE(journal) uint16_t RXTX_LOG_Walk(uint8_t filter, uint16_t first, RXTX_LogEntry_t *rows,
                                                   uint8_t count, uint16_t remove)
{
    uint16_t total = 0;
    uint32_t address = gNextFlashAddress;

    for (uint16_t scanned = 0; gLogReady && scanned < RXTX_LOG_SLOT_COUNT; ++scanned) {
        RXTX_LogFlashEntry_t flashEntry;

        address = (address == RXTX_LOG_FLASH_BASE ? RXTX_LOG_FLASH_END : address) - sizeof(flashEntry);
        PY25Q16_ReadBuffer(address, &flashEntry, sizeof(flashEntry));
        if (RXTX_LOG_IsBlankFlashEntry(&flashEntry))
            break;
        if (!RXTX_LOG_IsValidFlashEntry(&flashEntry))
            continue;

        const uint8_t flags = flashEntry.flags;
        if (flags & RXTX_LOG_FLAG_SESSION) {
            if (filter != RXTX_LOG_FILTER_ALL)
                continue;
        } else {
            if ((gNextTrafficSequence - 1u - flashEntry.trafficSeq) >= RXTX_LOG_VISIBLE_COUNT)
                break;
            if (filter != RXTX_LOG_FILTER_ALL && !(flags & RXTX_LOG_FLAG_TX) != (filter == RXTX_LOG_FILTER_RX))
                continue;
        }

        if (total == remove) {
            static const uint8_t removed = 0;
            PY25Q16_WriteBuffer(address + sizeof(flashEntry) - 1u, &removed, 1, false);
            remove = UINT16_MAX;
            continue;
        }
        if (total >= first && total - first < count) {
            RXTX_LogEntry_t *row = &rows[total - first];
            memcpy(row, &flashEntry, RXTX_LOG_ENTRY_COPY_SIZE);
            row->profile = flashEntry.reserved[0];
            row->codeType = flashEntry.reserved[1];
            row->code = flashEntry.reserved[2];
        }
        ++total;
    }

    return total;
}

NATIVE_CODE(journal) uint16_t RXTX_LOG_Fetch(uint8_t filter, uint16_t first, RXTX_LogEntry_t *rows, uint8_t count)
{
    return RXTX_LOG_Walk(filter, first, rows, count, UINT16_MAX);
}

NATIVE_CODE(journal) void RXTX_LOG_Delete(uint8_t filter, uint16_t index)
{
    RXTX_LOG_Walk(filter, 0, NULL, 0, index);
}

NATIVE_CODE(journal) void RXTX_LOG_Clear(void)
{
    gSessionActive = false;
    for (uint8_t sector = 0; sector < RXTX_LOG_FLASH_SECTOR_COUNT; ++sector)
        PY25Q16_SectorErase(RXTX_LOG_FLASH_BASE + (uint32_t)sector * RXTX_LOG_FLASH_SECTOR_SIZE);
    gNextSequence = 0;
    gNextTrafficSequence = 0;
    gNextFlashAddress = RXTX_LOG_FLASH_BASE;
}

#endif
