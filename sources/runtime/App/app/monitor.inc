/* VFO Monitor operations, included by main.c. No channel browser. */
#include "app/monitor.h"
#include "ui/inputbox.h"
#include "ui/ui.h"
#include "driver/keyboard.h"
#include "external/printf/printf.h"

typedef struct {
    uint16_t channel;
} MonitorRef;
static MonitorRef monitorRefs[MONITOR_LIMIT];
static uint8_t monitorCount;
static bool monitorLoaded;
static uint32_t markerFrequency[2];
static bool markerPresent[2];
static const uint16_t BAND_CHANNEL_FIRST = MR_CHANNELS_MAX - MAX_BANDS;
static const char *monitorNotice;

static uint32_t MonitorReadFrequency(uint16_t channel)
{
    return SETTINGS_FetchChannelFrequency(channel);
}
static bool MonitorValid(uint16_t channel)
{
    const ChannelAttributes_t *a = MR_GetChannelAttributes(channel);
    return a && a->band != 7;
}
void MONITOR_Invalidate(void)
{
    monitorLoaded = false;
    markerFrequency[0] = markerFrequency[1] = 0;
}
void MONITOR_Rebuild(void)
{
    monitorCount = 0;
    ChannelAttributes_t block[32];
    for (uint16_t first = MR_CHANNEL_FIRST; first <= MR_CHANNEL_LAST && monitorCount < MONITOR_LIMIT;
         first += 32) {
        const uint16_t n = MR_CHANNEL_LAST - first + 1 < 32 ? MR_CHANNEL_LAST - first + 1 : 32;
        PY25Q16_ReadBuffer(ADRESS_CHANNELS_ATTRIBUTES + (uint32_t)first * sizeof *block, block,
                           n * sizeof *block);
        for (uint8_t i = 0; i < n && monitorCount < MONITOR_LIMIT; ++i) {
            if (block[i].scanlist != 51 || block[i].band == 7)
                continue;
            const uint32_t f = MonitorReadFrequency(first + i);
            if (f && f != UINT32_MAX)
                monitorRefs[monitorCount++] = (MonitorRef){first + i};
        }
    }
    monitorLoaded = true;
    markerFrequency[0] = markerFrequency[1] = 0;
}
static void MonitorEnsure(void)
{
    if (!monitorLoaded)
        MONITOR_Rebuild();
}
uint8_t MONITOR_Count(void)
{
    MonitorEnsure();
    return monitorCount;
}
uint32_t MONITOR_Frequency(uint8_t index)
{
    MonitorEnsure();
    return index < monitorCount ? MonitorReadFrequency(monitorRefs[index].channel) : 0;
}
static int MonitorFind(uint32_t f)
{
    MonitorEnsure();
    for (uint8_t i = 0; i < monitorCount; ++i)
        if (MonitorReadFrequency(monitorRefs[i].channel) == f)
            return i;
    return -1;
}
// The six unused name bytes remember the original channel. CHIRP's name and
// memory layouts stay unchanged; legacy copies are resolved by frequency.
static uint16_t MonitorOrigin(uint16_t copy)
{
    uint8_t tag[6];
    PY25Q16_ReadBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)copy * 16 + 10, tag, 6);
    const uint32_t f = MonitorReadFrequency(copy);
    const uint16_t origin = tag[2] | ((uint16_t)tag[3] << 8);
    if (tag[0] == 0 && tag[1] == 'M' && tag[4] == (uint8_t)~tag[2] && tag[5] == (uint8_t)~tag[3] &&
        IS_MR_CHANNEL(origin) && MonitorValid(origin)) {
        const ChannelAttributes_t *a = MR_GetChannelAttributes(origin);
        if (a->scanlist != 51 && MonitorReadFrequency(origin) == f)
            return origin;
    }
    uint16_t match = UINT16_MAX;
    for (uint16_t ch = MR_CHANNEL_FIRST; ch <= MR_CHANNEL_LAST; ++ch) {
        const ChannelAttributes_t *a = MR_GetChannelAttributes(ch);
        if (a->band != 7 && a->scanlist != 51 && MonitorReadFrequency(ch) == f) {
            if (match != UINT16_MAX)
                return UINT16_MAX; // An ambiguous legacy copy must not jump lists.
            match = ch;
        }
    }
    return match;
}
static int MonitorFree(uint16_t start)
{
    for (uint16_t ch = start; ch <= MR_CHANNEL_LAST && ch < BAND_CHANNEL_FIRST; ++ch) {
        if (!MonitorValid(ch))
            return ch;
        const uint32_t f = MonitorReadFrequency(ch);
        if (!f || f == UINT32_MAX)
            return ch;
    }
    return -1;
}
static bool MonitorAdd(uint16_t source, const VFO_Info_t *vfo)
{
    if (IS_MR_CHANNEL(source) && source >= BAND_CHANNEL_FIRST)
        return false;
    const uint32_t f = IS_MR_CHANNEL(source) ? MonitorReadFrequency(source) : vfo->pRX->Frequency;
    if (!f || f == UINT32_MAX)
        return false;
    if (MonitorFind(f) >= 0)
        return true;
    if (IS_MR_CHANNEL(source) && MR_GetChannelAttributes(source)->scanlist == 51)
        source = MonitorOrigin(source);
    if (monitorCount >= MONITOR_LIMIT) {
        monitorNotice = "MONI FULL";
        return false;
    }
    const int free = MonitorFree(MR_CHANNEL_FIRST);
    if (free < 0) {
        monitorNotice = "MEMOIRES PLEINES";
        return false;
    }
    uint8_t name[16] = {0};
    if (IS_MR_CHANNEL(source)) {
        uint8_t data[16];
        PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)source * 16, data, 16);
        PY25Q16_ReadBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)source * 16, name, 16);
        ChannelAttributes_t a = *MR_GetChannelAttributes(source);
        a.scanlist = 51;
        a.exclude = 0;
        PY25Q16_WriteBuffer(ADRESS_CHANNELS + (uint32_t)free * 16, data, 16, false);
        MR_SetChannelAttributes(free, &a);
    } else {
        VFO_Info_t copy = *vfo;
        copy.SCANLIST_PARTICIPATION = 51;
        SETTINGS_SaveChannel(free, gEeprom.TX_VFO, &copy, 2);
        UI_FormatFrequency((char *)name, f);
    }
    memset(name + CHANNEL_NAME_LEN, 0, 10 - CHANNEL_NAME_LEN);
    name[10] = 0;
    name[11] = 'M';
    name[12] = source;
    name[13] = source >> 8;
    name[14] = (uint8_t)~name[12];
    name[15] = (uint8_t)~name[13];
    PY25Q16_WriteBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)free * 16, name, 16, false);
    // SETTINGS_SaveChannel may invalidate the cache: rebuild only once here.
    if (!monitorLoaded)
        MONITOR_Rebuild();
    else
        monitorRefs[monitorCount++] = (MonitorRef){free};
    markerFrequency[0] = markerFrequency[1] = 0;
    return true;
}
static void MonitorRetarget(uint16_t copy)
{
    uint16_t origin = MR_GetChannelAttributes(copy)->scanlist == 51 ? MonitorOrigin(copy) : UINT16_MAX;
    if (origin == copy)
        origin = UINT16_MAX;
    for (uint8_t v = 0; v < 2; ++v)
        if (gEeprom.ScreenChannel[v] == copy) {
            const uint16_t next = IS_MR_CHANNEL(origin) ? origin : FREQ_CHANNEL;
            if (!IS_MR_CHANNEL(next)) {
                gEeprom.VfoInfo[v].ExactFrequency = true;
                SETTINGS_SaveChannel(FREQ_CHANNEL, v, &gEeprom.VfoInfo[v], 2);
            }
            gEeprom.ScreenChannel[v] = next;
            if (IS_MR_CHANNEL(next))
                gEeprom.MrChannel[v] = next;
            RADIO_ConfigureChannel(v, VFO_CONFIGURE_RELOAD);
            gRequestSaveVFO = true;
            gFlagReconfigureVfos = true;
        }
}
static void MonitorRemoveIndex(uint8_t index)
{
    const uint16_t copy = monitorRefs[index].channel;
    MonitorRetarget(copy);
    uint8_t erased[16];
    memset(erased, 255, sizeof erased);
    PY25Q16_WriteBuffer(ADRESS_CHANNELS + (uint32_t)copy * 16, erased, 16, false);
    memset(erased, 0, sizeof erased);
    PY25Q16_WriteBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)copy * 16, erased, 16, false);
    ChannelAttributes_t a = {0};
    a.band = 7;
    MR_SetChannelAttributes(copy, &a);
    memmove(&monitorRefs[index], &monitorRefs[index + 1], (monitorCount - index - 1) * sizeof(MonitorRef));
    --monitorCount;
    markerFrequency[0] = markerFrequency[1] = 0;
}
static void MonitorRemove(uint32_t f)
{
    for (int i = monitorCount - 1; i >= 0; --i)
        if (MonitorReadFrequency(monitorRefs[i].channel) == f)
            MonitorRemoveIndex(i);
}
void MONITOR_ToggleCurrent(void)
{
    // Memory channels only: an unsaved VFO frequency cannot be monitored.
    if (gCurrentFunction == FUNCTION_TRANSMIT || !IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
        return;
    MONITOR_Rebuild();
    monitorNotice = NULL;
    const uint32_t f = gTxVfo->pRX->Frequency;
    if (MonitorFind(f) >= 0) {
        MonitorRemove(f);
        monitorNotice = "MONI DEL";
    } else if (MonitorAdd(gTxVfo->CHANNEL_SAVE, gTxVfo))
        monitorNotice = "MONI ADD";
    UI_DisplayPopup(monitorNotice ? monitorNotice : "FREQUENCE INVALIDE");
    ST7565_BlitFullScreen();
    SYSTEM_DelayMs(450);
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
}
static bool MonitorMarkerPresent(uint8_t v, uint32_t f)
{
    MonitorEnsure();
    v &= 1;
    if (markerFrequency[v] != f) {
        markerPresent[v] = MonitorFind(f) >= 0;
        markerFrequency[v] = f;
    }
    return markerPresent[v];
}
// Cache Monitor membership by display row; priority membership uses channel IDs.
static void MonitorDrawMarker(uint16_t channel, uint32_t f, uint8_t x, uint8_t centerY, uint8_t size)
{
    uint8_t priority = 0;
    for (uint8_t i = 0; i < 4; i++)
        if (channel == gEeprom.SCANLIST_PRIORITY_CH[i]) {
            priority = i + 1;
            break;
        }
    if (priority) {
        const char text[2] = {'0' + priority, 0};
        // Single: both symbols end at y12. Dual: both centres are y3/y35.
        GUI_DisplaySmallest(text, x, centerY - 2 - (size == 4), false, true);
    } else if (MonitorMarkerPresent(centerY >> 5, f)) {
        for (uint8_t row = 0; row < size; ++row)
            for (uint8_t col = 0; col < size; ++col)
                PutPixel(x + col, centerY - size / 2 + row, true);
    }
}
void MONITOR_DrawMarker(uint16_t channel, uint32_t f, uint8_t x, uint8_t centerY)
{
    MonitorDrawMarker(channel, f, x, centerY, 4);
}
void MONITOR_DrawSmallMarker(uint16_t channel, uint32_t f, uint8_t x, uint8_t centerY)
{
    MonitorDrawMarker(channel, f, x, centerY, 3);
}

#include "channel_editor.inc"
