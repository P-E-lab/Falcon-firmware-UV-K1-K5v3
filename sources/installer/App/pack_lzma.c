/* Falcon installer: LZMA decoder, after the LZMA specification's reference
 * decoder (LzmaSpec, public domain), fixed to lc = 0, lp = 0, pb = 1. The
 * output stops at s->total bytes; an end marker before it is an error. */
#include "pack_lzma.h"

#define PB_MASK ((1u << FP_LZMA_PB) - 1u)

static uint8_t next(fp_lzma *s)
{
    const int c = s->input(s->io);
    if (c < 0) {
        s->failed = true;
        return 0;
    }
    return (uint8_t)c;
}

static unsigned bit(fp_lzma *s, uint16_t *p)
{
    const uint32_t bound = (s->range >> 11) * *p;
    unsigned b;
    if (s->code < bound) {
        s->range = bound;
        *p += (2048u - *p) >> 5;
        b = 0;
    } else {
        s->range -= bound;
        s->code -= bound;
        *p -= *p >> 5;
        b = 1;
    }
    if (s->range < (1u << 24)) {
        s->range <<= 8;
        s->code = s->code << 8 | next(s);
    }
    return b;
}

static unsigned tree(fp_lzma *s, uint16_t *p, unsigned bits)
{
    unsigned m = 1;
    for (unsigned i = 0; i < bits; i++)
        m = m << 1 | bit(s, &p[m]);
    return m - (1u << bits);
}

static unsigned tree_reverse(fp_lzma *s, uint16_t *p, unsigned bits)
{
    unsigned m = 1, sym = 0;
    for (unsigned i = 0; i < bits; i++) {
        const unsigned b = bit(s, &p[m]);
        m = m << 1 | b;
        sym |= b << i;
    }
    return sym;
}

static unsigned direct(fp_lzma *s, unsigned bits)
{
    unsigned res = 0;
    while (bits--) {
        s->range >>= 1;
        s->code -= s->range;
        const uint32_t t = 0u - (s->code >> 31);
        s->code += s->range & t;
        res = res << 1 | (t + 1u);
        if (s->range < (1u << 24)) {
            s->range <<= 8;
            s->code = s->code << 8 | next(s);
        }
    }
    return res;
}

/* match / rep length - 2: 0..7 low, 8..15 mid, 16..271 high */
static unsigned length(fp_lzma *s, uint16_t *l, unsigned posState)
{
    if (!bit(s, &l[0]))
        return tree(s, &l[2 + posState * 8], 3);
    if (!bit(s, &l[1]))
        return 8 + tree(s, &l[2 + (PB_MASK + 1u + posState) * 8], 3);
    return 16 + tree(s, &l[2 + (2u << FP_LZMA_PB) * 8], 8);
}

bool FP_Lzma(fp_lzma *s)
{
    uint16_t *p = &s->p.isMatch[0];
    for (unsigned i = 0; i < sizeof s->p / 2; i++)
        p[i] = 1024;
    s->range = 0xFFFFFFFFu;
    s->code = 0;
    if (next(s))
        return false;
    for (unsigned i = 0; i < 4; i++)
        s->code = s->code << 8 | next(s);
    uint32_t pos = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    while (pos < s->total && !s->failed) {
        const unsigned posState = pos & PB_MASK;
        if (!bit(s, &s->p.isMatch[state << FP_LZMA_PB | posState])) {
            /* literal, coded against the byte rep0 + 1 back after a match */
            uint16_t *lp = s->p.literal;
            unsigned sym = 1;
            if (state >= 7) {
                uint8_t mb;
                if (!s->byte_at(s->io, rep0 + 1u, &mb))
                    return false;
                unsigned m = mb;
                do {
                    const unsigned mbit = m >> 7 & 1u;
                    m <<= 1;
                    const unsigned b = bit(s, &lp[(1u + mbit) << 8 | sym]);
                    sym = sym << 1 | b;
                    if (mbit != b)
                        break;
                } while (sym < 0x100u);
            }
            while (sym < 0x100u)
                sym = sym << 1 | bit(s, &lp[sym]);
            if (!s->output(s->io, 0, 1, (uint8_t)sym))
                return false;
            pos++;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        unsigned len;
        if (bit(s, &s->p.isRep[state])) {
            if (!pos)
                return false;
            if (!bit(s, &s->p.isRepG0[state])) {
                if (!bit(s, &s->p.isRep0Long[state << FP_LZMA_PB | posState])) {
                    state = state < 7 ? 9 : 11; /* short rep: one byte */
                    if (!s->output(s->io, rep0 + 1u, 1, 0))
                        return false;
                    pos++;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!bit(s, &s->p.isRepG1[state]))
                    dist = rep1;
                else {
                    if (!bit(s, &s->p.isRepG2[state]))
                        dist = rep2;
                    else {
                        dist = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = length(s, s->p.len[1], posState);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            len = length(s, s->p.len[0], posState);
            state = state < 7 ? 7 : 10;
            const unsigned slot = tree(s, s->p.posSlot[len < 3 ? len : 3], 6);
            if (slot < 4)
                rep0 = slot;
            else {
                const unsigned bits = (slot >> 1) - 1u;
                rep0 = (2u | (slot & 1u)) << bits;
                if (slot < 14)
                    rep0 += tree_reverse(s, s->p.posSpecial + rep0 - slot, bits);
                else
                    rep0 += direct(s, bits - 4u) << 4 | tree_reverse(s, s->p.align, 4);
                if (rep0 == 0xFFFFFFFFu)
                    return false; /* end marker before the end */
            }
        }
        len += 2;
        if (rep0 >= pos || len > s->total - pos)
            return false;
        if (!s->output(s->io, rep0 + 1u, len, 0))
            return false;
        pos += len;
    }
    return !s->failed && pos == s->total;
}

/* The installer's few divisions (progress bar) need no speed: a
 * shift-and-subtract divider replaces libgcc's 276-byte unrolled one. */
__attribute__((used)) unsigned long long __aeabi_uidivmod(unsigned n, unsigned d)
{
    unsigned q = 0, r = 0;
    for (int i = 31; i >= 0; --i) {
        r = r << 1 | (n >> i & 1u);
        if (d && r >= d) {
            r -= d;
            q |= 1u << i;
        }
    }
    return q | (unsigned long long)r << 32;
}
__attribute__((used)) unsigned __aeabi_uidiv(unsigned n, unsigned d)
{
    return (unsigned)__aeabi_uidivmod(n, d);
}
