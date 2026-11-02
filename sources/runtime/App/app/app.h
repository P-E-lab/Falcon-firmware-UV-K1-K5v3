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

#ifndef APP_APP_H
#define APP_APP_H

#include <stdbool.h>

#include "functions.h"
#include "frequencies.h"
#include "radio.h"
#include "driver/keyboard.h"

bool APP_IsScreenSaverDisplayed(void);
void APP_ModalBacklightTick(bool allowScreenSaver);
void APP_ModalScreenSaverExit(void);
bool APP_Ptt2Active(void);
bool APP_FalconMode(void);
void APP_ConsumeHeldKey(KEY_Code_t key);
bool APP_FalconLedValue(unsigned pin, bool value);
void APP_FalconToggle(void);
void APP_FalconDisable(void);
void APP_FalconRx(void);
void APP_FalconTx(void);
void APP_FalconTxEnd(void);
void APP_FalconSwitch(void);

void APP_FullWatchReset(void);
void APP_ResumeScanKey(KEY_Code_t key);
VFO_Info_t *APP_GetFullWatchDisplayVfo(uint8_t vfo);
VFO_Info_t *const *APP_GetFullWatchBackgroundVfos(uint8_t *count);
/* Watch dwell per position, in 10 ms ticks. Dual and Full Watch are tuned
 * independently: changing one never alters the other. */
#define DUAL_WATCH_DWELL_10MS 10u /* Dual: 100 ms per VFO                  */
#define FULL_WATCH_DWELL_10MS 10u /* Full Watch: 100 ms per watched position */
/* Full Watch leaves a position early once the receiver has had time to measure
 * it (FULL_WATCH_EARLY_10MS) and two successive RSSI readings are clearly below
 * the squelch-open threshold (FULL_WATCH_QUIET_MARGIN, in 0.5 dB steps). Near
 * or above the threshold, the full dwell is kept. */
#define FULL_WATCH_EARLY_10MS   4u
#define FULL_WATCH_QUIET_MARGIN 12u
void APP_FullWatchEarlyAdvance(void);
void APP_EndTransmission(void);
void APP_HandleEndTransmission(void);
void APP_StartListening(FUNCTION_Type_t function);
uint32_t APP_SetFreqByStepAndLimits(VFO_Info_t *pInfo, int8_t direction, uint32_t lower, uint32_t upper);
uint32_t APP_SetFrequencyByStep(VFO_Info_t *pInfo, int8_t direction);
void APP_Update(void);
void cancelUserInputModes(void);
void APP_TimeSlice10ms(void);
void APP_TimeSlice500ms(void);


void APP_SetAutoRoger(uint8_t selection);
bool APP_AutoRoger(void);
#endif
