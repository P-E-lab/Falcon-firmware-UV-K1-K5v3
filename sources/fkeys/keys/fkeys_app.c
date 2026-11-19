/* FKeys: reassign the Falcon VFO screen's long and F+ presses.
 * List: each key and its function (DEFAULT = the Falcon function). MENU opens
 * the function list, MENU picks, EXIT goes back. Leaving the app (EXIT or PTT)
 * saves the table: the loader writes it to the slot's FKeys sector (0xE000).
 * Row 0: the PRESET (FALCON, F4HWN, SONIC or CUSTOM).
 * Needs Falcon 3.3.0 or later (api keys_save). */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define ROWS  6u
#define ROW_H 7u
#define BAR_X 126u
#define REPEAT_MS 400u
#define RATE_MS 90u

static const app_api_t *A;
static char title[16];
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}
void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)v;
    return dst;
}
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d) { return A->uidivmod(n, d); }
uint32_t __aeabi_uidiv(uint32_t n, uint32_t d) { return (uint32_t)A->uidivmod(n, d); }
uint64_t __aeabi_idivmod(int32_t n, int32_t d) { return A->idivmod(n, d); }
int32_t __aeabi_idiv(int32_t n, int32_t d) { return (int32_t)A->idivmod(n, d); }

/* ---- 5x5 font: the Falcon list glyphs (font.c), punctuation in 3x5 ---- */
static const uint8_t font_upper[26][5] = {
    {14, 17, 31, 17, 17}, {30, 17, 30, 17, 30}, {15, 16, 16, 16, 15}, {30, 17, 17, 17, 30},
    {31, 16, 30, 16, 31}, {31, 16, 30, 16, 16}, {15, 16, 19, 17, 15}, {17, 17, 31, 17, 17},
    {14, 4, 4, 4, 14},    {7, 2, 2, 18, 12},    {17, 18, 28, 18, 17}, {16, 16, 16, 16, 31},
    {17, 27, 21, 17, 17}, {17, 25, 21, 19, 17}, {14, 17, 17, 17, 14}, {30, 17, 30, 16, 16},
    {14, 17, 17, 21, 10}, {30, 17, 30, 18, 17}, {15, 16, 14, 1, 30},  {31, 4, 4, 4, 4},
    {17, 17, 17, 17, 14}, {17, 17, 17, 10, 4},  {17, 17, 21, 21, 10}, {17, 10, 4, 10, 17},
    {17, 10, 4, 4, 4},    {31, 2, 4, 8, 31}};
static const uint8_t font_digits[10][5] = {
    {14, 17, 17, 17, 14}, {4, 12, 4, 4, 14},   {30, 1, 14, 16, 31},  {30, 1, 14, 1, 30},
    {18, 18, 31, 2, 2},   {31, 16, 30, 1, 30}, {14, 16, 30, 17, 14}, {31, 1, 2, 4, 4},
    {14, 17, 14, 17, 14}, {14, 17, 15, 1, 14}};
static const uint8_t font_percent[5] = {25, 26, 4, 11, 19};

/* base: 128-byte pages (the framebuffer, or the status line with y < 3). */
static void glyph(uint8_t *base, uint8_t x, uint8_t y, const uint8_t g[5])
{
    for (uint8_t row = 0; row < 5; row++)
        for (uint8_t col = 0; col < 5; col++)
            if (g[row] & (1u << (4 - col)))
                base[((y + row) >> 3) * 128u + x + col] |= (uint8_t)(1u << ((y + row) & 7u));
}
static void text5(const char *s, uint8_t x, uint8_t y)
{
    for (; *s && x <= 123u; s++, x += 6u) {
        const char c = *s;
        const uint8_t *g = c >= 'A' && c <= 'Z' ? font_upper[c - 'A']
                           : c >= '0' && c <= '9' ? font_digits[c - '0']
                           : c == '%'             ? font_percent
                                                  : NULL;
        if (g)
            glyph(&A->fb[0][0], x, y, g);
        else if (c != ' ') {
            const char one[2] = {c, 0};
            A->print_tiny(one, x, y, false, true);
        }
    }
}

/* ---- text blob ---- */
static void status(void)
{
    uint8_t page0[128]; /* the title is drawn in framebuffer page 0, then moved */
    memcpy(page0, A->fb[0], sizeof page0);
    memset(A->fb[0], 0, sizeof page0);
    char text[5], *p = text + 4;
    *p = 0;
    if (A->sys_battery_voltage && A->sys_battery_percent) {
        unsigned v = A->sys_battery_percent(*A->sys_battery_voltage);
        *--p = '%';
        do {
            *--p = (char)('0' + v % 10u);
            v /= 10u;
        } while (v && p > text);
    }
    const uint8_t battery_x = 128u - 6u * (uint8_t)(text + 4 - p);
    uint8_t length = 0;
    while (title[length])
        length++;
    uint8_t x = (uint8_t)((128u - length * 7u) / 2u);
    if (x + length * 7u > battery_x - 2u)
        x = battery_x - 2u - length * 7u;
    A->print_bold(title, x, 0, 0);
    memcpy(A->status_line, A->fb[0], sizeof page0);
    memcpy(A->fb[0], page0, sizeof page0);
    for (x = battery_x; *p; p++, x += 6u)
        glyph(A->status_line, x, 1, *p == '%' ? font_percent : font_digits[*p - '0']);
    A->blit_status();
}
static void set_title(const char *s)
{
    uint8_t i = 0;
    for (; s[i] && i < sizeof title - 1; i++)
        title[i] = s[i];
    title[i] = 0;
}
static void service(void)
{
    static uint8_t ticks;
    A->backlight_update();
    if (++ticks >= 250u) { /* about 2.5 s of 10 ms polls */
        ticks = 0;
        A->battery_sample();
        status();
    }
}

/* One key press, repeated while an arrow is held. */
static uint8_t read_key(void)
{
    static uint8_t held = APP_KEY_INVALID;
    static uint32_t since, last;
    for (;;) {
        service();
        const uint8_t key = A->get_key();
        const uint32_t now = A->ticks_ms();
        if (key == APP_KEY_SAVER) {
            A->delay_ms(20);
            continue;
        }
        if (key == APP_KEY_WAKE)
            return key;
        if (key != held) {
            held = key;
            since = last = now;
            if (key != APP_KEY_INVALID)
                return key;
        } else if ((key == APP_KEY_UP || key == APP_KEY_DOWN) && now - since >= REPEAT_MS &&
                   now - last >= RATE_MS) {
            last = now;
            return key;
        }
        A->delay_ms(10);
    }
}

static void scroll_bar(uint8_t top, uint8_t rows, uint8_t count, uint8_t y_end)
{
    if (count <= rows)
        return;
    const uint8_t span = y_end;
    for (uint8_t y = 0; y < y_end; y++)
        A->fb[y >> 3][BAR_X + 1] |= (uint8_t)(1u << (y & 7u));
    const uint8_t y0 = top * span / count, y1 = (top + rows) * span / count;
    for (uint8_t y = y0; y < y1 && y < y_end; y++)
        A->fb[y >> 3][BAR_X] |= (uint8_t)(1u << (y & 7u));
}


/* ---- the table (app/fkeys.h of the firmware) ---- */
#define FKEYS_ADDR 0xE000u
/* "FKY1", version 1, long[16], fplus[16] (read by the firmware), then the
 * CUSTOM table kept aside while a preset is in use (ignored by the firmware). */
static uint8_t table[5 + 32 + 32];
#define ACTIVE (table + 5)
#define SAVED  (table + 37)

/* Presets, indexed like the table: long[0..15] then fplus[0..15]. 0xFF = the
 * Falcon function. F4HWN and Sonic: their VFO keys that Falcon can do. */
#define D 0xFF
static const uint8_t preset_f4hwn[32] = {
    6, 0x81, 24, 9, D, 31, 2, 0x95, 0x89, 0x89, D, D, D, D, 4, D,
    6, 0x81, 24, 9, D, 31, 2, 26, 27, D, D, D, D, D, D, D};
static const uint8_t preset_sonic[32] = {
    0x81, 0x88, 13, 10, 2, 0x84, 9, D, 0x87, 0x89, D, D, D, D, D, D,
    6, D, D, D, D, D, 0x99, 0x97, 0x98, D, D, D, D, D, D, D};
static const char preset_names[4][7] = {"FALCON", "F4HWN", "SONIC", "CUSTOM"};
static bool same(const uint8_t *a, const uint8_t *b)
{
    for (uint8_t i = 0; i < 32; i++)
        if (a[i] != (b ? b[i] : D))
            return false;
    return true;
}
/* 0 FALCON (all default), 1 F4HWN, 2 SONIC, 3 CUSTOM. */
static uint8_t preset_of(void)
{
    return same(ACTIVE, NULL) ? 0 : same(ACTIVE, preset_f4hwn) ? 1 : same(ACTIVE, preset_sonic) ? 2 : 3;
}

/* The 25 editable presses: index into long[] (0..15) or fplus[] (16..31). */
static const uint8_t presses[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  14, 15, 13,
                                  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 29};
#define PRESSES (sizeof presses)
/* The Falcon function of each press, shown for a DEFAULT entry. */
static const char defaults[][15] = {
    "MODE",        "MEM TO VFO",   "VFO A B",      "FREQ MEM",    "STEP",
    "WIDE NARROW", "POWER",        "FALCON MODE",   "SQUELCH",     "BACKLIGHT",
    "LIST SCAN",   "LOCK KEYPAD",  "CLEAR ENTRY",  "FM RADIO",    "PRIORITY 1",
    "PRIORITY 2",  "PRIORITY 3",   "PRIORITY 4",   "APPS",        "SENTINEL",
    "SPECTRE LIST", "SPECTRE BAND", "SPECTRE RANGE", "ALL MEM FAST", "MONITOR LIST"};

typedef struct {
    uint8_t value;
    char name[15];
} func_t;
static const func_t funcs[] = {
    {0xFF, "DEFAULT"},      {0, "NONE"},           {1, "FLASHLIGHT"},     {2, "POWER"},
    {3, "MONITOR"},         {4, "SCAN"},           {6, "FM RADIO"},       {8, "LOCK KEYPAD"},
    {9, "FREQ MEM"},        {10, "MODE"},          {12, "PTT TOGGLE"},    {13, "WIDE NARROW"},
    {17, "SCANLST UP"},     {18, "SCANLST DOWN"},  {19, "RX MODE"},       {20, "SCAN ALL"},
    {23, "PTT 2"},          {24, "VFO A B"},       {25, "HISTORY"},       {26, "VOX"},
    {27, "REVERSE FQ"},     {28, "MESSAGES"},      {29, "FMESSAGE"},      {30, "BAND SCAN"},
    {0x81, "MEM TO VFO"},   {0x84, "STEP"},        {0x87, "FALCON MODE"},  {0x88, "SQUELCH"},
    {0x89, "BACKLIGHT"},    {0x8E, "LIST SCAN"},   {0x91, "PRIORITY 1"},  {0x92, "PRIORITY 2"},
    {0x93, "PRIORITY 3"},   {0x94, "PRIORITY 4"},  {0x95, "APPS"},        {0x96, "SENTINEL"},
    {0x40, "SENTINEL LIST"}, {0x41, "SENTINEL BAND"}, {0x42, "SENTINEL RANG"}, {0x97, "SPECTRE LIST"},
    {0x98, "SPECTRE BAND"}, {0x99, "SPECTRE RANGE"}, {31, "SPECTRE VFO"}, {0x9E, "ALL MEM FAST"}, {0x9D, "MONITOR LIST"},
};
#define FUNCS (sizeof funcs / sizeof funcs[0])

static void press_label(uint8_t p, char *s)
{
    const uint8_t i = presses[p], k = i & 15u;
    char *o = s;
    if (i < 16) {
        *o++ = 'L', *o++ = 'O', *o++ = 'N', *o++ = 'G', *o++ = ' ';
    } else
        *o++ = 'F', *o++ = '+';
    if (k <= 9)
        *o++ = (char)('0' + k);
    else if (k == 14)
        *o++ = '*';
    else if (k == 15)
        *o++ = 'F';
    else
        *o++ = 'E', *o++ = 'X', *o++ = 'I', *o++ = 'T';
    *o = 0;
}
static uint8_t func_index(uint8_t value)
{
    for (uint8_t i = 0; i < FUNCS; i++)
        if (funcs[i].value == value)
            return i;
    return 0; /* unknown: DEFAULT */
}
static void invert_row(uint8_t y)
{
    for (uint8_t py = y - 1u; py <= y + 5u; py++)
        for (uint8_t x = 0; x <= 124u; x++)
            A->fb[py >> 3][x] ^= (uint8_t)(1u << (py & 7u));
}
static uint8_t text_width(const char *s)
{
    uint8_t n = 0;
    while (s[n])
        n++;
    return n * 6u;
}

static void draw_list(uint8_t sel, uint8_t top)
{
    A->display_clear();
    set_title("VFO KEYS");
    status();
    char label[8];
    for (uint8_t row = 0; row < ROWS && top + row <= PRESSES; row++) {
        const uint8_t i = top + row, y = 1u + row * ROW_H; /* row 0: the preset */
        const char *name;
        if (!i) {
            text5("PRESET", 2, y);
            name = preset_names[preset_of()];
        } else {
            const uint8_t p = i - 1u, value = ACTIVE[presses[p]];
            press_label(p, label);
            text5(label, 2, y);
            name = value == 0xFF ? defaults[p] : funcs[func_index(value)].name;
        }
        text5(name, 124u - text_width(name), y);
        if (i == sel)
            invert_row(y);
    }
    scroll_bar(top, ROWS, PRESSES + 1u, 43);
    text5("MENU: EDIT", 1, 50);
    text5("EXIT", 104, 50);
    A->blit_full();
}

/* Preset list: FALCON, F4HWN, SONIC, CUSTOM. Leaving CUSTOM keeps it aside,
 * CUSTOM brings it back. Returns false on PTT (close the app). */
static bool choose_preset(void)
{
    uint8_t sel = preset_of();
    for (;;) {
        A->display_clear();
        set_title("PRESET");
        status();
        for (uint8_t i = 0; i < 4; i++) {
            text5(preset_names[i], 2, 1u + i * ROW_H);
            if (i == sel)
                invert_row(1u + i * ROW_H);
        }
        text5("MENU: SELECT", 1, 50);
        text5("EXIT", 104, 50);
        A->blit_full();
        const uint8_t key = read_key();
        if (key == APP_KEY_PTT)
            return false;
        if (key == APP_KEY_EXIT)
            return true;
        if (key == APP_KEY_UP)
            sel = (sel + 3u) & 3u;
        else if (key == APP_KEY_DOWN)
            sel = (sel + 1u) & 3u;
        else if (key == APP_KEY_MENU) {
            if (preset_of() == 3)
                memcpy(SAVED, ACTIVE, 32);
            if (sel == 3)
                memcpy(ACTIVE, SAVED, 32);
            else if (sel)
                memcpy(ACTIVE, sel == 1 ? preset_f4hwn : preset_sonic, 32);
            else
                memset(ACTIVE, D, 32);
            return true;
        }
    }
}

/* Function list for one press; returns false on PTT (close the app). */
static bool choose(uint8_t p)
{
    uint8_t *entry = &table[5 + presses[p]];
    uint8_t sel = func_index(*entry), top = 0;
    char label[8];
    press_label(p, label);
    for (;;) {
        if (sel < top)
            top = sel;
        else if (sel >= top + ROWS)
            top = sel - ROWS + 1u;
        A->display_clear();
        set_title(label);
        status();
        for (uint8_t row = 0; row < ROWS && top + row < FUNCS; row++) {
            const uint8_t y = 1u + row * ROW_H;
            text5(funcs[top + row].name, 2, y);
            if (top + row == sel)
                invert_row(y);
        }
        scroll_bar(top, ROWS, FUNCS, 43);
        text5("MENU: SELECT", 1, 50);
        text5("EXIT", 104, 50);
        A->blit_full();
        const uint8_t key = read_key();
        if (key == APP_KEY_PTT)
            return false;
        if (key == APP_KEY_EXIT)
            return true;
        if (key == APP_KEY_UP)
            sel = sel ? sel - 1u : FUNCS - 1u;
        else if (key == APP_KEY_DOWN)
            sel = sel + 1u < FUNCS ? sel + 1u : 0;
        else if (key == APP_KEY_MENU) {
            *entry = funcs[sel].value;
            return true;
        }
    }
}

static void message(const char *s)
{
    A->display_clear();
    set_title("FKEYS");
    status();
    text5(s, 2, 22);
    A->blit_full();
    A->delay_ms(2000);
}

__attribute__((section(".text.entry"))) void app_main(const app_api_t *api)
{
    if (!api || api->abi_major != 1 || api->api_level < 3)
        return;
    A = api;
    if (api->api_size < offsetof(app_api_t, keys_save) + sizeof(void *) || api->falcon_memory_tag != 0x314d4346u ||
        !api->keys_save || !api->sys_storage_read) {
        message("NEEDS FALCON 3.3.0");
        return;
    }
    A->sys_storage_read(FKEYS_ADDR, table, sizeof table);
    if (table[0] != 'F' || table[1] != 'K' || table[2] != 'Y' || table[3] != '1' || table[4] != 1) {
        memset(table, 0xFF, sizeof table);
        table[0] = 'F', table[1] = 'K', table[2] = 'Y', table[3] = '1', table[4] = 1;
    }
    uint8_t sel = 0, top = 0;
    bool open = true;
    while (open) {
        if (sel < top)
            top = sel;
        else if (sel >= top + ROWS)
            top = sel - ROWS + 1u;
        draw_list(sel, top);
        const uint8_t key = read_key();
        if (key == APP_KEY_EXIT || key == APP_KEY_PTT)
            open = false;
        else if (key == APP_KEY_UP)
            sel = sel ? sel - 1u : PRESSES;
        else if (key == APP_KEY_DOWN)
            sel = sel < PRESSES ? sel + 1u : 0;
        else if (key == APP_KEY_MENU)
            open = sel ? choose(sel - 1u) : choose_preset();
    }
    A->keys_save(table, sizeof table);
}
