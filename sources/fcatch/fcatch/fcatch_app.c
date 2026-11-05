/* FCatch: close-call capture with a history of the strong nearby signals.
 * Capture and CTCSS/DCS decoding derived from FSearch (Search Falcon), itself
 * derived from GOGUFW Search v1.5 by Gogu-Qs, Apache-2.0; see LICENSE, NOTICE.
 * ABI 1 / API 3 with the Falcon memory extension (names, Spectre bands).
 * The tone tables live in the assets (make_assets.py); the history lives on
 * the resident stack, so the 4 KiB overlay holds code and a few globals only.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define REG_FREQ_H 0x0du
#define REG_FREQ_L 0x0eu
#define REG_RX     0x30u
#define REG_SCAN   0x32u
#define REG_GPIO   0x33u
#define REG_CSS    0x51u
#define REG_RSSI   0x67u
#define REG_CTCSS  0x68u
#define REG_DCS_H  0x69u
#define REG_DCS_L  0x6au
#define LNA_VHF    0x04u /* BK4819 GPIO4, 0x40 >> 4 */
#define LNA_UHF    0x08u /* BK4819 GPIO3, 0x40 >> 3 */
#define CHANNELS   0x1de000u /* Falcon 4096-channel layout: 16 B per channel */
#define NAMES      0x1ee000u
#define BANDS      30u       /* Spectre bands: the last 30 memory channels */
#define HMAX       17u
#define BMAX       8u
#define STORE_MAGIC 0x48435446u /* "FTCH" */
#define AS_DCS     28u       /* asset offsets: proxy descriptor, 104 DCS codes, */
#define AS_CTCSS   236u      /* 50 CTCSS tones, then the saved block (store_t) */

/* The history, newest first, kept in the very block that is saved: the
 * resident config proxy writes it back to the app's own assets when the app
 * exits (Falcon 3.1.1+). Tone: 0 none, 1..50 CTCSS index + 1, 51..154 DCS + 51. */
typedef struct {
    uint32_t magic;
    uint8_t sqm, listen, count, pad; /* sqm: -dBm */
    uint32_t mask;                   /* Spectre bands */
    uint32_t f[HMAX];                /* 10 Hz */
    uint8_t tone[HMAX], hits[HMAX];
} store_t;
_Static_assert(sizeof(store_t) == 116u, "store_t must match STORE_LEN in make_assets.py");

/* One global block: a single base address for every global access. */
static struct {
    const app_api_t *api;
    store_t *st;
    uint32_t *blocked;
    uint32_t scan_prev, hold_f;
    char chname[9];
    uint16_t gpio, channels;
    uint8_t nblocked, bslot, scan_hits, low, last_tone, tone_hits;
    bool holding, memory;
} G;
#define A         G.api
#define blocked   G.blocked
#define scan_prev G.scan_prev
#define hold_f    G.hold_f
#define cfg       (*G.st)
#define chname    G.chname
#define HF        cfg.f
#define HT        cfg.tone
#define HH        cfg.hits
#define gpio      G.gpio
#define channels  G.channels
#define hcount    cfg.count
#define nblocked  G.nblocked
#define bslot     G.bslot
#define scan_hits G.scan_hits
#define low       G.low
#define last_tone G.last_tone
#define tone_hits G.tone_hits
#define holding   G.holding
#define memory    G.memory

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    if (d < s) while (n--) *d++ = *s++;
    else while (n--) d[n] = s[n];
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n) { return memmove(dst, src, n); }

/* ---- tones ---- */
static uint16_t table(uint16_t offset, uint8_t i)
{
    uint16_t v;
    A->asset_read((uint16_t)(offset + 2u * i), &v, 2);
    return v;
}
static uint32_t golay(uint16_t code)
{
    uint32_t word = (uint32_t)code + 0x800u, check = word;
    for (uint8_t i = 0; i < 12u; i++) { check <<= 1; if (check & 0x1000u) check ^= 0x08eau; }
    return word | ((check & 0x0ffeu) << 11);
}
static uint8_t decode_dcs(uint32_t raw) /* DCS index, or 0xff */
{
    uint16_t codes[104];
    A->asset_read(AS_DCS, codes, sizeof codes);
    for (uint8_t rot = 0; rot < 23u; rot++) {
        if (((raw >> 9) & 7u) == 4u) {
            const uint16_t code = (uint16_t)(raw & 0x1ffu);
            for (uint8_t i = 0; i < 104u; i++) if (codes[i] == code && golay(code) == raw) return i;
        }
        raw = (raw >> 1) | ((raw & 1u) ? 0x400000u : 0u);
    }
    return 0xffu;
}
static uint8_t ctcss_index(uint16_t measured)
{
    uint16_t tones[50];
    A->asset_read(AS_CTCSS, tones, sizeof tones);
    uint8_t best = 0; uint16_t smallest = 0xffffu;
    for (uint8_t i = 0; i < 50u; i++) {
        const uint16_t c = tones[i], d = measured > c ? measured - c : c - measured;
        if (d < smallest) { smallest = d; best = i; }
    }
    return best;
}

/* ---- text ---- */
static char *num(char *p, uint32_t v) { p = A->append_u32(p, v); *p = 0; return p; }
static char *pad(char *p, uint32_t v, uint32_t mod) /* v with leading zeros below mod */
{
    char *e = num(p, v + mod);
    memmove(p, p + 1, (size_t)(e - p));
    return e - 1;
}
static char *str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}
static uint8_t len(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }
static char *freq_text(char *p, uint32_t f)
{
    p = num(p, f / 100000u); *p++ = '.';
    return pad(p, f % 100000u, 100000u);
}
static char *tone_text(char *p, uint8_t t)
{
    if (!t) return str(p, "----");
    if (t <= 50u) {
        const uint16_t v = table(AS_CTCSS, (uint8_t)(t - 1u));
        p = num(p, v / 10u); *p++ = '.';
        return num(p, v % 10u);
    }
    const uint16_t c = table(AS_DCS, (uint8_t)(t - 51u));
    *p++ = 'D';
    return pad(p, (c >> 6) * 100u + ((c >> 3) & 7u) * 10u + (c & 7u), 1000u);
}
/* ---- drawing ---- */
static void line(const char *s, uint8_t l, bool center) { A->print_normal(s, 0, center ? 127 : 0, l); }
static void tiny_right(const char *s, uint8_t y) { A->print_tiny(s, (uint8_t)(127u - 4u * len(s)), y, false, true); }
static void screen(const char *title)
{
    char s[20], *p;
    A->status_clear();
    p = str(s, cfg.mask ? "BANDS SQ-" : "ALL SQ-");
    p = num(p, cfg.sqm);
    if (!cfg.listen) str(p, " MUTE");
    A->print_tiny(s, 1, 1, true, true);
    A->draw_battery(); A->blit_status();
    A->display_clear();
    A->print_bold(title, 0, 127, 0);
}
static void footer(const char *left, const char *right)
{
    A->print_tiny(left, 1, 49, false, true);
    tiny_right(right, 49);
    A->blit_full();
}

/* ---- memory and Spectre bands ---- */
static bool band_read(uint8_t b, uint32_t *range) /* start, stop */
{
    A->sys_storage_read(CHANNELS + 16u * (channels - BANDS + b), range, 8);
    return range[0] >= 1800000u && range[0] <= 130000000u;
}
static bool wanted(uint32_t f)
{
    for (uint8_t i = 0; i < nblocked; i++) if (blocked[i] == f) return false;
    if (!cfg.mask) return true;
    for (uint8_t b = 0; b < BANDS; b++) {
        uint32_t r[2];
        if ((cfg.mask >> b & 1u) && band_read(b, r) && f >= r[0] && f <= r[1]) return true;
    }
    return false;
}

/* ---- radio ---- */
static bool air(uint32_t f) { return f >= 10800000u && f < 13700000u; }
static uint32_t snap(uint32_t f) /* 8.33 kHz grid in the air band, else 6.25 kHz */
{
    if (air(f)) return (f * 3u + 1250u) / 2500u * 2500u / 3u;
    return (f + 312u) / 625u * 625u;
}
static int rssi(void) { return (int)((A->bk_read(REG_RSSI) & 0x1ffu) >> 1) - 160; }
static void rx_on(void) { A->bk_write(REG_RX, 0u); A->bk_write(REG_RX, 0xbff1u); }
static void tune(uint32_t f)
{
    A->bk_write(REG_GPIO, (uint16_t)((gpio & ~(LNA_VHF | LNA_UHF)) | (f < 28000000u ? LNA_VHF : LNA_UHF)));
    A->bk_write(0x38u, (uint16_t)f); A->bk_write(0x39u, (uint16_t)(f >> 16));
    A->bk_write(REG_CSS, 0x0300u);
    A->bk_write(0x37u, 0x9f1fu); /* BK4829 RX_TurnOn value */
    rx_on();
}
static void scan_start(void)
{
    holding = false; scan_hits = 0; scan_prev = 0;
    A->audio_path(false); A->led(false);
    A->bk_write(REG_GPIO, gpio);
    rx_on();
    A->bk_write(REG_SCAN, 0x0245u);
}
static void history_add(uint32_t f)
{
    uint8_t i = 0, tone = 0, hits = 0;
    while (i < hcount && HF[i] != f) i++;
    if (i < hcount) { tone = HT[i]; hits = HH[i]; }
    else if (hcount < HMAX) hcount++;
    else i = HMAX - 1u;
    memmove(&HF[1], &HF[0], 4u * i); memmove(&HT[1], &HT[0], i); memmove(&HH[1], &HH[0], i);
    HF[0] = f; HT[0] = tone; HH[0] = hits < 255u ? (uint8_t)(hits + 1u) : 255u;
}
static void lookup_name(uint32_t f)
{
    chname[0] = 0;
    if (!memory) return;
    uint32_t best = 201u; uint16_t match = 0xffffu;
    for (uint16_t i = 0; i < channels - BANDS; i++) {
        const uint32_t c = A->memory_read(i, 0);
        const uint32_t d = c > f ? c - f : f - c;
        if (c && d < best) { best = d; match = i; }
        if ((i & 31u) == 0) A->backlight_update();
    }
    if (match != 0xffffu) A->memory_read(match, chname);
    chname[8] = 0;
}
static bool capture(uint32_t f)
{
    f = snap(f);
    if (!wanted(f)) return false;
    tune(f);
    A->set_af(air(f) ? APP_AF_AM : APP_AF_FM);
    A->delay_ms(30);
    if (rssi() < -(int)cfg.sqm) return false;
    holding = true; hold_f = f; low = tone_hits = 0; last_tone = 0xffu;
    A->audio_path(cfg.listen); A->led(true); A->backlight_on();
    history_add(f);
    lookup_name(f);
    return true;
}
static bool poll(void) /* true when the live screen must be redrawn */
{
    if (!holding) {
        const uint16_t h = A->bk_read(REG_FREQ_H);
        if (h & 0x8000u) return false;
        const uint32_t f = ((uint32_t)(h & 0x07ffu) << 16) | A->bk_read(REG_FREQ_L);
        const uint32_t d = f > scan_prev ? f - scan_prev : scan_prev - f;
        scan_hits = f && d < 100u ? (uint8_t)(scan_hits + 1u) : 0u; scan_prev = f;
        A->bk_write(REG_SCAN, 0x0244u);
        if (scan_hits < 2u) { A->bk_write(REG_SCAN, 0x0245u); return false; }
        if (capture(f)) return true;
        scan_start();
        return false;
    }
    /* End: weak, or FM noise back (the raw RSSI floor can sit above the threshold). */
    if (rssi() < -(int)cfg.sqm - 3 || (A->bk_read(0x65u) & 0x7fu) > 45u) {
        if (++low >= 15u) { scan_start(); return true; }
    } else low = 0;
    if (HT[0]) return false;
    uint8_t t = 0;
    const uint16_t h = A->bk_read(REG_DCS_H), c = A->bk_read(REG_CTCSS);
    if (!(h & 0x8000u)) {
        const uint8_t i = decode_dcs(((uint32_t)(h & 0x0fffu) << 12) | (A->bk_read(REG_DCS_L) & 0x0fffu));
        if (i != 0xffu) t = (uint8_t)(i + 51u);
    } else if (!(c & 0x8000u)) {
        const uint8_t i = ctcss_index((uint16_t)(((uint32_t)(c & 0x1fffu) * 4843u) / 10000u));
        if (i == last_tone && ++tone_hits >= 2u) t = (uint8_t)(i + 1u);
        if (i != last_tone) { last_tone = i; tone_hits = 0; }
    } else return false;
    A->bk_write(REG_CSS, 0x0300u); /* next reading, as the Spectre does: no RX restart */
    HT[0] = t;
    return t != 0;
}

/* ---- keys ---- */
static void service(void)
{
    static uint8_t ticks;
    A->backlight_update();
    if (++ticks >= 50u) { ticks = 0; A->battery_sample(); }
}
static uint8_t key_press(void)
{
    const uint8_t key = A->get_key();
    if (key == APP_KEY_INVALID || key == APP_KEY_SAVER || key == APP_KEY_WAKE) return key;
    while (A->get_key() == key) { service(); A->delay_ms(10); }
    if (key == APP_KEY_UP || key == APP_KEY_DOWN) return A->nav_dir(key) > 0 ? APP_KEY_UP : APP_KEY_DOWN;
    return key;
}

/* ---- screens ---- */
static void draw_live(void)
{
    char s[24], *p;
    screen("FCATCH");
    if (holding) {
        A->draw_frequency(hold_f, hold_f >= 100000000u ? 24 : 10, 1);
        p = tone_text(s, HT[0]); p = str(p, "  X"); num(p, HH[0]);
        line(s, 3, true);
        if (chname[0]) { p = str(s, "CH: "); str(p, chname); line(s, 4, true); }
        footer(cfg.listen ? "*:BLOCK 0:MUTE" : "*:BLOCK 0:LISTEN", "EXIT");
        return;
    }
    line("SCANNING", 2, true);
    if (hcount) {
        p = str(s, "LAST: "); freq_text(p, HF[0]); line(s, 3, true);
    } else line("NO CAPTURE YET", 3, true);
    footer("MENU:BANDS 1:HIST", "EXIT");
}

/* Save one history entry to the VFO or a new memory, committed when the app exits. */
static bool save(uint8_t i, bool to_vfo)
{
    app_beam_channel_t ch;
    A->beam_get(&ch); /* keep the selected VFO's power and step */
    ch.rx_frequency = HF[i]; ch.tx_offset_frequency = 0;
    ch.tx_offset_direction = ch.frequency_reverse = 0;
    ch.modulation = air(HF[i]) ? 1u : 0u;
    ch.channel_bandwidth = 1; ch.rx_code = ch.tx_code = 0; ch.rx_codetype = ch.tx_codetype = 0;
    ch.dtmf_ptt_id_mode = ch.dtmf_decoding_enable = ch.scrambling_type = ch.compander = 0;
    memset(ch.name, 0, sizeof ch.name);
    if (HT[i]) {
        const bool dcs = HT[i] > 50u;
        ch.rx_code = ch.tx_code = (uint8_t)(HT[i] - (dcs ? 51u : 1u));
        ch.rx_codetype = ch.tx_codetype = dcs ? 2u : 1u;
    }
    if (to_vfo ? A->vfo_stage(&ch) : A->beam_save(&ch) != 0xffffu) return true;
    A->print_bold("NO FREE CHANNEL", 0, 127, 3); A->blit_full();
    A->delay_ms(1000);
    return false;
}

/* History (bands false) or Spectre band filter (bands true). True: saved, leave the app. */
static bool list(bool bands)
{
    uint8_t ids[BANDS], n = 0, sel = 0;
    if (bands) {
        uint32_t r[2];
        for (uint8_t b = 0; b < BANDS; b++) if (band_read(b, r)) ids[n++] = b;
    }
    for (;;) {
        char s[24], *p;
        if (!bands) n = hcount;
        if (sel >= n) sel = n ? (uint8_t)(n - 1u) : 0;
        if (bands) str(s, "BANDS");
        else {
            p = str(s, "HIST "); p = num(p, n ? sel + 1u : 0); *p++ = '/'; p = num(p, n);
        }
        screen(s);
        const uint8_t top = sel < 5u ? 0 : (uint8_t)(sel - 4u);
        for (uint8_t r = 0; r < 5u && top + r < n; r++) {
            const uint8_t i = (uint8_t)(top + r), l = (uint8_t)(r + 1u);
            if (bands) {
                p = str(s, cfg.mask >> ids[i] & 1u ? "[X] " : "[ ] ");
                A->sys_storage_read(NAMES + 16u * (channels - BANDS + ids[i]), p, 8);
                p[8] = 0;
                line(s, l, false);
            } else {
                p = freq_text(s, HF[i]); *p++ = ' '; tone_text(p, HT[i]);
                line(s, l, false);
                *s = 'X'; num(s + 1, HH[i]); tiny_right(s, (uint8_t)(8u * l + 1u));
            }
            if (i == sel) for (uint8_t x = 0; x < 128u; x++) A->fb[l][x] ^= 0xffu;
        }
        if (!n) line(bands ? "NO BAND" : "EMPTY", 3, true);
        footer(!n ? "" : bands ? "MENU:TOGGLE" : "MENU:VFO 2:MEM 3:DEL", "EXIT");
        const uint8_t key = key_press();
        if (key == APP_KEY_EXIT || key == APP_KEY_PTT) return false;
        if (key == APP_KEY_UP && sel + 1u < n) sel++; /* value up: next item */
        if (key == APP_KEY_DOWN && sel) sel--;
        if (n && key == APP_KEY_MENU && bands) cfg.mask ^= 1u << ids[sel];
        if (n && !bands && (key == APP_KEY_MENU || key == APP_KEY_2) && save(sel, key == APP_KEY_MENU))
            return true;
        if (!bands && n && key == APP_KEY_3) {
            const uint8_t k = (uint8_t)(n - sel - 1u);
            memmove(&HF[sel], &HF[sel + 1u], 4u * k); memmove(&HT[sel], &HT[sel + 1u], k);
            memmove(&HH[sel], &HH[sel + 1u], k);
            hcount--;
        }
        service(); A->delay_ms(20);
    }
}

__attribute__((section(".text.entry"))) void app_main(const app_api_t *api)
{
    if (!api || api->abi_major != 1 || api->api_level < 3 || api->api_size < 396) return;
    store_t store;
    uint32_t block[BMAX];
    A = api; G.st = &store; blocked = block;
    memory = A->api_size >= offsetof(app_api_t, memory_read) + sizeof(A->memory_read) &&
             A->falcon_memory_tag == 0x314d4346u && A->memory_read && A->sys_storage_read;
    channels = memory ? (uint16_t)A->memory_read(0xffffu, 0) : 0;
    if (channels <= BANDS) memory = false;
    A->cfg_load((uint8_t *)&store, sizeof store);
    if (cfg.magic != STORE_MAGIC || cfg.count > HMAX || !memory) {
        cfg.magic = STORE_MAGIC; cfg.sqm = 115; cfg.listen = 1; cfg.count = 0; cfg.mask = 0;
    }
    gpio = A->bk_read(REG_GPIO);
    scan_start();
    bool redraw = true;
    uint8_t ticks = 0;
    for (;;) {
        if (redraw || ++ticks >= 100u) { draw_live(); redraw = false; ticks = 0; }
        service();
        const uint8_t key = key_press();
        if (key == APP_KEY_EXIT || key == APP_KEY_PTT) break;
        if (key == APP_KEY_0) { cfg.listen = !cfg.listen; if (holding) A->audio_path(cfg.listen); redraw = true; }
        if (key == APP_KEY_STAR && holding) {
            blocked[bslot] = hold_f; bslot = (uint8_t)((bslot + 1u) % BMAX);
            if (nblocked < BMAX) nblocked++;
            scan_start(); redraw = true;
        }
        if (key == APP_KEY_9) { nblocked = bslot = 0; redraw = true; }
        if (key == APP_KEY_UP && cfg.sqm > 40u) { cfg.sqm--; redraw = true; } /* UP raises the threshold */
        if (key == APP_KEY_DOWN && cfg.sqm < 115u) { cfg.sqm++; redraw = true; }
        if ((key == APP_KEY_MENU && memory) || key == APP_KEY_1) {
            A->bk_write(REG_SCAN, 0x0244u);
            A->audio_path(false);
            if (list(key == APP_KEY_MENU)) break;
            scan_start(); redraw = true;
        }
        if (poll()) redraw = true;
        A->delay_ms(20);
    }
    A->audio_path(false); A->led(false);
    A->bk_write(REG_SCAN, 0x0244u); A->bk_write(REG_RX, 0u);
    A->cfg_save((const uint8_t *)&store, sizeof store);
}

uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d) { return A->uidivmod(n, d); }
uint32_t __aeabi_uidiv(uint32_t n, uint32_t d) { return (uint32_t)A->uidivmod(n, d); }
