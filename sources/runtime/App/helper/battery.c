/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <assert.h>

#include "battery.h"
#include "driver/backlight.h"
#include "driver/st7565.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/menu.h"
#include "ui/ui.h"

uint16_t gBatteryCalibration[6];
uint16_t gBatteryCurrentVoltage;
uint16_t gBatteryCurrent;
uint16_t gBatteryVoltages[4];
uint16_t gBatteryVoltageAverage;
uint8_t gBatteryDisplayLevel;
bool gChargingWithTypeC;
bool gLowBatteryBlink;
bool gLowBattery;
bool gLowBatteryConfirmed;
uint16_t gBatteryCheckCounter;

typedef enum { BATTERY_LOW_INACTIVE, BATTERY_LOW_ACTIVE, BATTERY_LOW_CONFIRMED } BatteryLow_t;

uint16_t lowBatteryCountdown;
const uint16_t lowBatteryPeriod = 30;

volatile uint16_t gPowerSave_10ms;

// Coefficients retain the original truncation at every curve segment.
typedef struct {
    uint16_t threshold;
    int16_t slope, intercept;
} BatterySegment;
static const BatterySegment BatterySegments[] = {
    {814, 214, -77},   {760, 1333, -988}, {729, 612, -440}, {630, 60, -37},   {813, 263, -118},
    {740, 479, -294},  {707, 1181, -813}, {682, 640, -431}, {630, 96, -60},   {826, 454, -280},
    {750, 592, -394},  {700, 500, -325},  {620, 250, -150}, {600, 250, -150}, {818, 238, -99},
    {745, 547, -352},  {703, 714, -476},  {668, 571, -376}, {623, 111, -69},  {813, 200, -65},
    {758, 1309, -967}, {726, 593, -424},  {630, 62, -39},
};
static const uint8_t BatterySegmentOffsets[] = {0, 4, 9, 14, 14, 19, 23};
unsigned int BATTERY_VoltsToPercent(const unsigned int voltage_10mV)
{
    const unsigned int profile = gEeprom.BATTERY_TYPE;
    for (unsigned int i = BatterySegmentOffsets[profile]; i < BatterySegmentOffsets[profile + 1]; ++i) {
        const BatterySegment *segment = &BatterySegments[i];
        if (voltage_10mV > segment->threshold) {
            const int p = segment->slope * voltage_10mV / 1000 + segment->intercept;
            return MIN(MAX(p, 0), 100);
        }
    }
    return 0;
}


void BATTERY_GetReadings(const bool bDisplayBatteryLevel)
{
    const uint8_t PreviousBatteryLevel = gBatteryDisplayLevel;
    const uint16_t Voltage =
        (gBatteryVoltages[0] + gBatteryVoltages[1] + gBatteryVoltages[2] + gBatteryVoltages[3]) / 4;

    gBatteryVoltageAverage = (Voltage * 760) / gBatteryCalibration[3];

    if (gBatteryVoltageAverage > 890)
        gBatteryDisplayLevel = 7; // battery overvoltage
    else if (gBatteryVoltageAverage < 630 &&
             (gEeprom.BATTERY_TYPE == BATTERY_TYPE_1600_MAH || gEeprom.BATTERY_TYPE == BATTERY_TYPE_2200_MAH))
        gBatteryDisplayLevel = 0; // battery critical
    else if (gBatteryVoltageAverage < 600 && (gEeprom.BATTERY_TYPE == BATTERY_TYPE_3500_MAH))
        gBatteryDisplayLevel = 0; // battery critical
    else {
        gBatteryDisplayLevel = 1;
        const uint8_t levels[] = {5, 17, 41, 65, 88};
        uint8_t perc = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
        //char str[64];
        //LogUart("----------\n");
        //sprintf(str, "%d %d %d %d %d %d %d\n", gBatteryVoltages[0], gBatteryVoltages[1], gBatteryVoltages[2], gBatteryVoltages[3], Voltage, gBatteryVoltageAverage, perc);
        //LogUart(str);

        for (uint8_t i = 6; i >= 2; i--) {
            //sprintf(str, "%d %d %d\n", perc, levels[i-2], i);
            //LogUart(str);
            if (perc > levels[i - 2]) {
                gBatteryDisplayLevel = i;
                break;
            }
        }
    }


    if ((gScreenToDisplay == DISPLAY_MENU) && UI_MENU_GetCurrentMenuId() == MENU_VOL)
        gUpdateDisplay = true;

    if (gBatteryCurrent < 501) {
        if (gChargingWithTypeC) {
            gUpdateStatus = true;
            gUpdateDisplay = true;
        }

        gChargingWithTypeC = false;
    } else {
        if (!gChargingWithTypeC) {
            gUpdateStatus = true;
            gUpdateDisplay = true;
            BACKLIGHT_TurnOn();
        }

        gChargingWithTypeC = true;
    }

    if (PreviousBatteryLevel != gBatteryDisplayLevel) {
        if (gBatteryDisplayLevel > 2)
            gLowBatteryConfirmed = false;
        else if (gBatteryDisplayLevel < 2) {
            gLowBattery = true;
        } else {
            gLowBattery = false;

            if (bDisplayBatteryLevel)
                UI_DisplayBattery(gBatteryDisplayLevel, gLowBatteryBlink);
        }

        if (!gLowBatteryConfirmed)
            gUpdateDisplay = true;

        lowBatteryCountdown = 0;
    }
}

void BATTERY_TimeSlice500ms(void)
{
    if (!gLowBattery) {
        return;
    }

    gLowBatteryBlink = ++lowBatteryCountdown & 1;

    UI_DisplayBattery(0, gLowBatteryBlink);

    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        return;
    }

    // not transmitting

    lowBatteryCountdown = 0;

    if (gChargingWithTypeC) {
        return;
    }

    if (gBatteryDisplayLevel != 0) {
        return;
    }


    gReducedService = true;

    FUNCTION_Select(FUNCTION_POWER_SAVE);

    ST7565_HardwareReset();

    if (gEeprom.BACKLIGHT_TIME < 61) {
        BACKLIGHT_TurnOff();
    }
}
