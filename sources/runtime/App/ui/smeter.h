#pragma once
#include <stdint.h>

// H4WHN Fusion 6.0.0 S-unit thresholds; graphical fill remains independent.
static inline uint8_t SmeterLevelFromDbm(int16_t dbm)
{
    if (dbm >= -93)
        return 9 + ((dbm + 93) < 40 ? (dbm + 93) : 40) / 10;
    if (dbm < -141)
        return 0;
    return (dbm + 147) / 6;
}
