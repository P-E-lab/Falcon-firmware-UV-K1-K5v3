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

#include "scheduler.h"
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif

#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/ui.h"

#include "driver/backlight.h"
#include "driver/gpio.h"

#define DECREMENT(cnt)                                                                                       \
    do {                                                                                                     \
        if (cnt > 0)                                                                                         \
            cnt--;                                                                                           \
    } while (0)

#define DECREMENT_AND_TRIGGER(cnt, flag)                                                                     \
    do {                                                                                                     \
        if (cnt > 0)                                                                                         \
            if (--cnt == 0)                                                                                  \
                flag = true;                                                                                 \
    } while (0)


// we come here every 10ms (100Hz tick)
void SysTick_Handler(void)
{
    static uint8_t cnt_500ms = 50;
    static uint16_t cnt_scan_led = 500;

    static uint8_t periodicCounters[] = {20, 15, 47, 100};
    static const struct {
        volatile bool *flag;
        uint8_t period;
    } periodic[] = {{&gNextTimeslice_listening, 20},
                    {&gNextTimeslice_display, 15},
                    {&gNextTimeslice_Monitor, 47},
                    {&gNextTimeslice_HTimeS, 100}};

    gGlobalSysTickCounter++;

    gNextTimeslice = true;
    gNextTimeslice_10ms = true;

    for (unsigned int i = 0; i < ARRAY_SIZE(periodic); ++i) {
        if (--periodicCounters[i] == 0) {
            periodicCounters[i] = periodic[i].period;
            *periodic[i].flag = true;
        }
    }

    // 5000ms scan led beacon
    if (--cnt_scan_led == 0) {
        cnt_scan_led = 500;
        gNextTimeslice_SCAN_LED = true;
    } else if (cnt_scan_led == 470) {
        gNextTimeslice_SCAN_LED_OFF = true;
    }

    // 180000 ticks fit in 32 bits: 30 minutes must never wrap at 65536.
    if (gAutoPttCountdown && --gAutoPttCountdown == 0) {
        gNextTimeslice_AutoPtt = true;
        gAutoPttCountdown = (uint32_t)gAutoPtt_Time * 100u;
    }
#ifdef ENABLE_FEAT_F4HWN
    DECREMENT_AND_TRIGGER(gVfoSaveCountdown_10ms, gScheduleVfoSave);
#endif
    // 500ms tasks (ShowNames, history, timers)
    if (--cnt_500ms == 0) {
        cnt_500ms = 50;
        gNextTimeslice_history = true;
        gNextTimeslice_500ms = true;
#ifdef ENABLE_FEAT_F4HWN
        DECREMENT_AND_TRIGGER(gTxTimerCountdownAlert_500ms - ALERT_TOT * 2, gTxTimeoutReachedAlert);
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
        DECREMENT(gRxTimerCountdown_500ms);
#endif
#endif

        DECREMENT_AND_TRIGGER(gTxTimerCountdown_500ms, gTxTimeoutReached);
        DECREMENT(gSerialConfigCountDown_500ms);
    }

    if ((gGlobalSysTickCounter & 3) == 0)
        gNextTimeslice40ms = true;


    DECREMENT(gFoundCDCSSCountdown_10ms);

#ifdef ENABLE_VOX
    DECREMENT(gVoxStopCountdown_10ms);
#endif

    DECREMENT(gFoundCTCSSCountdown_10ms);

    if (gCurrentFunction == FUNCTION_FOREGROUND)
        DECREMENT_AND_TRIGGER(gBatterySaveCountdown_10ms, gSchedulePowerSave);

    if (gCurrentFunction == FUNCTION_POWER_SAVE)
        DECREMENT_AND_TRIGGER(gPowerSave_10ms, gPowerSaveCountdownExpired);

    // Same eligible function states as H4WHN. A stale last-RX flag must
    // neither freeze post-RX holds nor prevent the power-save wake sequence.
    if (gEeprom.RX_MODE != RX_MODE_SIMPLE && !gCssBackgroundScan &&
#ifdef ENABLE_FMRADIO
        !gFmRadioMode &&
#endif
        GUI_IsStandbyScreen() && gCurrentFunction != FUNCTION_MONITOR &&
        gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_RECEIVE)
        DECREMENT_AND_TRIGGER(gDualVfoCountdown_10ms, gDualVfoSchedule);

    DECREMENT_AND_TRIGGER(gTailNoteEliminationCountdown_10ms, gFlagTailNoteEliminationComplete);


    DECREMENT(boot_counter_10ms);
}
