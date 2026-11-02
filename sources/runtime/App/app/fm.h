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

#ifndef APP_FM_H
#define APP_FM_H

#ifdef ENABLE_FMRADIO

#include "driver/keyboard.h"

#define FM_CHANNEL_UP   0x01
#define FM_CHANNEL_DOWN 0xFF

enum {
    FM_SCAN_OFF = 0U,
};

extern uint16_t gFM_Channels[48];
extern bool gFmRadioMode;
extern bool gFM_No_Rx;
extern uint8_t gFmRadioCountdown_500ms;
// Doubts about          whether this should be signed or not
extern uint16_t gFM_FrequencyDeviation;
extern bool gFmNameDisplay;
extern uint16_t gFM_RestoreCountdown_10ms;
extern bool gFM_ManualMode;    // false = AUTO seek, true = MANUAL step
extern bool gFM_Mute;          // true = RX does not interrupt radio
extern uint16_t gFM_Memory[9]; // nine frequency memory slots (0 = empty)

extern uint8_t gFM_Favourite, gFM_SavedSlot, gFM_RSSI, gFM_Quality;
extern bool gFM_SignalValid;
void FM_TimeSlice500ms(void);

void FM_Memory_Load(void);
void FM_Memory_Save(uint8_t slot);
// returns first valid channel starting at Channel
int FM_ConfigureChannelState(void);
void FM_TurnOff(void);
int FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit);

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

void FM_Start(void);
void FM_TimeSlice10ms(void);
void FM_CancelSeek(void);
void FM_SetVfoAudio(bool enabled);

const char *FM_FindRadioName(uint16_t freq);

#endif

#endif
