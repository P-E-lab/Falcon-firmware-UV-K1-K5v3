#include "app/fm.h"
#include "apps/app_overlay.h"
#include "app/monitor.h"
#include "app/app.h"
/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>
#include <stdlib.h> // abs()
#include "menu.h"
#include "app/spectrum.h"
#include "bitmaps.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/st7565.h"
#include "driver/py25q16.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "font.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/ui.h"


#ifdef ENABLE_FEAT_F4HWN
#include "driver/system.h"
#endif

center_line_t center_line = CENTER_LINE_NONE;
static bool dualRxArrowVisible = true;
static uint32_t lastDualRxFrequency;
static uint8_t lastDualRxVfo;


#ifdef ENABLE_FEAT_F4HWN
// static int8_t RxBlink;
static int8_t RxBlinkLed = 0;
static int8_t RxBlinkLedCounter;
#endif

const char *VfoStateStr[] = {[VFO_STATE_NORMAL] = "",
                             [VFO_STATE_BUSY] = "BUSY",
                             [VFO_STATE_BAT_LOW] = "BAT LOW",
                             [VFO_STATE_TX_DISABLE] = "TX DISABLE",
                             [VFO_STATE_TIMEOUT] = "TIMEOUT",
                             [VFO_STATE_ALARM] = "ALARM",
                             [VFO_STATE_VOLTAGE_HIGH] = "VOLT HIGH"};

// ============================================================================
// DISPLAY HELPER FUNCTIONS
// ============================================================================

// H4WHN 6.1.0 classic meter geometry. One renderer serves Single and Dual.
static void UI_DrawSingleValues(void);
static void UI_DrawSingleModulation(const VFO_Info_t *vfo);
static void UI_DrawScanSQL(const char *text, uint8_t y);

// Dual uses page 3; Single uses page 4 (y=32..38).
static void UI_BeginCenterStrip(uint8_t line, uint8_t saved[128])
{
    memcpy(saved, gFrameBuffer[line], 128);
    memset(gFrameBuffer[line], 0, 128);
}
static void UI_EndCenterStrip(uint8_t line, const uint8_t saved[128], bool blit)
{
    if (blit && memcmp(saved, gFrameBuffer[line], 128))
        ST7565_BlitLine(line);
}

static void DrawLevelBar(uint8_t xpos, uint8_t line, uint8_t level, uint8_t bars, bool classic)
{
    level = MIN(level, bars);
    for (uint8_t i = 0; i < level; ++i) {
        uint8_t *p = gFrameBuffer[line] + xpos + i * 5;
        if (classic) {
            p[0] = p[3] = i < bars - 4 ? (~(0x7Fu >> (i + 1))) & 0x7F : 0x7F;
            p[1] = p[2] = i < bars - 4 ? p[0] : 0x41;
        } else {
            p[0] = p[3] = 0x3E;
            p[1] = p[2] = i < bars - 4 ? 0x3E : 0x22;
        }
    }
}

#ifdef ENABLE_AUDIO_BAR
// Shared microphone level and decay; scanner TX need not change FUNCTION state.
void UI_DrawTxAudioBar(uint8_t line)
{
    if (!gSetting_mic_bar || gTx1750Active)
        return;
    static uint8_t barsOld;
    static const uint8_t barsList[] = {0, 0, 0, 1, 2, 3, 5, 7, 9, 12, 15, 18, 21, 25, 25, 25};
    uint32_t voice = BK4819_GetVoiceAmplitudeOut();
    voice = voice >= 18 ? voice - 18 : 0;
    voice = MIN(voice * 16, 32768u) + 1;
    uint8_t log = 0;
    while (voice >>= 1)
        ++log;
    const uint8_t bars = barsList[log];
    barsOld = barsOld - bars > 1 ? barsOld - 1 : bars;
    DrawLevelBar(2, line, barsOld, 25, false);
}

void UI_DisplayAudioBar(void)
{
    if (!gSetting_mic_bar || (gLowBattery && !gLowBatteryConfirmed) ||
        gCurrentFunction != FUNCTION_TRANSMIT || gScreenToDisplay != DISPLAY_MAIN || gTx1750Active)
        return;
    RxBlinkLed = 0;
    RxBlinkLedCounter = 0;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    const uint8_t line = gEeprom.RX_MODE != RX_MODE_SIMPLE ? 3 : 4;
    uint8_t saved[128];
    UI_BeginCenterStrip(line, saved);
    UI_DrawTxAudioBar(line);
    center_line = CENTER_LINE_AUDIO_BAR;
    UI_EndCenterStrip(line, saved, true);
}
#endif

// Preserve the caller API, without the obsolete scope buffer and renderer.
#ifdef ENABLE_FEAT_F4HWN_AUDIO_SCOPE
void UI_DisplayAudioScope(void)
{
    UI_DisplayAudioBar();
}
#endif

// Pure framebuffer rendering: scanner and VFO share the same scale and styles.
void UI_DrawRxMeter(int16_t dbm, uint8_t line)
{
    const uint8_t level = dbm >= -93 ? 9 : dbm < -141 ? 0 : (dbm + 147) / 6;
    const uint8_t over = level == 9 ? MIN(dbm + 93, 40) : 0;
    const uint8_t extra = over / 10;
    char text[16];
    sprintf(text, "% 4d dBm", dbm);
    GUI_DisplaySmallest(text, 2, line * 8 + 1, false, true);
    if (!extra)
        sprintf(text, "S%u", level);
    else
        sprintf(text, "+%02u", over);
    UI_PrintStringSmallNormal(text, 38, 0, line);
    DrawLevelBar(62, line, level + extra, 13, gEeprom.METER_CLASSIC && gCurrentFunction != FUNCTION_TRANSMIT);
}

// H4WHN gauge pixels, with caller-supplied endpoints and scanner index.
void UI_DrawScanGauge(uint8_t line, uint8_t left, uint8_t right, uint32_t index, uint32_t total)
{
    if (!total)
        total = 1;
    index = MIN(index, total - 1);
    const uint8_t cols = right - left - 3;
    const uint32_t head = total <= 1 ? cols - 1 : index * (cols - 1) / (total - 1);
    gFrameBuffer[line][left] = gFrameBuffer[line][right] = 0x0c;
    gFrameBuffer[line][left + 1] = gFrameBuffer[line][right - 1] = 0x12;
    for (uint8_t col = 0; col < cols; ++col)
        gFrameBuffer[line][left + 2 + col] = col <= head ? 0x2d : 0x21;
}

void DisplayRSSIBar(const bool now)
{
    if (gScreenToDisplay != DISPLAY_MAIN || gCurrentFunction == FUNCTION_TRANSMIT ||
        (gEeprom.KEY_LOCK && gKeypadLocked > 0) || center_line != CENTER_LINE_RSSI)
        return;
    const uint8_t line = gEeprom.RX_MODE != RX_MODE_SIMPLE ? 3 : 4;
    const int16_t dbm = BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band];
    uint8_t saved[128];
    UI_BeginCenterStrip(line, saved);
    UI_DrawRxMeter(dbm, line);
    UI_EndCenterStrip(line, saved, now);
}

#include "full_watch.inc"

// Shared central RX/TX meter.
static void UI_DrawCenterLine(void)
{
    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        if (gSetting_mic_bar) {
            center_line = CENTER_LINE_AUDIO_BAR;
            UI_DisplayAudioBar();
        }
    } else if (FUNCTION_IsRx()) {
        center_line = CENTER_LINE_RSSI;
        DisplayRSSIBar(false);
    }
}

#ifdef ENABLE_AGC_SHOW_DATA
/**
 * @brief Debug display for AGC internal state
 * @param now Force immediate screen update
 * 
 * Shows AGC gain configuration for troubleshooting RX levels:
 * - AGC enable flag
 * - Gain index
 * - Calculated gain in dB
 * - Signal strength
 * - RSSI raw value
 */
void UI_MAIN_PrintAGC(bool now)
{
    char buf[20];
    memset(gFrameBuffer[3], 0, 128);
    union {
        struct {
            uint16_t _ : 5;
            uint16_t agcSigStrength : 7;
            int16_t gainIdx : 3;
            uint16_t agcEnab : 1;
        };
        uint16_t __raw;
    } reg7e;
    reg7e.__raw = BK4819_ReadRegister(0x7E);
    uint8_t gainAddr = reg7e.gainIdx < 0 ? 0x14 : 0x10 + reg7e.gainIdx;
    union {
        struct {
            uint16_t pga : 3;
            uint16_t mixer : 2;
            uint16_t lna : 3;
            uint16_t lnaS : 2;
        };
        uint16_t __raw;
    } agcGainReg;
    agcGainReg.__raw = BK4819_ReadRegister(gainAddr);
    int8_t lnaShortTab[] = {-28, -24, -19, 0};
    int8_t lnaTab[] = {-24, -19, -14, -9, -6, -4, -2, 0};
    int8_t mixerTab[] = {-8, -6, -3, 0};
    int8_t pgaTab[] = {-33, -27, -21, -15, -9, -6, -3, 0};
    int16_t agcGain = lnaShortTab[agcGainReg.lnaS] + lnaTab[agcGainReg.lna] + mixerTab[agcGainReg.mixer] +
                      pgaTab[agcGainReg.pga];

    sprintf(buf, "%d%2d %2d %2d %3d", reg7e.agcEnab, reg7e.gainIdx, -agcGain, reg7e.agcSigStrength,
            BK4819_GetRSSI());
    UI_PrintStringSmallNormal(buf, 2, 0, 3);
    if (now)
        ST7565_BlitLine(3);
}
#endif

// ============================================================================
// DISPLAY UPDATE TIMESLICE
// ============================================================================

/**
 * @brief 500ms timeslice updates for main display
 * 
 * Called periodically to update display elements that change
 * at slower rates than the main loop (RSSI bar, audio bar, LEDs)
 * 
 * Handles:
 * - RSSI bar updates (every 500ms)
 * - Audio level bar updates
 * - RX LED blinking/indication
 * - End-of-transmission (EOT) visual/audio feedback
 */
// Update only the activity strip. No RSSI read or RF/audio register write.
__attribute__((noinline)) bool UI_ScanBlinkVisible(void)
{
    return ((gGlobalSysTickCounter / 50u) & 1u) == 0;
}

void UI_RefreshDualActivity(void)
{
    if (gEeprom.RX_MODE == RX_MODE_SIMPLE || gScreenToDisplay != DISPLAY_MAIN ||
        center_line == CENTER_LINE_IN_USE || gCurrentFunction == FUNCTION_TRANSMIT ||
        (gLowBattery && !gLowBatteryConfirmed) || (gEeprom.KEY_LOCK && gKeypadLocked))
        return;
    const bool visible = UI_ScanBlinkVisible();
    if (gEeprom.RX_MODE == RX_MODE_ML && !gCssBackgroundScan &&
        (gCurrentFunction == FUNCTION_FOREGROUND || gCurrentFunction == FUNCTION_POWER_SAVE)) {
        UI_MAIN_DrawMLWatchArrows();
        ST7565_BlitLine(3);
    }
    const uint8_t scanning = APP_GetScanningVfo();
    if (FUNCTION_IsRx()) {
        dualRxArrowVisible = visible;
        const uint8_t line = (gEeprom.RX_VFO & 1u) ? 4 : 0;
        const uint8_t *arrow = visible ? BITMAP_VFO_Default : BITMAP_VFO_Empty;
        if (memcmp(gFrameBuffer[line], arrow, sizeof(BITMAP_VFO_Default))) {
            memcpy(gFrameBuffer[line], arrow, sizeof(BITMAP_VFO_Default));
            ST7565_DrawLine(0, line + 1, gFrameBuffer[line], sizeof(BITMAP_VFO_Default));
        }
    } else if (scanning < 2) {
        const uint8_t line = scanning ? 4 : 0;
        uint8_t saved[22];
        memcpy(saved, gFrameBuffer[line] + 8, sizeof(saved));
        memset(gFrameBuffer[line] + 8, 0, sizeof(saved));
        if (visible)
            GUI_DisplaySmallest("SCAN", 12, line * 8 + 1, false, true);
        if (memcmp(saved, gFrameBuffer[line] + 8, sizeof(saved)))
            ST7565_DrawLine(8, line + 1, gFrameBuffer[line] + 8, sizeof(saved));
    } else
        dualRxArrowVisible = true;
}

void UI_MAIN_TimeSlice500ms(void)
{
    // Only update if main display is active
    if (gScreenToDisplay == DISPLAY_MAIN && !gFmRadioMode) {

#ifdef ENABLE_AGC_SHOW_DATA
        // Debug: show AGC data on center line
        UI_MAIN_PrintAGC(true);
        return;
#endif

        // Update RSSI bar during reception
        if (FUNCTION_IsRx()) {
            DisplayRSSIBar(true);
            if (gEeprom.RX_MODE == RX_MODE_SIMPLE && !(gLowBattery && !gLowBatteryConfirmed) &&
                !(gEeprom.KEY_LOCK && gKeypadLocked)) {
                uint8_t saved[64];
                memcpy(saved, gFrameBuffer[6] + 96, 32);
                memcpy(saved + 32, gFrameBuffer[0] + 96, 32);
                UI_DrawSingleModulation(&gEeprom.VfoInfo[gEeprom.TX_VFO]);
                if (memcmp(saved, gFrameBuffer[6] + 96, 32))
                    ST7565_DrawLine(96, 7, gFrameBuffer[6] + 96, 32);
                if (memcmp(saved + 32, gFrameBuffer[0] + 96, 32))
                    ST7565_DrawLine(96, 1, gFrameBuffer[0] + 96, 32);
            }
        }
        UI_RefreshDualActivity();
        char scanSQL[8];
        if (APP_ScanSqlText(scanSQL)) {
            const unsigned y = gEeprom.RX_MODE == RX_MODE_SIMPLE ? 49 : ((gEeprom.TX_VFO & 1u) ? 49 : 17);
            UI_DrawScanSQL(scanSQL, y);
            ST7565_DrawLine(0, y / 8 + 1, gFrameBuffer[y / 8], 25);
        }
#ifdef ENABLE_FEAT_F4HWN // Blink Green Led for white...
        if (!FUNCTION_IsRx() && gSetting_set_eot > 0 && RxBlinkLed == 2) {
            if (RxBlinkLedCounter <= 8) {
                if (RxBlinkLedCounter % 2 == 0) {
                    if (gSetting_set_eot > 1) {
                        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
                    }
                } else {
                    if (gSetting_set_eot > 1) {
                        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
                    }
                }
                RxBlinkLedCounter += 1;
            } else {
                RxBlinkLed = 0;
            }
        }
#endif
    }
}

// ============================================================================
// MAIN DISPLAY RENDERING
// ============================================================================

/**
 * @brief Render main frequency/channel display
 * 
 * This is the primary display showing:
 * - VFO A and VFO B with frequencies/channels
 * - TX/RX status and modulation
 * - Signal strength indicators
 * - TX power and offset information
 * - Scan list assignments
 * - Battery status
 * 
 * Layout (2 VFO mode):
 *  Line 0-1: VFO A frequency/channel + status
 *  Line 2-3: Center line (RSSI, audio, etc.)
 *  Line 4-5: VFO B frequency/channel + status
 *  Line 6-7: Status bar (battery, lock, etc.)
 * 
 * Layout (Main Only mode):
 *  Line 0-3: Active VFO with large frequency display
 *  Line 4-5: Center information (RSSI, audio, etc.)
 *  Line 6: Status/VFO indicator
 */
// Five-pixel rows, matching the direct SQ/STEP/W/N/L/M/H glyphs.
void UI_DrawRowGlyph(uint8_t (*buffer)[128], const uint8_t *glyph, uint8_t x, uint8_t y, uint8_t width)
{
    uint8_t rows[5];
    FP_Read(glyph, rows, 5);
    glyph = rows;
    for (uint8_t row = 0; row < 5; ++row) {
        uint8_t *p = buffer[(y + row) / 8] + x;
        const uint8_t mask = 1u << ((y + row) % 8);
        for (uint8_t col = 0; col < width; ++col)
            if (glyph[row] & (1u << (width - 1 - col)))
                p[col] |= mask;
    }
}
static __attribute__((noinline)) uint8_t CompactAdvance(char c)
{
    return c == '.' ? 2 : c == ':' ? 4 : c == '/' || c == ' ' ? 4 : 6;
}
uint8_t UI_CompactTextWidth(const char *text)
{
    unsigned width = 0;
    for (; *text; ++text)
        width += CompactAdvance(*text);
    return width ? width - 1 : 0;
}
void UI_DrawCompactText5x5(const char *text, uint8_t x, uint8_t y)
{
    for (; *text && x < LCD_WIDTH; ++text) {
        UI_DrawText5x5(text, x, y, 1);
        x += CompactAdvance(*text);
    }
}

void UI_DrawText5x5(const char *text, uint8_t x, uint8_t y, uint8_t count)
{
    const uint8_t(*const upper)[5] = gFont5x5Upper;
    const uint8_t(*const lower)[5] = gFont5x5Lower;
    const uint8_t(*const digits)[5] = gFont5x5Digits;
    for (uint8_t i = 0; i < count && x + i * 6 + 4 < LCD_WIDTH; ++i) {
        const uint8_t c = (uint8_t)text[i];
        if (c == 0 || c == 0xFF)
            break;
        const uint8_t *glyph = c >= 'A' && c <= 'Z'   ? upper[c - 'A']
                               : c >= 'a' && c <= 'z' ? lower[c - 'a']
                               : c >= '0' && c <= '9' ? digits[c - '0']
                                                      : NULL;
        if (!glyph) {
            // Retain spaces and printable punctuation from the compact font.
            char text[2] = {(char)c, 0};
            if (c >= ' ' && c < 127)
                GUI_DisplaySmallest(text, x + i * 6, y, false, true);
            continue;
        }
        UI_DrawRowGlyph(gFrameBuffer, glyph, x + i * 6, y, 5);
    }
}

// Reuse the VFO five-pixel font for the edition label.
void UI_DrawMenuEdition(void)
{
    UI_DrawText5x5("FALCON", 90, 48, 6);
}

// Keep the existing value font and vertical position; center its actual ink.
void UI_PrintSmallAt(const char *text, uint8_t x, uint8_t end, uint8_t y, bool bold)
{
    const unsigned length = strlen(text);
    const unsigned spacing = end > x && length * 7u > end - x ? 6u : 7u;
    if (end > x && length * spacing < end - x)
        x += (end - x - length * spacing + 1u) / 2u;
    const uint8_t(*font)[6] = bold ? gFontSmallBold : gFontSmall;
    const uint8_t shift = y & 7u;
    for (unsigned i = 0; text[i]; ++i) {
        const uint8_t c = text[i];
        if (c <= ' ' || c > 127)
            continue;
        static const uint8_t degree[6] = {3, 5, 3, 0, 0, 0};
        uint8_t glyph[6];
        FP_Read(c == 127 ? degree : font[c - ' ' - 1], glyph, 6);
        for (uint8_t col = 0; col < 6 && x + i * spacing + col + 1u < (end > x ? end : 128); ++col) {
            const uint8_t px = x + i * spacing + col + 1u;
            gFrameBuffer[y / 8u][px] |= glyph[col] << shift;
            if (shift && y / 8u + 1u < 7)
                gFrameBuffer[y / 8u + 1u][px] |= glyph[col] >> (8u - shift);
        }
    }
}

static void UI_PrintSingleValueCentered(const char *text, uint8_t center, uint8_t y)
{
    uint16_t first = UINT16_MAX, last = 0;
    for (uint8_t i = 0; text[i]; ++i) {
        const uint8_t c = (uint8_t)text[i];
        if (c <= ' ' || c >= 127)
            continue;
        for (uint8_t col = 0; col < sizeof(gFontSmallBold[0]); ++col) {
            if (FP_Byte(&gFontSmallBold[c - ' ' - 1][col])) {
                const uint16_t x = i * (sizeof(gFontSmallBold[0]) + 1) + col;
                if (first == UINT16_MAX)
                    first = x;
                last = x;
            }
        }
    }
    if (first == UINT16_MAX)
        return;
    const uint8_t start = center - (last - first) / 2 - first - 1;
    UI_PrintSmallAt(text, start, 0, y, true);
}
// Preserve the two display conventions while sharing integer conversion.
static void UI_FormatStep(char *text, uint16_t step, bool dual)
{
    const unsigned whole = step / 100, fraction = step % 100;
    const char suffix = dual ? 'K' : 0;
    if (!fraction && (!dual || step >= 1000))
        sprintf(text, "%u%c", whole, suffix);
    else if (dual && step >= 1000 && fraction == 50)
        sprintf(text, "%u.5K", whole);
    else
        sprintf(text, "%u.%02u%c", whole, fraction, suffix);
}

// x=1 is shared by the full VFO frame and the 500-ms refresh, even when
// the text is empty during the off phase. The 5-pixel Single value is centred
// on the STEP digit middle: SQL row 2 + y=49 = STEP row 3 + y=48 = y=51.
static void UI_DrawScanSQL(const char *text, uint8_t y)
{
    // Both callers place this five-row value at y=17 or y=49.
    const uint8_t mask = 0x3e;
    uint8_t *row = gFrameBuffer[y / 8];
    for (unsigned x = 0; x < 25; ++x)
        row[x] &= ~mask;
    UI_DrawText5x5(text, 1, y, 4);
    // Match the five-pixel digit cells: a full-width minus leaves the normal
    // one-pixel gap. Every digit and the sign-inclusive centre stay fixed.
    const uint8_t dash = row[3] & 8u;
    row[4] |= dash;
    row[5] |= dash;
}

static void UI_DrawSingleValues(void)
{
    const VFO_Info_t *vfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
    char text[8];
    UI_PrintSingleValueCentered(gPowerShort[vfo->OUTPUT_POWER], 91, 48);
    UI_DrawSingleModulation(vfo);
    UI_FormatStep(text, gStepFrequencyTable[vfo->STEP_SETTING], false);
    UI_PrintSingleValueCentered(text, 41, 48);
    if (APP_ScanSqlText(text)) {
        UI_DrawScanSQL(text, 49);
    } else {
        sprintf(text, "%u", gEeprom.SQUELCH_LEVEL);
        UI_PrintSingleValueCentered(text, 12, 48);
    }
    UI_PrintSingleValueCentered(vfo->CHANNEL_BANDWIDTH & 1u ? "N" : "W", 67, 48);
}


static void UI_DrawSingleParameterLabels(void)
{
    static const struct {
        char text[5];
        uint8_t x;
    } labels[] = {{"SQL", 4}, {"STEP", 30}, {"BW", 62}, {"POW", 83}, {"MOD", 107}};
    for (unsigned i = 0; i < 5; ++i)
        UI_DrawText5x5(labels[i].text, labels[i].x, 42, 4);
}

// One seven-pixel pill for memory numbers and scanlist initials.
void UI_DrawIdentityBubble(const char *text, uint8_t left, uint8_t top, uint8_t width)
{
    const unsigned textWidth = strlen(text) * 6 - 1;
    UI_DrawText5x5(text, left + (width - textWidth) / 2, top + 1, 4);
    uint8_t *pixels = gFrameBuffer[top / 8] + left;
    const unsigned shift = top & 7u;
    for (unsigned col = 0; col < width; ++col) {
        const unsigned mask = (col == 0 || col + 1 == width ? 0x3eu : 0x7fu) << shift;
        pixels[col] ^= mask;
        if (shift > 1)
            pixels[128 + col] ^= mask >> 8;
    }
}

// The same initials, font and pill geometry in Single and Dual VFO.
void UI_DrawScanListBubble(uint8_t list, uint8_t top)
{
    if (!list || list > MR_CHANNELS_LIST)
        return;
    const unsigned slot = list - 1;
    char *stored = gListName[slot];
    // Recover an invalid RAM name once using the same non-waiting read as R1.
    // Never poll flash WIP while composing a frame: serial service must resume.
    // Canonicalise an erased name so it never causes periodic reads.
    char name[4];
    bool reloaded = false;
retry:; // The retry shares validation, copy and trailing-space trimming.
    unsigned length = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const uint8_t c = stored[i];
        if (!c)
            break;
        if (c < ' ' || c >= 127) {
            if (!reloaded) {
                PY25Q16_ReadBuffer(0x008900 + slot * 10, stored, 6);
                stored[6] = 0;
                reloaded = true;
                goto retry;
            }
            stored[i] = 0;
            break;
        }
        name[i] = c;
        if (c != ' ')
            length = i + 1;
    }
    name[length] = 0;
    if (length)
        UI_DrawIdentityBubble(name, 104, top, 21);
}

static uint32_t singleCodeFrequency;
static DCS_CodeType_t singleCodeType;
static uint8_t singleCodeOption;

void UI_MAIN_ClearReceivedCode(void)
{
    singleCodeType = CODE_TYPE_OFF;
}

static void UI_DrawSingleModulation(const VFO_Info_t *vfo)
{
    // Refresh only the MOD value; preserve the labels and the power column.
    memset(gFrameBuffer[6] + 98, 0, 30);
    char codeText[8];
    if (vfo->Modulation == MODULATION_FM && FUNCTION_IsRx() &&
        gRxVfo->pRX->Frequency == vfo->pRX->Frequency) {
        if (singleCodeFrequency != vfo->pRX->Frequency)
            UI_MAIN_ClearReceivedCode();
        singleCodeFrequency = vfo->pRX->Frequency;
        DCS_CodeType_t type;
        uint8_t option;
        if (DCS_ReadReceivedCode(&type, &option)) {
            singleCodeType = type;
            singleCodeOption = option;
        }
    } else
        UI_MAIN_ClearReceivedCode();
    // The received code temporarily replaces the list number, above its name.
    if (singleCodeType != CODE_TYPE_OFF) {
        memset(gFrameBuffer[0] + 102, 0, 26);
        DCS_FormatCode(codeText, singleCodeType, singleCodeOption, false);
        UI_DrawCompactText5x5(codeText, 114 - UI_CompactTextWidth(codeText) / 2, 1);
    }
    const DCS_CodeType_t displayType =
        vfo->pRX->CodeType != CODE_TYPE_OFF ? vfo->pRX->CodeType : vfo->pTX->CodeType;
    const uint8_t displayOption = vfo->pRX->CodeType != CODE_TYPE_OFF ? vfo->pRX->Code : vfo->pTX->Code;
    if (displayType != CODE_TYPE_OFF) {
        DCS_FormatCode(codeText, displayType, displayOption, true);
        UI_DrawCompactText5x5(codeText, 128 - UI_CompactTextWidth(codeText), 48);
    } else
        UI_PrintSingleValueCentered(gModulationStr[vfo->Modulation], 115, 48);
}

static void UI_DisplayMainDual(void)
{
    char String[32];
    center_line = CENTER_LINE_NONE;
    UI_DisplayClear();

    if (gLowBattery && !gLowBatteryConfirmed) {
        UI_DisplayPopup("LOW BATTERY");
        if (gScreenToDisplay != DISPLAY_CHANNEL)
            ST7565_BlitFullScreen();
        return;
    }

    if (gEeprom.KEY_LOCK && gKeypadLocked > 0) {
        UI_PrintString("Long press #", 0, LCD_WIDTH, 1, 8);
        UI_PrintString("to unlock", 0, LCD_WIDTH, 3, 8);
        if (gScreenToDisplay != DISPLAY_CHANNEL)
            ST7565_BlitFullScreen();
        return;
    }

    /* Pages 0/4: identity; 1/5: frequency; 2/6: parameters.
     * Page 3 is shared by the H4WHN RX meter, TX audio. */
    const unsigned int activeTxVFO =
        (gEeprom.RX_MODE == RX_MODE_FULL || APP_Ptt2Active()) && gCurrentFunction == FUNCTION_TRANSMIT
            ? gEeprom.RX_VFO & 1u
            : gEeprom.TX_VFO & 1u;
    const unsigned int activeRxVFO = gEeprom.RX_VFO & 1u;
    if (FUNCTION_IsRx()) {
        lastDualRxVfo = activeRxVFO;
        lastDualRxFrequency = gRxVfo->pRX->Frequency;
        dualRxArrowVisible = UI_ScanBlinkVisible();
    }

    for (unsigned int vfo = 0; vfo < 2; vfo++) {
        const uint8_t line = (vfo == 0) ? 0 : 4;
        const VFO_Info_t *info = APP_GetFullWatchDisplayVfo(vfo);
        const uint16_t channel = info ? info->CHANNEL_SAVE : gEeprom.ScreenChannel[vfo];
        if (!info)
            info = &gEeprom.VfoInfo[vfo];
        const bool isMain = (vfo == activeTxVFO);
        const bool isRx = (vfo == activeRxVFO) && FUNCTION_IsRx();
        const bool isTx = (vfo == activeTxVFO) && (gCurrentFunction == FUNCTION_TRANSMIT);
        uint32_t frequency = info->pRX->Frequency;

        if (isTx)
            frequency = info->pTX->Frequency;

        /* VFO marker and memory/frequency name occupy the top Tiny line. */
        char title[22];
        if (IS_MR_CHANNEL(channel)) {
            SETTINGS_FetchChannelName(title, channel);
            if (title[0] == '\0')
                sprintf(title, "CH-%04u", channel + 1);
        } else {
            sprintf(title, "VFO %c", vfo == 0 ? 'A' : 'B');
        }
        // Reserve the right-hand memory/list block without overlapping names.
        // The number block moved left: eight small-font characters keep
        // long memory names clear of its first pixel, including 4-digit MR.
        title[IS_MR_CHANNEL(channel) ? 8 : 12] = '\0';
        /* Active VFO: keep the existing 7-pixel-high bold font.
         * Inactive VFO: use the normal font so the selected VFO name is
         * immediately distinguishable without changing the line height
         * or the dual-VFO geometry. */
        uint8_t titleX = 32;
        if (IS_MR_CHANNEL(channel)) {
            // The frequency's small-font ink starts at field X=32 + one pixel.
            // Draw first into the cleared row so the existing cached marker query
            // also tells us whether the name needs shifting, without a new lookup.
            MONITOR_DrawSmallMarker(channel, info->pRX->Frequency, 33, line * 8 + 3);
            if (gFrameBuffer[line][33]) {
                titleX = 37;
            }
        }
        if (isMain)
            UI_PrintStringSmallBold(title, titleX, 0, line);
        else
            UI_PrintStringSmallNormal(title, titleX, 0, line);

        /* The left selection marker follows the currently receiving VFO
         * temporarily during RX, then returns to the selected TX VFO. */
        const bool markerActive = isMain || (isRx && !isTx);
        if (isRx && !isTx && !dualRxArrowVisible)
            memcpy(gFrameBuffer[line], BITMAP_VFO_Empty, sizeof(BITMAP_VFO_Empty));
        else if (markerActive)
            memcpy(gFrameBuffer[line] + 0, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));


        if (isTx)
            GUI_DisplaySmallest("TX", 10, line == 0 ? 1 : 33, false, true);
        else if (isRx) {
            const char *audioSource =
                info->Modulation == MODULATION_AM
                    ? gSubMenu_SET_AUD_AM[gSetting_set_audio_am < 3 ? gSetting_set_audio_am : 0]
                : info->Modulation == MODULATION_USB
                    ? "USB"
                    : gSubMenu_SET_AUD_FM[gSetting_set_audio_fm < 5 ? gSetting_set_audio_fm : 0];
            char audio[8];
            if (info->Modulation == MODULATION_USB)
                strcpy(audio, "USB");
            else
                FP_CopyString(audio, audioSource, sizeof audio);
            const unsigned width = strlen(audio) * 4 - 1;
            GUI_DisplaySmallest(audio, 10 + (20 - width) / 2, line == 0 ? 1 : 33, false, true);
        } else if (vfo == APP_GetScanningVfo()) {
            if (UI_ScanBlinkVisible())
                GUI_DisplaySmallest("SCAN", 12, line * 8 + 1, false, true);
        } else if (gCurrentFunction != FUNCTION_TRANSMIT && lastDualRxFrequency && vfo == lastDualRxVfo &&
                   lastDualRxFrequency == info->pRX->Frequency)
            GUI_DisplaySmallest(">>", 8, line * 8 + 1, false, true);

        /* Compact Tiny frequency: complete frequency on both VFOs, leaving
         * the parameter row below unobstructed. */
        sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
        const char *frequencyText =
            isMain && INPUTBOX_FrequencyIsActive() ? INPUTBOX_FrequencyGetString() : String;
        const char *display =
            VfoState[vfo] == VFO_STATE_TX_DISABLE ? VfoStateStr[VFO_STATE_TX_DISABLE] : frequencyText;
        UI_PrintStringSmallNormal(display, 32, 0, line + 1);
        /* Align to the actual ink, including leading spaces below 100 MHz
         * and the small font's inset on digit 1. */
        const uint8_t frequencyFirstX = 33;
        /* Parameter anchors belong to the complete frequency, never the input
         * preview: a one-digit preview must not move POW over STEP. */
        uint8_t lastGlyph[6];
        FP_Read(gFontSmall[String[strlen(String) - 1] - ' ' - 1], lastGlyph, 6);
        uint8_t lastCol = 5;
        while (lastCol > 0 && !lastGlyph[lastCol])
            --lastCol;
        const uint8_t frequencyLastX = 33 + (strlen(String) - 1) * 7 + lastCol;

        uint8_t memoryBubbleEnd = 3;
        if (IS_MR_CHANNEL(channel)) {
            char memNo[6];
            sprintf(memNo, "%03u", channel + 1);
            const uint8_t width = strlen(memNo) * 6 + 3;
            memoryBubbleEnd = 3 + width;
            UI_DrawIdentityBubble(memNo, 3, line * 8 + 8, width);
            const ChannelAttributes_t *att = MR_GetChannelAttributes(channel);
            if (att && att->scanlist > 0 && att->scanlist <= MR_CHANNELS_LIST) {
                char number[4];
                sprintf(number, "%02u", att->scanlist);
                UI_DrawText5x5(number, 109, line * 8 + 1, 2);
                UI_DrawScanListBubble(att->scanlist, line * 8 + 8);
            }
        }

        /* Keep modulation centred under the three scanlist initials. */
        const uint8_t paramShift = 15;
        /* Bottom line: compact H4HWN-style parameter row.  Fixed columns
         * prevent the small glyphs from touching one another. */
        const char *mod = gModulationStr[info->Modulation];
        if (info->Modulation == MODULATION_FM) {
            switch (info->pRX->CodeType) {
                case CODE_TYPE_CONTINUOUS_TONE:
                    mod = "CT";
                    break;
                case CODE_TYPE_DIGITAL:
                    mod = "DC";
                    break;
                case CODE_TYPE_REVERSE_DIGITAL:
                    mod = "DC";
                    break;
                default:
                    mod = "FM";
                    break;
            }
        }

        char step[8];
        UI_FormatStep(step, gStepFrequencyTable[info->STEP_SETTING], true);

        const char *bw = (info->CHANNEL_BANDWIDTH & 1u) ? "N" : "W";
        const char *pwr = gPowerShort[info->OUTPUT_POWER];

        const uint8_t parameterY = line == 0 ? 17 : 49;
        if (!strcmp(mod, "AM") || !strcmp(mod, "FM") || !strcmp(mod, "USB"))
            UI_DrawText5x5(mod, 94 + paramShift - (mod[2] ? 3 : 0), parameterY, 3);
        else
            GUI_DisplaySmallest(mod, 96 + paramShift, parameterY, false, true);
        /* STEP: first visible column aligned with the frequency above. */
        {
            const uint8_t(*const digit5)[5] = gFont5x5Digits;
            const uint8_t *const glyphDot = gFont5x5Dot;
            const uint8_t *const glyphK = gFont5x5Upper['K' - 'A'];
            const uint8_t lineNo = (line == 0) ? 17 : 49;
            for (uint8_t i = 0; step[i] != '\0'; ++i) {
                const uint8_t *glyph;
                if (step[i] >= '0' && step[i] <= '9')
                    glyph = digit5[step[i] - '0'];
                else if (step[i] == '.')
                    glyph = glyphDot;
                else
                    glyph = glyphK;
                UI_DrawRowGlyph(gFrameBuffer, glyph, frequencyFirstX - (step[0] == '1') + i * 6, lineNo, 5);
            }
        }

        /* W/N has a fixed position, midway between the usual 12.5K step
         * and the right-aligned power letter. */
        UI_DrawText5x5(bw, 71, parameterY, 1);
        UI_DrawText5x5(pwr, frequencyLastX - (strlen(pwr) * 6 - 2), parameterY, 2);
        char sql[8];
        if (isMain && APP_ScanSqlText(sql))
            UI_DrawScanSQL(sql, parameterY);
        else {
            sprintf(sql, "SQ%u", gEeprom.SQUELCH_LEVEL);
            const uint8_t sqlWidth = strlen(sql) * 6 - 1;
            UI_DrawText5x5(sql, (3 + (IS_MR_CHANNEL(channel) ? memoryBubbleEnd : 24) - sqlWidth) / 2,
                           parameterY, 4);
        }

        // R (reverse) takes the place of the offset direction.
        if (info->FrequencyReverse || info->freq_config_RX.Frequency != info->freq_config_TX.Frequency) {
            const char *dir = info->FrequencyReverse ? "R"
                              : (info->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_ADD) ? "+"
                              : (info->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_SUB)
                                  ? "-"
                                  : "";
            if (dir[0])
                GUI_DisplaySmallest(dir, memoryBubbleEnd + (frequencyFirstX - memoryBubbleEnd - 3) / 2,
                                    line * 8 + 9, false, true);
        }
    }

    UI_DrawCenterLine();
    UI_MAIN_DrawFullWatchPriorities();
    if (gScreenToDisplay != DISPLAY_CHANNEL)
        ST7565_BlitFullScreen();
}

NATIVE_CODE(vfo) void UI_DisplayMainNative(void)
{
    if (gEeprom.RX_MODE != RX_MODE_SIMPLE) {
        UI_DisplayMainDual();
        return;
    }
    char String[22]; // String buffer for formatted text

    center_line = CENTER_LINE_NONE; // Center line initially available

    // Clear screen and prepare frame buffer
    UI_DisplayClear();

    // ================================================================
    // CHECK FOR LOW BATTERY ALERT
    // ================================================================

    if (gLowBattery && !gLowBatteryConfirmed) {
        // Display battery critical warning and block further operation
        UI_DisplayPopup("LOW BATTERY");
        if (gScreenToDisplay != DISPLAY_CHANNEL)
            ST7565_BlitFullScreen();
        return;
    }

    // ================================================================
    // CHECK FOR KEYPAD LOCK ALERT
    // ================================================================

    if (gEeprom.KEY_LOCK && gKeypadLocked > 0) { // Display keypad lock message
        UI_PrintString("Long press #", 0, LCD_WIDTH, 1, 8);
        UI_PrintString("to unlock", 0, LCD_WIDTH, 3, 8);
        if (gScreenToDisplay != DISPLAY_CHANNEL)
            ST7565_BlitFullScreen();
        return;
    }

    // ================================================================
    // RENDER VFO
    // ================================================================

    // GUI style: CLASSIC in main-only, MODERN in dual-screen
    gSetting_set_gui = 1;
    const unsigned int vfo_num = gEeprom.TX_VFO & 1u;
    const VFO_Info_t *vfoInfo = &gEeprom.VfoInfo[vfo_num];
    uint32_t frequency = vfoInfo->pRX->Frequency;
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        frequency = vfoInfo->pTX->Frequency;
    const bool isMR = IS_MR_CHANNEL(gEeprom.ScreenChannel[gEeprom.TX_VFO]);

    const uint16_t channel = gEeprom.ScreenChannel[vfo_num];
    const bool inputting = gInputBoxIndex != 0;
    if (isMR) {
        if (inputting)
            sprintf(String, "M%.4s", INPUTBOX_GetAscii());
        else
            sprintf(String, "%03u", channel + 1u);
        // Normal font adds a one-pixel inset: visible ink starts at x=3.
        UI_PrintStringSmallNormal(String, 2, 0, 0);
        for (unsigned x = 2; x < 38; ++x)
            gFrameBuffer[0][x] <<= 1;
        const ChannelAttributes_t *att = MR_GetChannelAttributes(channel);
        if (att && att->scanlist && att->scanlist <= MR_CHANNELS_LIST) {
            char number[4];
            snprintf(number, sizeof number, "%02u", att->scanlist);
            UI_DrawText5x5(number, 109, 1, 2);
            UI_DrawScanListBubble(att->scanlist, 8);
        }
        if (!inputting) {
            char name[22];
            SETTINGS_FetchChannelName(name, channel);
            if (!name[0])
                UI_FormatFrequency(name, frequency);
            name[vfoInfo->Modulation == MODULATION_USB ? 7 : 8] = 0;
            // Shift the monitor square and memory name four pixels together.
            MONITOR_DrawMarker(channel, frequency, 31, 11);
            const uint8_t nameX = gFrameBuffer[1][31] ? 37 : 31;
            UI_PrintString(name, nameX, 0, 1, 8);
            for (uint8_t x = nameX; x < nameX + 64; ++x) {
                const uint16_t ink = gFrameBuffer[1][x] | (gFrameBuffer[2][x] << 8);
                gFrameBuffer[0][x] |= (ink & 127u) << 1;
                gFrameBuffer[1][x] = ink >> 7;
                gFrameBuffer[2][x] = ink >> 15;
            }
        }
    } else {
        UI_PrintStringSmallNormal("VFO", 2, 0, 0);
        for (unsigned x = 2; x < 38; ++x)
            gFrameBuffer[0][x] <<= 1;
    }

    // Both Single modes share the memory frequency's raised y position.
    if (INPUTBOX_FrequencyIsActive())
        strcpy(String, INPUTBOX_FrequencyGetString());
    else
        sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
    const char *dot = strchr(String, '.');
    const uint8_t prefix = dot && dot - String > 3 ? 8 : 7;
    if (strlen(String) > prefix)
        UI_PrintString(String + prefix, 99, 0, 3, 8);
    String[prefix] = 0;
    UI_DisplayFrequency(String, 40 - prefix * 4, 3, false);
    const VfoState_t state = VfoState[vfo_num];
    if (state != VFO_STATE_NORMAL) {
        const char *msg = state < ARRAY_SIZE(VfoStateStr) ? VfoStateStr[state] : "";
        memset(gFrameBuffer[3], 0, 256);
        UI_PrintString(msg, (LCD_WIDTH - strlen(msg) * 8) / 2, 0, 3, 8);
    }
    // Shift nine pixels: memory frequency moves up three from its v1.0.7 position.
    for (unsigned x = 0; x < 128; ++x) {
        const uint16_t ink = gFrameBuffer[3][x] | (gFrameBuffer[4][x] << 8);
        gFrameBuffer[1][x] |= (ink & 1u) << 7;
        gFrameBuffer[2][x] |= ink >> 1;
        gFrameBuffer[3][x] = ink >> 9;
        gFrameBuffer[4][x] = 0;
    }
    if (gSetting_set_ptt_session)
        UI_PrintStringSmallBold("T", LCD_WIDTH + 4, 0, 3);
    if (vfoInfo->FrequencyReverse)
        UI_PrintStringSmallBold("R", LCD_WIDTH + 4, 0, 2);
    else if (vfoInfo->freq_config_RX.Frequency != vfoInfo->freq_config_TX.Frequency) {
        static const char *const direction[] = {"", "+", "-"};
        UI_PrintStringSmallBold(direction[vfoInfo->TX_OFFSET_FREQUENCY_DIRECTION % 3], LCD_WIDTH + 4, 0, 2);
    }
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        GUI_DisplaySmallestDark("TX", 2, 23, false, false);
    else if (FUNCTION_IsRx())
        GUI_DisplaySmallestDark("RX", 2, 23, false, false);
    UI_DrawSingleParameterLabels();
    UI_DrawCenterLine();
    UI_DrawSingleValues();
    if (gScreenToDisplay != DISPLAY_CHANNEL)
        ST7565_BlitFullScreen();
}
void UI_DisplayMain(void)
{
    if (APP_NativeBegin(NATIVE_VFO)) {
        UI_DisplayMainNative();
        APP_NativeEnd();
    }
}
