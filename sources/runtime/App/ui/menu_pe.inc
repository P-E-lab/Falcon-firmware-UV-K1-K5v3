static uint8_t first;
/* UV-KX menu geometry, drawn into Sonic's existing framebuffer.
 * Reference: joaquimorg/UV-KX-firmware, src/ui/gui.c and menu.c.
 * No U8G2 runtime, second framebuffer, or changes to menu actions. */
#include "../font.h"
#define MENU_DRAW __attribute__((section(".text.menu_draw"), noinline, noclone))
static const char ns_menu_0[] = "%-+:=^!?_()*~";
static const char ns_menu_1[] = "MENU";
static const char ns_menu_2[] = "%02u/%02u";
static const char ns_menu_3[] = "%u.%02uV %u%%";
static const char ns_menu_4[] = "%u%%";
static const char ns_menu_5[] NATIVE_DATA(menu) = "%02u";
static const char ns_menu_6[] NATIVE_DATA(menu) = "LIGHT";
static const char ns_menu_7[] NATIVE_DATA(menu) = "LOCK KP";
static const char ns_menu_8[] NATIVE_DATA(menu) = "W / N";
static const char ns_menu_10[] NATIVE_DATA(menu) = "SURE?";
static const char ns_menu_11[] NATIVE_DATA(menu) = "WAIT!";
static const char ns_menu_12[] NATIVE_DATA(menu) = "NONE";
static const char ns_menu_13[] NATIVE_DATA(menu) = "%.3s.%.3sMHz";
static const char ns_menu_14[] NATIVE_DATA(menu) = "%u.%06u";
static const char ns_menu_15[] NATIVE_DATA(menu) = "MHz";
static const char ns_menu_16[] NATIVE_DATA(menu) = "--";
static const char ns_menu_17[] NATIVE_DATA(menu) = "^";
static const char ns_menu_18[] NATIVE_DATA(menu) = "SCAN";
static const char ns_menu_19[] NATIVE_DATA(menu) = "%02u\nITEMS";

static void MENU_DRAW MenuPixel(int x, int y, bool black)
{
    if (x < 0 || x >= 128 || y < 0 || y >= 64)
        return;
    uint8_t *p = y < 8 ? &gStatusLine[x] : &gFrameBuffer[(y - 8) / 8][x];
    const uint8_t mask = 1u << (y & 7);
    if (black)
        *p |= mask;
    else
        *p &= ~mask;
}

static void MENU_DRAW MenuBox(int x, int y, int w, int h, bool black)
{
    const int right = x + w < LCD_WIDTH ? x + w : LCD_WIDTH;
    const int bottom = y + h < LCD_HEIGHT ? y + h : LCD_HEIGHT;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    // Each LCD byte owns eight vertical pixels. Merge the rectangle a page
    // at a time, preserving every bit outside it and avoiding per-pixel calls.
    while (y < bottom) {
        const int next = (y | 7) + 1 < bottom ? (y | 7) + 1 : bottom;
        const uint8_t mask = (uint8_t)((0xffu << (y & 7)) & (0xffu >> (7 - ((next - 1) & 7))));
        uint8_t *line = y < 8 ? gStatusLine : gFrameBuffer[(y - 8) / 8];
        for (int col = x; col < right; ++col) {
            if (black)
                line[col] |= mask;
            else
                line[col] &= (uint8_t)~mask;
        }
        y = next;
    }
}

// Same 5x5 row glyphs as the Dual-VFO 001/01, including its 3-pixel slash.
static const uint8_t menuPunctuation[][5] = {
    {25, 26, 4, 11, 19}, // % (same status-bar punctuation)
    {0, 0, 31, 0, 0},    // -
    {0, 4, 14, 4, 0},    // +
    {0, 4, 0, 4, 0},     // :
    {0, 31, 0, 31, 0},   // =
    {4, 10, 17, 0, 0},   // ^
    {4, 4, 4, 0, 4},     // !
    {14, 1, 6, 0, 4},    // ?
    {0, 0, 0, 0, 31},    // _
    {2, 4, 4, 4, 2},     // (
    {8, 4, 4, 4, 8},     // )
    {0, 10, 4, 10, 0},   // *
    {0, 10, 21, 0, 0},   // ~
};

static const uint8_t *MENU_DRAW MenuGlyph(unsigned c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z')
        return gFont5x5Upper[c - 'A'];
    if (c >= '0' && c <= '9')
        return gFont5x5Digits[c - '0'];
    if (c == '.')
        return gFont5x5Dot;
    if (c == '/')
        return gFont5x5Slash;
    const char *chars = ns_menu_0;
    const char *p = c ? strchr(chars, c) : NULL;
    return p ? menuPunctuation[p - chars] : NULL;
}

static int MENU_DRAW MenuTextWidth(const char *s)
{
    int width = 0;
    for (; *s && *s != '\n'; ++s)
        width += *s == '/' ? 4 : 6;
    return width ? width - 1 : 0;
}

static void MENU_DRAW MenuText(const char *s, int x, int baseline, int end, bool black)
{
    for (; *s && *s != '\n'; ++s) {
        uint8_t rows[5];
        const uint8_t *g = MenuGlyph((uint8_t)*s);
        if (g) {
            FP_Read(g, rows, 5);
            g = rows;
        }
        const int width = *s == '/' ? 3 : 5;
        if (g) {
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < width && x + col < end; ++col)
                    if (g[row] & (1u << (width - 1 - col)))
                        MenuPixel(x + col, baseline - 5 + row, black);
        } else if ((uint8_t)*s > ' ' && (uint8_t)*s < 127) {
            uint8_t tiny[3];
            FP_Read(gFont3x5[(uint8_t)*s - 32], tiny, 3);
            // The VFO's compact punctuation fallback, centred in the same cell.
            for (int col = 0; col < 3 && x + 1 + col < end; ++col)
                for (int row = 0; row < 5; ++row)
                    if (tiny[col] & (1u << row))
                        MenuPixel(x + 1 + col, baseline - 5 + row, black);
        }
        x += width + 1;
        if (x >= end)
            break;
    }
}

static void MENU_DRAW MenuCentered(const char *s, int start, int end, int y, bool black)
{
    int first = -1, last = 0, x = 0;
    for (const char *p = s; *p && *p != '\n'; ++p) {
        const int width = *p == '/' ? 3 : 5;
        uint8_t rows[5];
        const uint8_t *g = MenuGlyph((uint8_t)*p);
        if (g) {
            FP_Read(g, rows, 5);
            g = rows;
        }
        uint8_t tiny[3] = {0};
        if (!g && (uint8_t)*p > 32 && (uint8_t)*p < 127)
            FP_Read(gFont3x5[(uint8_t)*p - 32], tiny, 3);
        unsigned ink = 0;
        if (g)
            for (unsigned row = 0; row < 5; ++row)
                ink |= g[row];
        for (int col = 0; col < width; ++col) {
            bool pixel = g ? (ink & (1u << (width - 1 - col))) != 0
                           : (uint8_t)*p > ' ' && (uint8_t)*p < 127 && col >= 1 && col <= 3 && tiny[col - 1];
            if (pixel) {
                if (first < 0)
                    first = x + col;
                last = x + col;
            }
        }
        x += width + 1;
    }
    if (first < 0)
        return;
    const int width = last - first + 1;
    MenuText(s, width < end - start ? (start + end - width) / 2 - first : start, y, end, black);
}

static void MENU_DRAW MenuDrawHeader(void)
{
    char s[20];
    memset(gStatusLine, 0, sizeof(gStatusLine));
    MenuBox(0, 0, 128, 7, true);
    // MENU on the category screen, else the category name; the counter follows it.
    const char *title = gMenuCategories ? ns_menu_1 : gMenuCatNames[gMenuCategory];
    MenuText(title, 2, 6, 128, false);
    sprintf(s, ns_menu_2, 1 + gMenuCursor, gMenuListCount);
    const int x = 9 + MenuTextWidth(title);
    MenuText(s, x, 6, 128, false);
    const int end = x + MenuTextWidth(s);
    const unsigned percent = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
    if (gSetting_battery_text == 1)
        UI_FormatVoltage(s, gBatteryVoltageAverage);
    else if (gSetting_battery_text == 3)
        sprintf(s, ns_menu_3, gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100, percent);
    else
        s[0] = 0;
    // Voltage + percent that would touch the counter: the percent alone.
    if (gSetting_battery_text == 2 || (gSetting_battery_text == 3 && 126 - MenuTextWidth(s) < end + 6))
        sprintf(s, ns_menu_4, percent);
    MenuText(s, 126 - MenuTextWidth(s), 6, 128, false);
}

/* Format only: previewing neighbouring options must never apply a setting. */
static NATIVE_CODE(menu) void MenuOption(int m, int32_t v, char *s)
{
    s[0] = 0;
    switch (m) {
        case MENU_TXP: {
            const uint8_t power = POWER_FromIndex(v);
            strcpy(s, gPowerNames[power]);
            if (v < 5) {
                strcat(s, "\n");
                strcat(s, gLowPowerWatts[v]);
            }
            break;
        }
        case MENU_METER_STYLE:
            strcpy(s, v ? "CLASSIC" : "TINY");
            break;
        case MENU_SET_VOLUME:
            if (!v)
                strcpy(s, "OFF");
            else
                sprintf(s, "%02u", (unsigned)v);
            break;
        case MENU_PRICH1:
        case MENU_PRICH2:
        case MENU_PRICH3:
        case MENU_PRICH4:
            if (v == MR_CHANNELS_MAX) {
                strcpy(s, "NONE");
                break;
            }
            SETTINGS_FetchChannelName(s, v);
            char *frequency = s + strlen(s);
            *frequency++ = '\n';
            UI_FormatFrequency(frequency, SETTINGS_FetchChannelFrequency(v));
            break;
        case MENU_SQLHYS:
            sprintf(s, "%u dbm", (unsigned)v);
            break;
        case MENU_SQLSCN:
            sprintf(s, "%d dbm", (int)v - 130);
            break;
        case MENU_LED:
            FP_CopyString(s, gSubMenu_LED[v], sizeof(gSubMenu_LED[v]));
            break;
        case MENU_SQL:
        case MENU_ABR_MIN:
        case MENU_ABR_MAX:
            sprintf(s, "%d", v);
            break;
        case MENU_STEP: {
            uint16_t step = gStepFrequencyTable[FREQUENCY_GetStepIdxFromSortedIdx(v)];
            sprintf(s, "%u.%02ukHz", step / 100, step % 100);
            break;
        }
        case MENU_R_DCS:
        case MENU_T_DCS:
            if (!v)
                strcpy(s, "OFF");
            else
                sprintf(s, "D%03o%c", DCS_GetOption(v < 105 ? v - 1 : v - 105), v < 105 ? 'N' : 'I');
            break;
        case MENU_R_CTCS:
        case MENU_T_CTCS:
            if (!v)
                strcpy(s, "OFF");
            else
                sprintf(s, "%u.%uHz", CTCSS_Options[v - 1] / 10, CTCSS_Options[v - 1] % 10);
            break;
        case MENU_SFT_D:
            FP_CopyString(s, gSubMenu_SFT_D[v], sizeof(gSubMenu_SFT_D[v]));
            break;
        case MENU_OFFSET:
            sprintf(s, "%u.%05u", v / 100000, abs(v) % 100000);
            break;
        case MENU_W_N:
            FP_CopyString(s, gSubMenu_W_N[v], sizeof(gSubMenu_W_N[v]));
            break;
        case MENU_SCR:
            FP_CopyString(s, gSubMenu_SCRAMBLER[v], sizeof(gSubMenu_SCRAMBLER[v]));
            break;
        case MENU_ABR:
            if (v == 0 || v == 61)
                strcpy(s, v ? "ON" : "OFF");
            else
                sprintf(s, "%02um:%02us", v * 5 / 60, v * 5 % 60);
            break;
        case MENU_AM:
            strcpy(s, gModulationStr[v]);
            break;
        case MENU_AUTOLK:
            if (!v)
                strcpy(s, "OFF");
            else
                sprintf(s, "%02um:%02us", v * 15 / 60, v * 15 % 60);
            break;
        case MENU_COMPAND:
        case MENU_ABR_ON_TX_RX:
            FP_CopyString(s, gSubMenu_RX_TX[v], sizeof(gSubMenu_RX_TX[v]));
            break;
        case MENU_STE:
        case MENU_D_ST:
        case MENU_SET_TMR:
        case MENU_SET_INV:
        case MENU_BTN_INV:
        case MENU_SET_MET:
        case MENU_SET_RXG:
            FP_CopyString(s, gSubMenu_OFF_ON[v != 0], sizeof(gSubMenu_OFF_ON[v != 0]));
            break;
        case MENU_MEM_CH:
        case MENU_DEL_CH:
        case MENU_MEM_NAME:
            if (v == MR_CHANNELS_MAX)
                strcpy(s, "None");
            else
                UI_GenerateChannelStringEx(s, RADIO_CheckValidChannel(v, false, 0), v);
            break;
        case MENU_SAVE:
            if (!v)
                strcpy(s, "OFF");
            else
                sprintf(s, "1:%u", v);
            break;
        case MENU_TDR:
            FP_CopyString(s, gSubMenu_RXMode[v], sizeof(gSubMenu_RXMode[v]));
            break;
        case MENU_TONE:
            FP_CopyString(s, gSubMenu_TONE[v], sizeof(gSubMenu_TONE[v]));
            break;
        case MENU_TOT:
            sprintf(s, "%02um:%02us", (v + 1) * 5 / 60, (v + 1) * 5 % 60);
            break;
        case MENU_RP_STE:
            if (!v)
                strcpy(s, "OFF");
            else
                sprintf(s, "%u*100ms", v);
            break;
        case MENU_MIC: {
            const uint8_t mic = gMicGain_dB2[v < ARRAY_SIZE(gMicGain_dB2) ? v : ARRAY_SIZE(gMicGain_dB2) - 1];
            sprintf(s, "+%u.%udB", mic / 2, mic % 2 * 5);
            break;
        }
        case MENU_LIST_CH:
            if (!v || v == MR_CHANNELS_LIST + 1)
                strcpy(s, v ? "ALL" : "OFF");
            else {
                sprintf(s, "%02u\n", v);
                unsigned i = 0;
                while (i < 6 && gListName[v - 1][i] && (uint8_t)gListName[v - 1][i] != 0xff) {
                    s[3 + i] = gListName[v - 1][i];
                    ++i;
                }
                s[3 + i] = 0;
            }
            break;
        case MENU_BAT_TXT:
            FP_CopyString(s, gSubMenu_BAT_TXT[v], sizeof(gSubMenu_BAT_TXT[v]));
            break;
        case MENU_PONMSG:
            FP_CopyString(s, gSubMenu_PONMSG[v], sizeof(gSubMenu_PONMSG[v]));
            break;
        case MENU_PONSCR:
            FP_CopyString(s, gSubMenu_PONSCR[v], sizeof(gSubMenu_PONSCR[v]));
            break;
        case MENU_ROGER:
            FP_CopyString(s, gSubMenu_ROGER[v], sizeof(gSubMenu_ROGER[v]));
            break;
        case MENU_AUTO_ROGER:
            FP_CopyString(s, gSubMenu_AUTO_ROGER[v], sizeof(gSubMenu_AUTO_ROGER[v]));
            break;
#ifdef ENABLE_VOX
        case MENU_VOX:
            if (v)
                sprintf(s, "%u", v);
            else
                FP_CopyString(s, gSubMenu_OFF_ON[0], sizeof(gSubMenu_OFF_ON[0]));
            break;
#endif
        case MENU_RESET:
            FP_CopyString(s, gSubMenu_RESET[v], sizeof(gSubMenu_RESET[v]));
            break;
        case MENU_F_LOCK:
            FP_CopyString(s, gSubMenu_F_LOCK[v], sizeof(gSubMenu_F_LOCK[v]));
            break;
#ifdef ENABLE_F_CAL_MENU
        case MENU_F_CALI:
            sprintf(s, "%d", v);
            break;
#endif
        case MENU_BATCAL: {
            const uint16_t vol = (uint32_t)gBatteryVoltageAverage * gBatteryCalibration[3] / v;
            sprintf(s, "%u.%02uV\n%u", vol / 100, vol % 100, v);
            break;
        }
        case MENU_BATTYP:
            FP_CopyString(s, gSubMenu_BATTYP[v], sizeof(gSubMenu_BATTYP[v]));
            break;
        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
            strcpy(s, gSubMenu_SIDEFUNCTIONS[v].name);
            break;
        case MENU_SET_CTR:
            sprintf(s, "%u", (unsigned)v);
            break;
        case MENU_SET_LCK:
            FP_CopyString(s, gSubMenu_SET_LCK[v], sizeof(gSubMenu_SET_LCK[v]));
            break;
        case MENU_SET_AUD:
            if (gTxVfo->Modulation == MODULATION_USB)
                strcpy(s, "N/A");
            else
                FP_CopyString(
                    s, gTxVfo->Modulation == MODULATION_AM ? gSubMenu_SET_AUD_AM[v] : gSubMenu_SET_AUD_FM[v],
                    8);
            break;
#ifdef ENABLE_FEAT_F4HWN_NARROWER
        case MENU_SET_NFM:
            FP_CopyString(s, gSubMenu_SET_NFM[v], sizeof(gSubMenu_SET_NFM[v]));
            break;
#endif
        default:
            strcpy(s, "N/A");
            break;
    }
}

// H4WHN's exact rounded six-pixel gauge, centred in this popup.
static void NATIVE_CODE(menu) MenuGauge(unsigned value, unsigned maximum)
{
    const unsigned filled = 48 + (value - 1) * 64 / (maximum - 1);
    for (unsigned x = 46; x <= 114; ++x) {
        const uint8_t pixels = x == 46 || x == 114   ? 0x0C
                               : x == 47 || x == 113 ? 0x12
                               : x <= filled         ? 0x2D
                                                     : 0x21;
        for (unsigned y = 0; y < 6; ++y)
            MenuPixel(x, 46 + y, (pixels >> y) & 1u);
    }
}

static void NATIVE_CODE(menu) MenuPopup(const char *title)
{
    // Identical double border, manually rounded corners and inverted title.
    MenuBox(34, 7, 93, 56, false);
    MenuBox(35, 8, 91, 54, true);
    MenuBox(36, 9, 88, 51, false);
    const uint8_t corners[][2] = {{35, 8},  {36, 8},  {35, 9},  {125, 8},  {124, 8},  {125, 9},
                                  {35, 61}, {36, 61}, {35, 60}, {125, 61}, {124, 61}, {125, 60}};
    for (unsigned i = 0; i < ARRAY_SIZE(corners); ++i)
        MenuPixel(corners[i][0], corners[i][1], false);
    MenuBox(36, 9, 89, 6, true);
    MenuCentered(title, 35, 125, 14, false);
}

static NATIVE_CODE(menu) void DisplayMenuNative(void)
{

    char s[64];
    const int m = UI_MENU_GetCurrentMenuId();
    UI_DisplayClear();
    MenuDrawHeader();
    BACKLIGHT_TurnOn();
    // Keep the existing selected-setting preview effects, once per redraw.
    if (m == MENU_SCR) {
        if (gSubMenuSelection)
            BK4819_EnableScramble(gSubMenuSelection - 1);
        else
            BK4819_DisableScramble();
    }
    if (m == MENU_SET_VOLUME && gIsInSubMenu) {
        const uint8_t saved = gEeprom.VOLUME_GAIN;
        gEeprom.VOLUME_GAIN = gSubMenuSelection;
        BK4819_SetRxAudioGain();
        gEeprom.VOLUME_GAIN = saved;
    }
    if (m == MENU_ABR_MAX && gIsInSubMenu)
        BACKLIGHT_SetBrightness(gSubMenuSelection);
    if (m == MENU_SET_CTR)
        ST7565_SetContrast(gIsInSubMenu ? gSubMenuSelection : gSetting_set_ctr);
    else if (m == MENU_SET_INV)
        ST7565_ContrastAndInv();
#ifdef ENABLE_F_CAL_MENU
    if (m == MENU_F_CALI)
        writeXtalFreqCal(gSubMenuSelection, false);
#endif
    if (first > gMenuCursor)
        first = gMenuCursor;
    if (gMenuCursor >= first + 6)
        first = gMenuCursor - 5;
    if (first + 6 > gMenuListCount)
        first = gMenuListCount > 6 ? gMenuListCount - 6 : 0;
    for (unsigned row = 0; row < 6 && first + row < gMenuListCount; ++row) {
        const unsigned idx = first + row;
        const int y = 15 + 9 * row;
        const bool selected = idx == gMenuCursor;
        // Categories: the name alone, in a square highlight up to the bar.
        if (selected && gMenuCategories) {
            MenuBox(0, y - 6, 60, 7, true);
        } else if (selected) {
            MenuBox(0, y - 6, 14, 7, true);
            MenuBox(17, y - 6, 43, 7, true);
            MenuPixel(13, y - 6, false);
            MenuPixel(13, y, false);
            MenuPixel(17, y - 6, false);
            MenuPixel(17, y, false);
        }
        if (!gMenuCategories) {
            sprintf(s, ns_menu_5, idx + 1);
            MenuText(s, 1, y, 12, !selected);
        }
        UI_MENU_ItemName(s, idx);
        MenuText(s, gMenuCategories ? 2 : 19, y, 60, !selected);
    }
    const int bottom = 15 + 9 * ((gMenuListCount - first < 6 ? gMenuListCount - first : 6) - 1);
    MenuBox(60, 9, 1, bottom - 8, true);
    const int valueY = (9 + bottom + 5) / 2;
    MenuOption(m, gSubMenuSelection, s);
    if (gMenuCategories)
        sprintf(s, ns_menu_19, UI_MENU_CategorySize(gMenuCursor));
    if (m == MENU_F1SHRT || m == MENU_F1LONG || m == MENU_F2SHRT || m == MENU_F2LONG || m == MENU_MLONG) {
        switch (gSubMenu_SIDEFUNCTIONS[gSubMenuSelection].id) {
            case ACTION_OPT_FLASHLIGHT:
                strcpy(s, ns_menu_6);
                break;
            case ACTION_OPT_KEYLOCK:
                strcpy(s, ns_menu_7);
                break;
            case ACTION_OPT_WN:
                strcpy(s, ns_menu_8);
                break;
            default:
                break;
        }
    }
    const char *second = strchr(s, '\n');
    if (second) {
        MenuCentered(s, 62, 127, valueY - 3, true);
        MenuCentered(second + 1, 62, 127, valueY + 4, true);
    } else
        MenuCentered(s, 62, 127, valueY, true);
    if (gIsInSubMenu || gAskForConfirmation) {
        char title[7];
        FP_CopyString(title, MenuList[UI_MENU_ItemIndex(gMenuCursor)].name, sizeof title);
        MenuPopup(title);
        if (gAskForConfirmation) {
            MenuCentered(gAskForConfirmation == 1 ? ns_menu_10 : ns_menu_11, 39, 121, 36, true);
        } else if (m == MENU_SET_VOLUME) {
            MenuOption(m, gSubMenuSelection, s);
            MenuCentered(s, 39, 121, 29, true);
            const char *detail = strchr(s, '\n');
            if (detail)
                MenuCentered(detail + 1, 39, 121, 38, true);
            if (gSubMenuSelection) {
                MenuGauge(gSubMenuSelection, 63);
            }
        } else if (m == MENU_MEM_CH || m == MENU_DEL_CH || m == MENU_MEM_NAME || MENU_PriorityIndex(m) < 4 ||
                   m == MENU_OFFSET || m == MENU_BATCAL
#ifdef ENABLE_F_CAL_MENU
                   || m == MENU_F_CALI
#endif
        ) {
            if (MENU_PriorityIndex(m) < 4) {
                if (gSubMenuSelection == MR_CHANNELS_MAX)
                    strcpy(s, ns_menu_12);
                else
                    UI_GenerateChannelStringEx(s, true, gSubMenuSelection);
            } else
                MenuOption(m, gSubMenuSelection, s);
            if (m == MENU_OFFSET && gInputBoxIndex) {
                const char *ascii = INPUTBOX_GetAscii();
                sprintf(s, ns_menu_13, ascii, ascii + 3);
            }
            MenuCentered(s, 39, 121, 25, true);
            if (m == MENU_BATCAL) {
                const char *second = strchr(s, '\n');
                if (second)
                    MenuCentered(second + 1, 39, 121, 34, true);
            }
#ifdef ENABLE_F_CAL_MENU
            if (m == MENU_F_CALI) {
                const uint32_t xtal = (0x4f0000u + 22656 + gSubMenuSelection) * 5;
                sprintf(s, ns_menu_14, xtal / 1000000, xtal % 1000000);
                MenuCentered(s, 39, 121, 34, true);
                MenuCentered(ns_menu_15, 39, 121, 43, true);
            }
#endif
            if (m == MENU_MEM_CH || m == MENU_DEL_CH || m == MENU_MEM_NAME || MENU_PriorityIndex(m) < 4) {
                const bool valid =
                    IS_MR_CHANNEL(gSubMenuSelection) && RADIO_CheckValidChannel(gSubMenuSelection, false, 0);
                if (m == MENU_MEM_NAME && valid && edit_index >= 0)
                    strcpy(s, edit);
                else if (gSubMenuSelection < MR_CHANNELS_MAX)
                    SETTINGS_FetchChannelName(s, gSubMenuSelection);
                else
                    s[0] = 0;
                MenuCentered(s[0] ? s : ns_menu_16, 39, 121, 34, true);
                if (m == MENU_MEM_NAME && valid && edit_index >= 0 && edit_index < 10) {
                    char prefix[17];
                    memcpy(prefix, edit, edit_index);
                    prefix[edit_index] = 0;
                    MenuText(ns_menu_17,
                             (160 - MenuTextWidth(edit)) / 2 + MenuTextWidth(prefix) + (edit_index ? 1 : 0),
                             43, 122, true);
                }
                if (valid) {
                    uint32_t f = SETTINGS_FetchChannelFrequency(gSubMenuSelection);
                    UI_FormatFrequency(s, f);
                    MenuCentered(s, 39, 121, 52, true);
                }
            }
        } else {
            int32_t min, max;
            if (MENU_GetLimits(m, &min, &max) == 0) {
                const bool multiline = m == MENU_BATTYP || m == MENU_TXP;
                const int capacity = multiline ? 3 : 5;
                const int visible = max - min + 1 < capacity ? max - min + 1 : capacity;
                int32_t start = gSubMenuSelection - (multiline ? 1 : 2);
                if (start < min)
                    start = min;
                if (start > max - visible + 1)
                    start = max - visible + 1;
                const int pitch = multiline ? 14 : 9;
                const int top = 22 + (44 - ((visible - 1) * pitch + (multiline ? 13 : 7))) / 2;
                for (int row = 0; row < visible; ++row) {
                    const int32_t value = start + row;
                    const int y = top + row * pitch;
                    const bool selected = value == gSubMenuSelection;
                    MenuOption(m, value, s);
                    if (selected) {
                        // Rounded as the editors' selections: the four corner pixels off.
                        const int height = multiline ? 13 : 7;
                        MenuBox(39, y - 6, 82, height, true);
                        for (unsigned c = 0; c < 4; ++c)
                            MenuPixel(c & 1 ? 120 : 39, y - 6 + (c & 2 ? height - 1 : 0), false);
                    }
                    if (multiline) {
                        MenuCentered(s, 39, 121, y, !selected);
                        const char *line = strchr(s, '\n');
                        if (line)
                            MenuCentered(line + 1, 39, 121, y + 6, !selected);
                    } else {
                        for (char *p = s; *p; ++p)
                            if (*p == '\n')
                                *p = ' ';
                        MenuCentered(s, 39, 121, y, !selected);
                    }
                }
            }
        }
        if (gCssBackgroundScan && (m == MENU_R_CTCS || m == MENU_R_DCS))
            MenuCentered(ns_menu_18, 39, 121, 56, true);
    }
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

void UI_MenuDrawHeader(void)
{
    MenuDrawHeader();
}
void UI_DisplayMenu(void)
{
    if (APP_NativeBegin(NATIVE_MENU)) {
        DisplayMenuNative();
        APP_NativeEnd();
    } else {
        APP_MenuOpen();
        gRequestDisplayScreen = DISPLAY_MAIN;
    }
}
