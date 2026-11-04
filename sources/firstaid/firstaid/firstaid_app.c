/* FirstAid: first aid steps on the radio (ERC Guidelines 2025), the FGuide
 * engine.
 * Status line: page title (bold) and battery percent.
 * Home: the sections (arrows + MENU, or digit 1-9 / 0 for the first 10).
 * Section: "KEY: TEXT" lines in the 5x5 font of the Falcon lists, scrolled
 * with the arrows. The text comes from make_text.py (assets, then text_tail
 * when it does not fit). On the CPR page, MENU starts the metronome. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "text_tail.h"

#define SECTIONS  TEXT_SECTIONS
#define ROWS_HOME 7u /* y 1..43, the key help line at y 50 */
#define ROWS_PAGE 8u /* y 1..50 */
#define ROW_H     7u
#define BAR_X     126u
#define REPEAT_MS 400u
#define RATE_MS   90u
#define BEAT_MS   545u /* 110 a minute */
#define PUSHES    30u

static const app_api_t *A;
static char line[64];
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
static void text_read(uint16_t offset, void *dst, uint16_t len)
{
    uint8_t *d = dst;
    while (len && offset < TEXT_ASSET_SIZE) {
        uint16_t n = TEXT_ASSET_SIZE - offset < len ? TEXT_ASSET_SIZE - offset : len;
        A->asset_read(offset, d, n);
        offset += n, d += n, len -= n;
    }
    if (len)
        memcpy(d, &text_tail[offset - TEXT_ASSET_SIZE], len);
}
/* The length-prefixed field at *offset, as a C string in line[]. */
static const char *text_field(uint16_t *offset)
{
    uint8_t n;
    text_read(*offset, &n, 1);
    const uint8_t size = n < sizeof line ? n : sizeof line - 1;
    text_read(*offset + 1u, line, size);
    line[size] = 0;
    *offset += 1u + n;
    return line;
}
static uint16_t section_start(uint8_t s, uint8_t *lines)
{
    uint8_t entry[3];
    text_read(3u * s, entry, 3);
    if (lines)
        *lines = entry[2];
    return entry[0] | (uint16_t)entry[1] << 8;
}

/* ---- status line: bold title at the left, battery percent at the right ---- */
/* Bold title centered on the screen, moved left only to clear the percent. */
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
        A->backlight_on(); /* stays lit while the app is open (BLTime re-armed) */
        A->battery_sample();
        status();
    }
}

/* The key down at the last poll, shared by both readers: a key still held
 * when the metronome returns is not taken again. */
static uint8_t held = APP_KEY_INVALID;

/* One key press, repeated while an arrow is held. */
static uint8_t read_key(void)
{
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

static void draw_home(uint8_t selected, uint8_t top)
{
    A->display_clear();
    set_title("FIRSTAID");
    status();
    for (uint8_t row = 0; row < ROWS_HOME && top + row < SECTIONS; row++) {
        const uint8_t s = top + row, y = 1u + row * ROW_H;
        uint16_t offset = section_start(s, NULL);
        const char number[3] = {s < 9u ? (char)('1' + s) : '1', s < 9u ? 0 : (char)('0' + s - 9u), 0};
        text5(number, 2, y);
        text5(text_field(&offset), 20, y);
        if (s == selected)
            for (uint8_t py = y - 1u; py <= y + 5u; py++)
                for (uint8_t x = 0; x <= 124u; x++)
                    A->fb[py >> 3][x] ^= (uint8_t)(1u << (py & 7u));
    }
    scroll_bar(top, ROWS_HOME, SECTIONS, 49);
    text5("MENU/1-0: OPEN", 1, 50);
    text5("EXIT", 104, 50);
    A->blit_full();
}

static void draw_page(uint8_t s, uint8_t top, uint8_t lines)
{
    A->display_clear();
    uint16_t offset = section_start(s, NULL);
    text_field(&offset); /* home label */
    set_title(text_field(&offset));
    status();
    for (uint8_t i = 0; i < top + ROWS_PAGE && i < lines; i++) {
        const char *text = text_field(&offset);
        if (i >= top)
            text5(text, 0, 1u + (i - top) * ROW_H);
    }
    scroll_bar(top, ROWS_PAGE, lines, 56);
    A->blit_full();
}

/* One new key press without waiting (APP_KEY_INVALID if none). */
static uint8_t poll_key(void)
{
    service();
    const uint8_t key = A->get_key();
    if (key == held || key == APP_KEY_SAVER)
        return APP_KEY_INVALID;
    held = key;
    return key;
}

static void draw_metronome(uint8_t count, bool push_only)
{
    A->display_clear();
    set_title("CPR 110/MIN");
    status();
    const char digits[3] = {count < 10u ? ' ' : (char)('0' + count / 10u), (char)('0' + count % 10u), 0};
    A->display_freq(digits, 51, 1, false);
    if (push_only)
        text5("PUSH ONLY", 37, 30);
    else if (count == PUSHES) {
        text5("GIVE 2 BREATHS", 22, 30);
        text5("MENU: PUSH AGAIN", 16, 38);
    } else
        text5("30 PUSHES, 2 BREATHS", 4, 30);
    text5("0: MODE", 1, 50);
    text5("EXIT", 104, 50);
    A->blit_full();
}

/* CPR metronome: a beep 110 times a minute, the pushes counted to 30. With
 * breaths, a high double beep at 30 and a pause until MENU; push only, the
 * count goes on. The speaker stays on meanwhile: switched on for each beep,
 * it would not be heard yet. Returns false when the app must close (PTT). */
static bool beats(void);
static bool metronome(void)
{
    A->prepare_tone();
    A->audio_path(true);
    const bool stay = beats();
    A->audio_path(false);
    A->tones_off_rx();
    return stay;
}
static bool beats(void)
{
    bool push_only = false, paused = false;
    uint8_t count = 0;
    uint32_t next = A->ticks_ms();
    for (;;) {
        const uint8_t key = poll_key();
        if (key == APP_KEY_PTT)
            return false;
        if (key == APP_KEY_EXIT)
            return true;
        if (key == APP_KEY_WAKE)
            draw_metronome(count, push_only);
        if (key == APP_KEY_0 || (key == APP_KEY_MENU && paused)) {
            push_only ^= key == APP_KEY_0;
            paused = false, count = 0;
            next = A->ticks_ms();
            draw_metronome(count, push_only);
        }
        if (!paused && A->ticks_ms() - next < 0x80000000u) {
            next += BEAT_MS;
            count = count < PUSHES ? count + 1u : 1u;
            paused = count == PUSHES && !push_only;
            draw_metronome(count, push_only);
            A->play_tone_raw(paused ? 1600 : 1000, paused ? 150 : 40);
            if (paused)
                A->delay_ms(100), A->play_tone_raw(1600, 150);
        }
        A->delay_ms(5);
    }
}

/* Returns false when the app must close (PTT). */
static bool page(uint8_t s)
{
    uint8_t lines, top = 0;
    section_start(s, &lines);
    const uint8_t last = lines > ROWS_PAGE ? lines - ROWS_PAGE : 0;
    for (;;) {
        draw_page(s, top, lines);
        const uint8_t key = read_key();
        if (key == APP_KEY_EXIT)
            return true;
        if (key == APP_KEY_PTT)
            return false;
        if (key == APP_KEY_MENU && s == TEXT_CPR && !metronome())
            return false;
        if (key == APP_KEY_UP && top)
            top--;
        else if (key == APP_KEY_DOWN && top < last)
            top++;
    }
}

__attribute__((section(".text.entry"))) void app_main(const app_api_t *api)
{
    if (!api || api->abi_major != 1 || api->api_level < 3)
        return;
    A = api;
    A->backlight_on();
    uint8_t selected = 0, top = 0;
    for (;;) {
        if (selected < top)
            top = selected;
        else if (selected >= top + ROWS_HOME)
            top = selected - ROWS_HOME + 1u;
        draw_home(selected, top);
        const uint8_t key = read_key();
        if (key == APP_KEY_EXIT || key == APP_KEY_PTT)
            return;
        if (key == APP_KEY_UP)
            selected = selected ? selected - 1u : SECTIONS - 1u;
        else if (key == APP_KEY_DOWN)
            selected = selected + 1u < SECTIONS ? selected + 1u : 0;
        else if (key == APP_KEY_MENU || key <= APP_KEY_9) {
            if (key <= APP_KEY_9) {
                const uint8_t s = key ? key - 1u : 9u;
                if (s >= SECTIONS)
                    continue;
                selected = s;
            }
            if (!page(selected))
                return;
        }
    }
}
