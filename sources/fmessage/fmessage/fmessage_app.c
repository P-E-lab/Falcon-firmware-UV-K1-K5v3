/* FMessage: short text messages between radios, over the BK4819 FSK modem.
 *
 * Three screens: home (who is here, the message being written or the last
 * one sent), the inbox and a received message.  The app listens as long as
 * it is open.  Opening it announces HELLO; the radios already listening
 * answer READY, after a random delay so that the answers do not collide.
 * Closing it sends LEAVE.  A message goes to everyone; each receiver
 * confirms it with an ACK (random delay too) and its callsign gets a '+'.
 * PTT plays the user's call melody so that the others hear the call and
 * open the app.  A new message beeps (RX SOUND) and the green LED flashes
 * until no message is left unread.  Nothing is transmitted while the channel is busy: F and
 * PTT are refused with a low beep, the automatic frames wait.
 *
 * Frame, 56 bytes (the modem adds its own CRC):
 *   0 magic+version  1 type  2 message number  3-4 sender
 *   5-6 reference (ACK: sender of the message)  7 length  8-55 text.
 *
 * Messages live on app_main's stack (outside the 4 KiB overlay) and are lost
 * when the app closes.  Settings: the user block in the assets
 * (../fmessage_user.h), written by FMessage Option. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "../fmessage_user.h"
#include "fmessage_assets.h"

#define TXT      64u          /* message length */
#define FIRST    14u          /* characters after "MSG:" on the first line */
#define LINE     18u          /* characters per line */
#define N_RECV   6u
#define N_PEER   12u
#define N_JOB    4u
#define FRAME_W  28u
#define MAGIC    0xF2u
#define LONG_MS  600u
#define TAP_MS   800u
#define NONE     0xFFu

enum { F_HELLO = 1, F_READY, F_LEAVE, F_MSG, F_ACK };
enum { S_HOME, S_INBOX, S_MSG, S_QUIT, S_DEL };

typedef struct { char cs[2]; uint8_t id, unread; char text[TXT + 1]; } recv_t; /* read up to 6 bytes past text */
typedef struct { uint8_t type, id; char ref[2]; uint32_t due; } job_t;

/* Word-sized fields: one Thumb load each, no narrowing.  They live on the
 * stack with the messages, so that only two pointers stay in the overlay. */
struct state {
    uint8_t user[FMU_LEN];
    unsigned screen, back, sel, top, item, dirty, running, busy, calling;
    unsigned nrecv, npeer, njob, rx, here, acked, id, sent;
    unsigned hk, hlong, hms, hrep;
    unsigned len, cur, pkey, pidx, pms;
    char str[STR_STRIDE];
};

struct data {
    struct state s;
    recv_t recv[N_RECV];
    job_t job[N_JOB];
    char peer[N_PEER][2];
    uint16_t frame[FRAME_W];
    char edit[TXT + 8];  /* the last line is read 18 characters wide */
};

static const app_api_t *gapi;
#ifdef __arm__
register struct data *D __asm__("r7"); /* the stack data, in a register for good */
#else
static struct data *D; /* host tests */
#endif
#define A gapi
#define g (D->s)

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
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d) { return A->uidivmod(n, d); }
uint32_t __aeabi_uidiv(uint32_t n, uint32_t d) { return (uint32_t)A->uidivmod(n, d); }

/* ---------------------------------------------------------------- helpers */
static unsigned now(void) { return A->ticks_ms(); }
static bool due(unsigned t) { return (int32_t)(now() - t) >= 0; }
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
static char *put(char *d, const char *s)
{
    while ((*d = *s++)) d++;
    return d;
}
static void cp2(char *d, const char *s) { d[0] = s[0], d[1] = s[1]; }
static char *put2(char *d, const char *cs)
{
    cp2(d, cs), d[2] = 0;
    return d + 2;
}
static char *num(char *d, unsigned v) { return A->append_u32(d, v); }
static bool same(const char *a, const char *b) { return a[0] == b[0] && a[1] == b[1]; }
static char *mycs(void) { return (char *)&g.user[FMU_CALLSIGN]; }
static unsigned unread(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g.nrecv; i++) n += D->recv[i].unread;
    return n;
}

/* ------------------------------------------------------------------- draw */
/* Small font, one framebuffer page per line, 7 pixels per character. */
static void at(unsigned r, unsigned col, const char *s) { A->print_normal(s, (uint8_t)(1u + col * 7u), 0, (uint8_t)r); }
/* n characters of s at most. */
static void part(unsigned r, unsigned col, const char *s, unsigned n)
{
    char l[LINE + 1];
    memcpy(l, s, n), l[n] = 0;
    at(r, col, l);
}
/* Three lines of a message: 'first' characters from column col, then two
 * full lines (the copies may read past the text, never past its buffer). */
static void lines(unsigned r, unsigned col, unsigned first, const char *s)
{
    for (unsigned k = 0; k < 4u; k++, s += first, first = LINE, col = 0) part(r + k, col, s, first);
}
/* Bold small font (the Spectre list titles): at x, or centered if x = 0. */
static void bold(unsigned r, unsigned x, const char *s) { A->print_bold(s, (uint8_t)x, x ? 0 : 127, (uint8_t)r); }
/* Bold text in the status line, drawn in the (cleared) page 0 then moved. */
static void status(const char *s, unsigned x)
{
    uint8_t *page = A->fb[0];
    if (*s) bold(0, x, s);
    memcpy(A->status_line, page, 128);
    memset(page, 0, 128);
    A->blit_status();
}

/* Inbox and message: pages 1-6 move 3 pixels up, so that the lines sit
 * centered between the title and the key hints. */
static void raise(void)
{
    for (uint8_t *c = A->fb[0]; c < A->fb[0] + 128u * 6u; c++) *c = (uint8_t)(*c >> 3 | c[128] << 5);
}

static void draw(void)
{
    const unsigned s = g.screen;
    char buf[20], *d = buf;
    A->display_clear();
    d = put(d, S(X_TITLE + s));
    if (s == S_HOME) {
        /* HERE: three callsigns on the first line, four on the second */
        bold(0, 1, S(X_WORD + 2));
        for (unsigned i = 0, n = 0; i < g.npeer && n < 7u; i++) {
            if (!(g.here >> i & 1u)) continue;
            d = put2(buf, D->peer[i]);
            if (g.acked >> i & 1u) put(d, "+");
            at(n >= 3u, n < 3u ? 6u + n * 4u : (n - 3u) * 4u, buf);
            n++;
        }
        *buf = 0;
    }
    if (s == S_MSG) put2(d, D->recv[g.item].cs);
    if (s != S_HOME) status(buf, !(s & 1u)); /* inbox, quit, delete: centered */

    if (s == S_HOME) {
        /* MSG: 14 + 18 + 18 + 14 characters */
        bold(2, 1, S(X_WORD + 3));
        lines(2, 4, FIRST, D->edit);
        const unsigned c = g.pkey != NONE && g.cur ? g.cur - 1u : g.cur;
        const unsigned row = c < FIRST ? 2u : 3u + (c - FIRST) / LINE;
        const unsigned col = c < FIRST ? 4u + c : (c - FIRST) % LINE;
        for (unsigned k = 0; k < 6u; k++) A->fb[row][1u + col * 7u + k] |= 0x80u;
        if (g.sent) bold(6, 86, S(X_WORD + 4));
        if (unread()) put(num(buf, unread()), S(X_WORD + 5)), bold(6, 1, buf);
    } else if (s == S_INBOX) {
        /* five lines, "*AB:TEXT"; the selected one inverted 1 pixel beyond the text */
        if (!g.nrecv) A->print_normal(S(X_WORD + 7), 0, 127, 3);
        if (g.sel < g.top) g.top = g.sel;
        if (g.sel >= g.top + 5u) g.top = g.sel - 4u;
        for (unsigned r = 0; r < 5u && g.top + r < g.nrecv; r++) {
            const recv_t *m = &D->recv[g.top + r];
            d = buf;
            *d++ = m->unread ? '*' : ' ';
            put2(d, m->cs), d[2] = ':';
            memcpy(d + 3, m->text, LINE - 4u), buf[LINE] = 0;
            at(r + 1u, 0, buf);
            if (g.top + r == g.sel)
                for (unsigned x = 0; x < 128u; x++) A->fb[r + 1u][x] ^= 0xFFu, A->fb[r][x] ^= 0x80u;
        }
        raise();
    } else if (s == S_MSG) {
        lines(1, 0, LINE, D->recv[g.item].text);
    } else {
        A->print_normal(S(X_QUIT + 2u * (s - S_QUIT)), 0, 127, 2);
        A->print_normal(S(X_QUIT + 2u * (s - S_QUIT) + 1u), 0, 127, 3);
    }
    if (s == S_HOME) {
        /* the status line, inverted as the Spectre one: "AB - READY" (BUSY,
         * CALL) on the left, the frequency on the right */
        char hdr[16];
        const unsigned f = A->rx_freq();
        put(put2(hdr, mycs()), S(X_WORD + (g.calling ? 6u : g.busy))); /* " - READY" */
        d = num(buf, f / 100000u);
        *d++ = '.';
        num(d, 100000u + f % 100000u);
        memcpy(d, d + 1, 6); /* drop the leading 1 */
        memset(A->status_line, 0x7F, 128); /* one pixel above and below the text */
        A->print_tiny(hdr, 1, 1, true, false);
        A->print_tiny(buf, 128, 1, true, false); /* 128: right aligned */
        A->blit_status();
    } else
        /* bottom line, small font, left, center, right: the key hints */
        for (unsigned k = 0; k < 3u; k++) {
            const char *h = S(X_HINT + 3u * s + k);
            const unsigned w = 128u - 4u * slen(h);
            A->print_tiny(h, (uint8_t)(k == 0 ? 1u : k == 1 ? w / 2u : w), 50, false, true);
        }
    A->blit_full();
}

/* -------------------------------------------------------------------- FSK */
static unsigned bkr(unsigned r) { return A->bk_read((uint8_t)r); }
static void bkw(unsigned r, unsigned v) { A->bk_write((uint8_t)r, (uint16_t)v); }
static void seq(unsigned off, unsigned len)
{
    uint16_t w[SEQ_RX_LEN / 2u];
    asset(off, w, len);
    for (unsigned i = 0; i < len / 2u; i += 2) {
        if (w[i] == SEQ_DELAY) A->delay_ms(w[i + 1]);
        else bkw(w[i], w[i + 1]);
    }
}
#define SEQ(n) seq(n, n##_LEN)

static void rxArm(void)
{
    g.rx = 0;
    SEQ(SEQ_SETUP);
    SEQ(SEQ_RX);
}
static bool busy(void) { return g.rx || (bkr(0x0C) & 2u); }
/* nothing received, nothing on the channel, TX allowed */
static bool idle(void) { return !busy() && !A->tx_state(); }
static void beep(unsigned hz, unsigned ms)
{
    A->play_tone((uint16_t)hz, (uint16_t)ms);
    rxArm();
}

static void build(unsigned type, unsigned id, const char *ref)
{
    uint8_t *f = (uint8_t *)D->frame;
    memset(f, 0, 2u * FRAME_W);
    f[0] = MAGIC, f[1] = (uint8_t)type, f[2] = (uint8_t)id;
    cp2((char *)f + 3, mycs());
    if (ref) cp2((char *)f + 5, ref);
}
static void send(void)
{
    SEQ(SEQ_SETUP);
    A->tx_set_params();
    SEQ(SEQ_TX_LOAD);
    for (unsigned i = 0; i < FRAME_W; i++) bkw(0x5F, D->frame[i]);
    SEQ(SEQ_TX_GO);
    for (unsigned t = 200; t && !(bkr(0x0C) & 1u); t--) A->delay_ms(5);
    SEQ(SEQ_TX_END);
    A->tx_end();
    rxArm();
    g.dirty = 1;
}
static void melody(void)
{
    uint8_t ix[2];
    uint16_t m[MELODY_MAX];
    asset(MELODY_INDEX + 2u * (g.user[FMU_CALL] < FMU_SOUNDS ? g.user[FMU_CALL] : 0u), ix, sizeof ix);
    asset(MELODY + 4u * ix[0], m, 4u * ix[1]);
    A->tx_set_params();
    A->tx_mute(true);
    A->set_af(APP_AF_MUTE);
    bkw(0x70, 0xC200u);
    bkw(0x30, 0xC3FAu);
    A->delay_ms(1000); /* silent carrier: lets the receivers wake up first */
    for (unsigned i = 0; i < 2u * ix[1]; i += 2) A->play_tone_raw(m[i], m[i + 1]);
    bkw(0x70, 0);
    bkw(0x30, 0xC1FEu);
    A->tx_mute(true);
    A->tx_end();
    rxArm();
}

/* --------------------------------------------------------------- protocol */
static void queue(unsigned type, unsigned id, const char *ref)
{
    if (g.njob >= N_JOB) return;
    job_t *j = &D->job[g.njob++];
    j->type = (uint8_t)type, j->id = (uint8_t)id;
    j->due = now() + 300u + A->rand32() % 2200u;
    if (ref) cp2(j->ref, ref);
}
static unsigned peer(const char *cs)
{
    unsigned i = 0;
    while (i < g.npeer && !same(D->peer[i], cs)) i++;
    if (i == N_PEER) return 31u;
    if (i == g.npeer) g.npeer++, cp2(D->peer[i], cs);
    g.here |= 1u << i;
    return i;
}

static void received(void)
{
    const uint8_t *f = (const uint8_t *)D->frame;
    const char *from = (const char *)f + 3;
    const unsigned type = f[1], id = f[2];
    if (f[0] != MAGIC || type - 1u > 4u || f[7] > TXT || same(from, mycs())) return;
    const unsigned p = peer(from);
    g.dirty = 1;
    if (type == F_HELLO) g.acked &= ~(1u << p), queue(F_READY, 0, NULL); /* back: no message yet */
    if (type == F_LEAVE) g.here &= ~(1u << p);
    if (type == F_ACK && same((const char *)f + 5, mycs()) && id == g.id) g.acked |= 1u << p;
    if (type != F_MSG) return;
    queue(F_ACK, id, from);
    for (unsigned i = 0; i < g.nrecv; i++)
        if (D->recv[i].id == id && same(D->recv[i].cs, from)) return; /* a resend */
    for (unsigned i = N_RECV - 1u; i; i--) D->recv[i] = D->recv[i - 1u];
    if (g.nrecv < N_RECV) g.nrecv++;
    recv_t *r = &D->recv[0];
    cp2(r->cs, from), r->id = (uint8_t)id, r->unread = 1;
    memcpy(r->text, f + 8, f[7]), r->text[f[7]] = 0;
    /* Falcon 3.0 keeps it for its MESSAGES page: API level 3 with message_save */
    if (A->api_level == 3u && A->api_size >= sizeof(app_api_t)) A->message_save(r, f[7]);
    if (g.screen == S_INBOX && g.sel + 1u < g.nrecv) g.sel++;
    if (g.screen == S_MSG && g.item < N_RECV - 1u) g.item++;
    /* backlight on a message as the radio's own setting says (RX or TX/RX) */
    uint8_t bl = 0xFFu;
    A->sys_storage_read(0x00A157u, &bl, 1); /* level 3: always present */
    if (bl & 0x80u) A->backlight_on();
    if (g.user[FMU_RX_SOUND]) A->play_tone(1800, 100), A->delay_ms(80), beep(1800, 100); /* two short beeps */
}

static void rxPoll(void)
{
    while (bkr(0x0C) & 1u) {
        bkw(0x02, 0);
        const unsigned irq = bkr(0x02);
        if (!(irq & 0x3000u)) continue;
        for (unsigned n = irq & 0x2000u ? FRAME_W - g.rx : 4u; n--;) {
            const unsigned w = bkr(0x5F);
            if (g.rx < FRAME_W) D->frame[g.rx++] = (uint16_t)w;
        }
    }
    if (g.rx < FRAME_W) return;
    const unsigned crcError = bkr(0x0B) & 0x10u;
    g.rx = 0;
    SEQ(SEQ_RX);
    if (!crcError) received();
}

/* Automatic frames (HELLO, READY, ACK) leave once due and the channel is
 * free. */
static void transmit(void)
{
    for (unsigned i = 0; i < g.njob; i++) {
        job_t *j = &D->job[i];
        if (!due(j->due) || !idle()) continue;
        build(j->type, j->id, j->ref);
        *j = D->job[--g.njob];
        send();
        return;
    }
}
/* F (send / resend) and PTT (call): refused with a low beep if busy. */
static bool canSend(void)
{
    if (idle()) return true;
    beep(440, 200);
    return false;
}

/* ----------------------------------------------------------------- editor */
static void commit(void) { g.pkey = NONE; }
static inline __attribute__((always_inline)) void clear(void) { g.len = g.cur = g.sent = 0, D->edit[0] = 0; }
static void insert(unsigned c)
{
    if (g.len >= TXT) return;
    for (unsigned i = g.len; i > g.cur; i--) D->edit[i] = D->edit[i - 1u];
    D->edit[g.cur++] = (char)c, D->edit[++g.len] = 0;
    g.sent = 0;
}
static void tap(unsigned k)
{
    const char *set = S(X_KEYSET + k);
    const unsigned n = slen(set);
    if (g.pkey == k && now() - g.pms < TAP_MS) {
        g.pidx = g.pidx + 1u < n ? g.pidx + 1u : 0;
        D->edit[g.cur - 1u] = set[g.pidx];
    } else {
        commit();
        if (g.len < TXT) insert((uint8_t)set[0]), g.pkey = n > 1u ? k : NONE, g.pidx = 0;
    }
    g.pms = now();
}

/* ------------------------------------------------------------------- keys */
static void quit(void)
{
    build(F_LEAVE, 0, NULL);
    for (unsigned t = 300; t && busy(); t--) rxPoll(), A->delay_ms(10);
    if (idle()) send();
    g.running = 0;
}

static void press(unsigned k, bool held)
{
    const unsigned s = g.screen;
    const bool up = k == APP_KEY_UP, down = k == APP_KEY_DOWN, menu = k == APP_KEY_MENU,
               ex = k == APP_KEY_EXIT;
    if (s == S_HOME) {
        if (k <= APP_KEY_9) {
            if (held) commit(), insert('0' + k);
            else tap(k);
            return;
        }
        commit();
        if (k == APP_KEY_STAR) {
            if (held) clear();
            else if (g.cur) {
                for (unsigned i = --g.cur; i < g.len; i++) D->edit[i] = D->edit[i + 1u];
                g.len--, g.sent = 0;
            }
        } else if (up) {
            if (g.cur) g.cur--;
        } else if (down) {
            if (g.cur < g.len) g.cur++;
        } else if (k == APP_KEY_F) {
            if (!g.len || !canSend()) return;
            if (!g.sent) g.id = (g.id + 1u) & 0xFFu, g.acked = 0, g.sent = 1; /* else: resend */
            build(F_MSG, g.id, NULL);
            ((uint8_t *)D->frame)[7] = (uint8_t)g.len;
            memcpy((uint8_t *)D->frame + 8, D->edit, g.len);
            send();
        } else if (k == APP_KEY_PTT) {
            if (canSend()) g.calling = 1, draw(), melody(), g.calling = 0;
        } else if (menu) {
            g.screen = S_INBOX, g.top = g.sel = 0;
            for (unsigned i = g.nrecv; i--;)
                if (D->recv[i].unread) g.sel = i; /* the most recent unread */
        } else if (ex) {
            if (g.nrecv) g.screen = S_QUIT;
            else quit();
        }
        return;
    }
    if (k == APP_KEY_STAR && (s == S_MSG || (s == S_INBOX && g.nrecv))) {
        g.back = s, g.screen = S_DEL;
        return;
    }
    if (s == S_DEL) {
        if (menu) {
            const unsigned i = g.back == S_MSG ? g.item : g.sel;
            memcpy(&D->recv[i], &D->recv[i + 1u], sizeof(recv_t) * (--g.nrecv - i));
            if (g.sel && g.sel >= g.nrecv) g.sel--;
            g.screen = S_INBOX;
        }
        if (ex) g.screen = g.back;
        return;
    }
    if (s == S_INBOX && g.nrecv) {
        const unsigned n = g.nrecv;
        if (up) g.sel = (g.sel ? g.sel : n) - 1u;
        if (down) g.sel = g.sel + 1u < n ? g.sel + 1u : 0;
        if (menu) g.item = g.sel, D->recv[g.sel].unread = 0, g.screen = S_MSG;
    } else if (s == S_MSG && ex) {
        g.screen = S_INBOX;
        return;
    } else if (s == S_QUIT && menu) {
        quit();
    }
    if (s == S_MSG && menu) clear(); /* respond: an empty MSG */
    if (ex || (s == S_MSG && menu)) g.screen = S_HOME;
}

/* On the home screen, digits and * act on release, or once held LONG_MS;
 * every other key acts when pressed, the arrows repeat. */
static bool deferred(unsigned k) { return g.screen == S_HOME && (k <= APP_KEY_9 || k == APP_KEY_STAR); }
static void keys(void)
{
    const unsigned k = A->get_key(), t = now();
    if (k >= APP_KEY_SAVER) { g.hk = APP_KEY_INVALID, g.dirty |= k == APP_KEY_WAKE; return; }
    if (k != g.hk) {
        const unsigned was = g.hk;
        g.hk = k, g.hms = g.hrep = t;
        if (was != APP_KEY_INVALID && !g.hlong && deferred(was)) press(was, false);
        g.hlong = 0;
        if (k != APP_KEY_INVALID) {
            A->backlight_on();
            if (!deferred(k)) press(k, false);
        }
    } else if (k != APP_KEY_INVALID) {
        if (deferred(k)) {
            if (g.hlong || t - g.hms < LONG_MS) return;
            g.hlong = 1, press(k, true);
        } else if ((k == APP_KEY_UP || k == APP_KEY_DOWN) && t - g.hms >= 400u && t - g.hrep >= 90u) {
            g.hrep = t, press(k, false);
        } else return;
    } else return;
    g.dirty = 1;
}

__attribute__((used, noinline)) static void run(const app_api_t *a)
{
    if (!a || a->abi_major != 1 || a->api_level < 3) return;
    struct data data;
    D = &data, gapi = a;
    memset(&data, 0, sizeof data);
    asset(0, g.user, FMU_LEN);
    g.running = g.dirty = 1, g.hk = APP_KEY_INVALID;
    g.id = A->rand32();
    commit();
    A->backlight_on();
    rxArm();
    queue(F_HELLO, 0, NULL);

    while (g.running) {
        keys();
        rxPoll();
        transmit();
        if (g.pkey != NONE && now() - g.pms >= TAP_MS) commit(), g.dirty = 1;
        const unsigned b = busy();
        if (b != g.busy) g.busy = b, g.dirty = 1;
        if (g.dirty) g.dirty = 0, draw();
        /* green LED: a 100 ms flash every second while a message is unread */
        A->led(unread() && (now() & 1023u) < 100u);
        A->delay_ms(10);
        A->backlight_update();
    }
    A->led(false);
    SEQ(SEQ_RESET);
}

#ifdef __arm__
/* Entry: r7 holds the data pointer while the app runs, so save the caller's. */
__attribute__((naked, section(".text.entry"), used)) void app_main(const app_api_t *a)
{
    (void)a;
    __asm__ volatile("push {r7, lr}\n\tbl run\n\tpop {r7, pc}");
}
#endif
