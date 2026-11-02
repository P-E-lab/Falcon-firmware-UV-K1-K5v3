/* FMessage Option: the settings of FMessage (callsign, RX sound, call sound)
 * and a reminder of its keys.  The settings are FMessage's user block
 * (../fmessage_user.h), reached through the configuration proxy: cfg_load
 * reads it, cfg_save stages it and the loader writes it once this app has
 * closed.  EXIT saves and quits. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "../fmessage_user.h"
#include "fmsgoption_assets.h"

#define ROWS    7u      /* list lines above the key hint */
#define LONG_MS 600u
#define TAP_MS  800u
#define NONE    0xFFu

enum { V_LIST, V_KEYS, V_CALLSIGN };

static struct {
    const app_api_t *api;
    uint8_t user[FMU_LEN];
    unsigned view, sel, top, dirty, ok, len, pkey, pidx, pms, hk, hms, hlong, base;
    char cs[3];
    char line[24];
    char str[STR_STRIDE];
} g;
#define A (g.api)

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)v;
    return dst;
}

static unsigned now(void) { return A->ticks_ms(); }
static void asset(unsigned off, void *b, unsigned n) { A->asset_read((uint16_t)off, b, (uint16_t)n); }
static const char *S(unsigned i)
{
    asset(STR + i * STR_STRIDE, g.str, STR_STRIDE);
    return g.str;
}
static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}
/* ---- 5x5 text, the Falcon list font ---- */
static void text(const char *s, unsigned x, unsigned y)
{
    for (; *s && x <= 123u; s++, x += 6u) {
        uint8_t rows[5];
        unsigned c = (uint8_t)*s;
        if (c < 0x20u || c > 0x5Fu) c = 0x20u;
        asset(FONT + (c - 0x20u) * 5u, rows, 5);
        for (unsigned p = 0; p < 25u; p++)
            if (rows[p / 5u] & (16u >> p % 5u)) A->fb[(y + p / 5u) >> 3][x + p % 5u] |= (uint8_t)(1u << ((y + p / 5u) & 7u));
    }
}
/* The four settings rows are centered between the title and the key hints. */
static unsigned rowY(unsigned r) { return g.base + r * 7u; }
static void invert(unsigned r)
{
    for (unsigned y = rowY(r) - 1u; y <= rowY(r) + 5u; y++)
        for (unsigned x = 0; x < 128u; x++) A->fb[y >> 3][x] ^= (uint8_t)(1u << (y & 7u));
}
/* Bold title in the status line: the Spectre list title font. */
static void title(const char *s)
{
    uint8_t keep[128];
    uint8_t *page = A->fb[0];
    memcpy(keep, page, 128);
    memset(page, 0, 128);
    A->print_bold(s, (uint8_t)((128u - 7u * slen(s)) / 2u), 0, 0);
    memcpy(A->status_line, page, 128);
    memcpy(page, keep, 128);
    A->blit_status();
}

static void draw(void)
{
    A->display_clear();
    g.base = g.ok && g.view == V_LIST ? 11u : 1u;
    title(S(X_TITLE + g.view));
    if (!g.ok) {
        text(S(X_NOAPP), 1, rowY(1));
        text(S(X_NOAPP + 1), 1, rowY(2));
    } else if (g.view == V_KEYS) {
        for (unsigned r = 0; r < ROWS && g.top + r < N_KEYS; r++) text(S(X_KEYS + g.top + r), 1, rowY(r));
    } else if (g.view == V_CALLSIGN) {
        /* centered, in the normal font of the title */
        A->print_normal(g.cs, 57, 0, 1);
        const unsigned c = g.pkey != NONE && g.len ? g.len - 1u : g.len;
        if (c < 2u)
            for (unsigned k = 0; k < 6u; k++) A->fb[1][57u + c * 7u + k] |= 0x80u;
    } else {
        for (unsigned r = 0; r < N_ROW; r++) {
            text(S(X_ROW + r), 1, rowY(r));
            const char *v = r == 0 ? (g.line[0] = (char)g.user[FMU_CALLSIGN], g.line[1] = (char)g.user[FMU_CALLSIGN + 1], g.line[2] = 0, g.line)
                          : r == 1 ? S(X_VAL + (g.user[FMU_RX_SOUND] != 0))
                          : r == 2 ? S(X_SOUND + g.user[FMU_CALL])
                                   : "";
            text(v, 127u - 6u * slen(v), rowY(r));
            if (r == g.sel) invert(r);
        }
    }
    /* key hints, small font: left, center, right */
    for (unsigned k = 0; k < 3u; k++) {
        const char *h = S(X_HINT + 3u * g.view + k);
        const unsigned w = 128u - 4u * slen(h);
        A->print_tiny(h, (uint8_t)(k == 0 ? 1u : k == 1 ? w / 2u : w), 50, false, true);
    }
    A->blit_full();
}

/* ---- callsign: two characters, multi-tap as in FMessage ---- */
static void commit(void) { g.pkey = NONE; }
static void tap(unsigned k)
{
    const char *set = S(X_KEYSET + k);
    const unsigned n = slen(set);
    if (g.pkey == k && now() - g.pms < TAP_MS) {
        g.pidx = g.pidx + 1u < n ? g.pidx + 1u : 0;
        g.cs[g.len - 1u] = set[g.pidx];
    } else {
        commit();
        if (g.len < 2u) g.cs[g.len++] = set[0], g.cs[g.len] = 0, g.pkey = n > 1u ? k : NONE, g.pidx = 0;
    }
    g.pms = now();
}

/* F: the selected call sound, in the loudspeaker only (no transmission). */
static void play(void)
{
    uint8_t ix[2];
    uint16_t m[MELODY_MAX];
    asset(MELODY_INDEX + 2u * g.user[FMU_CALL], ix, 2);
    asset(MELODY + 4u * ix[0], m, 4u * ix[1]);
    A->prepare_tone();
    A->audio_path(true);
    for (unsigned i = 0; i < 2u * ix[1]; i += 2) A->play_tone_raw(m[i], m[i + 1]);
    A->audio_path(false);
    A->tones_off_rx();
}

/* Returns false to quit. */
static bool press(unsigned k, bool held)
{
    const bool up = k == APP_KEY_UP, down = k == APP_KEY_DOWN, menu = k == APP_KEY_MENU,
               ex = k == APP_KEY_EXIT;
    if (!g.ok) return !ex;
    if (g.view == V_CALLSIGN) {
        if (k <= APP_KEY_9) {
            if (held) {
                commit();
                if (g.len < 2u) g.cs[g.len++] = (char)('0' + k), g.cs[g.len] = 0;
            } else tap(k);
            return true;
        }
        commit();
        if (k == APP_KEY_STAR && g.len) g.cs[--g.len] = 0;
        if (menu && g.len == 2u && g.cs[0] != ' ' && g.cs[1] != ' ')
            g.user[FMU_CALLSIGN] = (uint8_t)g.cs[0], g.user[FMU_CALLSIGN + 1] = (uint8_t)g.cs[1], g.view = V_LIST;
        if (ex) g.view = V_LIST;
        return true;
    }
    if (g.view == V_KEYS) {
        if (up && g.top) g.top--;
        if (down && g.top + ROWS < N_KEYS) g.top++;
        if (ex) g.view = V_LIST;
        return true;
    }
    if (ex) return false;
    if (k == APP_KEY_F) play();
    if (up) g.sel = (g.sel ? g.sel : N_ROW) - 1u;
    if (down) g.sel = g.sel + 1u < N_ROW ? g.sel + 1u : 0;
    if (menu) {
        if (g.sel == 0) g.view = V_CALLSIGN, g.len = 0, g.cs[0] = 0, commit();
        if (g.sel == 1) g.user[FMU_RX_SOUND] = !g.user[FMU_RX_SOUND];
        if (g.sel == 2) g.user[FMU_CALL] = (uint8_t)(g.user[FMU_CALL] + 1u < FMU_SOUNDS ? g.user[FMU_CALL] + 1u : 0);
        if (g.sel == 3) g.view = V_KEYS, g.top = 0;
    }
    return true;
}

/* Digits and * of the callsign editor act on release, or once held
 * LONG_MS; every other key acts when pressed. */
static bool deferred(unsigned k) { return g.view == V_CALLSIGN && (k <= APP_KEY_9 || k == APP_KEY_STAR); }

__attribute__((section(".text.entry"), used)) void app_main(const app_api_t *api)
{
    if (!api || api->abi_major != 1 || api->api_level < 3) return;
    A = api;
    A->cfg_load(g.user, FMU_LEN);
    uint32_t magic;
    memcpy(&magic, g.user, 4);
    g.ok = magic == FMU_MAGIC;
    if (g.user[FMU_CALL] >= FMU_SOUNDS) g.user[FMU_CALL] = 0;
    uint8_t saved[FMU_LEN];
    memcpy(saved, g.user, FMU_LEN);
    g.hk = APP_KEY_INVALID, g.dirty = 1;
    commit();
    A->backlight_on();

    for (bool running = true; running;) {
        const unsigned k = A->get_key(), t = now();
        if (k >= APP_KEY_SAVER) {
            g.hk = APP_KEY_INVALID, g.dirty |= k == APP_KEY_WAKE;
        } else if (k != g.hk) {
            const unsigned was = g.hk;
            g.hk = k, g.hms = t;
            if (was != APP_KEY_INVALID && !g.hlong && deferred(was)) press(was, false), g.dirty = 1;
            g.hlong = 0;
            if (k != APP_KEY_INVALID && !deferred(k)) A->backlight_on(), running = press(k, false), g.dirty = 1;
        } else if (k != APP_KEY_INVALID && deferred(k) && !g.hlong && t - g.hms >= LONG_MS) {
            g.hlong = 1, press(k, true), g.dirty = 1;
        }
        if (g.pkey != NONE && t - g.pms >= TAP_MS) commit(), g.dirty = 1;
        if (g.dirty) g.dirty = 0, draw();
        A->delay_ms(10);
        A->backlight_update();
    }
    for (unsigned i = 0; g.ok && i < FMU_LEN; i++)
        if (saved[i] != g.user[i]) {
            A->cfg_save(g.user, FMU_LEN);
            break;
        }
}
