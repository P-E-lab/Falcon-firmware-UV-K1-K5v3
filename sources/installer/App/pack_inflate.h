#ifndef FALCON_PACK_INFLATE_H
#define FALCON_PACK_INFLATE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint16_t count[16], symbol[288];
} fp_huffman;
typedef struct {
    fp_huffman lit, dist, lens;
    uint8_t lengths[320];
    uint32_t bits;
    unsigned nbits;
    bool failed;
    void *io;
    int (*input)(void *);
    bool (*output)(void *, unsigned distance, unsigned length, uint8_t literal);
} fp_inflate;
bool FP_Inflate(fp_inflate *s);
#endif
