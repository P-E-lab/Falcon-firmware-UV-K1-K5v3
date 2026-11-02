#include "apps/app_overlay.h"
/* Copyright 2023 Dual Tachyon
 * Licensed under the Apache License, Version 2.0.
 * V109: broadcast FM layout; the shared status bar remains unchanged.
 */
#ifdef ENABLE_FMRADIO
#include <string.h>
#include "app/fm.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/main.h"
#include "ui/ui.h"
static const char ns_fm_100[] NATIVE_DATA(fm) = "%u.%u";
static const char ns_fm_0[] NATIVE_DATA(fm) = "FM";
static const char ns_fm_1[] NATIVE_DATA(fm) = "MAN";
static const char ns_fm_2[] NATIVE_DATA(fm) = "AUTO";
static const char ns_fm_3[] NATIVE_DATA(fm) = "MHz";
static const char ns_fm_4[] NATIVE_DATA(fm) = "FM RADIO";
static const char ns_fm_5[] NATIVE_DATA(fm) = "SIG";
static const char ns_fm_6[] NATIVE_DATA(fm) = "Q";
static const char ns_fm_7[] NATIVE_DATA(fm) = "--.-";
static const char ns_fm_8[] NATIVE_DATA(fm) = "SAVED M0";
static const char ns_fm_9[] NATIVE_DATA(fm) = "VFO RX OFF";
static const char ns_fm_10[] NATIVE_DATA(fm) = "VFO RX ON";

// Compact clipped native font for the 42-pixel favourite cells and footer.
static NATIVE_CODE(fm) void FM_Text(const char *text, unsigned x, unsigned page)
{
    for (; *text && x + 6 <= 128; text++, x += 6) {
        if (*text > ' ' && *text < 127)
            FP_Read(gFontSmall[(unsigned)*text - 33], &gFrameBuffer[page][x], 6);
    }
}

static NATIVE_CODE(fm) void FM_Bar(unsigned x, unsigned count, unsigned width, unsigned filled)
{
    uint8_t *p = &gFrameBuffer[5][x];
    for (unsigned i = 0; i < count; i++, p += width + 2)
        memset(p, i < filled ? 0x3e : 0x20, width);
}

static NATIVE_CODE(fm) void FM_FrequencyText(char *text, unsigned f)
{
    sprintf(text, ns_fm_100, f / 10, f % 10);
}

static NATIVE_CODE(fm) void DisplayFMNative(void)
{
    char text[20];
    const unsigned frequency = gEeprom.FM_FrequencyPlaying;
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    UI_PrintStringSmallBold(ns_fm_0, 2, 0, 0);
    UI_PrintStringSmallBold(gFM_ManualMode ? ns_fm_1 : ns_fm_2, gFM_ManualMode ? 105 : 98, 0, 0);

    uint8_t favourite = gFM_Favourite;
    if (favourite && gFM_Memory[favourite - 1] != frequency)
        favourite = 0;
    // Identify a stored station even when reached by manual tuning or seek.
    if (!favourite)
        for (unsigned i = 0; i < 9; ++i)
            if (gFM_Memory[i] == frequency) {
                favourite = i + 1;
                break;
            }
    if (favourite) {
        text[0] = 'M';
        text[1] = '0' + favourite;
        text[2] = 0;
        UI_PrintStringSmallBold(text, 57, 0, 0);
    }

    FM_FrequencyText(text, frequency);
    const bool wide = frequency >= 1000;
    const unsigned x = wide ? 27 : 33;
    char *decimal = text + (wide ? 4 : 3);
    UI_PrintString(decimal, wide ? 69 : 62, 0, 1, 8);
    *decimal = 0;
    UI_DisplayFrequency(text, x, 1, false);
    FM_Text(ns_fm_3, wide ? 82 : 75, 2);

    if (gFmNameDisplay) {
        const char *name = FM_FindRadioName(frequency);
        if (!name)
            name = ns_fm_4;
        UI_PrintString(name, 0, 127, 3, 8);
        FM_Text(ns_fm_5, 2, 5);
        // Relative BK1080 levels: deliberately no invented S-unit/dBm scale.
        unsigned signal = gFM_SignalValid ? gFM_RSSI / 8 : 0;
        if (signal > 8)
            signal = 8;
        FM_Bar(25, 8, 4, signal);
        FM_Text(ns_fm_6, 87, 5);
        unsigned quality = gFM_SignalValid ? (gFM_Quality + 1) / 4 : 0;
        if (quality > 4)
            quality = 4;
        FM_Bar(98, 4, 5, quality);
    } else {
        // Same 1..9 keypad arrangement, with a number even when occupied.
        for (unsigned i = 0; i < 9; i++) {
            const unsigned cellX = (i % 3) * 43;
            const unsigned page = 3 + i / 3;
            uint8_t glyph[3];
            FP_Read(gFont3x5['1' - ' ' + i], glyph, 3);
            for (unsigned c = 0; c < 3; c++)
                gFrameBuffer[page][cellX + 1 + c] = glyph[c] << 1;
            if (gFM_Memory[i])
                FM_FrequencyText(text, gFM_Memory[i]);
            else
                strcpy(text, ns_fm_7);
            FM_Text(text, cellX + 9, page);
            if (favourite == i + 1)
                for (unsigned dx = 0; dx < 41; dx++)
                    gFrameBuffer[page][cellX + dx] ^= 0x7f;
        }
    }

    if (gFM_SavedSlot) {
        strcpy(text, ns_fm_8);
        text[7] += gFM_SavedSlot;
        FM_Text(text, 2, 6);

    } else {
        const bool off = gFM_No_Rx || gFM_Mute;
        UI_DrawText5x5(off ? ns_fm_9 : ns_fm_10, off ? 34 : 37, 50, off ? 10 : 9);
    }
    ST7565_BlitFullScreen();
}
#endif

void UI_DisplayFM(void)
{
    if (APP_NativeBegin(NATIVE_FM)) {
        DisplayFMNative();
        APP_NativeEnd();
    }
}
