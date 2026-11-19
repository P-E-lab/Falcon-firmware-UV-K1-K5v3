#ifndef APP_FKEYS_H
#define APP_FKEYS_H

#include <stdbool.h>
#include "driver/keyboard.h"

/* FKeys: the VFO screen's long and F+ presses, reassigned by the FKeys app or
 * CHIRP in the slot's FKeys sector (ADRESS_FKEYS). Format, version 1:
 *   "FKY1", version (1), long[16], fplus[16]   indexed by KEY_0..KEY_F
 * Each entry: 0xFF default, 0..ACTION_OPT_LEN-1 a shortcut action,
 * FKEYS_SENTINEL + mode (lists 0, band 1, range 2), FKEYS_LONG | key or
 * FKEYS_FPLUS | key: the native long or F+ function of that key. */
#define FKEYS_MAGIC    "FKY1"
#define FKEYS_VERSION  1u
#define FKEYS_KEYS     16u
#define FKEYS_DEFAULT  0xFFu
#define FKEYS_SENTINEL 0x40u
#define FKEYS_LONG     0x80u
#define FKEYS_FPLUS    0x90u

void FKEYS_Load(void);
bool FKEYS_Intercept(KEY_Code_t key, bool pressed, bool held);

#endif
