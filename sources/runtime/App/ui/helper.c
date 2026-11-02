//H
#include <string.h>

#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font.h"
#include "ui/helper.h"
#include "ui/main.h"
#include "ui/inputbox.h"
#include "misc.h"
#include "settings.h"

void UI_GenerateChannelStringEx(char *pString, const bool bShowPrefix, const uint16_t ChannelNumber)
{
    if (gInputBoxIndex > 0) {
        for (unsigned int i = 0; i < 4; i++) {
            pString[i] = (gInputBox[i] == 10) ? '-' : gInputBox[i] + '0';
        }

        pString[4] = 0;
        return;
    }

    if (bShowPrefix) {
        // BUG here? Prefixed NULLs are allowed
        sprintf(pString, "CH-%04u", ChannelNumber + 1);
    } else if (ChannelNumber == MR_CHANNEL_LAST + 1) {
        strcpy(pString, "None");
    } else if (ChannelNumber == 0xFFFF) {
        strcpy(pString, "NULL");
    } else {
        sprintf(pString, "%04u", ChannelNumber + 1);
    }
}

void UI_PrintStringBuffer(const char *pString, uint8_t *buffer, uint32_t char_width, const uint8_t *font)
{
    const size_t Length = strlen(pString);
    const unsigned int char_spacing = char_width + 1;
    for (size_t i = 0; i < Length; i++) {
        const unsigned int index = pString[i] - ' ' - 1;
        if (pString[i] > ' ' && pString[i] < 127) {
            const uint32_t offset = i * char_spacing + 1;
            FP_Read(font + index * char_width, buffer + offset, char_width);
        }
    }
}

void UI_PrintString(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t Width)
{
    size_t i;
    size_t Length = strlen(pString);

    if (End > Start)
        Start += (((End - Start) - (Length * Width)) + 1) / 2;

    for (i = 0; i < Length; i++) {
        const unsigned int ofs = (unsigned int)Start + (i * Width);
        if (pString[i] > ' ' && pString[i] < 127) {
            const unsigned int index = pString[i] - ' ' - 1;
            FONT_DrawBigGlyph(index, gFrameBuffer[Line] + ofs, gFrameBuffer[Line + 1] + ofs);
        }
    }
}

void UI_PrintStringSmall(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t char_width,
                         const uint8_t *font)
{
    const size_t Length = strlen(pString);
    const unsigned int char_spacing = char_width + 1;

    if (End > Start) {
        Start += (((End - Start) - Length * char_spacing) + 1) / 2;
    }

    UI_PrintStringBuffer(pString, gFrameBuffer[Line] + Start, char_width, font);
}


void UI_PrintStringSmallNormal(const char *pString, uint8_t Start, uint8_t End, uint8_t Line)
{
    UI_PrintStringSmall(pString, Start, End, Line, ARRAY_SIZE(gFontSmall[0]), (const uint8_t *)gFontSmall);
}

void UI_PrintStringSmallbackground(const char *pString, uint8_t Start, uint8_t End, uint8_t Line,
                                   uint8_t background)
{
    if (pString == NULL)
        return;

    const size_t Length = strlen(pString);
    const unsigned int char_width = ARRAY_SIZE(gFontSmall[0]);
    const unsigned int spacing = 1;
    const unsigned int space_width = 4;

    if (Line >= ARRAY_SIZE(gFrameBuffer))
        return;

    size_t start_pos = (size_t)Start;
    size_t end_pos = (size_t)End;

    // Legacy compatibility:
    // old UI code (main/VFO) uses Start = LCD_WIDTH + x, End = 0
    // meaning "draw starting at x without centering"
    const bool legacy_absolute_mode = (End == 0 && Start >= LCD_WIDTH);
    if (legacy_absolute_mode)
        start_pos -= LCD_WIDTH;

    // Calculate actual visible text width
    size_t text_width = 0;
    for (size_t i = 0; i < Length; i++) {
        const char c = pString[i];

        if (c > ' ') {
            const unsigned int index = (unsigned int)c - ' ' - 1;
            if (index < ARRAY_SIZE(gFontSmall)) {
                uint8_t glyph[6];
                FP_Read(gFontSmall[index], glyph, 6);
                unsigned int char_width_used = char_width;
                while (char_width_used > 0 && glyph[char_width_used - 1] == 0)
                    char_width_used--;

                text_width += char_width_used;
                if (i + 1 < Length)
                    text_width += spacing;
            }
        } else {
            text_width += space_width;
        }
    }

    // Center only when a real [Start..End] range is provided,
    // not in legacy absolute mode
    if (!legacy_absolute_mode && end_pos > start_pos) {
        const size_t available = end_pos - start_pos;
        if (available > text_width)
            start_pos += (available - text_width + 1) / 2;
    }

    if (start_pos >= LCD_WIDTH)
        return;

    size_t draw_limit = LCD_WIDTH;
    if (!legacy_absolute_mode && end_pos > start_pos && end_pos < draw_limit)
        draw_limit = end_pos;

    uint8_t *line_buf = gFrameBuffer[Line];

    // Fill background only under the text area
    if (background) {
        size_t fill_width = text_width;
        if (start_pos + fill_width > draw_limit)
            fill_width = draw_limit - start_pos;

        memset(line_buf + start_pos, 0xFF, fill_width);
    }

    size_t cursor_pos = start_pos;

    for (size_t i = 0; i < Length; i++) {
        const char c = pString[i];

        if (c > ' ') {
            const unsigned int index = (unsigned int)c - ' ' - 1;
            if (index < ARRAY_SIZE(gFontSmall)) {
                uint8_t glyph[6];
                FP_Read(gFontSmall[index], glyph, 6);
                unsigned int char_width_used = char_width;
                while (char_width_used > 0 && glyph[char_width_used - 1] == 0)
                    char_width_used--;

                if (cursor_pos < draw_limit) {
                    size_t writable = char_width_used;
                    if (cursor_pos + writable > draw_limit)
                        writable = draw_limit - cursor_pos;

                    switch (background) {
                        case 0:
                            memcpy(line_buf + cursor_pos, glyph, writable);
                            break;

                        case 1:
                            for (size_t j = 0; j < writable; j++)
                                line_buf[cursor_pos + j] = (uint8_t)~glyph[j];
                            break;

                        default:
                            memcpy(line_buf + cursor_pos, glyph, writable);
                            break;
                    }
                }

                cursor_pos += char_width_used;

                if (i + 1 < Length)
                    cursor_pos += spacing;
            }
        } else {
            cursor_pos += space_width;
        }

        if (cursor_pos >= draw_limit)
            break;
    }
}

void UI_PrintStringSmallBold(const char *pString, uint8_t Start, uint8_t End, uint8_t Line)
{
    const uint8_t *font = (uint8_t *)gFontSmallBold;
    const uint8_t char_width = ARRAY_SIZE(gFontSmallBold[0]);
    UI_PrintStringSmall(pString, Start, End, Line, char_width, font);
}


void UI_DisplayFrequency(const char *string, uint8_t X, uint8_t Y, bool center)
{
    const unsigned int char_width = 13;
    uint8_t *pFb0 = gFrameBuffer[Y] + X;
    uint8_t *pFb1 = pFb0 + 128;
    bool bCanDisplay = false;

    uint8_t len = strlen(string);
    for (int i = 0; i < len; i++) {
        char c = string[i];
        if (c == '-')
            c = '9' + 1;
        if (bCanDisplay || c != ' ') {
            bCanDisplay = true;
            if (c >= '0' && c <= '9' + 1) {
                FP_Read(gFontBigDigits[c - '0'], pFb0 + 2, char_width - 3);
                FP_Read(gFontBigDigits[c - '0'] + char_width - 3, pFb1 + 2, char_width - 3);
            } else if (c == '.') {
                *pFb1 = 0x60;
                pFb0++;
                pFb1++;
                *pFb1 = 0x60;
                pFb0++;
                pFb1++;
                *pFb1 = 0x60;
                pFb0++;
                pFb1++;
                continue;
            }

        } else if (center) {
            pFb0 -= 6;
            pFb1 -= 6;
        }
        pFb0 += char_width;
        pFb1 += char_width;
    }
}

void UI_DrawPixelBuffer(uint8_t (*buffer)[128], uint8_t x, uint8_t y, bool black)
{
    const uint8_t pattern = 1 << (y % 8);
    if (black)
        buffer[y / 8][x] |= pattern;
    else
        buffer[y / 8][x] &= ~pattern;
}

static void sort(int16_t *a, int16_t *b)
{
    if (*a > *b) {
        int16_t t = *a;
        *a = *b;
        *b = t;
    }
}

#ifdef ENABLE_FEAT_F4HWN
void PutPixel(uint8_t x, uint8_t y, bool fill)
{
    UI_DrawPixelBuffer(gFrameBuffer, x, y, fill);
}

void PutPixelStatus(uint8_t x, uint8_t y, bool fill)
{
    UI_DrawPixelBuffer(&gStatusLine, x, y, fill);
}

// The frame of the persistent popups, as the menu option popups: a white
// margin around it, a one-pixel border left and top, two pixels right and
// bottom (the shadow), rounded corners: three pixels cut at each corner, one
// pixel closing the top left one, the margin rounded the same way. Rows
// outside 0..clip are left alone, so a popup can be drawn in parts.
void UI_DrawPopupFrame(int x, int y, int right, int bottom, int clip)
{
    for (int py = y - 1; py <= bottom + 1; ++py)
        for (int px = x - 1; px <= right + 1; ++px) {
            if (px < 0 || px > 127 || py < 0 || py > clip)
                continue;
            const int dx = MIN(px - x, right - px), dy = MIN(py - y, bottom - py);
            if (dx + dy < 0) // the margin's corners keep the screen
                continue;
            PutPixel(px, py, dx >= 0 && dy >= 0 && dx + dy > 1 &&
                                 (px == x || py == y || px >= right - 1 || py >= bottom - 1 ||
                                  (dx == 1 && dy == 1)));
        }
}

#include <string.h>
#define END_SCREEN 126
void GUI_DisplaySmallest(const char *pString, uint8_t x, uint8_t y, bool statusbar, bool fill)
{
    uint8_t c;
    uint8_t pixels;
    const uint8_t *p = (const uint8_t *)pString;

    // Aligne à droite si x atteint le bord droit (128)
    if (x == 128) {
        size_t len = strlen(pString);
        uint8_t total_width = (uint8_t)(len * 4);

        if (128 >= total_width) {
            x = END_SCREEN - total_width;
        } else {
            x = 0; // Sécurité si le texte est plus large que l'écran
        }
    }

    while ((c = *p++)) {
        c -= 0x20;
        uint8_t glyph[3];
        FP_Read(gFont3x5[c], glyph, 3);
        for (int i = 0; i < 3; ++i) {
            pixels = glyph[i];
            for (int j = 0; j < 6; ++j) {
                if (pixels & 1) {
                    if (statusbar)
                        PutPixelStatus(x + i, y + j, fill);
                    else
                        PutPixel(x + i, y + j, fill);
                }
                pixels >>= 1;
            }
        }
        x += 4;
    }
}

void GUI_DisplaySmallestInverse(const char *pString, uint8_t x, uint8_t Line, bool statusbar, bool fill,
                                uint8_t end)
{
    GUI_DisplaySmallest(pString, x, (Line * 8) + 1, statusbar, fill);

    uint8_t start = x - 2;
    uint8_t *buffer = statusbar ? gStatusLine : gFrameBuffer[Line];
    buffer[start] ^= 0x3E;
    for (uint8_t i = start + 1; i < end; i++)
        buffer[i] ^= 0x7F;
    buffer[end] ^= 0x3E;
}

#endif

void UI_DrawLineBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black)
{
    if (x2 == x1) {
        sort(&y1, &y2);
        for (int16_t i = y1; i <= y2; i++) {
            UI_DrawPixelBuffer(buffer, x1, i, black);
        }
    } else {
        const int multipl = 1000;
        int a = (y2 - y1) * multipl / (x2 - x1);
        int b = y1 - a * x1 / multipl;

        sort(&x1, &x2);
        for (int i = x1; i <= x2; i++) {
            UI_DrawPixelBuffer(buffer, i, i * a / multipl + b, black);
        }
    }
}

void UI_DisplayPopup(const char *string)
{
    memset(gFrameBuffer[2], 0, 256);
    const unsigned length = strlen(string);
    if (length <= 15)
        UI_PrintString(string, 4, 0, 2, 8);
    else
        UI_DrawText5x5(string, 4, 21, MIN(length, 20u));
    // Center the visible glyphs, including names padded with EEPROM spaces.
    unsigned first = 4, last = 124;
    while (first < last && !(gFrameBuffer[2][first] | gFrameBuffer[3][first]))
        ++first;
    while (last > first && !(gFrameBuffer[2][last - 1] | gFrameBuffer[3][last - 1]))
        --last;
    const unsigned width = last - first, start = 4 + (120 - width) / 2;
    for (unsigned line = 2; line < 4; ++line) {
        memmove(gFrameBuffer[line] + start, gFrameBuffer[line] + first, width);
        memset(gFrameBuffer[line], 0, start);
        memset(gFrameBuffer[line] + start + width, 0, 128 - start - width);
    }
    for (uint8_t x = 4; x < 124; ++x) {
        gFrameBuffer[2][x] ^= 0xFF;
        gFrameBuffer[3][x] ^= 0xFF;
    }
    gFrameBuffer[2][4] = gFrameBuffer[2][123] = 0xFE;
    gFrameBuffer[3][4] = gFrameBuffer[3][123] = 0x7F;
}

void UI_DisplayClear()
{
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
}

void UI_StatusClear()
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
}

// wide_spacing = true: 6 px
// wide_spacing = false: 4 px
void GUI_DisplaySmallestDark(const char *pString, uint8_t x, uint8_t y, bool statusbar, bool wide_spacing)
{
    if (!pString || !*pString)
        return;

    const uint8_t char_height = 6;
    const uint8_t char_width = wide_spacing ? 6 : 4;

    uint8_t base_x = x;
    uint8_t end_x = x;

    uint8_t c;
    const uint8_t *p = (const uint8_t *)pString;

// Define a local macro to simplify drawing calls
#define DRAW_PIXEL(px, py, color)                                                                            \
    if (statusbar)                                                                                           \
        PutPixelStatus((px), (py), (color));                                                                 \
    else                                                                                                     \
        PutPixel((px), (py), (color))

    while ((c = *p++) != '\0') {
        if (c < 0x20) {
            end_x += char_width;
            continue;
        }

        c -= 0x20;

        // Top line: changed to WHITE (false) for positive mode
        if (y > 0) {
            for (uint8_t dx = 0; dx < char_width; dx++) {
                DRAW_PIXEL(end_x + dx, y - 1, false);
            }
        }

        // Background: changed to WHITE (false)
        for (uint8_t dy = 0; dy < char_height; dy++) {
            for (uint8_t dx = 0; dx < char_width; dx++) {
                DRAW_PIXEL(end_x + dx, y + dy, false);
            }
        }

        // Letters: changed to BLACK (true)
        uint8_t glyph[3];
        FP_Read(gFont3x5[c], glyph, 3);
        for (uint8_t col = 0; col < 3; col++) {
            uint8_t pixels = glyph[col];
            for (uint8_t row = 0; row < 6; row++) {
                if (pixels & 1) {
                    uint8_t offset = wide_spacing ? 1 : 0;
                    DRAW_PIXEL(end_x + col + offset, y + row, true);
                }
                pixels >>= 1;
            }
        }

        end_x += char_width;
    }

    // Vertical border lines: changed to WHITE (false) to blend into the new background
    for (uint8_t dy = 0; dy <= char_height; dy++) {
        uint8_t line_y = y + dy - 1;
        if (line_y < 64) {
            if (base_x >= 2) {
                DRAW_PIXEL(base_x - 2, line_y, false);
            }
            if (base_x >= 1) {
                DRAW_PIXEL(base_x - 1, line_y, false);
            }
            if (end_x < 128) {
                DRAW_PIXEL(end_x, line_y, false);
            }
        }
    }

#undef DRAW_PIXEL // Clean up the local macro
}

void UI_FormatFrequency(char *text, uint32_t frequency)
{
    sprintf(text, "%u.%05u", frequency / 100000, frequency % 100000);
}

void UI_FormatVoltage(char *text, unsigned int voltage)
{
    sprintf(text, "%u.%02uV", voltage / 100u, voltage % 100u);
}

void UI_DrawMenuKeyHints(const char *act_menu, const char *act_exit)
{
    const uint8_t sp = 6u;
    const uint8_t ae = (uint8_t)strlen(act_exit);
    const uint8_t xm = 4u;
    const uint8_t xe = (uint8_t)(124u - ae * 4u - sp - 16u);

    GUI_DisplaySmallestInverse("MENU", xm, 6, false, true, (uint8_t)(xm + 16u));
    GUI_DisplaySmallest(act_menu, (uint8_t)(xm + 16u + sp), 49, false, true);

    GUI_DisplaySmallestInverse("EXIT", xe, 6, false, true, (uint8_t)(xe + 16u));
    GUI_DisplaySmallest(act_exit, (uint8_t)(xe + 16u + sp), 49, false, true);
}
