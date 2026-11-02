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

#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

typedef struct {
    const char name[7]; // menu display area only has room for 6 characters
    uint8_t menu_id;
} t_menu_item;

enum {
    MENU_LED = 0,
    MENU_SQL,
    MENU_STEP,
    MENU_R_DCS,
    MENU_R_CTCS,
    MENU_T_DCS,
    MENU_T_CTCS,
    MENU_SFT_D,
    MENU_OFFSET,
    MENU_TOT,
    MENU_W_N,
    MENU_SCR,
    MENU_MEM_CH,
    MENU_DEL_CH,
    MENU_MEM_NAME,
    MENU_SAVE,
    MENU_ABR,
    MENU_ABR_ON_TX_RX,
    MENU_ABR_MIN,
    MENU_ABR_MAX,
    MENU_AUTOLK,
    MENU_LIST_CH,
    MENU_STE,
    MENU_RP_STE,
    MENU_MIC,
    MENU_COMPAND,
    MENU_D_ST,
    MENU_PONMSG,
    MENU_PONSCR,
    MENU_ROGER,
    MENU_AUTO_ROGER,
#ifdef ENABLE_VOX
    MENU_VOX,
#endif
    MENU_TDR,
    MENU_TONE,
    MENU_VOL,
    MENU_BAT_TXT,
    MENU_AM,

#ifndef ENABLE_FEAT_F4HWN
#endif
    MENU_RESET,
    MENU_F_LOCK,
#ifdef ENABLE_F_CAL_MENU
    MENU_F_CALI,
#endif
#ifdef ENABLE_FEAT_F4HWN
    MENU_SET_PWR,
    MENU_SET_CTR,
    MENU_SET_INV,
    MENU_BTN_INV,
    MENU_SET_LCK,
    MENU_SET_MET,
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    MENU_SET_TMR,
    MENU_SET_AUD,
    MENU_SET_RXG,
#endif
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    MENU_SET_NFM,
#endif
#endif
    MENU_BATCAL, // battery voltage calibration
    MENU_F1SHRT,
    MENU_F1LONG,
    MENU_F2SHRT,
    MENU_F2LONG,
    MENU_MLONG,
    MENU_BATTYP,
    MENU_TXP,
    MENU_METER_STYLE,
    MENU_SET_VOLUME,
    MENU_PRICH1,
    MENU_PRICH2,
    MENU_PRICH3,
    MENU_PRICH4,
    MENU_SQLSCN,
    MENU_SQLHYS
};

static inline uint8_t MENU_PriorityIndex(uint8_t id)
{
    return (uint8_t)(id - MENU_PRICH1);
}
// Menu categories (as F4HWN): ALL is the whole MenuList, the others a subset.
enum {
    MENU_CAT_ALL = 0,
    MENU_CAT_CHANNELS,
    MENU_CAT_SCAN,
    MENU_CAT_KEYS,
    MENU_CAT_POWER,
    MENU_CAT_DISPLAY,
    MENU_CAT_TIMERS,
    MENU_CAT_AUDIO,
    MENU_CAT_RADIO,
    MENU_CAT_SERVICE,
    MENU_CAT_COUNT
};

extern uint8_t gMenuCategory;  // category whose entries are listed
extern bool gMenuCategories;   // the category screen is shown
extern const char gMenuCatNames[MENU_CAT_COUNT][9];

uint8_t UI_MENU_CategorySize(uint8_t cat);
uint8_t UI_MENU_ItemIndex(uint8_t pos);
void UI_MENU_ItemName(char *s, uint8_t pos);
void UI_MENU_ShowCategories(bool show);
void UI_MENU_Close(void);

extern const uint8_t FIRST_HIDDEN_MENU_ITEM;
extern const t_menu_item MenuList[];
extern const char gSubMenu_LED[4][9];
extern const char gSubMenu_SFT_D[3][4];
extern const char gSubMenu_W_N[2][7];
extern const char gSubMenu_OFF_ON[2][4];
extern const char gSubMenu_NA[4];
extern const char gSubMenu_TOT[11][7];

#ifdef ENABLE_FEAT_F4HWN
extern const char gSubMenu_SET_TOT[4][7];
extern const char gSubMenu_SET_LCK[2][9];
extern const char gSubMenu_SET_MET[3][8];
#ifdef ENABLE_FEAT_F4HWN_NARROWER
extern const char gSubMenu_SET_NFM[2][9];
#endif
extern const char gSubMenu_SET_AUD_FM[5][8];
extern const char gSubMenu_SET_AUD_AM[3][8];
#endif

extern const char gSubMenu_PONMSG[3][8];
extern const char gSubMenu_PONSCR[POWER_ON_SCREEN_COUNT][15];
extern const char gSubMenu_ROGER[6][9];
extern const char gSubMenu_AUTO_ROGER[7][6];
extern const char gSubMenu_RXMode[4][11];
extern const char gSubMenu_TONE[2][6];
extern const char gSubMenu_RESET[2][5];
extern const char gSubMenu_F_LOCK[F_LOCK_LEN][16];
extern const char gSubMenu_RX_TX[4][6];
extern const char gSubMenu_BAT_TXT[4][10];
extern const char gSubMenu_BATTYP[5][16];
extern const uint8_t gSubMenu_BATTYP_profiles[5];
typedef struct {
    char *name;
    uint8_t id;
} t_sidefunction;
extern const uint8_t gSubMenu_SIDEFUNCTIONS_size;
extern const t_sidefunction gSubMenu_SIDEFUNCTIONS[];

extern bool gIsInSubMenu;

extern uint8_t gMenuCursor;

extern int32_t gSubMenuSelection;

extern char edit_original[17];
extern char edit[17];
extern int edit_index;

void UI_DisplayMenu(void);
#ifdef ENABLE_CUSTOM_MENU_LAYOUT
void UI_MenuDrawHeader(void);
#endif
int UI_MENU_GetCurrentMenuId();

#endif
extern const char gSubMenu_SCRAMBLER[11][8];

void UI_MENU_InitList(void);
