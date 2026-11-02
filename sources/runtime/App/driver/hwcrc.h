/* PY32 hardware CRC unit: CRC-32/MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF,
 * no reflection, no final XOR) over 32-bit words. tools/build_payload.py
 * computes the same value for the external store. */
#ifndef DRIVER_HWCRC_H
#define DRIVER_HWCRC_H

#include <stdint.h>

void HWCRC_Begin(void);
void HWCRC_Feed(const void *data, uint32_t bytes); /* bytes: multiple of 4 */
uint32_t HWCRC_Result(void);
uint32_t HWCRC_Compute(const void *data, uint32_t bytes);

#endif
