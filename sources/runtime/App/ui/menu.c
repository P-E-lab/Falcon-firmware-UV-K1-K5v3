#include "app/spectrum.h"
#include "apps/app_overlay.h"
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
#include <stdlib.h>
#include "store.h"
#include "../app/menu.h"
#include "../bitmaps.h"
#include "../board.h"
#include "../dcs.h"
#include "../driver/backlight.h"
#include "../driver/bk4819.h"
#include "../driver/st7565.h"
#include "../external/printf/printf.h"
#include "../frequencies.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../settings.h"

#ifdef ENABLE_FEAT_F4HWN
#include "../version.h"
#endif

#include "helper.h"
#include "inputbox.h"
#include "menu.h"
#include "main.h"
#include "ui.h"


const t_menu_item MenuList[] FP_RESOURCE = {
    // F4WHN ALL order, with Falcon-only entries beside their related settings.
    {"Step", MENU_STEP},
    {"POWER", MENU_TXP},
    {"RxDCS", MENU_R_DCS},
    {"RxCTCS", MENU_R_CTCS},
    {"TxDCS", MENU_T_DCS},
    {"TxCTCS", MENU_T_CTCS},
    {"TxODir", MENU_SFT_D},
    {"TxOffs", MENU_OFFSET},
    {"BANDW", MENU_W_N},
    {"Scramb", MENU_SCR},
    {"Compnd", MENU_COMPAND},
    {"Mode", MENU_AM},
    {"ChList", MENU_LIST_CH},
    {"ChSave", MENU_MEM_CH},
    {"ChDele", MENU_DEL_CH},
    {"ChName", MENU_MEM_NAME},
    {"PRICH1", MENU_PRICH1},
    {"PRICH2", MENU_PRICH2},
    {"PRICH3", MENU_PRICH3},
    {"PRICH4", MENU_PRICH4},
    {"F1Shrt", MENU_F1SHRT},
    {"F1Long", MENU_F1LONG},
    {"F2Shrt", MENU_F2SHRT},
    {"F2Long", MENU_F2LONG},
    {"M Long", MENU_MLONG},
    {"KeyLck", MENU_AUTOLK},
    {"TxTOut", MENU_TOT},
    {"BatSav", MENU_SAVE},
#ifdef ENABLE_FEAT_F4HWN
    {"BatTxt", MENU_BAT_TXT},
#endif
    {"Mic", MENU_MIC},
    {"MicBar", MENU_SET_MET},
    {"POnMsg", MENU_PONMSG},
    {"PONSCR", MENU_PONSCR},
    {"LIGHT", MENU_LED},
    {"BLTime", MENU_ABR},
    {"BLMin", MENU_ABR_MIN},
    {"BLMax", MENU_ABR_MAX},
    {"BLTxRx", MENU_ABR_ON_TX_RX},
    {"Roger", MENU_ROGER},
    {"AUROG", MENU_AUTO_ROGER},
#ifdef ENABLE_VOX
    {"VOX", MENU_VOX},
#endif
    {"Tone", MENU_TONE},
    {"STE", MENU_STE},
    {"RP STE", MENU_RP_STE},
    {"RxMode", MENU_TDR},
    {"Sql", MENU_SQL},
    {"SQLSCN", MENU_SQLSCN},
    {"SQLHYS", MENU_SQLHYS},
#ifdef ENABLE_FEAT_F4HWN
    {"SetCtr", MENU_SET_CTR},
    {"SetInv", MENU_SET_INV},
#endif
#ifdef ENABLE_FEAT_F4HWN
    {"BtnInv", MENU_BTN_INV},
#endif
#ifdef ENABLE_FEAT_F4HWN
    {"SetLck", MENU_SET_LCK},
#endif
    {"SetMet", MENU_METER_STYLE},
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    {"SetRxA", MENU_SET_AUD},
    {"SetRxG", MENU_SET_RXG}, // RX AF noise gate
#endif
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    {"SetTmr", MENU_SET_TMR},
#endif
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    {"SetNFM", MENU_SET_NFM},
#endif
    {"SetVol", MENU_SET_VOLUME},
    // Hidden maintenance entries remain at the end.
    {"F Lock", MENU_F_LOCK},
#ifdef ENABLE_F_CAL_MENU
    {"FrCali", MENU_F_CALI},
#endif
    {"BatCal", MENU_BATCAL},
    {"BatTyp", MENU_BATTYP},
    {"Reset", MENU_RESET},
    {"", 0xff}};

void UI_MENU_InitList(void)
{
    gMenuListCount = 0;
    while (gMenuListCount < 255 && FP_Byte(MenuList[gMenuListCount].name))
        ++gMenuListCount;
}

const uint8_t FIRST_HIDDEN_MENU_ITEM = MENU_F_LOCK;

const char gSubMenu_SFT_D[][4] FP_RESOURCE = {"OFF", "+", "-"};

const char gSubMenu_W_N[][7] FP_RESOURCE = {"WIDE", "NARROW"};

const char gSubMenu_LED[][9] FP_RESOURCE = {"ON", "SOS", "BEACON", "OFF"};

const char gSubMenu_OFF_ON[][4] FP_RESOURCE = {"OFF", "ON"};

const char gSubMenu_NA[4] = {"N/A"};

const char gSubMenu_PONMSG[][8] FP_RESOURCE = {"ON", "LOGO", "OFF"};
const char gSubMenu_PONSCR[][15] FP_RESOURCE = {"NORMAL",   "SPECTRE LIST", "SPECTRE BAND", "SPECTRE RANGE",
                                                 "SENTINEL", "FM RADIO"};

const char gSubMenu_RXMode[][11] FP_RESOURCE = {"SINGLE", "DUAL", "FULL WATCH", "ML WATCH"};

const char gSubMenu_TONE[][6] FP_RESOURCE = {"OFF", "1750"};

const char gSubMenu_ROGER[][9] FP_RESOURCE = {"OFF", "STARWARS", "R2D2", "FALCON", "ROGGER", "OURO"};
const char gSubMenu_AUTO_ROGER[7][6] FP_RESOURCE = {"OFF", "10sec", "30sec", "2mn", "5mn", "10mn", "30mn"};

const char gSubMenu_RESET[][5] FP_RESOURCE = {"NoCH", "ALL"};

const char gSubMenu_F_LOCK[F_LOCK_LEN][16] FP_RESOURCE = {
    "UNLOCK\nALL", // 0 = F_LOCK_NONE    — TX everywhere
    "LOCK\nALL",   // 1 = F_LOCK_ALL     — TX blocked
    "VHF-UHF",     // 2 = F_LOCK_136_500
    "PMR + LPD",   // 3 = F_LOCK_PMR_LPD
};

const char gSubMenu_RX_TX[][6] FP_RESOURCE = {"OFF", "TX", "RX", "TX/RX"};

const char gSubMenu_BAT_TXT[][10] FP_RESOURCE = {"NONE", "VOLTAGE", "PERCENT", "VOLT + %"};

const char gSubMenu_BATTYP[5][16] FP_RESOURCE = {"1400mAh\nUV-K1", "2500mAh\nUV-K1", "1600mAh\nUV-K5",
                                                 "2200mAh\nUV-K5", "3500mAh\nUV-K5"};

// Menu order differs from the existing EEPROM/profile IDs. Keep those IDs stable.
const uint8_t gSubMenu_BATTYP_profiles[5] = {BATTERY_TYPE_1400_MAH_SONIC, BATTERY_TYPE_2500_MAH,
                                             BATTERY_TYPE_1600_MAH, BATTERY_TYPE_2200_MAH,
                                             BATTERY_TYPE_3500_MAH};

const char gSubMenu_SCRAMBLER[11][8] FP_RESOURCE = {"OFF",    "2600Hz", "2700Hz", "2800Hz",
                                                    "2900Hz", "3000Hz", "3100Hz", "3200Hz",
                                                    "3300Hz", "3400Hz", "3500Hz"};

#ifdef ENABLE_FEAT_F4HWN

// const char gSubMenu_SET_PTT[][8] FP_RESOURCE =
// {
//     "CLASSIC",
//     "ONEPUSH"
// };// PTTDEL

const char gSubMenu_SET_TOT[][7] = {"OFF", "SOUND", "VISUAL", "ALL"};

const char gSubMenu_SET_AUD_FM[5][8] FP_RESOURCE = {"FLAT", "CLEAN", "MID", "BOOST", "MAX"};

const char gSubMenu_SET_AUD_AM[3][8] FP_RESOURCE = {"SHARP", "STOCK", "OPEN"};

const char gSubMenu_SET_LCK[][9] FP_RESOURCE = {"KEYS", "KEYS+PTT"};


#ifdef ENABLE_FEAT_F4HWN_NARROWER
const char gSubMenu_SET_NFM[][9] FP_RESOURCE = {"NARROW", "NARROWER"};
#endif

#endif

const t_sidefunction gSubMenu_SIDEFUNCTIONS[] = {
    {"NONE", ACTION_OPT_NONE},
#ifdef ENABLE_FLASHLIGHT
    {"FLASH\nLIGHT", ACTION_OPT_FLASHLIGHT},
#endif
    {"POWER", ACTION_OPT_POWER},
    {"MONITOR", ACTION_OPT_MONITOR},
#ifdef ENABLE_FMRADIO
    {"FM RADIO", ACTION_OPT_FM},
#endif
    {"LOCK\nKEYPAD", ACTION_OPT_KEYLOCK},
    {"FREQ / MEM", ACTION_OPT_VFO_MR},
    {"MODE", ACTION_OPT_SWITCH_DEMODUL},
#ifdef ENABLE_FEAT_F4HWN
    {"PTT TOGGLE", ACTION_OPT_PTT},
    {"WIDE\nNARROW", ACTION_OPT_WN},

#endif
    {"ScanLst Up", ACTION_OPT_SCANLIST_UP},
    {"ScanLst Dw", ACTION_OPT_SCANLIST_DOWN},
    {"RXMOD", ACTION_OPT_SINGLE_DUO},
    {"SCAN", ACTION_OPT_SCAN},
    {"SCAN ALL", ACTION_OPT_SCAN_ALL},
    {"PTT 2", ACTION_OPT_PTT2},
    {"HISTORY", ACTION_OPT_HISTORY},
#ifdef ENABLE_FALCON_MESSAGES
    {"MESSAGES", ACTION_OPT_MESSAGES},
#endif
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    {"FMESSAGE", ACTION_OPT_FMESSAGE},
#endif
#ifdef ENABLE_VOX
    {"VOX", ACTION_OPT_VOX},
#endif
    {"REVERSE FQ", ACTION_OPT_REVERSE},
    {"VFO A/B", ACTION_OPT_VFO_AB}, // last: not offered to M Long
};

const uint8_t gSubMenu_SIDEFUNCTIONS_size = ARRAY_SIZE(gSubMenu_SIDEFUNCTIONS);

bool gIsInSubMenu;
uint8_t gMenuCursor;
int UI_MENU_GetCurrentMenuId()
{
    if (gMenuCursor < ARRAY_SIZE(MenuList))
        return FP_Byte(&MenuList[gMenuCursor].menu_id);

    return FP_Byte(&MenuList[ARRAY_SIZE(MenuList) - 1].menu_id);
}

int32_t gSubMenuSelection;

// edit box
char edit_original[17]; // a copy of the text before editing so that we can easily test for changes/difference
char edit[17];
int edit_index;

#ifdef ENABLE_CUSTOM_MENU_LAYOUT
#include "menu_pe.inc"
#else
void UI_DisplayMenu(void)
{
    const unsigned int menu_list_width = 6; // max no. of characters on the menu list (left side)
    const unsigned int menu_item_x1 = (9 * menu_list_width) + 3;
    const unsigned int menu_item_x2 = LCD_WIDTH - 1;
    unsigned int i;
    char String[64]; // bigger cuz we can now do multi-line in one string (use '\n' char)
    const int m = UI_MENU_GetCurrentMenuId();
    UI_DisplayClear();

#ifndef ENABLE_CUSTOM_MENU_LAYOUT
    // original menu layout
    for (i = 0; i < 3; i++)
        if (gMenuCursor > 0 || i > 0)
            if ((gMenuListCount - 1) != gMenuCursor || i != 2)
                UI_PrintString(MenuList[gMenuCursor + i - 1].name, 0, 0, i * 2, 8);

    // invert the current menu list item pixels
    for (i = 0; i < (8 * menu_list_width); i++) {
        gFrameBuffer[2][i] ^= 0xFF;
        gFrameBuffer[3][i] ^= 0xFF;
    }

    // draw vertical separating dotted line
    for (i = 0; i < 7; i++)
        gFrameBuffer[i][(8 * menu_list_width) + 1] = 0xAA;

    // draw the little sub-menu triangle marker
    if (gIsInSubMenu)
        memcpy(gFrameBuffer[0] + (8 * menu_list_width) + 1, BITMAP_CurrentIndicator,
               sizeof(BITMAP_CurrentIndicator));

    // draw the menu index number/count
    sprintf(String, "%2u.%u", 1 + gMenuCursor, gMenuListCount);

    UI_PrintStringSmallNormal(String, 2, 0, 6);

#else
    { // custom menu layout: 4 visible items, selected at row 2, inverted
        const int menu_index = gMenuCursor;

        // Draw 4 items: -1, 0(selected), +1, +2 at rows 1..4
        for (int slot = 1; slot < 5; slot++) {
            const int k = menu_index + slot - 2; // item index relative to selected

            // resolve wrap-around
            int ki = k;
            if (ki < 0)
                ki += (int)gMenuListCount;
            else if (ki >= (int)gMenuListCount)
                ki -= (int)gMenuListCount;

            if (ki < 0 || ki >= (int)gMenuListCount)
                continue;

            const unsigned int row = (unsigned int)slot;
            if (slot == 2)
                UI_PrintStringSmallBold(MenuList[ki].name, 0, 0, row); // selected — bold
            else
                UI_PrintStringSmallNormal(MenuList[ki].name, 0, 0, row); // others — normal
        }

        // Invert selected row (row 2), left side only
        for (i = 0; i < (unsigned int)(8 * menu_list_width); i++)
            gFrameBuffer[2][i] ^= 0xFF;

        // Sub-menu triangle marker at separator, same row as selected
        if (gIsInSubMenu)
            memcpy(gFrameBuffer[2] + (8 * menu_list_width) + 1, BITMAP_CurrentIndicator,
                   sizeof(BITMAP_CurrentIndicator));

        // Index counter at row 6
        sprintf(String, "%02u/%u", 1 + gMenuCursor, gMenuListCount);
        UI_PrintStringSmallNormal(String, 6, 0, 6);
        // Center the seven-pixel counter ink at y=50 in the footer (y=45..55).
        for (uint8_t x = 7; x < 6 + strlen(String) * 7; ++x) {
            const uint8_t pixels = gFrameBuffer[6][x];
            gFrameBuffer[5][x] |= pixels << 7;
            gFrameBuffer[6][x] = pixels >> 1;
        }
    }
#endif

    // **************

    memset(String, 0, sizeof(String));

    bool already_printed = false;

    /* Brightness is set to max in some entries of this menu. Return it to the configured brightness
       level the "next" time we enter here.I.e., when we move from one menu to another.
       It also has to be set back to max when pressing the Exit key. */

    BACKLIGHT_TurnOn();

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    uint8_t gaugeLine = 0;
    uint8_t gaugeMin = 0;
    uint8_t gaugeMax = 0;
    //#endif

    switch (m) {
        case MENU_TXP:
            strcpy(String, gPowerNames[POWER_FromIndex(gSubMenuSelection)]);
            break;


        case MENU_SQLHYS:
            sprintf(String, "%u dbm", (unsigned)gSubMenuSelection);
            break;
        case MENU_SQLSCN:
            sprintf(String, "%d dbm", (int)gSubMenuSelection - 130);
            break;
        case MENU_SQL:
            sprintf(String, "%d", gSubMenuSelection);
            break;

        case MENU_MIC:
            // display the mic gain in actual dB rather than just an index number
            const uint8_t mic = gMicGain_dB2[gSubMenuSelection];
            sprintf(String, "+%u.%udB", mic / 2, (mic % 2) * 5);
            gaugeLine = 4;
            gaugeMin = 0;
            gaugeMax = 8;
            break;

        case MENU_STEP: {
            uint16_t step = gStepFrequencyTable[FREQUENCY_GetStepIdxFromSortedIdx(gSubMenuSelection)];
            sprintf(String, "%d.%02ukHz", step / 100, step % 100);
            break;
        }

        case MENU_R_DCS:
        case MENU_T_DCS:
            if (gSubMenuSelection == 0)
                strcpy(String, gSubMenu_OFF_ON[0]);
            else if (gSubMenuSelection < 105)
                sprintf(String, "D%03oN", DCS_GetOption(gSubMenuSelection - 1));
            else
                sprintf(String, "D%03oI", DCS_GetOption(gSubMenuSelection - 105));
            break;

        case MENU_R_CTCS:
        case MENU_T_CTCS: {
            if (gSubMenuSelection == 0)
                strcpy(String, gSubMenu_OFF_ON[0]);
            else
                sprintf(String, "%u.%uHz", CTCSS_Options[gSubMenuSelection - 1] / 10,
                        CTCSS_Options[gSubMenuSelection - 1] % 10);
            break;
        }

        case MENU_SFT_D:
            strcpy(String, gSubMenu_SFT_D[gSubMenuSelection]);
            break;

        case MENU_OFFSET:
            if (!gIsInSubMenu || gInputBoxIndex == 0) {
                sprintf(String, "%3d.%05u", gSubMenuSelection / 100000, abs(gSubMenuSelection) % 100000);
                UI_PrintStringSmallBold(String, menu_item_x1, menu_item_x2, 2);
            } else {
                const char *ascii = INPUTBOX_GetAscii();
                sprintf(String, "%.3s.%.3s  ", ascii, ascii + 3);
                UI_PrintStringSmallBold(String, menu_item_x1, menu_item_x2, 2);
            }

            UI_PrintStringSmallBold("MHz", menu_item_x1, menu_item_x2, 3);

            already_printed = true;
            break;

        case MENU_W_N:
            strcpy(String, gSubMenu_W_N[gSubMenuSelection]);
            break;

        case MENU_SCR:
            strcpy(String, gSubMenu_SCRAMBLER[gSubMenuSelection]);
            if (gSubMenuSelection > 0)
                BK4819_EnableScramble(gSubMenuSelection - 1);
            else
                BK4819_DisableScramble();
            break;
        case MENU_ABR:
            if (gSubMenuSelection == 0) {
                strcpy(String, gSubMenu_OFF_ON[0]);
            } else if (gSubMenuSelection < 61) {
                sprintf(String, "%02dm:%02ds", (((gSubMenuSelection) * 5) / 60),
                        (((gSubMenuSelection) * 5) % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(4, 1, 60, gSubMenuSelection);
                gaugeLine = 4;
                gaugeMin = 1;
                gaugeMax = 60;
                //#endif
            } else {
                strcpy(String, "ON");
            }

            // Obsolete ???
            //if(BACKLIGHT_GetBrightness() < 4)
            //    BACKLIGHT_SetBrightness(4);
            break;

        case MENU_ABR_MIN:
        case MENU_ABR_MAX:
            sprintf(String, "%d", gSubMenuSelection);
            if (gIsInSubMenu)
                BACKLIGHT_SetBrightness(gSubMenuSelection);
            // Obsolete ???
            //else if(BACKLIGHT_GetBrightness() < 4)
            //    BACKLIGHT_SetBrightness(4);
            break;

        case MENU_AM:
            strcpy(String, gModulationStr[gSubMenuSelection]);
            break;

        case MENU_AUTOLK:
            if (gSubMenuSelection == 0)
                strcpy(String, gSubMenu_OFF_ON[0]);
            else {
                sprintf(String, "%02dm:%02ds", ((gSubMenuSelection * 15) / 60),
                        ((gSubMenuSelection * 15) % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(4, 1, 40, gSubMenuSelection);
                gaugeLine = 4;
                gaugeMin = 1;
                gaugeMax = 40;
                //#endif
            }
            break;

        case MENU_COMPAND:
        case MENU_ABR_ON_TX_RX:
            strcpy(String, gSubMenu_RX_TX[gSubMenuSelection]);
            break;
        case MENU_STE:
        case MENU_D_ST:
#ifdef ENABLE_FEAT_F4HWN
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER //calypso
        case MENU_SET_TMR:
#endif
#endif
            strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            break;

        case MENU_MEM_CH:
        case MENU_DEL_CH: {
            if (gSubMenuSelection == MR_CHANNELS_MAX) {
                UI_PrintString("None", menu_item_x1, menu_item_x2, 2, 8);
                already_printed = true;
                break;
            } else {
                const bool valid = RADIO_CheckValidChannel(gSubMenuSelection, false, 0);

                UI_GenerateChannelStringEx(String, valid, gSubMenuSelection);
                UI_PrintStringSmallBold(String, menu_item_x1, menu_item_x2, 1);

                if (valid &&
                    !gAskForConfirmation) { // show the frequency so that the user knows the channels frequency
                    const uint32_t frequency = SETTINGS_FetchChannelFrequency(gSubMenuSelection);
                    sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
                    UI_PrintStringSmallBold(String, menu_item_x1, menu_item_x2, 3);
                }

                SETTINGS_FetchChannelName(String, gSubMenuSelection);
                UI_PrintStringSmallBold(String[0] ? String : "--", menu_item_x1, menu_item_x2, 2);
                already_printed = true;
                break;
            }
        }

        case MENU_MEM_NAME: {
            const bool valid = RADIO_CheckValidChannel(gSubMenuSelection, false, 0);

            UI_GenerateChannelStringEx(String, valid, gSubMenuSelection);
            UI_PrintStringSmallBold(String, menu_item_x1, menu_item_x2, 1);

            if (valid) {
                const uint32_t frequency = SETTINGS_FetchChannelFrequency(gSubMenuSelection);

                //if (!gIsInSubMenu || edit_index < 0)
                if (!gIsInSubMenu)
                    edit_index = -1;
                if (edit_index < 0) { // show the channel name
                    SETTINGS_FetchChannelName(String, gSubMenuSelection);
                    char *pPrintStr = String[0] ? String : "--";
                    UI_PrintStringSmallBold(pPrintStr, menu_item_x1, menu_item_x2, 2);
                } else { // show the channel name being edited
                    //UI_PrintString(edit, menu_item_x1, 0, 2, 8);
                    UI_PrintStringSmallBold(edit, menu_item_x1, menu_item_x2, 2);
                    if (edit_index < CHANNEL_NAME_LEN)
                        //UI_PrintString("^", menu_item_x1 + (8 * edit_index), 0, 4, 8);  // show the cursor
                        UI_PrintStringSmallBold("^", menu_item_x1 - 1 + (8 * edit_index), 0,
                                                4); // show the cursor
                }

                if (!gAskForConfirmation) { // show the frequency so that the user knows the channels frequency
                    sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
                    UI_PrintStringSmallBold(String, menu_item_x1, menu_item_x2,
                                            3 + (gIsInSubMenu && edit_index >= 0));
                }
            }

            already_printed = true;
            break;
        }

        case MENU_SAVE:
            sprintf(String, gSubMenuSelection == 0 ? gSubMenu_OFF_ON[0] : "1:%u", gSubMenuSelection);
            break;

        case MENU_TDR:
            strcpy(String, gSubMenu_RXMode[gSubMenuSelection]);
            break;

        case MENU_TONE:
            strcpy(String, gSubMenu_TONE[gSubMenuSelection]);
            break;

        case MENU_TOT:
            sprintf(String, "%02dm:%02ds", (((gSubMenuSelection + 1) * 5) / 60),
                    (((gSubMenuSelection + 1) * 5) % 60));
            //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
            //ST7565_Gauge(4, 5, 179, gSubMenuSelection);
            gaugeLine = 4;
            gaugeMin = 5;
            gaugeMax = 179;
            //#endif
            break;

        case MENU_RP_STE:
            sprintf(String, gSubMenuSelection == 0 ? gSubMenu_OFF_ON[0] : "%u*100ms", gSubMenuSelection);
            break;

        case MENU_LIST_CH:
            if (gSubMenuSelection == MR_CHANNELS_LIST + 1) {
                strcpy(String, "ALL");
            } else if (gSubMenuSelection == 0) {
                strcpy(String, "OFF");
            } else if (gSubMenuSelection >= 1 && gSubMenuSelection <= MR_CHANNELS_LIST) {
                char cleanName[10];
                const char *rawName = gListName[gSubMenuSelection - 1];

                uint8_t i;
                for (i = 0; i < 6; i++) {
                    char c = rawName[i];
                    if (c == '\0' || (uint8_t)c == 0xFF) {
                        break;
                    }
                    cleanName[i] = c;
                }
                cleanName[i] = '\0';

                if (cleanName[0] == '\0') {
                    sprintf(String, "%02u", gSubMenuSelection);
                } else {
                    sprintf(String, "%02u\n%s", gSubMenuSelection, cleanName);
                }
            } else {
                strcpy(String, "N/A");
            }
            break;

        case MENU_LED:
            strcpy(String, gSubMenu_LED[gSubMenuSelection]);
            break;

        case MENU_BAT_TXT:
            strcpy(String, gSubMenu_BAT_TXT[gSubMenuSelection]);
            break;


        case MENU_PONMSG:
            strcpy(String, gSubMenu_PONMSG[gSubMenuSelection]);
            break;

        case MENU_PONSCR:
            strcpy(String, gSubMenu_PONSCR[gSubMenuSelection]);
            break;

        case MENU_AUTO_ROGER:
            strcpy(String, gSubMenu_AUTO_ROGER[gSubMenuSelection]);
            break;
#ifdef ENABLE_VOX
        case MENU_VOX:
            sprintf(String, gSubMenuSelection == 0 ? gSubMenu_OFF_ON[0] : "%u", gSubMenuSelection);
            break;
#endif
        case MENU_ROGER:
            strcpy(String, gSubMenu_ROGER[gSubMenuSelection]);
            break;

        case MENU_RESET:
            strcpy(String, gSubMenu_RESET[gSubMenuSelection]);
            break;

        case MENU_F_LOCK: // allow everything
            strcpy(String, gSubMenu_F_LOCK[gSubMenuSelection]);
            break;

#ifdef ENABLE_F_CAL_MENU
        case MENU_F_CALI: {
            const uint32_t value = 22656 + gSubMenuSelection;
            const uint32_t xtal_Hz = (0x4f0000u + value) * 5;

            writeXtalFreqCal(gSubMenuSelection, false);

            sprintf(String, "%d\n%u.%06u\nMHz", gSubMenuSelection, xtal_Hz / 1000000, xtal_Hz % 1000000);
        } break;
#endif

        case MENU_BATCAL: {
            const uint16_t vol =
                (uint32_t)gBatteryVoltageAverage * gBatteryCalibration[3] / gSubMenuSelection;
            sprintf(String, "%u.%02uV\n%u", vol / 100, vol % 100, gSubMenuSelection);
            break;
        }

        case MENU_BATTYP:
            strcpy(String, gSubMenu_BATTYP[gSubMenuSelection]);
            break;

        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
            strcpy(String, gSubMenu_SIDEFUNCTIONS[gSubMenuSelection].name);
            break;


#ifdef ENABLE_FEAT_F4HWN

        case MENU_SET_CTR:
            sprintf(String, "%u", (unsigned)gSubMenuSelection);
            break;

        case MENU_SET_INV:
#ifdef ENABLE_FEAT_F4HWN_INV
            strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            ST7565_ContrastAndInv();
#else
            strcpy(String, gSubMenu_NA);
#endif
            break;

        case MENU_BTN_INV:
            strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            break;

            /*  case MENU_TX_LOCK:
            if(TX_freq_check(gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Frequency) == 0)
            {
                strcpy(String, "Inside\nF Lock\nPlan");
            }
            else
            {
                strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            }
            break;
      */
        case MENU_SET_LCK:
            strcpy(String, gSubMenu_SET_LCK[gSubMenuSelection]);
            break;

        case MENU_SET_MET:
            strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            break;

        case MENU_SET_RXG:
            strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            break;

        case MENU_SET_AUD:
            if (gTxVfo->Modulation == MODULATION_AM)
                strcpy(String, gSubMenu_SET_AUD_AM[gSubMenuSelection]);
            else if (gTxVfo->Modulation == MODULATION_USB)
                strcpy(String, "N/A");
            else
                strcpy(String, gSubMenu_SET_AUD_FM[gSubMenuSelection]);
            break;

#ifdef ENABLE_FEAT_F4HWN_NARROWER
        case MENU_SET_NFM:
            strcpy(String, gSubMenu_SET_NFM[gSubMenuSelection]);
            break;
#endif

#endif
    }

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    if (gaugeLine != 0) {
        ST7565_Gauge(gaugeLine, gaugeMin, gaugeMax, gSubMenuSelection);
    }
    //#endif

    if (!already_printed) { // we now do multi-line text in a single string

        unsigned int y;
        unsigned int lines = 1;
        unsigned int len = strlen(String);
        bool small = false;

        if (len > 0) {
            // count number of lines
            for (i = 0; i < len; i++) {
                if (String[i] == '\n' && i < (len - 1)) { // found new line char
                    lines++;
                    String[i] = 0; // null terminate the line
                }
            }

            if (lines > 3) { // use small text
                small = true;
                if (lines > 7)
                    lines = 7;
            }

            // center vertically'ish
            /*
            if (small)
                y = 3 - ((lines + 0) / 2);  // untested
            else
                y = 2 - ((lines + 0) / 2);
            */

            // center vertically in rows 0..5 (row 6 = counter)
            y = (6 - (lines < 6 ? lines : 6)) / 2;

            // only for SysInf
            if (m == MENU_VOL) {
                sprintf(edit, "%u.%02uV %u%%", gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                        BATTERY_VoltsToPercent(gBatteryVoltageAverage));

                UI_PrintStringSmallNormal(edit, 54, 127, 1);

#ifdef ENABLE_FEAT_F4HWN
                UI_PrintStringSmallNormal(Edition, 54, 127, 6);
#endif

                y = 2;
            }

            // draw the text lines
            for (i = 0; i < len && lines > 0; lines--) {
                if (small)
                    UI_PrintStringSmallNormal(String + i, menu_item_x1, menu_item_x2, y);
                else
                    UI_PrintStringSmallBold(String + i, menu_item_x1, menu_item_x2, y);

                // look for start of next line
                while (i < len && String[i] >= 32)
                    i++;

                // hop over the null term char(s)
                while (i < len && String[i] < 32)
                    i++;

                y += 1;
            }
        }
    }

    if ((m == MENU_R_CTCS || m == MENU_R_DCS) && gCssBackgroundScan)
        UI_PrintStringSmallBold("SCAN", menu_item_x1, menu_item_x2, 4);

    if ((m == MENU_RESET || m == MENU_MEM_CH || m == MENU_MEM_NAME || m == MENU_DEL_CH) &&
        gAskForConfirmation) {
        char *pPrintStr = (gAskForConfirmation == 1) ? "SURE?" : "WAIT!";
        UI_PrintStringSmallBold(pPrintStr, menu_item_x1, menu_item_x2, 4);
    }

    //*******************LINES***************** */

    for (uint8_t y = 4; y <= 57; y += 2) {
        UI_DrawLineBuffer(gFrameBuffer, 49, y, 49, y, 1); // Left vertical dashed line (X = 30)
    }

    for (uint8_t i = 0; i <= 127; i += 2) {
        UI_DrawLineBuffer(gFrameBuffer, i, 2, i, 2, 1); // Hory X
    }

    for (uint8_t i = 0; i <= 47; i += 2) {
        UI_DrawLineBuffer(gFrameBuffer, i, 44, i, 44, 1); // Hory X
    }

    for (uint8_t i = 51; i <= 127; i += 2) {
        UI_DrawLineBuffer(gFrameBuffer, i, 44, i, 44, 1); // Hory X
    }
    UI_DrawMenuEdition();

    ST7565_BlitFullScreen();
}

#endif
