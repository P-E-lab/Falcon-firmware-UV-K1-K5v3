/* Falcon external store (see store_layout.h): native code modules and the
 * read-only resources tagged FP_RESOURCE. */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stdint.h>
#include "store_layout.h"

/* Read-only data kept in the store. Never dereference such an object
 * directly: read it with FP_Read / FP_Byte / FP_CopyString. */
#define FP_RESOURCE __attribute__((section(".fp_resources"), used))

void FP_Read(const void *src, void *dst, uint32_t len);
uint8_t FP_Byte(const void *src);
void FP_CopyString(char *dst, const char *src, uint32_t cap);

/* Copy native module `id` into `dst` (STORE_MODULE_MAX bytes, zero-filled
 * beyond the module) and verify it. False if the store is absent, belongs to
 * another build, or the module is damaged. */
bool STORE_LoadModule(uint8_t id, uint8_t *dst);

/* Boot-time check; afterwards true while the store of the running slot is
 * missing or belongs to another build (the UI then asks for a reinstall). */
void STORE_Check(void);
bool STORE_IsMissing(void);

#endif
