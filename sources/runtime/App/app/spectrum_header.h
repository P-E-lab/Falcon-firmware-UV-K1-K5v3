/* V82: bold 7x9 frequency digits and right-aligned channel name.
 * No row duplication or changes to VFO UI. */
#pragma once
#include "font.h"
#include "store.h"
#include "ui/main.h"
#include "ui/smeter.h"
// Exact Dual-VFO memory/list digits, on the free row below the RX name.
static void SpectrumTinyDigit(unsigned digit, unsigned x, uint8_t fb[7][128])
{
    UI_DrawRowGlyph(fb, gFont5x5Digits[digit], x, 10, 5);
}
static unsigned SpectrumMemoryIdentity(unsigned channel, unsigned list, uint8_t fb[7][128])
{
    unsigned number = channel + 1;
    const unsigned count = number >= 1000 ? 4 : 3;
    const bool hasList = list > 0 && list <= 50;
    const unsigned lastDigit = hasList ? list % 10 : number % 10;
    const unsigned lastCol = lastDigit == 1 ? 3 : 4;
    const unsigned end = hasList ? count * 6 + 10 + lastCol : (count - 1) * 6 + lastCol;
    const unsigned start = 122 - end; // five blank columns, matching the RX name
    unsigned divisor = count == 4 ? 1000 : 100;
    for (unsigned i = 0; i < count; ++i) {
        SpectrumTinyDigit(number / divisor, start + i * 6, fb);
        number %= divisor;
        divisor /= 10;
    }
    if (hasList) {
        const uint8_t *const slash = gFont5x5Slash;
        UI_DrawRowGlyph(fb, slash, start + count * 6, 10, 3);
        SpectrumTinyDigit(list / 10, start + count * 6 + 4, fb);
        SpectrumTinyDigit(list % 10, start + count * 6 + 10, fb);
    }
    return start;
}
static unsigned SpectrumText(const char *text, unsigned x, unsigned y, uint8_t fb[7][128])
{
    for (unsigned i = 0; text[i] && (unsigned char)text[i] != 255; ++i) {
        unsigned char c = text[i];
        if (c <= ' ' || c >= 127) {
            x += 4;
            continue;
        }
        uint8_t glyph[6];
        FP_Read(gFontSmall[c - ' ' - 1], glyph, sizeof glyph);
        unsigned first = 0, end = 6;
        while (first < end && !glyph[first])
            ++first;
        while (end > first && !glyph[end - 1])
            --end;
        unsigned width = end - first;
        if (x + width > 128)
            break;
        for (unsigned col = 0; col < width; ++col)
            for (unsigned row = 0; row < 8; ++row)
                if (glyph[first + col] & (1u << row))
                    ActivityPixel(fb, x + col, y + row);
        x += width + 1;
    }
    return x;
}
static void SpectrumHeader(const char *freq, const char *name, uint8_t fb[7][128])
{
    static const uint8_t digits[10][9] = {
        {30, 33, 33, 33, 33, 33, 33, 33, 30}, {4, 12, 20, 4, 4, 4, 4, 4, 31},
        {30, 33, 1, 1, 2, 4, 8, 16, 63},      {30, 33, 1, 1, 14, 1, 1, 33, 30},
        {2, 6, 10, 18, 34, 63, 2, 2, 2},      {63, 32, 32, 32, 62, 1, 1, 33, 30},
        {14, 16, 32, 32, 62, 33, 33, 33, 30}, {63, 1, 2, 2, 4, 4, 8, 8, 8},
        {30, 33, 33, 33, 30, 33, 33, 33, 30}, {30, 33, 33, 33, 31, 1, 1, 2, 28}};
    unsigned x = 1;
    for (unsigned i = 0; freq[i]; ++i) {
        unsigned char c = freq[i];
        if (c >= '0' && c <= '9') {
            for (unsigned row = 0; row < 9; ++row)
                for (unsigned col = 0; col < 6; ++col)
                    if (digits[c - '0'][row] & (1u << (5 - col))) {
                        ActivityPixel(fb, x + col, row);
                        ActivityPixel(fb, x + col + 1, row);
                    }
            x += 8;
        } else if (c == '.') {
            ActivityPixel(fb, x, 7);
            ActivityPixel(fb, x, 8);
            ActivityPixel(fb, x + 1, 7);
            ActivityPixel(fb, x + 1, 8);
            x += 3;
        } else
            x += 4;
    }
    // Preserve a clear gap after the complete frequency and exactly five
    // blank columns at the right edge. Clip long names at whole characters.
    const unsigned right = 123;
    const unsigned available = x + 5 < right ? right - x - 5 : 0;
    char visible[20];
    unsigned count = 0, advance = 0, width = 0;
    for (unsigned i = 0; name[i] && (unsigned char)name[i] != 255 && count < sizeof(visible) - 1; ++i) {
        unsigned char c = name[i];
        unsigned first = 0, end = 6;
        if (c <= ' ' || c >= 127) {
            if (advance + 4 > available)
                break;
            advance += 4;
        } else {
            uint8_t glyph[6];
            FP_Read(gFontSmall[c - ' ' - 1], glyph, sizeof glyph);
            while (first < end && !glyph[first])
                ++first;
            while (end > first && !glyph[end - 1])
                --end;
            if (advance + end - first > available)
                break;
            width = advance + end - first;
            advance = width + 1;
        }
        visible[count++] = c;
    }
    while (count && (unsigned char)visible[count - 1] <= ' ')
        --count;
    visible[count] = 0;
    if (count && width)
        SpectrumText(visible, right - width, 1, fb);
}

// The numeric RX level is on the plot axis; the header retains its S reading.
static void SpectrumDbm(int dbm, unsigned x, uint8_t fb[7][128])
{
    for (unsigned column = x; column < x + 12; ++column) {
        fb[1][column] &= ~0xF8u;
    }
    const unsigned level = dbm == INT16_MIN ? 0 : SmeterLevelFromDbm(dbm);
    char label[3] = {'S', dbm == INT16_MIN ? '-' : (level <= 9 ? '0' + level : '+'), 0};
    UI_DrawText5x5(label, x, 11, 2);
}
