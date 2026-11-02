/* Copyright 2023 fagci
 * https://github.com/fagci
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

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "../bitmaps.h"
#include "../board.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
#include "../driver/gpio.h"
#include "../driver/keyboard.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../frequencies.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/helper.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ADRESS_STATE   0xC000
#define ADRESS_VERSION 0xC010
#define ADRESS_PARAMS  0xC020
#define ADRESS_HISTORY 0xD000


typedef enum { AUTOLOCK_OFF, AUTOLOCK_10S, AUTOLOCK_20S, AUTOLOCK_30S } AUTOLOCK_t;

extern const char scanStepNames[24][6];
extern const uint16_t scanStepValues[24];
extern const uint16_t jumpSizes[24];

typedef enum State {
    SPECTRUM,
    STILL,
    BAND_LIST_SELECT,
    SCANLIST_SELECT,
    PARAMETERS_SELECT,
    HISTORY_MENU_SELECT,
    RANGE_EDIT,
} State;


typedef enum Mode {
    CHANNEL_MODE,
    SCAN_RANGE_MODE,
    SCAN_BAND_MODE,
} Mode;

typedef enum StepsCount {
    STEPS_128,
    STEPS_64,
    STEPS_32,
    STEPS_16,
} StepsCount;


typedef struct bandparameters {
    char BandName[9];
    uint32_t Startfrequency; // Start frequency in MHz /100
    uint32_t Stopfrequency;  // Stop frequency in MHz /100
    STEP_Setting_t scanStep;
    ModulationMode_t modulationType;
    uint8_t bandwidth;
} bandparameters;

#define MAX_BANDS 30


typedef struct SpectrumSettings {
    StepsCount stepsCount;
    STEP_Setting_t scanStepIndex;
    uint16_t rssiTriggerLevelUp;
    BK4819_FilterBandwidth_t listenBw;
    int16_t dbMin;
    int16_t dbMax;
    ModulationMode_t modulationType;
    bool scanListEnabled[MR_CHANNELS_LIST];
    bool bandEnabled[MAX_BANDS];
} SpectrumSettings;

typedef struct KeyboardState {
    KEY_Code_t current;
    KEY_Code_t prev;
    uint8_t counter;
    bool fKeyPressed;
} KeyboardState;

typedef struct ScanInfo {
    uint16_t rssi, rssiMin, rssiMax;
    uint32_t f;
    uint16_t scanStep, i;
} ScanInfo;

typedef struct PeakInfo {
    uint16_t t;
    uint16_t rssi;
    uint32_t f;
    uint16_t i;
} PeakInfo;

typedef struct ChannelInfo_t {
    uint32_t frequency;
    uint32_t offset;
} __attribute__((packed)) ChannelInfo_t;

uint8_t APP_GetScanningVfo(void); // UINT8_MAX outside a VFO scan.
void APP_RunSpectrum(void);
void APP_RunScanner(Mode mode, bool sentinel);
void APP_RunSentinel(void);
void APP_RunHistory(void);
void APP_RunMessages(void);
#define SCAN_VFO_FREQUENCY 0x40u
void APP_RunCurrentScanList(uint8_t list);
void APP_RunAllMemoryScan(void);
extern uint8_t gVfoScanSQL, gSpectrumSQL, gScanSQLHys;
extern uint16_t gScanSqlPreview;
void APP_LoadScanSQL(void);
void APP_SaveScanSQL(void);
void APP_AdjustScanSQL(int direction);
bool APP_ScanSqlText(char *text);
bool APP_MLHoldRx(void);
void APP_MLWatchStop(void);
bool APP_MLWatchEligible(void);
void APP_RunMLWatch(void);
VFO_Info_t *APP_GetMLWatchDisplayVfo(uint8_t vfo);
void APP_RunVfoScanRange(void);
void APP_RunSpectrumMode(Mode mode); // 0=FREQ, 1=SCANLIST, 2=RANGE, 3=BAND
void ClearSettings(void);
void LoadSettings(void);
extern Mode Spectrum_state;


#endif

bool APP_SpectrumBacklightOff(void);

void APP_ResourceRedraw(void);
