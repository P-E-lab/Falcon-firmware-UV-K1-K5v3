#include "apps/app_overlay.h"
/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * OURO_KA52: simple FM, auto-seek only, no memory, no band switching
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifdef ENABLE_FMRADIO

#include <string.h>
#include "nav_invert.h"
#include "app/action.h"
#include "app/fm.h"
#include "app/generic.h"
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/py25q16.h"
#include "driver/system.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

// Fixed band: 87.5–108.0 MHz
#define FM_BAND 0

// ---- Globals (kept for settings.c / app.c / action.c) ----
uint16_t gFM_Channels[48];
bool gFmRadioMode;
uint8_t gFmRadioCountdown_500ms;
uint16_t gFM_RestoreCountdown_10ms;
bool gFM_ManualMode = false;
bool gFM_Mute = false;
bool gFM_No_Rx = false;
bool gFmNameDisplay = true;
uint8_t gFM_Favourite, gFM_SavedSlot, gFM_RSSI, gFM_Quality;
bool gFM_SignalValid;
static uint16_t fmMeasuredFrequency;
static uint8_t fmNoticeWait;

// ── FM nine-slot memory ──────────────────────────────────────────────────
// 0 = empty; otherwise, frequency (875..1080)
// EEPROM: 0xA070 (slots 0-3), 0xA078 (slots 4-7), and 0xA080 (slots 8-9)
// Stored in the unused part of the FM channels 0xA028..0xA0A7 (eeprom_compat)
#define FM_MEMORY_EEPROM_ADDR0 0xA070
#define FM_MEMORY_EEPROM_ADDR1 0xA078
#define FM_MEMORY_EEPROM_ADDR2 0xA080

uint16_t gFM_Memory[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

// ── FM radios list (50 × 15 bytes, written by CHIRP) ────────────────────
// Record: ul16 freq (0.1 MHz units, 0 = empty) + char name[15]
#define FM_RADIOS_MAX         50
#define FM_RADIOS_EEPROM_ADDR 0x008AF4
#define FM_RADIO_NAME_LEN     15
#define FM_RADIO_RECORD       (2 + FM_RADIO_NAME_LEN)

static char gFM_RadioName[FM_RADIO_NAME_LEN + 1]; // zero-terminated copy

const char *FM_FindRadioName(uint16_t freq)
{
    uint8_t buf[2];

    for (uint8_t i = 0; i < FM_RADIOS_MAX; i++) {
        PY25Q16_ReadBuffer(FM_RADIOS_EEPROM_ADDR + (uint16_t)i * FM_RADIO_RECORD, buf, 2);

        uint16_t f = buf[0] | ((uint16_t)buf[1] << 8);
        if (f != freq)
            continue;

        // Frequency match — load only this station's name
        PY25Q16_ReadBuffer(FM_RADIOS_EEPROM_ADDR + (uint16_t)i * FM_RADIO_RECORD + 2,
                           (uint8_t *)gFM_RadioName, FM_RADIO_NAME_LEN);

        uint8_t j;
        for (j = 0; j < FM_RADIO_NAME_LEN; j++) {
            char c = gFM_RadioName[j];
            if (c == '\0' || c == (char)0xFF)
                break;
            if (c < 32 || c > 126)
                gFM_RadioName[j] = ' ';
        }
        gFM_RadioName[j] = '\0';

        while (j > 0 && gFM_RadioName[j - 1] == ' ')
            j--;
        gFM_RadioName[j] = '\0';

        if (gFM_RadioName[0] != '\0')
            return gFM_RadioName;
        return NULL;
    }
    return NULL;
}

void FM_Memory_Load(void)
{
    const uint16_t lo = BK1080_GetFreqLoLimit(FM_BAND), hi = BK1080_GetFreqHiLimit(FM_BAND);
    uint8_t buf[8];
    for (uint8_t block = 0; block < 3; ++block) {
        PY25Q16_ReadBuffer(FM_MEMORY_EEPROM_ADDR0 + block * 8, buf, 8);
        const uint8_t count = block == 2 ? 1 : 4;
        for (uint8_t i = 0; i < count; ++i) {
            const uint16_t f = buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
            gFM_Memory[block * 4 + i] = f >= lo && f <= hi ? f : 0;
        }
    }
}

static uint8_t pendingFavourite;
void FM_Memory_Save(uint8_t slot)
{
    if (APP_OverlayBusy()) {
        pendingFavourite = slot + 1;
        return;
    }
    if (slot >= 9)
        return;
    const uint32_t address = FM_MEMORY_EEPROM_ADDR0 + (slot / 4) * 8;
    const uint8_t offset = (slot % 4) * 2;
    uint8_t buf[8];
    PY25Q16_ReadBuffer(address, buf, 8);
    buf[offset] = gFM_Memory[slot];
    buf[offset + 1] = gFM_Memory[slot] >> 8;
    PY25Q16_WriteBuffer(address, buf, sizeof buf, false);
}

const uint8_t BUTTON_STATE_PRESSED = 1 << 0;
const uint8_t BUTTON_STATE_HELD = 1 << 1;
const uint8_t BUTTON_EVENT_HELD = BUTTON_STATE_PRESSED | BUTTON_STATE_HELD;
const uint8_t BUTTON_EVENT_SHORT = 0;

// ---- Compatibility state used by the existing application ----

int FM_ConfigureChannelState(void)
{
    gEeprom.FM_IsMrMode = false;
    gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
    return 0;
}


int FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit)
{
    int ret = -1;
    const uint16_t Test2 = BK1080_ReadRegister(BK1080_REG_07);
    const uint16_t Deviation = BK1080_REG_07_GET_FREQD(Test2);

    if (BK1080_REG_07_GET_SNR(Test2) <= 2) {
        BK1080_FrequencyDeviation = Deviation;
        BK1080_BaseFrequency = Frequency;
        return ret;
    }
    const uint16_t Status = BK1080_ReadRegister(BK1080_REG_10);
    if ((Status & BK1080_REG_10_MASK_AFCRL) != BK1080_REG_10_AFCRL_NOT_RAILED ||
        BK1080_REG_10_GET_RSSI(Status) < 10) {
        BK1080_FrequencyDeviation = Deviation;
        BK1080_BaseFrequency = Frequency;
        return ret;
    }
    if (Deviation >= 280 && Deviation <= 3815) {
        BK1080_FrequencyDeviation = Deviation;
        BK1080_BaseFrequency = Frequency;
        return ret;
    }
    if (Frequency > LowerLimit && (Frequency - BK1080_BaseFrequency) == 1) {
        if (BK1080_FrequencyDeviation & 0x800 || BK1080_FrequencyDeviation < 20) {
            BK1080_FrequencyDeviation = Deviation;
            BK1080_BaseFrequency = Frequency;
            return ret;
        }
    }
    if (Frequency >= LowerLimit && (BK1080_BaseFrequency - Frequency) == 1) {
        if ((BK1080_FrequencyDeviation & 0x800) == 0 || BK1080_FrequencyDeviation > 4075) {
            BK1080_FrequencyDeviation = Deviation;
            BK1080_BaseFrequency = Frequency;
            return ret;
        }
    }
    ret = 0;
    BK1080_FrequencyDeviation = Deviation;
    BK1080_BaseFrequency = Frequency;
    return ret;
}


// Seek advances one settled channel per foreground tick; keyboard/RX stay live.
static uint16_t fmSeekStart, fmSeekFrequency;
static uint8_t fmSeekWait;
static int8_t fmSeekDirection;
static bool fmArrowHeld;

void FM_CancelSeek(void)
{
    if (!fmSeekDirection)
        return;
    fmSeekDirection = 0;
    gFM_SignalValid = false;
    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, FM_BAND);
    if (gFmRadioMode && gCurrentFunction == FUNCTION_FOREGROUND) {
        GPIO_EnableAudioPath();
        gEnableSpeaker = true;
    }
}

static void FM_SeekNext(int8_t direction)
{
    FM_CancelSeek();
    fmSeekStart = fmSeekFrequency = gEeprom.FM_FrequencyPlaying;
    fmSeekDirection = direction;
    fmSeekWait = 0;
    gFM_Favourite = gFM_SavedSlot = 0;
    GPIO_DisableAudioPath();
    gEnableSpeaker = false;
}

static NATIVE_CODE(fm) void FM_SeekPoll(void)
{
    const uint16_t lo = BK1080_GetFreqLoLimit(FM_BAND), hi = BK1080_GetFreqHiLimit(FM_BAND);
    if (fmSeekFrequency != fmSeekStart && FM_CheckFrequencyLock(fmSeekFrequency, lo) == 0) {
        gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency = fmSeekFrequency;
        gRequestSaveFM = true;
        FM_CancelSeek();
        gRequestDisplayScreen = DISPLAY_FM;
        return;
    }
    fmSeekFrequency += fmSeekDirection;
    if (fmSeekFrequency < lo)
        fmSeekFrequency = hi;
    if (fmSeekFrequency > hi)
        fmSeekFrequency = lo;
    if (fmSeekFrequency == fmSeekStart) {
        FM_CancelSeek();
        return;
    }
    BK1080_SetFrequency(fmSeekFrequency, FM_BAND);
}

void FM_TimeSlice10ms(void)
{
    if (!fmSeekDirection)
        return;
    if (!gFmRadioMode || gScreenToDisplay != DISPLAY_FM || gCurrentFunction != FUNCTION_FOREGROUND ||
        gFM_RestoreCountdown_10ms) {
        FM_CancelSeek();
        return;
    }
    if (fmSeekWait && --fmSeekWait)
        return;
    fmSeekWait = 10;
    if (APP_NativeBegin(NATIVE_FM)) {
        FM_SeekPoll();
        APP_NativeEnd();
    }
    if (gRequestSaveFM) {
        SETTINGS_SaveFM();
        gRequestSaveFM = false;
    }
}

void FM_TimeSlice500ms(void)
{
    if (fmSeekDirection || !gFmRadioMode || gScreenToDisplay != DISPLAY_FM ||
        gCurrentFunction != FUNCTION_FOREGROUND || gFM_RestoreCountdown_10ms)
        return;
    if (!gFM_SignalValid || fmMeasuredFrequency != gEeprom.FM_FrequencyPlaying) {
        gFM_RSSI = BK1080_REG_10_GET_RSSI(BK1080_ReadRegister(BK1080_REG_10));
        gFM_Quality = BK1080_REG_07_GET_SNR(BK1080_ReadRegister(BK1080_REG_07));
        fmMeasuredFrequency = gEeprom.FM_FrequencyPlaying;
        gFM_SignalValid = true;
        gUpdateDisplay = true;
    }
    if (fmNoticeWait && --fmNoticeWait == 0) {
        gFM_SavedSlot = 0;
        gUpdateDisplay = true;
    }
}

// ---- Manual frequency step: 0.1 MHz ----

static void FM_StepFreq(int8_t direction)
{
    gFM_Favourite = gFM_SavedSlot = 0;
    const uint16_t lo = BK1080_GetFreqLoLimit(FM_BAND);
    const uint16_t hi = BK1080_GetFreqHiLimit(FM_BAND);
    uint16_t freq = gEeprom.FM_FrequencyPlaying + direction;
    if (freq < lo)
        freq = hi;
    if (freq > hi)
        freq = lo;

    gEeprom.FM_FrequencyPlaying = freq;
    gEeprom.FM_SelectedFrequency = freq;
    BK1080_SetFrequency(freq, FM_BAND);
    gRequestSaveFM = true;
    gRequestDisplayScreen = DISPLAY_FM;
}

// ---- Key handling ----

static NATIVE_CODE(fm) void FMKeysNative(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    uint8_t state = bKeyPressed + 2 * bKeyHeld;
    if (Key != KEY_UP && Key != KEY_DOWN && bKeyPressed)
        FM_CancelSeek();

    if (Key >= KEY_1 && Key <= KEY_9) {
        const uint8_t slot = Key - KEY_1;
        if (bKeyHeld && bKeyPressed) {
            gFM_Memory[slot] = gEeprom.FM_FrequencyPlaying;
            FM_Memory_Save(slot);
            gFM_Favourite = gFM_SavedSlot = slot + 1;
            fmNoticeWait = 4;
            gRequestDisplayScreen = DISPLAY_FM;
        } else if (state == BUTTON_EVENT_SHORT && gFM_Memory[slot] != 0) {
            gFM_Favourite = slot + 1;
            gFM_SavedSlot = 0;
            gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency = gFM_Memory[slot];
            BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, FM_BAND);
            gRequestSaveFM = true;
            gRequestDisplayScreen = DISPLAY_FM;
        }
        return;
    }

    switch (Key) {
        case KEY_UP:
        case KEY_DOWN:
            if (state == BUTTON_EVENT_SHORT || (bKeyPressed && bKeyHeld)) {
                const int8_t dir = NAV_DIR(Key == KEY_UP ? -1 : 1);
                if (state == BUTTON_EVENT_SHORT || gFM_ManualMode || !fmArrowHeld) {
                    if (gFM_ManualMode)
                        FM_StepFreq(dir);
                    else
                        FM_SeekNext(dir);
                }
                fmArrowHeld = bKeyHeld;
            }
            if (!bKeyPressed)
                fmArrowHeld = false;
            break;
        case KEY_STAR:
            if (state == BUTTON_EVENT_HELD) {
                // Long press: toggle mute
                gFM_Mute = !gFM_Mute;
                gRequestDisplayScreen = DISPLAY_FM;
            } else if (state == BUTTON_EVENT_SHORT) {
                // Short press: toggle auto/manual
                gFM_ManualMode = !gFM_ManualMode;
                gRequestDisplayScreen = DISPLAY_FM;
            }
            break;
        case KEY_EXIT:
            if (state == BUTTON_EVENT_SHORT)
                ACTION_FM();
            break;
        case KEY_MENU:
            if (state == BUTTON_EVENT_SHORT) {
                gFmNameDisplay = !gFmNameDisplay;
                gRequestDisplayScreen = DISPLAY_FM;
            }
            break;
        case KEY_F:
            GENERIC_Key_F(bKeyPressed, bKeyHeld);
            break;
        case KEY_PTT:
            GENERIC_Key_PTT(bKeyPressed);
            break;
        case KEY_0:
            if (state == BUTTON_EVENT_SHORT) {
                gFM_No_Rx = !gFM_No_Rx;
                gRequestDisplayScreen = DISPLAY_FM;
                if (gFM_No_Rx) {
                    BK4819_Sleep(); //Locks Radio Rx in FM Radio
                } else {
                    BK4819_RX_TurnOn(); //Wake up
                    FM_SetVfoAudio(false);
                    SYSTEM_DelayMs(10);
                }
            }
            break;
        default:
            break;
    }
}

// RF/DSP stay active: only the VFO's audio DAC is gated during broadcast FM.
// Read/modify/write preserves the live register, including every RF control bit.
void FM_SetVfoAudio(bool enabled)
{
    if (!gFmRadioMode)
        return;
    const uint16_t before = BK4819_ReadRegister(BK4819_REG_30);
    const uint16_t after =
        enabled ? before | BK4819_REG_30_ENABLE_AF_DAC : before & ~BK4819_REG_30_ENABLE_AF_DAC;
    if (after != before)
        BK4819_WriteRegister(BK4819_REG_30, after);
}

// ---- FM_TurnOff / FM_Start ----

void FM_TurnOff(void)
{
    gFM_Favourite = gFM_SavedSlot = fmNoticeWait = 0;
    gFM_SignalValid = false;
    FM_SetVfoAudio(true);
    gFmRadioMode = false;
    gFM_RestoreCountdown_10ms = 0;
    gFM_Mute = false;
    gFM_ManualMode = false;
    BK1080_Init0();
    BK4819_PickRXFilterPathBasedOnFrequency(gRxVfo->freq_config_RX.Frequency);
    GPIO_EnableAudioPath();
    gEnableSpeaker = true;
    gUpdateStatus = true;
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    gEeprom.CURRENT_STATE = 0;
    SETTINGS_WriteCurrentState();
#endif
}

void FM_Start(void)
{
    if (!gFmRadioMode) {
        gFM_No_Rx = true; // Fresh FM session: VFO RX defaults OFF.
        gFM_ManualMode = gFM_Mute = false;
    }
    // Broadcast FM monitors the selected VFO only, including dual-VFO mode.
    RADIO_SelectVfos();
    gDualVfoSchedule = false;
    gFmRadioMode = true;
    gFM_RestoreCountdown_10ms = 0;
    gEeprom.FM_Band = FM_BAND;
    gEeprom.FM_IsMrMode = false;


    // Ensure FrequencyPlaying is valid before passing to BK1080_Init.
    // After a hard reset the RAM field may be 0 (or stale), which causes
    // BK1080_Init to enter powerdown mode instead of tuning, and makes
    // Keep the current frequency in the fixed broadcast band.
    {
        const uint16_t lo = BK1080_GetFreqLoLimit(FM_BAND);
        const uint16_t hi = BK1080_GetFreqHiLimit(FM_BAND);
        if (gEeprom.FM_FrequencyPlaying < lo || gEeprom.FM_FrequencyPlaying > hi) {
            gEeprom.FM_FrequencyPlaying =
                (gEeprom.FM_SelectedFrequency >= lo && gEeprom.FM_SelectedFrequency <= hi)
                    ? gEeprom.FM_SelectedFrequency
                    : lo;
        }
    }
    BK1080_Init(gEeprom.FM_FrequencyPlaying, FM_BAND);
    BK4819_PickRXFilterPathBasedOnFrequency(10320000);
    FM_SetVfoAudio(false);
    if (gFM_No_Rx)
        BK4819_Sleep();

    FM_Memory_Load(); // load memory slots from EEPROM
    GPIO_EnableAudioPath();
    gEnableSpeaker = true;
    gUpdateStatus = true;

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    gEeprom.CURRENT_STATE = 3;
    SETTINGS_WriteCurrentState();
#endif
}

#endif

void FM_ProcessKeys(KEY_Code_t key, bool pressed, bool held)
{
    if (key == KEY_EXIT) {
        FM_CancelSeek();
        if (!pressed && !held)
            ACTION_FM();
        return;
    }
    if (key == KEY_PTT) {
        FM_CancelSeek();
        GENERIC_Key_PTT(pressed);
        return;
    }
    if (APP_NativeBegin(NATIVE_FM)) {
        FMKeysNative(key, pressed, held);
        APP_NativeEnd();
    }
    if (gFmRadioMode && (gFM_No_Rx || gFM_Mute) && FUNCTION_IsRx()) {
        FUNCTION_Init();
        FUNCTION_Select(FUNCTION_FOREGROUND);
        gFM_RestoreCountdown_10ms = 1;
    }
    if (pendingFavourite) {
        uint8_t slot = pendingFavourite - 1;
        pendingFavourite = 0;
        FM_Memory_Save(slot);
    }
}
