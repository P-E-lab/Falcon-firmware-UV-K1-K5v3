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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "frequencies.h"
#include <helper/battery.h>
#include "radio.h"
#include <driver/backlight.h>

enum POWER_OnDisplayMode_t {
    POWER_ON_DISPLAY_MODE_MESSAGE,
    POWER_ON_DISPLAY_MODE_LOGO,
    POWER_ON_DISPLAY_MODE_NONE
};
typedef enum POWER_OnDisplayMode_t POWER_OnDisplayMode_t;

enum TxLockModes_t {
    F_LOCK_NONE,    // 0 = UNLOCK ALL   — TX on all frequencies
    F_LOCK_ALL,     // 1 = DISABLE ALL  — TX blocked everywhere
    F_LOCK_136_500, // 2 = 136-500 MHz only
    F_LOCK_PMR_LPD, // 3 = PMR446 + LPD433 only
    F_LOCK_LEN
};

enum {
    TX_OFFSET_FREQUENCY_DIRECTION_OFF = 0,
    TX_OFFSET_FREQUENCY_DIRECTION_ADD,
    TX_OFFSET_FREQUENCY_DIRECTION_SUB
};

enum {
    OUTPUT_POWER_LOW = 0,
    OUTPUT_POWER_MID,
    OUTPUT_POWER_HIGH,
    OUTPUT_POWER_LOW2,
    OUTPUT_POWER_LOW3,
    OUTPUT_POWER_LOW4,
    OUTPUT_POWER_LOW5,
    OUTPUT_POWER_LEN
};

// Preserve the stored V3.1 LOW/MID/HIGH IDs (0/1/2). The remaining IDs use
// the already reserved third power bit; all other channel bits stay intact.
static inline uint8_t POWER_ToIndex(uint8_t power)
{
    return power == OUTPUT_POWER_LOW ? 0 : power <= OUTPUT_POWER_HIGH ? power + 4 : power - 2;
}
static inline uint8_t POWER_FromIndex(uint8_t index)
{
    return index == 0 ? OUTPUT_POWER_LOW : index >= 5 ? index - 4 : index + 2;
}
extern const char gPowerShort[OUTPUT_POWER_LEN][3];
extern const char gPowerNames[OUTPUT_POWER_LEN][7];
extern const char gLowPowerWatts[5][9];

enum ACTION_OPT_t {
    ACTION_OPT_NONE = 0,
    ACTION_OPT_FLASHLIGHT,
    ACTION_OPT_POWER,
    ACTION_OPT_MONITOR,
    ACTION_OPT_SCAN,
    ACTION_OPT_ALARM,
    ACTION_OPT_FM,
    ACTION_OPT_1750,
    ACTION_OPT_KEYLOCK,
    ACTION_OPT_VFO_MR,
    ACTION_OPT_SWITCH_DEMODUL,
    ACTION_OPT_BLMIN_TMP_OFF, //BackLight Minimum Temporay OFF
#ifdef ENABLE_FEAT_F4HWN
    ACTION_OPT_PTT,
    ACTION_OPT_WN,
    ACTION_OPT_BACKLIGHT,
    ACTION_OPT_MUTE,
    ACTION_OPT_RXA,
#endif
    ACTION_OPT_SCANLIST_UP,
    ACTION_OPT_SCANLIST_DOWN,
    ACTION_OPT_SINGLE_DUO,
    ACTION_OPT_SCAN_ALL,
    ACTION_OPT_PTT2 = 23, // 21/22 remain removed Hub/Manager IDs.
    ACTION_OPT_VFO_AB = 24,
    ACTION_OPT_HISTORY = 25,
    ACTION_OPT_VOX = 26,
    ACTION_OPT_REVERSE = 27,
    ACTION_OPT_MESSAGES = 28,
    ACTION_OPT_FMESSAGE = 29,
    ACTION_OPT_BAND_SCAN = 30,
    ACTION_OPT_LEN
};


enum ALARM_Mode_t { ALARM_MODE_SITE = 0, ALARM_MODE_TONE };
typedef enum ALARM_Mode_t ALARM_Mode_t;

enum ROGER_Mode_t {
    ROGER_MODE_OFF = 0,
    ROGER_MODE_R2D2 = 3,
    ROGER_MODE_ROGER = 4,
    ROGER_MODE_OURO = 6,
    ROGER_MODE_STARWARS = 10,
    ROGER_MODE_FALCON = 9
};
typedef enum ROGER_Mode_t ROGER_Mode_t;

enum CHANNEL_DisplayMode_t { MDF_FREQUENCY = 0, MDF_CHANNEL, MDF_NAME, MDF_NAME_FREQ };
typedef enum CHANNEL_DisplayMode_t CHANNEL_DisplayMode_t;

enum RXMode_t { RX_MODE_SIMPLE = 0, RX_MODE_DUAL = 1, RX_MODE_FULL = 2, RX_MODE_ML = 3 };
typedef enum RXMode_t RXMode_t;

// PONSCR: the screen opened after power-up.
enum {
    POWER_ON_SCREEN_NORMAL = 0,
    POWER_ON_SCREEN_SPECTRE_LIST,
    POWER_ON_SCREEN_SPECTRE_BAND,
    POWER_ON_SCREEN_SPECTRE_RANGE,
    POWER_ON_SCREEN_SENTINEL,
    POWER_ON_SCREEN_FM,
    POWER_ON_SCREEN_COUNT
};

typedef struct {
    uint16_t ScreenChannel[2]; // current channels for VFO A/B
    uint16_t FreqChannel[2];   // reserved for VFO A/B compatibility
    uint16_t MrChannel[2];     // last memory channels for VFO A/B

    // The actual VFO index (0-upper/1-lower) that is now used for RX,
    // It is being alternated by dual watch, and flipped by crossband
    uint8_t RX_VFO;

    // The main VFO index (0-upper/1-lower) selected by the user
    //
    uint8_t TX_VFO;

    uint8_t field7_0xa;
    uint8_t field8_0xb;

#ifdef ENABLE_FMRADIO
    uint16_t FM_SelectedFrequency;
    uint8_t FM_SelectedChannel;
    bool FM_IsMrMode;
    uint16_t FM_FrequencyPlaying;
    uint8_t FM_Band : 2;
    //uint8_t         FM_Space : 2;
#endif

    uint8_t SQUELCH_LEVEL;
    uint8_t TX_TIMEOUT_TIMER;
    bool KEY_LOCK;
#ifdef ENABLE_FEAT_F4HWN
    bool KEY_LOCK_PTT;
    bool SET_NAV;
#endif
    bool FlashlightOnRX; // Falcon mode (legacy EEPROM bit)
    uint8_t CHANNEL_DISPLAY_MODE;
    bool TAIL_TONE_ELIMINATION;
    bool VFO_OPEN;
    uint8_t RX_MODE;
    bool TONE_1750;
    bool METER_CLASSIC;
    uint8_t BATTERY_SAVE;
    uint8_t BACKLIGHT_TIME;
    uint8_t SCAN_LIST_DEFAULT;
    bool SCAN_LIST_ENABLED;
    uint16_t SCANLIST_PRIORITY_CH[6];
    uint8_t CURRENT_STATE;
    uint8_t CURRENT_LIST;
    uint8_t AUTO_KEYPAD_LOCK;
    POWER_OnDisplayMode_t POWER_ON_DISPLAY_MODE;
    uint8_t POWER_ON_SCREEN; // PONSCR, POWER_ON_SCREEN_*
    ROGER_Mode_t ROGER;
    uint8_t AUTO_ROGER;
    uint8_t REPEATER_TAIL_TONE_ELIMINATION;
    uint8_t KEY_1_SHORT_PRESS_ACTION;
    uint8_t KEY_1_LONG_PRESS_ACTION;
    uint8_t KEY_2_SHORT_PRESS_ACTION;
    uint8_t KEY_2_LONG_PRESS_ACTION;
#ifdef ENABLE_VOX
    bool VOX_SWITCH;
    uint8_t VOX_LEVEL;
    uint16_t VOX1_THRESHOLD;
    uint16_t VOX0_THRESHOLD;
#endif
    uint8_t MIC_SENSITIVITY;
    uint8_t MIC_SENSITIVITY_TUNING;
    int16_t BK4819_XTAL_FREQ_LOW;
    uint8_t VOLUME_GAIN;
#ifdef ENABLE_FEAT_F4HWN
    uint8_t VOLUME_GAIN_BACKUP;
#endif
    uint8_t DAC_GAIN;
    VFO_Info_t VfoInfo[2];
    uint8_t KEY_M_LONG_PRESS_ACTION;
    uint8_t BACKLIGHT_MIN;
    uint8_t BACKLIGHT_MAX;
    BATTERY_Type_t BATTERY_TYPE;
#ifdef ENABLE_RSSI_BAR
    uint8_t S0_LEVEL;
    uint8_t S9_LEVEL;
    uint8_t SCRAMBLING_TYPE;
#endif
} EEPROM_Config_t;

extern EEPROM_Config_t gEeprom;

void SETTINGS_InitEEPROM(void);
void SETTINGS_LoadCalibration(void);
uint32_t SETTINGS_FetchChannelFrequency(const uint16_t channel);
void SETTINGS_FetchChannelName(char *s, const uint16_t channel);
void SETTINGS_FactoryReset(bool bIsAll); // 0=NoCH, 1=ALL
#ifdef ENABLE_FMRADIO
void SETTINGS_SaveFM(void);
#endif
void SETTINGS_SaveVfoIndices(void);
void SETTINGS_SaveVfoIndicesFlush(void);
void SETTINGS_SaveVfoNow(void);
void SETTINGS_ReadVfoRecord(uint8_t vfo, uint8_t offset, void *buffer, uint8_t size);
void SETTINGS_SetVfoFrequency(uint32_t frequency);
void SETTINGS_SaveSettings(void);
void SETTINGS_SaveChannelName(uint16_t channel, const char *name);
void SETTINGS_SaveChannel(uint16_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode);
enum {
    CHANNEL_PARAMETER_STEP = 1,
    CHANNEL_PARAMETER_BANDWIDTH = 2,
    CHANNEL_PARAMETER_POWER = 4,
    CHANNEL_PARAMETER_MODULATION = 8,
    CHANNEL_PARAMETER_REVERSE = 16
};
void SETTINGS_SaveChannelParameters(uint8_t VFO, const VFO_Info_t *pVFO, uint8_t fields);
void SETTINGS_SaveBatteryCalibration(const uint16_t *batteryCalibration);
void SETTINGS_UpdateChannel(uint16_t channel, const VFO_Info_t *pVFO, bool keep);
void SETTINGS_WriteBuildOptions(void);
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
void SETTINGS_WriteCurrentState(void);
#endif

uint8_t *SETTINGS_ShortcutSetting(uint8_t index);

extern const uint8_t gRogerModes[6];
uint8_t SETTINGS_RogerIndex(uint8_t mode);
void SETTINGS_SaveAutoRoger(void);
#endif
