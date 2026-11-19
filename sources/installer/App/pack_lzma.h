/* Falcon installer: LZMA decoder (raw LZMA1 stream, lc = 0, lp = 0, pb = 1,
 * as tools/build_payload.py writes it). Like the inflater it replaces, it has
 * no window of its own: matches are copied by the caller from what it already
 * wrote (external NOR), and the byte a literal after a match is coded against
 * is read back the same way. */
#ifndef FALCON_PACK_LZMA_H
#define FALCON_PACK_LZMA_H
#include <stdbool.h>
#include <stdint.h>

#define FP_LZMA_LC 0
#define FP_LZMA_LP 0
#define FP_LZMA_PB 1

typedef struct {
    uint16_t isMatch[12 << FP_LZMA_PB], isRep[12], isRepG0[12], isRepG1[12], isRepG2[12];
    uint16_t isRep0Long[12 << FP_LZMA_PB];
    uint16_t posSlot[4][64], posSpecial[115], align[16];
    uint16_t len[2][2 + (2 << FP_LZMA_PB) * 8 + 256]; /* match, rep: choice, choice 2, low, mid, high */
    uint16_t literal[0x300];
} fp_lzma_probs;

typedef struct {
    fp_lzma_probs p;
    uint32_t range, code;
    uint32_t total; /* bytes to produce */
    bool failed;
    void *io;
    int (*input)(void *);
    /* a literal (distance 0) or a copy of count bytes from distance back */
    bool (*output)(void *, unsigned distance, unsigned count, uint8_t literal);
    /* the byte distance back from the end of what was produced */
    bool (*byte_at)(void *, unsigned distance, uint8_t *b);
} fp_lzma;

bool FP_Lzma(fp_lzma *s);
#endif
