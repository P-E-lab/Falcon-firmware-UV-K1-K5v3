#define _Static_assert(a, b)
/* Host test of FMessage: the app logic with a simulated radio.
 * Builds fmessage_app.c with a fake app_api_t; frames sent by the app are
 * captured, frames from other radios are injected through the FSK FIFO. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define memcpy app_memcpy
#define memset app_memset
#define __aeabi_uidivmod app_uidivmod
#define __aeabi_uidiv app_uidiv
#include "fmessage_app.c"
#include "fonts.h"
#undef memcpy
#undef memset

static uint8_t fb[8][128], sline[128];
static char grid[8][19], statusText[64], hint[128];
static uint32_t clk;
static uint8_t assets[4096];
static int squelch;
/* RX injection */
static uint16_t rxq[16][FRAME_W];
static int nrxq, rxpos = -1, rxReady;
/* TX capture */
static uint16_t txf[64][FRAME_W];
static int ntx, txi, loading;
static int beeps[16], nbeep;
/* keys: (time, key) pairs; key held until next event */
static struct { uint32_t t; uint8_t k; } ev[256];
static int nev, evi;
static uint8_t curkey = APP_KEY_INVALID;

static void f_clear(void) { memset(fb, 0, sizeof fb); memset(grid, ' ', sizeof grid); for (int i = 0; i < 8; i++) grid[i][18] = 0; hint[0] = 0; }
static uint8_t pfb[8][128]; /* status line + 7 pages, rendered */
static void str(const char *s, uint8_t start, uint8_t end, uint8_t line, const uint8_t *font)
{
    const unsigned w = 6, len = strlen(s);
    if (end > start) start += ((end - start) - len * (w + 1) + 1) / 2;
    for (unsigned x = start; *s; s++, x += w + 1)
        if (*s > ' ') for (unsigned i = 0; i < w && x + i < 128; i++) fb[line][x + i] |= font[(*s - '!') * w + i];
}
static void f_normal(const char *s, uint8_t a, uint8_t e, uint8_t l) { str(s, a, e, l, &HgFontSmall[0][0]); }
static void f_bold(const char *s, uint8_t a, uint8_t e, uint8_t l) { str(s, a, e, l, &HgFontSmallBold[0][0]); }
static void f_tiny(const char *s, uint8_t x, uint8_t y, bool sb, bool fill)
{
    if (x == 128) x = 126 - strlen(s) * 4;
    for (; *s; s++, x += 4)
        for (int i = 0; i < 3; i++) {
            uint8_t px = HgFont3x5[*s - 0x20][i];
            for (int j = 0; j < 6; j++, px >>= 1)
                if (px & 1) {
                    unsigned yy = y + j; uint8_t *b = sb ? &sline[x + i] : &fb[yy / 8][x + i];
                    if (sb) yy = y + j;
                    const uint8_t m = 1u << (yy % 8);
                    if (fill) *b |= m; else *b &= ~m;
                }
        }
}
static void f_nop(void) {}
static void f_nop1(bool b) { (void)b; }
static int ledOn, ledFlashes; static void f_led(bool b) { if (b && !ledOn) ledFlashes++; ledOn = b; }
static uint8_t f_getkey(void)
{
    while (evi < nev && ev[evi].t <= clk) curkey = ev[evi++].k;
    return curkey;
}
static void f_delay(uint32_t ms) { clk += ms; }
static void f_tone(uint16_t hz, uint16_t ms) { if (nbeep < 16) beeps[nbeep++] = hz; clk += ms; }
static void f_tone_raw(uint16_t hz, uint16_t ms) { (void)hz; clk += ms; }
static uint32_t f_ticks(void) { return clk; }
static uint32_t seed = 12345;
static uint32_t f_rand(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static uint16_t f_asset(uint16_t off, void *b, uint16_t n) { memcpy(b, assets + off, n); return n; }
static uint64_t f_udiv(uint32_t n, uint32_t d) { return (uint64_t)(n % d) << 32 | (n / d); }
static char *f_u32(char *d, uint32_t u) { return d + sprintf(d, "%u", u); }
static uint32_t f_rxfreq(void) { return 44600625; }
static uint8_t f_txstate(void) { return 0; }
static void f_setaf(uint8_t m) { (void)m; }
static void f_rect(app_fb_t f, int16_t a, int16_t b, int16_t c, int16_t d, bool k) { (void)f; (void)a; (void)b; (void)c; (void)d; (void)k; }
static void f_bkw(uint8_t r, uint16_t v)
{
    if (r == 0x59 && v == 0x3068) rxReady = 1;
    if (r == 0x59 && v == 0x8068) loading = 1, txi = 0;
    else if (r == 0x5F && loading && txi < (int)FRAME_W) txf[ntx][txi++] = v;
    else if (r == 0x59 && v == 0x2868 && loading) loading = 0, ntx++;
}
static uint16_t f_bkr(uint8_t r)
{
    if (r == 0x0C) {
        unsigned v = squelch ? 2u : 0u;
        if (loading) v |= 1u; /* TX finished */
        if (rxpos < 0 && nrxq && rxReady) rxpos = 0, rxReady = 0;
        if (rxpos >= 0) v |= 1u;
        return (uint16_t)v;
    }
    if (r == 0x02) return rxpos >= 0 ? 0x2000u : 0;
    if (r == 0x5F) {
        uint16_t w = rxq[0][rxpos++];
        if (rxpos >= (int)FRAME_W) { memmove(rxq, rxq + 1, sizeof rxq[0] * --nrxq); rxpos = -1; }
        return w;
    }
    return 0;
}

static app_api_t api;
static void f_sysread(uint32_t a, void *p, uint32_t n) { (void)a; memset(p, 0, n); }
static int nsave; static char lastsave[80]; static void f_save(const void *m, unsigned n) { nsave++; snprintf(lastsave, sizeof lastsave, "%.2s:%.*s", (const char *)m, (int)n, (const char *)m + 4); }
static int callSeen;
static void inject(unsigned type, unsigned id, const char *from, const char *ref, const char *text)
{
    uint8_t *f = (uint8_t *)rxq[nrxq++];
    memset(f, 0, 2 * FRAME_W);
    f[0] = MAGIC, f[1] = type, f[2] = id, f[3] = from[0], f[4] = from[1];
    if (ref) f[5] = ref[0], f[6] = ref[1];
    if (text) f[7] = strlen(text), memcpy(f + 8, text, f[7]);
}
static int nshot;
static void show(const char *label)
{
    char name[64]; snprintf(name, sizeof name, "shot%02d.pbm", nshot++);
    FILE *f = fopen(name, "w"); fprintf(f, "P1\n128 64\n");
    for (int y = 0; y < 64; y++) { for (int x = 0; x < 128; x++) fprintf(f, "%d ", y < 8 ? sline[x] >> y & 1 : fb[(y - 8) / 8][x] >> ((y - 8) % 8) & 1); fprintf(f, "\n"); }
    fclose(f); printf("%s: %s\n", name, label);
}
static void frames(void)
{
    static int shown;
    for (; shown < ntx; shown++) {
        uint8_t *f = (uint8_t *)txf[shown];
        printf("  TX: type=%u id=%u from=%c%c ref=%c%c len=%u text=\"%.*s\"\n", f[1], f[2], f[3], f[4],
               f[5] ? f[5] : '.', f[6] ? f[6] : '.', f[7], f[7], f + 8);
    }
}
/* Run the app main loop until time t (the loop body of app_main). */
static void loop(uint32_t until)
{
    while (clk < until && g.running) {
        keys(); rxPoll(); transmit();
        if (g.pkey != NONE && now() - g.pms >= TAP_MS) commit(), g.dirty = 1;
        const unsigned b = busy();
        if (b != g.busy) g.busy = b, g.dirty = 1;
        if (g.dirty) g.dirty = 0, draw();
        A->led(unread() && (now() & 1023u) < 100u);
        clk += 10;
    }
}
static void key(uint8_t k, uint32_t hold)
{
    ev[nev].t = clk, ev[nev++].k = k;
    ev[nev].t = clk + hold, ev[nev++].k = APP_KEY_INVALID;
    loop(clk + hold + 100);
    keys(); /* see the release even after a long blocking send */
}
static void taps(uint8_t k, int n) { for (int i = 0; i < n; i++) key(k, 60); }

int main(void)
{
    FILE *fa = fopen("fmessage_assets.bin", "rb");
    fread(assets, 1, sizeof assets, fa);
    fclose(fa);
    api.abi_major = 1, api.api_level = 3, api.fb = fb;
    api.display_clear = f_clear, api.print_normal = f_normal, api.print_bold = f_bold;
    api.print_tiny = f_tiny, api.blit_full = f_nop, api.blit_status = f_nop, api.status_line = sline;
    api.get_key = f_getkey, api.delay_ms = f_delay, api.play_tone = f_tone, api.play_tone_raw = f_tone_raw;
    api.ticks_ms = f_ticks, api.rand32 = f_rand, api.asset_read = f_asset, api.uidivmod = f_udiv;
    api.append_u32 = f_u32, api.rx_freq = f_rxfreq, api.tx_state = f_txstate, api.tx_set_params = f_nop;
    api.tx_end = f_nop, api.tx_mute = f_nop1, api.set_af = f_setaf, api.bk_read = f_bkr, api.bk_write = f_bkw;
    api.led = f_led, api.backlight_on = f_nop, api.backlight_update = f_nop, api.draw_rect = f_rect;

    /* app_main without its endless loop */
    struct data data;
    api.api_size = sizeof(app_api_t), api.message_save = f_save, api.sys_storage_read = f_sysread;
    gapi = &api, D = &data;
    memset(&data, 0, sizeof data);
    asset(0, g.user, FMU_LEN);
    g.running = g.dirty = 1, g.hk = APP_KEY_INVALID;
    g.id = A->rand32() & 0xFFu;
    commit();
    rxArm();
    queue(F_HELLO, 0, NULL);

    loop(3000);
    show("start"); frames();

    inject(F_READY, 0, "CD", NULL, NULL);
    inject(F_HELLO, 0, "EF", NULL, NULL);
    loop(clk + 4000);
    show("CD ready, EF hello (we answer READY)"); frames();

    taps(APP_KEY_4, 2); loop(clk + 900); taps(APP_KEY_4, 3); loop(clk + 900);
    key(APP_KEY_1, 60); loop(clk + 900);
    key(APP_KEY_7, 800);                       /* long press: digit 7 */
    show("typed HI.7"); frames();

    key(APP_KEY_F, 60);
    show("after F (send)"); frames();

    inject(F_ACK, (uint8_t)g.id, "CD", "AA", NULL);
    loop(clk + 500);
    show("ACK from CD"); frames();

    inject(F_MSG, 9, "EF", NULL, "RDV AU COL 14H ON SE RETROUVE AU PARKING DU HAUT");
    loop(clk + 3000);
    show("message from EF"); frames();
    printf("  beeps:"); for (int i = 0; i < nbeep; i++) printf(" %d", beeps[i]); printf("\n");

    inject(F_MSG, 9, "EF", NULL, "RDV AU COL 14H ON SE RETROUVE AU PARKING DU HAUT");
    loop(clk + 3000);
    printf("  after resend from EF: nrecv=%u (expect 1)\n", g.nrecv); frames();

    printf("  LED flashes while unread: %d\n", ledFlashes);
    key(APP_KEY_MENU, 60); show("inbox");
    key(APP_KEY_MENU, 60); show("message");
    key(APP_KEY_MENU, 60); show("respond -> home (MSG empty)");
    { int f0 = ledFlashes; loop(clk + 3000); printf("  LED flashes after reading: %d (expect 0)\n", ledFlashes - f0); }

    key(APP_KEY_STAR, 60); show("* deletes one");
    key(APP_KEY_UP, 60); key(APP_KEY_UP, 60); taps(APP_KEY_2, 1); loop(clk + 900);
    show("cursor left x2, insert A");
    key(APP_KEY_F, 60); show("F: new message (RESEND, + reset)"); frames();
    key(APP_KEY_F, 60); show("F again: resend same id"); frames();

    squelch = 1; loop(clk + 100);
    nbeep = 0; key(APP_KEY_F, 60); key(APP_KEY_PTT, 60);
    show("busy: F and PTT refused"); printf("  beeps:"); for (int i = 0; i < nbeep; i++) printf(" %d", beeps[i]); printf("\n"); frames();
    squelch = 0; loop(clk + 100);
    int before = ntx; key(APP_KEY_PTT, 60); printf("  PTT free: frames sent %d (melody only, expect 0)\n", ntx - before);

    inject(F_MSG, 10, "CD", NULL, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ01"); loop(clk + 3000);
    inject(F_MSG, 11, "GH", NULL, "SECOND"); loop(clk + 3000);
    show("home 64 layout, 3 received"); frames();
    for (int i = 0; i < 20; i++) taps(APP_KEY_9, 1), loop(clk + 900);
    show("typed long text"); frames();
    key(APP_KEY_MENU, 60); show("inbox 3");
    key(APP_KEY_DOWN, 60); key(APP_KEY_STAR, 60); show("* -> delete?");
    key(APP_KEY_EXIT, 60); show("exit -> back to inbox, nothing deleted"); printf("  nrecv=%u (expect 3)\n", g.nrecv);
    key(APP_KEY_STAR, 60); key(APP_KEY_MENU, 60); show("M -> deleted, inbox"); printf("  nrecv=%u (expect 2) sel=%u\n", g.nrecv, g.sel);
    key(APP_KEY_MENU, 60); show("open message"); key(APP_KEY_STAR, 60); show("* in message -> delete?");
    key(APP_KEY_MENU, 60); show("deleted -> inbox"); printf("  nrecv=%u (expect 1)\n", g.nrecv);
    key(APP_KEY_MENU, 60); key(APP_KEY_STAR, 60); key(APP_KEY_MENU, 60); show("last deleted -> empty inbox"); printf("  nrecv=%u (expect 0)\n", g.nrecv);
    key(APP_KEY_STAR, 60); show("* on empty inbox: nothing");
    key(APP_KEY_EXIT, 60); key(APP_KEY_EXIT, 60); printf("running=%u (no messages: quit directly)\n", g.running);
    printf("saved to Falcon: %d, last \"%s\"\n", nsave, lastsave);
    printf("stack data: %zu bytes\n", sizeof(struct data));
    return 0;
}
