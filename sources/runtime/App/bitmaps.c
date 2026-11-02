
#include "bitmaps.h"

// all these images are on their right sides
// turn your monitor 90-deg anti-clockwise to see the images

const uint8_t gFontPowerSave[2][6] = {
    {0x00, 0x7f, 0x9, 0x9, 0x9, 0x6},
    {0x00, 0x26, 0x49, 0x49, 0x49, 0x32},
};

const uint8_t gFontKeyLock[9] = {0x7c, 0x46, 0x45, 0x45, 0x45, 0x45, 0x45, 0x46, 0x7c};

const uint8_t gFontLight[9] = {
    0b00001100, 0b00010010, 0b00100001, 0b01101101, 0b01111001,
    0b01101101, 0b00100001, 0b00010010, 0b00001100,
};

const uint8_t BITMAP_BatteryLevel[2] = {0b01011101, 0b01011101};

// Quansheng way (+ pole to the left)
const uint8_t BITMAP_BatteryLevel1[17] = {0b00000000, 0b00111110, 0b00100010, 0b01000001, 0b01000001,
                                          0b01000001, 0b01000001, 0b01000001, 0b01000001, 0b01000001,
                                          0b01000001, 0b01000001, 0b01000001, 0b01000001, 0b01000001,
                                          0b01000001, 0b01111111};

// Compact filled arrow (F4HWN style)
const uint8_t BITMAP_VFO_Default[7] = {0b00111110, 0b00111110, 0b00011100, 0b00011100,
                                       0b00001000, 0b00001000, 0b00000000};

// Compact hollow arrow (F4HWN style)
const uint8_t BITMAP_VFO_NotDefault[7] = {0b00100010, 0b00100010, 0b00010100, 0b00010100,
                                          0b00001000, 0b00001000, 0b00000000};

// Empty (for blinking)
const uint8_t BITMAP_VFO_Empty[7] = {0b00000000, 0b00000000, 0b00000000, 0b00000000,
                                     0b00000000, 0b00000000, 0b00000000};

const uint8_t BITMAP_compand[6] = {0b00000000, 0b00111100, 0b01000010, 0b01000010, 0b01000010, 0b00100100};


const uint8_t BITMAP_CurrentIndicator[8] = {0xFF, 0xFF, 0x7E, 0x7E, 0x3C, 0x3C, 0x18, 0x18};
