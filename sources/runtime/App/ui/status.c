#include "app/app.h"
/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif
#include "bitmaps.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/helper.h"
#include "ui/ui.h"
#include "ui/status.h"
#include "ui/main.h"
#include "ui/menu.h"
#include "font.h"

#ifdef ENABLE_FEAT_F4HWN
// S-level, updated in ui/main.c during reception

#endif

#ifdef ENABLE_FEAT_F4HWN
static const uint8_t statusTinyPunctuation[3][5] = {{0, 0, 4, 0, 0}, {4, 4, 31, 4, 4}, {25, 26, 4, 11, 19}};

static void Status_DrawTinyChar(uint8_t x, uint8_t y, char c)
{
    const uint8_t *glyph = NULL;
    if (c >= '0' && c <= '9')
        glyph = gFont5x5Digits[c - '0'];
    else if (c == ':')
        glyph = statusTinyPunctuation[0];
    else if (c == '+')
        glyph = statusTinyPunctuation[1];
    else if (c == '.')
        glyph = gFont5x5Dot;
    else if (c >= 'A' && c <= 'Z')
        glyph = gFont5x5Upper[c - 'A'];
    else if (c == '%')
        glyph = statusTinyPunctuation[2];
    else if (c == 'S') {
        const uint8_t *const sGlyph = gFont5x5Upper['S' - 'A'];
        glyph = sGlyph;
    }
    if (!glyph)
        return;
    UI_DrawRowGlyph((uint8_t(*)[128])gStatusLine, glyph, x, y, 5);
}

static void Status_DrawTinyText(uint8_t x, uint8_t y, const char *text)
{
    while (*text) {
        Status_DrawTinyChar(x, y, *text++);
        x += 6;
    }
}
#endif

#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
#ifndef ENABLE_FEAT_F4HWN_DEBUG
static void convertTime(uint8_t *line, uint8_t type)
{
    uint16_t t = (type == 0) ? (gTxTimerCountdown_500ms / 2) : (3600 - gRxTimerCountdown_500ms / 2);
    uint8_t m = t / 60;
    uint8_t s = t - (m * 60);

    char str[6];
    sprintf(str, "%02u:%02u", m, s);
    Status_DrawTinyText((uint8_t)(line - gStatusLine), 1, str);
}
#endif
#endif

void UI_DisplayStatus()
{
#ifdef ENABLE_CUSTOM_MENU_LAYOUT
    if (gScreenToDisplay == DISPLAY_MENU) {
        gUpdateStatus = false;
        UI_MenuDrawHeader();
        ST7565_BlitStatusLine();
        return;
    }
#endif
    char str[12] = "";
    gUpdateStatus = false;
    memset(gStatusLine, 0, sizeof(gStatusLine));
    uint8_t POS_MOD = 1;            // DW, XB, MO (glyphs)
    const uint8_t POS_F = 57;       // Shared F/lock slot, four pixels farther from voltage
    const uint8_t POS_LOCK = POS_F; // Lock uses the same slot, clear of voltage.

#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    if (gSetting_set_tmr) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            POS_MOD += 2;
            convertTime(gStatusLine + POS_MOD, 0);
            POS_MOD += 29;
        } else if (FUNCTION_IsRx()) {
            POS_MOD += 2;
            convertTime(gStatusLine + POS_MOD, 1);
            POS_MOD += 29;
        }
    }
#endif

    if ((gScreenToDisplay == DISPLAY_MAIN || gScreenToDisplay == DISPLAY_CHANNEL) &&
        !(gSetting_set_tmr && (gCurrentFunction == FUNCTION_TRANSMIT || FUNCTION_IsRx()))) {
        const char *watch = gEeprom.RX_MODE == RX_MODE_SIMPLE ? "SW" : "DW";
        if (gEeprom.RX_MODE == RX_MODE_FULL)
            watch = "FW";
        if (gEeprom.RX_MODE == RX_MODE_ML)
            watch = "MW";
        Status_DrawTinyText(3, 1, watch);
        POS_MOD = 18;
    }

    // Falcon mode indicator in the former flashlight slot.
    if (APP_FalconMode()) {
        GUI_DisplaySmallest("F", POS_MOD + 3, 1, true, true);
        POS_MOD += 10;
    }

    // 7. BACKLIGHT (B) - VERTICAL, shifted right when the flashlight is also enabled
    if (gBackLight) {
        POS_MOD += 3;
        gStatusLine[POS_MOD++] |= 0x0C;
        gStatusLine[POS_MOD++] |= 0x12;
        gStatusLine[POS_MOD++] |= 0x65;
        gStatusLine[POS_MOD++] |= 0x79;
        gStatusLine[POS_MOD++] |= 0x65;
        gStatusLine[POS_MOD++] |= 0x12;
        gStatusLine[POS_MOD++] |= 0x0C;
    }

#ifdef ENABLE_VOX
    // VOX indicator, in the font of the SW/DW/FW/MW label.
    if (gEeprom.VOX_SWITCH) {
        POS_MOD += 3;
        Status_DrawTinyText(POS_MOD, 1, "VX");
        POS_MOD += 12;
    }
#endif


    // 7. F-KEY AND LOCK - SEPARATE
    if (gWasFKeyPressed) {
        gStatusLine[POS_F + 0] = 0x7F;
        gStatusLine[POS_F + 1] = 0x41;
        gStatusLine[POS_F + 2] = 0x75;
        gStatusLine[POS_F + 3] = 0x75;
        gStatusLine[POS_F + 4] = 0x75;
        gStatusLine[POS_F + 5] = 0x7D;
        gStatusLine[POS_F + 6] = 0x7F;
    }
    if (gEeprom.KEY_LOCK) {
        gStatusLine[POS_LOCK + 0] = 0x7C;
        gStatusLine[POS_LOCK + 1] = 0x7A;
        gStatusLine[POS_LOCK + 2] = 0x79;
        gStatusLine[POS_LOCK + 3] = 0x49;
        gStatusLine[POS_LOCK + 4] = 0x79;
        gStatusLine[POS_LOCK + 5] = 0x7A;
        gStatusLine[POS_LOCK + 6] = 0x7C;
    }

    if (gSetting_battery_text == 1) {
        UI_FormatVoltage(str, gBatteryVoltageAverage);
        Status_DrawTinyText((uint8_t)(127 - (strlen(str) * 6) + 1), 1, str);
    } else if (gSetting_battery_text == 2) {
        sprintf(str, "%u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
        Status_DrawTinyText((uint8_t)(127 - (strlen(str) * 6) + 1), 1, str);
    } else if (gSetting_battery_text == 3) {
        char percent[5];
        UI_FormatVoltage(str, gBatteryVoltageAverage);
        sprintf(percent, "%u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));

        /* Keep percent at the existing right edge. Place voltage immediately
         * to its left with a fixed 3-pixel gap. Both use the same 5x5 glyphs
         * as the Dual-VFO parameter labels. */
        const uint8_t percentPos = (uint8_t)(127 - (strlen(percent) * 6));
        const uint8_t voltagePos = (uint8_t)(percentPos - 5 - (strlen(str) * 6));
        Status_DrawTinyText(voltagePos, 1, str);

        /* Keep the % at the same right-hand position, while leaving one
         * additional pixel between the last percentage digit and the %. */
        const size_t percentDigits = strlen(percent) - 1;
        if (percentDigits > 0) {
            char percentValue[5];
            memcpy(percentValue, percent, percentDigits);
            percentValue[percentDigits] = '\0';
            Status_DrawTinyText((uint8_t)(percentPos - 1), 1, percentValue);
        }
        Status_DrawTinyChar((uint8_t)(percentPos + ((strlen(percent) - 1) * 6)), 1, '%');
    }

    if (gScreenToDisplay == DISPLAY_MAIN || gScreenToDisplay == DISPLAY_CHANNEL)
        for (unsigned x = 0; x < LCD_WIDTH; ++x)
            gStatusLine[x] ^= 0x7fu;

    ST7565_BlitStatusLine();
}
