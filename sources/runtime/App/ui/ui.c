#include "app/monitor.h"
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

#include <assert.h>
#include <string.h>

#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif
#include "driver/keyboard.h"
#include "misc.h"
#ifdef ENABLE_FMRADIO
#include "ui/fmradio.h"
#endif

#include "driver/st7565.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/menu.h"
#include "settings.h"
#include "ui/ui.h"
#include "../misc.h"
#include "store.h"

GUI_DisplayType_t gScreenToDisplay;
GUI_DisplayType_t gRequestDisplayScreen = DISPLAY_INVALID;

uint8_t gAskForConfirmation;
bool gAskToSave;
bool gAskToDelete;


void (*UI_DisplayFunctions[])(void) = {
    [DISPLAY_CHANNEL] = &CHANNEL_Display,
    [DISPLAY_MAIN] = &UI_DisplayMain,
    [DISPLAY_MENU] = &UI_DisplayMenu,

#ifdef ENABLE_FMRADIO
    [DISPLAY_FM] = &UI_DisplayFM,
#endif
};

static_assert(ARRAY_SIZE(UI_DisplayFunctions) == DISPLAY_N_ELEM);

/* Shown when the external store of the running slot is missing or was
 * overwritten: fonts and screens live there, so this uses its own glyphs. */
static void DisplayStoreMissing(void)
{
    static const char glyphChars[] = "ACDEFGILMNORSTU";
    static const uint8_t glyphs[][5] = {
        {0x1E, 0x05, 0x05, 0x05, 0x1E}, {0x0E, 0x11, 0x11, 0x11, 0x11}, {0x1F, 0x11, 0x11, 0x11, 0x0E},
        {0x1F, 0x15, 0x15, 0x15, 0x11}, {0x1F, 0x05, 0x05, 0x05, 0x01}, {0x0E, 0x11, 0x11, 0x15, 0x1D},
        {0x11, 0x11, 0x1F, 0x11, 0x11}, {0x1F, 0x10, 0x10, 0x10, 0x10}, {0x1F, 0x02, 0x04, 0x02, 0x1F},
        {0x1F, 0x02, 0x04, 0x08, 0x1F}, {0x0E, 0x11, 0x11, 0x11, 0x0E}, {0x1F, 0x05, 0x05, 0x0D, 0x12},
        {0x12, 0x15, 0x15, 0x15, 0x09}, {0x01, 0x01, 0x1F, 0x01, 0x01}, {0x0F, 0x10, 0x10, 0x10, 0x0F},
    };
    static const char *const lines[] = {"MODULES MISSING", "REINSTALL FALCON"};

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    for (unsigned line = 0; line < 2; line++) {
        const char *text = lines[line];
        unsigned x = (LCD_WIDTH - strlen(text) * 6u) / 2u;
        for (; *text; text++, x += 6u) {
            const char *g = strchr(glyphChars, *text);
            if (*text != ' ' && g)
                memcpy(&gFrameBuffer[2u + line * 2u][x], glyphs[g - glyphChars], 5);
        }
    }
    ST7565_BlitFullScreen();
}

void GUI_DisplayScreen(void)
{
    if (STORE_IsMissing()) {
        DisplayStoreMissing();
        return;
    }
    if (gScreenToDisplay != DISPLAY_INVALID) {
        UI_DisplayFunctions[gScreenToDisplay]();
    }
}

void GUI_SelectNextDisplay(GUI_DisplayType_t Display)
{
    if (Display == DISPLAY_INVALID)
        return;

    if (gScreenToDisplay != Display) {
        if (Display == DISPLAY_MENU)
            SETTINGS_SaveVfoNow();
        if (gScreenToDisplay == DISPLAY_MENU && UI_MENU_GetCurrentMenuId() == MENU_SET_CTR)
            ST7565_SetContrast(gSetting_set_ctr);
        if (gScreenToDisplay == DISPLAY_MENU)
            UI_MENU_Close();
        gInputBoxIndex = 0;
        INPUTBOX_FrequencyBegin();
        gIsInSubMenu = false;
        gCssBackgroundScan = false;

        gAskForConfirmation = 0;
        gAskToSave = false;
        gAskToDelete = false;

        HideFKeyIcon();
    }

    gScreenToDisplay = Display;
    gUpdateDisplay = true;
}
