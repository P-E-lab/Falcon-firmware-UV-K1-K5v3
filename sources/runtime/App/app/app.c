#include "app/monitor.h"
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
#include <stdint.h>
#include <string.h>
#include "app/action.h"
#include "app/app.h"
#ifdef ENABLE_FLASHLIGHT
#include "app/flashlight.h"
#endif
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif
#include "app/generic.h"
#include "app/main.h"
#include "app/menu.h"
#if defined(ENABLE_UART) || defined(ENABLE_USB)
#include "app/uart.h"
#include "scheduler.h"
#endif
#include "py32f0xx.h"
#include "board.h"
#include "driver/backlight.h"
#ifdef ENABLE_FMRADIO
#include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"

#if defined(ENABLE_OVERLAY)
#include "sram-overlay.h"
#endif
#include "ui/battery.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/menu.h"
#include "ui/status.h"
#include "ui/ui.h"
#include "app/spectrum.h"
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
#include "app/rxtx_log.h"
#endif
#include "driver/py25q16.h"

static bool flagSaveVfo;
static bool flagSaveSettings;
static bool flagSaveChannel;


#include "falcon_mode.inc"

static void ProcessKey(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

void (*ProcessKeysFunctions[])(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) = {
    [DISPLAY_MAIN] = &MAIN_ProcessKeys,
    [DISPLAY_CHANNEL] = &CHANNEL_ProcessKeys,
    [DISPLAY_MENU] = &MENU_ProcessKeys,

#ifdef ENABLE_FMRADIO
    [DISPLAY_FM] = &FM_ProcessKeys,
#endif
};

static_assert(ARRAY_SIZE(ProcessKeysFunctions) == DISPLAY_N_ELEM);

#include "full_watch.inc"

static uint32_t fullWatchRetuneTick, fullWatchCheckTick;
static uint8_t fullWatchQuietReadings;

// H4WHN 6.1.0: retune, then arm the next 100-ms watch interval.
static void DualVfoAlternate(void)
{
    fullWatchRetuneTick = gGlobalSysTickCounter;
    fullWatchQuietReadings = 0;
    if (gEeprom.RX_MODE == RX_MODE_FULL)
        FullWatchAlternate();
    else {
        gEeprom.RX_VFO ^= 1u;
        gRxVfo = &gEeprom.VfoInfo[gEeprom.RX_VFO];
    }
    gCurrentVfo = gRxVfo;
    RADIO_SetupRegisters(false);
    // Reset detection on every retune, including power-save wake cycles.
    gRxVfoIsActive = false;
    gRxReceptionMode = RX_MODE_NONE;
    gDualVfoSchedule = false;
    gDualVfoCountdown_10ms = gEeprom.RX_MODE == RX_MODE_FULL ? FULL_WATCH_DWELL_10MS : DUAL_WATCH_DWELL_10MS;
}

// Full Watch only: move on from a quiet position before the full dwell.
__attribute__((noinline)) void APP_FullWatchEarlyAdvance(void)
{
    const uint32_t now = gGlobalSysTickCounter;
    const uint32_t elapsed = now - fullWatchRetuneTick;
    if (gEeprom.RX_MODE != RX_MODE_FULL || gDualVfoSchedule || gCurrentFunction != FUNCTION_FOREGROUND ||
        gRxReceptionMode != RX_MODE_NONE || g_SquelchLost || elapsed < FULL_WATCH_EARLY_10MS ||
        elapsed >= FULL_WATCH_DWELL_10MS || now == fullWatchCheckTick)
        return;
    fullWatchCheckTick = now; // at most one reading per 10 ms tick
    if (BK4819_GetRSSI() + FULL_WATCH_QUIET_MARGIN < gRxVfo->SquelchOpenRSSIThresh) {
        if (++fullWatchQuietReadings >= 2)
            gDualVfoSchedule = true;
    } else {
        fullWatchQuietReadings = 0;
    }
}

static void CheckForIncoming(void)
{
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && (gFM_Mute || gFM_No_Rx))
        return;
#endif
    if (!g_SquelchLost) {
        if (gEeprom.RX_MODE == RX_MODE_FULL && gRxReceptionMode == RX_MODE_DETECTED &&
            (gCurrentFunction == FUNCTION_FOREGROUND || gCurrentFunction == FUNCTION_POWER_SAVE)) {
            gRxReceptionMode = RX_MODE_NONE;
            gDualVfoCountdown_10ms = FULL_WATCH_DWELL_10MS;
            gDualVfoSchedule = false;
        }
        return;
    }
    // H4WHN: start the detection hold once, not on every foreground iteration.
    if (gEeprom.RX_MODE != RX_MODE_SIMPLE && gRxReceptionMode == RX_MODE_NONE) {
        gDualVfoCountdown_10ms = 100;
        gDualVfoSchedule = false;
    }
    gRxReceptionMode = RX_MODE_DETECTED;
    if (gCurrentFunction != FUNCTION_INCOMING)
        FUNCTION_Select(FUNCTION_INCOMING);
}

static void HandleIncoming(void)
{
    if (!g_SquelchLost) { // squelch is closed
        if (gCurrentFunction != FUNCTION_FOREGROUND) {
            FUNCTION_Select(FUNCTION_FOREGROUND);
            gUpdateDisplay = true;
        }
        return;
    }

    bool bFlag = (gCurrentCodeType == CODE_TYPE_OFF);

    if (g_CTCSS_Lost && gCurrentCodeType == CODE_TYPE_CONTINUOUS_TONE) {
        bFlag = true;
        gFoundCTCSS = false;
    }

    if (g_CDCSS_Lost && gCDCSSCodeType == CDCSS_POSITIVE_CODE &&
        (gCurrentCodeType == CODE_TYPE_DIGITAL || gCurrentCodeType == CODE_TYPE_REVERSE_DIGITAL)) {
        gFoundCDCSS = false;
    } else if (!bFlag)
        return;

    APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
}

static void HandleReceive(void)
{
#define END_OF_RX_MODE_SKIP 0
#define END_OF_RX_MODE_END  1
#define END_OF_RX_MODE_TTE  2

    uint8_t Mode = END_OF_RX_MODE_SKIP;

    if (gFlagTailNoteEliminationComplete) {
        Mode = END_OF_RX_MODE_END;
        goto Skip;
    }


    switch (gCurrentCodeType) {
        default:
        case CODE_TYPE_OFF:
            break;

        case CODE_TYPE_CONTINUOUS_TONE:
        case CODE_TYPE_DIGITAL:
        case CODE_TYPE_REVERSE_DIGITAL:
            if ((gFoundCTCSS && gFoundCTCSSCountdown_10ms == 0) ||
                (gFoundCDCSS && gFoundCDCSSCountdown_10ms == 0)) {
                gFoundCTCSS = false;
                gFoundCDCSS = false;
                Mode = END_OF_RX_MODE_END;
                goto Skip;
            }
            break;
    }

    if (g_SquelchLost) {
        if (!gEndOfRxDetectedMaybe) {
            switch (gCurrentCodeType) {
                case CODE_TYPE_OFF:
                    if (gEeprom.SQUELCH_LEVEL) {
                        if (g_CxCSS_TAIL_Found) {
                            Mode = END_OF_RX_MODE_TTE;
                            g_CxCSS_TAIL_Found = false;
                        }
                    }
                    break;

                case CODE_TYPE_CONTINUOUS_TONE:
                    if (g_CTCSS_Lost) {
                        gFoundCTCSS = false;
                    } else if (!gFoundCTCSS) {
                        gFoundCTCSS = true;
                        gFoundCTCSSCountdown_10ms = 100; // 1 sec
                    }

                    if (g_CxCSS_TAIL_Found) {
                        Mode = END_OF_RX_MODE_TTE;
                        g_CxCSS_TAIL_Found = false;
                    }
                    break;

                case CODE_TYPE_DIGITAL:
                case CODE_TYPE_REVERSE_DIGITAL:
                    if (g_CDCSS_Lost && gCDCSSCodeType == CDCSS_POSITIVE_CODE) {
                        gFoundCDCSS = false;
                    } else if (!gFoundCDCSS) {
                        gFoundCDCSS = true;
                        gFoundCDCSSCountdown_10ms = 100; // 1 sec
                    }

                    if (g_CxCSS_TAIL_Found) {
                        if (BK4819_GetCTCType() == 1)
                            Mode = END_OF_RX_MODE_TTE;

                        g_CxCSS_TAIL_Found = false;
                    }

                    break;
            }
        }
    } else
        Mode = END_OF_RX_MODE_END;

    if (!gEndOfRxDetectedMaybe && Mode == END_OF_RX_MODE_SKIP && gNextTimeslice40ms &&
        gEeprom.TAIL_TONE_ELIMINATION &&
        (gCurrentCodeType == CODE_TYPE_DIGITAL || gCurrentCodeType == CODE_TYPE_REVERSE_DIGITAL) &&
        BK4819_GetCTCType() == 1)
        Mode = END_OF_RX_MODE_TTE;
    else
        gNextTimeslice40ms = false;

Skip:
    switch (Mode) {
        case END_OF_RX_MODE_SKIP:
            break;

        case END_OF_RX_MODE_END:
            if (gEeprom.RX_MODE != RX_MODE_SIMPLE) {
                gRxVfoIsActive = false;
                gRxReceptionMode = RX_MODE_NONE;
                gDualVfoCountdown_10ms = gEeprom.RX_MODE == RX_MODE_FULL ? 420 : 100;
                gDualVfoSchedule = false;
            }
            RADIO_SetupRegisters(true);

            gUpdateDisplay = true;

            break;

        case END_OF_RX_MODE_TTE:
            if (gEeprom.TAIL_TONE_ELIMINATION) {
                GPIO_DisableAudioPath();

                gTailNoteEliminationCountdown_10ms = 20;
                gFlagTailNoteEliminationComplete = false;
                gEndOfRxDetectedMaybe = true;
                gEnableSpeaker = false;
            }
            break;
    }
}

static void HandlePowerSave()
{
    if (!gRxIdleMode) {
        CheckForIncoming();
    }
}

static void (*HandleFunction_fn_table[])(void) = {
    [FUNCTION_FOREGROUND] = &CheckForIncoming, [FUNCTION_TRANSMIT] = &FUNCTION_NOP,
    [FUNCTION_MONITOR] = &FUNCTION_NOP,        [FUNCTION_INCOMING] = &HandleIncoming,
    [FUNCTION_RECEIVE] = &HandleReceive,       [FUNCTION_POWER_SAVE] = &HandlePowerSave,
    [FUNCTION_BAND_SCOPE] = &FUNCTION_NOP,
};

static_assert(ARRAY_SIZE(HandleFunction_fn_table) == FUNCTION_N_ELEM);

static void HandleFunction(void)
{
    HandleFunction_fn_table[gCurrentFunction]();
}

void APP_StartListening(FUNCTION_Type_t function)
{
    FullWatchPromoteCurrentBackground();
    const unsigned int vfo = gEeprom.RX_VFO;

    if (gEeprom.RX_MODE != RX_MODE_SIMPLE) {
        gRxVfoIsActive = true;
        gRxReceptionMode = RX_MODE_LISTENING;
        gDualVfoCountdown_10ms = gEeprom.RX_MODE == RX_MODE_FULL ? 420 : 10; // H4 Full Watch RX hold
        gDualVfoSchedule = false;
    }

#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    gRxTimerCountdown_500ms = 7200;
#endif

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && !gFM_Mute) {
        if (gFM_RestoreCountdown_10ms == 0)
            BK1080_Init0();
    }
#endif

    // clear the other vfo's rssi level (to hide the antenna symbol)
    gVFO_RSSI_bar_level[!vfo] = 0;

    GPIO_EnableAudioPath();
    gEnableSpeaker = true;

    if (gSetting_backlight_on_tx_rx & BACKLIGHT_ON_TR_RX) {
        BACKLIGHT_TurnOn();
    }

    BK4819_SetRxAudioGain(); // AF DAC Gain (after Gain-1 and Gain-2)

    RADIO_SetModulation(gRxVfo->Modulation); // no need, set it now

    FUNCTION_Select(function);
    if (gEeprom.RX_MODE == RX_MODE_FULL && g_SquelchLost)
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    if (function == FUNCTION_RECEIVE)
        APP_FalconRx();

    if (function == FUNCTION_MONITOR) { // squelch is disabled
        if (gScreenToDisplay != DISPLAY_MENU &&
            gScreenToDisplay != DISPLAY_CHANNEL) // 1of11 .. don't close the menu
            GUI_SelectNextDisplay(DISPLAY_MAIN);
    } else
        gUpdateDisplay = true;

    gUpdateStatus = true;
}

uint32_t APP_SetFreqByStepAndLimits(VFO_Info_t *pInfo, int8_t direction, uint32_t lower, uint32_t upper)
{
    uint32_t Frequency = FREQUENCY_RoundToStep(
        pInfo->freq_config_RX.Frequency + (direction * pInfo->StepFrequency), pInfo->StepFrequency);

#ifdef ENABLE_FEAT_F4HWN
    if (Frequency > upper)
#else
    if (Frequency >= upper)
#endif
        Frequency = lower;

    else if (Frequency < lower)
        Frequency = FREQUENCY_RoundToStep(upper - pInfo->StepFrequency, pInfo->StepFrequency);

    return Frequency;
}

#define BX4819_band1_lower 1400000
#define BX4819_band2_upper 116000000

uint32_t APP_SetFrequencyByStep(VFO_Info_t *pInfo, int8_t direction)
{
    return APP_SetFreqByStepAndLimits(pInfo, direction, BX4819_band1_lower, BX4819_band2_upper);
}

static void CheckRadioInterrupts(void)
{

    while (BK4819_ReadRegister(BK4819_REG_0C) & 1u) { // BK chip interrupt request
        // clear interrupts
        BK4819_WriteRegister(BK4819_REG_02, 0);
        // fetch interrupt status bits

        union {
            struct {
                uint16_t __UNUSED : 1;
                uint16_t fskRxSync : 1;
                uint16_t sqlLost : 1;
                uint16_t sqlFound : 1;
                uint16_t voxLost : 1;
                uint16_t voxFound : 1;
                uint16_t ctcssLost : 1;
                uint16_t ctcssFound : 1;
                uint16_t cdcssLost : 1;
                uint16_t cdcssFound : 1;
                uint16_t cssTailFound : 1;
                uint16_t reserved11 : 1;
                uint16_t fskFifoAlmostFull : 1;
                uint16_t fskRxFinied : 1;
                uint16_t fskFifoAlmostEmpty : 1;
                uint16_t fskTxFinied : 1;
            };
            uint16_t __raw;
        } interrupts;

        interrupts.__raw = BK4819_ReadRegister(BK4819_REG_02);


        if (interrupts.cssTailFound)
            g_CxCSS_TAIL_Found = true;

        if (interrupts.cdcssLost) {
            g_CDCSS_Lost = true;
            gCDCSSCodeType = BK4819_GetCDCSSCodeType();
        }

        if (interrupts.cdcssFound)
            g_CDCSS_Lost = false;

        if (interrupts.ctcssLost)
            g_CTCSS_Lost = true;

        if (interrupts.ctcssFound)
            g_CTCSS_Lost = false;

#ifdef ENABLE_VOX
        if (interrupts.voxLost) {
            g_VOX_Lost = true;
            gVoxPauseCountdown = 10;

            if (gEeprom.VOX_SWITCH) {
                if (gCurrentFunction == FUNCTION_POWER_SAVE && !gRxIdleMode) {
                    gPowerSave_10ms = power_save2_10ms;
                    gPowerSaveCountdownExpired = 0;
                }

                if (gEeprom.RX_MODE != RX_MODE_SIMPLE &&
                    (gDualVfoSchedule || gDualVfoCountdown_10ms < dual_watch_count_after_vox_10ms)) {
                    gDualVfoCountdown_10ms = dual_watch_count_after_vox_10ms;
                    gDualVfoSchedule = false;
                    gUpdateStatus = true;
                }
            }
        }

        if (interrupts.voxFound) {
            g_VOX_Lost = false;
            gVoxPauseCountdown = 0;
        }
#endif

        if (interrupts.sqlLost) {
            g_SquelchLost = true;
            if (gEeprom.RX_MODE != RX_MODE_FULL)
                BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
            gRxTimerCountdown_500ms = 7200;
#endif
        }

        if (interrupts.sqlFound) {
            g_SquelchLost = false;
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        }
    }
}

static void TX1750_Off(void)
{
    GPIO_DisableAudioPath();
    gEnableSpeaker = false;

    RADIO_SendEndOfTransmission();

    gTx1750Active = false;

#ifdef ENABLE_VOX
    gVoxResumeCountdown = 80;
#endif

    SYSTEM_DelayMs(5);

    RADIO_SetupRegisters(true);

    if (gScreenToDisplay != DISPLAY_MENU && gScreenToDisplay != DISPLAY_CHANNEL)
        gRequestDisplayScreen = DISPLAY_MAIN;
}

void APP_EndTransmission(void)
{
    APP_FalconTxEnd();
    if (gTx1750Active) {
        BK4819_StopTransmitTone();
        gTx1750Active = false;
    }

    // back to RX mode
    RADIO_SendEndOfTransmission();

    gFlagEndTransmission = true;

    if (gMonitor) {
        //turn the monitor back on
        gFlagReconfigureVfos = true;
    }
}


void APP_HandleEndTransmission(void)
{
    if (gFlagEndTransmission) {
        FUNCTION_Select(FUNCTION_FOREGROUND);
    } else {
        APP_EndTransmission();

        if (gEeprom.REPEATER_TAIL_TONE_ELIMINATION == 0)
            FUNCTION_Select(FUNCTION_FOREGROUND);
        else
            gRTTECountdown_10ms = gEeprom.REPEATER_TAIL_TONE_ELIMINATION * 10;
    }

    gFlagEndTransmission = false;
}

#ifdef ENABLE_VOX
static void HandleVox(void)
{
    if (gVoxResumeCountdown == 0) {
        if (gVoxPauseCountdown)
            return;
    } else {
        g_VOX_Lost = false;
        gVoxPauseCountdown = 0;
    }

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        return;
#endif

    if (gCurrentFunction == FUNCTION_RECEIVE || gCurrentFunction == FUNCTION_MONITOR)
        return;

    if (APP_GetScanningVfo() != UINT8_MAX)
        return;

    if (gVOX_NoiseDetected) {
        if (g_VOX_Lost)
            gVoxStopCountdown_10ms = vox_stop_count_down_10ms;
        else if (gVoxStopCountdown_10ms == 0)
            gVOX_NoiseDetected = false;

        if (gCurrentFunction == FUNCTION_TRANSMIT && !gPttIsPressed && !gVOX_NoiseDetected) {
            APP_HandleEndTransmission();

            gUpdateStatus = true;
            gUpdateDisplay = true;
        }
        return;
    }

    if (g_VOX_Lost) {
        gVOX_NoiseDetected = true;

        if (gCurrentFunction == FUNCTION_POWER_SAVE)
            FUNCTION_Select(FUNCTION_FOREGROUND);

        if (gCurrentFunction != FUNCTION_TRANSMIT && !SerialConfigInProgress()) {
            RADIO_PrepareTX();
            gUpdateDisplay = true;
        }
    }
}
#endif

void APP_SetAutoRoger(uint8_t selection)
{
    static const uint16_t seconds[7] = {0, 10, 30, 120, 300, 600, 1800};
    if (selection >= ARRAY_SIZE(seconds))
        selection = 0;
    const uint32_t interruptMask = __get_PRIMASK();
    __disable_irq();
    gEeprom.AUTO_ROGER = selection;
    gAutoPtt_Time = seconds[selection];
    gAutoPttCountdown = (uint32_t)gAutoPtt_Time * 100u;
    gNextTimeslice_AutoPtt = false;
    __set_PRIMASK(interruptMask);
}

// Use the same selected VFO, validity checks, modulation, codes and power as PTT.
bool APP_AutoRoger(void)
{
    if (gPttIsPressed || GPIO_IsPttPressed() || gFlagPrepareTX || gCurrentFunction == FUNCTION_TRANSMIT ||
        gReducedService || gMonitor || gEeprom.ROGER == ROGER_MODE_OFF || SerialConfigInProgress())
        return false;
    RADIO_PrepareTX();
    if (gCurrentFunction != FUNCTION_TRANSMIT)
        return false;
    BK4819_EnterTxMute();
    // Let the receiving radio wake and acquire squelch/CSS before the tone.
    // This muted carrier is independent of either radio's backlight state.
    SYSTEM_DelayMs(600);
    APP_EndTransmission();
    FUNCTION_Select(FUNCTION_FOREGROUND);
    gFlagEndTransmission = false;
    gUpdateDisplay = true;
    gUpdateStatus = true;
    return true;
}

void APP_Update(void)
{
    if (gCurrentFunction != FUNCTION_TRANSMIT && !SerialConfigInProgress() && PY25Q16_TakeError()) {
        UI_DisplayPopup("FLASH ERROR");
        ST7565_BlitFullScreen();
    }

#ifdef ENABLE_USB
    if (UART_IsCommandAvailable(UART_PORT_VCP)) {
        // SCHEDULER_Disable();
        UART_HandleCommand(UART_PORT_VCP);
        // SCHEDULER_Enable();
    }
#endif

#ifdef ENABLE_FEAT_F4HWN
    if (gCurrentFunction == FUNCTION_TRANSMIT && (gTxTimeoutReachedAlert || SerialConfigInProgress())) {
        if (gSetting_set_tot >= 2) {
            if (gEeprom.BACKLIGHT_TIME == 0) {
                if (gBlinkCounter == 0 || gBlinkCounter == 250) {
                    if (!APP_FalconMode())
                        GPIO_TogglePin(GPIO_PIN_FLASHLIGHT);
                }
            } else {
                if (gBlinkCounter == 0) {
                    //BACKLIGHT_TurnOn();
                    BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MAX);
                } else if (gBlinkCounter == 15000) {
                    //BACKLIGHT_TurnOff();
                    BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MIN);
                }
            }
        }

        gBlinkCounter++;

        if ((gSetting_set_tot == 3 && gEeprom.BACKLIGHT_TIME != 0 && gBlinkCounter > 74000) ||
            (gSetting_set_tot == 3 && gEeprom.BACKLIGHT_TIME == 0 && gBlinkCounter > 79000) ||
            (gSetting_set_tot != 3 && gBlinkCounter > 76000)) // try to calibrate 10 times
        {
            gBlinkCounter = 0;

            if (gSetting_set_tot == 1 || gSetting_set_tot == 3) {
                BK4819_DisableScramble();
                BK4819_PlaySingleTone(gTxTimeoutToneAlert, 30, 1, true);
                gTxTimeoutToneAlert += 100;
            }
        }
    }
#endif

    if (gCurrentFunction == FUNCTION_TRANSMIT &&
        (gTxTimeoutReached || SerialConfigInProgress())) { // transmitter timed out or must de-key
        gTxTimeoutReached = false;

#ifdef ENABLE_FEAT_F4HWN
        if (gBacklightCountdown_500ms > 0 || gEeprom.BACKLIGHT_TIME == 61) {
            //BACKLIGHT_TurnOn();
            BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MAX);
        }

        gTxTimeoutReachedAlert = false;
        gTxTimeoutToneAlert = 800;

        if (gSetting_set_ptt_session) // Improve OnePush if TOT
        {
            if (gPttOnePushCounter == 1) {
                gPttOnePushCounter = 3;
            } else if (gPttOnePushCounter == 2) {
                ProcessKey(KEY_PTT, false, false);
                gPttIsPressed = false;
                gPttOnePushCounter = 0;
                gPttWasReleased = true;
                //if (gKeyReading1 != KEY_INVALID)
                //  gPttWasReleased = true;
            }
#if defined(ENABLE_FEAT_F4HWN_CTR) || defined(ENABLE_FEAT_F4HWN_INV)
            ST7565_ContrastAndInv();
#endif
        }
#endif

        APP_EndTransmission();
        RADIO_SetVfoState(VFO_STATE_TIMEOUT);

        GUI_DisplayScreen();
    }

    if (gReducedService)
        return;

    if (gNextTimeslice_AutoPtt && gScreenToDisplay == DISPLAY_MAIN && !FUNCTION_IsRx() &&
        APP_GetScanningVfo() == UINT8_MAX
#ifdef ENABLE_FMRADIO
        && !gFmRadioMode
#endif
    ) {
        gNextTimeslice_AutoPtt = false;
        APP_AutoRoger();
    }
    if (gCurrentFunction != FUNCTION_TRANSMIT)
        HandleFunction();

#ifdef ENABLE_FMRADIO
    //  if (gFmRadioCountdown_500ms > 0)
    if (gFmRadioMode && gFmRadioCountdown_500ms > 0) // 1of11
        return;
#endif

    APP_FullWatchEarlyAdvance();
    if (gEeprom.RX_MODE != RX_MODE_SIMPLE &&
#ifdef ENABLE_FMRADIO
        !gFmRadioMode &&
#endif
        gDualVfoSchedule && !gCssBackgroundScan && !gPttIsPressed &&
        gCurrentFunction != FUNCTION_POWER_SAVE && !gMonitor && !INPUTBOX_FrequencyIsActive()) {
        DualVfoAlternate();
    }

#ifdef ENABLE_VOX
    if (gEeprom.VOX_SWITCH)
        HandleVox();
#endif


    if (gSchedulePowerSave) {
        if (gPttIsPressed || gKeyBeingHeld || gEeprom.BATTERY_SAVE == 0 || gCssBackgroundScan ||
            !GUI_IsStandbyScreen()
#ifdef ENABLE_FMRADIO
            || gFmRadioMode
#endif
        ) {
            gBatterySaveCountdown_10ms = battery_save_count_10ms;
        } else {
            FUNCTION_Select(FUNCTION_POWER_SAVE);
        }

        gSchedulePowerSave = false;
    }

    // H4WHN power-save sequence: wake/retune, check, retune/check, sleep.
    // Full Watch advances its native sequence over successive wake cycles.
    if (gPowerSaveCountdownExpired && gCurrentFunction == FUNCTION_POWER_SAVE) {
        static bool goToSleep;
        if (gRxIdleMode) {
            BK4819_Conditional_RX_TurnOn_and_GPIO6_Enable();
#ifdef ENABLE_VOX
            if (gEeprom.VOX_SWITCH)
                BK4819_EnableVox(gEeprom.VOX1_THRESHOLD, gEeprom.VOX0_THRESHOLD);
#endif
            if (gEeprom.RX_MODE != RX_MODE_SIMPLE && !gCssBackgroundScan) {
                DualVfoAlternate();
                goToSleep = false;
            }
            FUNCTION_Init();
            gPowerSave_10ms = power_save1_10ms;
            gRxIdleMode = false;
        } else if (gEeprom.RX_MODE == RX_MODE_SIMPLE || gCssBackgroundScan || goToSleep) {
            gPowerSave_10ms = gEeprom.BATTERY_SAVE * 10;
            gRxIdleMode = true;
            goToSleep = false;
#ifdef ENABLE_VOX
            BK4819_DisableVox();
#endif
            BK4819_Sleep();
            BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
        } else {
            DualVfoAlternate();
            gPowerSave_10ms = power_save1_10ms;
            goToSleep = true;
        }
        gPowerSaveCountdownExpired = false;
    }
}

// A side key is a momentary PTT source. The selected VFO index never changes;
// RADIO_PrepareTX routes this source to its opposite original A/B VFO.
static KEY_Code_t ptt2Key = KEY_INVALID;
static bool ptt2Down;
bool APP_Ptt2Active(void)
{
    return ptt2Key != KEY_INVALID;
}

static void Ptt2Restore(void)
{
    if (APP_Ptt2Active() && !ptt2Down && gCurrentFunction != FUNCTION_TRANSMIT && !gFlagPrepareTX) {
        ptt2Key = KEY_INVALID;
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);
        APP_FullWatchReset();
        gRxVfoIsActive = false;
        gRxReceptionMode = RX_MODE_NONE;
        gDualVfoSchedule = false;
        gDualVfoCountdown_10ms = 10;
        gUpdateDisplay = true;
        gUpdateStatus = true;
    }
}
static __attribute__((noinline, noclone)) bool Ptt2ProcessKey(KEY_Code_t *key, bool pressed, bool held)
{
    if (APP_Ptt2Active() && *key == ptt2Key) {
        if (pressed)
            return true; // Suppress long/repeat actions while held.
        ptt2Down = false;
        gPttIsPressed = false;
        *key = KEY_PTT;
        return false;
    }
    if ((*key != KEY_SIDE1 && *key != KEY_SIDE2 && *key != KEY_MENU) || gWasFKeyPressed)
        return false;
    const uint8_t slot = *key == KEY_MENU ? 4 : *key == KEY_SIDE1 ? 0 : 2;
    const bool shortPtt = slot != 4 && *SETTINGS_ShortcutSetting(slot) == ACTION_OPT_PTT2;
    if (!shortPtt && (!held || *SETTINGS_ShortcutSetting(slot == 4 ? 4 : slot + 1) != ACTION_OPT_PTT2))
        return false;
    if (!pressed)
        return true;
    if (APP_Ptt2Active() || gEeprom.RX_MODE == RX_MODE_SIMPLE || gScreenToDisplay != DISPLAY_MAIN ||
        gPttIsPressed || GPIO_IsPttPressed() || gCurrentFunction == FUNCTION_TRANSMIT || gFlagPrepareTX ||
        gReducedService || SerialConfigInProgress()
#ifdef ENABLE_FMRADIO
        || gFmRadioMode
#endif
    )
        return true;
    ptt2Key = *key;
    ptt2Down = true;
    gPttIsPressed = true;
    gPttWasPressed = gPttWasReleased = false;
    APP_FullWatchReset();
    *key = KEY_PTT;
    return false;
}

void StopTransmitting(void)
{
    ProcessKey(KEY_PTT, false, false);
    gPttIsPressed = false;
    if (gKeyReading1 != KEY_INVALID)
        gPttWasReleased = true;

#ifdef ENABLE_FEAT_F4HWN
#if defined(ENABLE_FEAT_F4HWN_CTR) || defined(ENABLE_FEAT_F4HWN_INV)
    ST7565_ContrastAndInv();
#endif
#endif
}

// 0: ordinary key, 1: native shortcut, 2: F + parameter shortcut,
// 3: editor letter, 4: scanlist shortcut (slightly faster than parameters).
// Capture before the first action clears F, so a maintained chord keeps its
// original meaning and cannot start tuning the frequency or invoke SK1/SK2.
static uint8_t ParameterRepeatKind(KEY_Code_t key)
{
    if (gScreenToDisplay == DISPLAY_CHANNEL && CHANNEL_CanRepeat(key))
        return 3;
    if (gScreenToDisplay != DISPLAY_MAIN || gCurrentFunction == FUNCTION_TRANSMIT ||
        INPUTBOX_FrequencyIsActive() || gInputBoxIndex)
        return 0;
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        return 0;
#endif
    if (gWasFKeyPressed || (gScanSqlPreview && (key == KEY_UP || key == KEY_DOWN)))
        return key == KEY_SIDE1 || key == KEY_SIDE2 || key == KEY_UP || key == KEY_DOWN ? 2 : 0;
    if (key == KEY_0 || key == KEY_4 || key == KEY_5 || key == KEY_6 || key == KEY_8)
        return 1;
    if (key == KEY_SIDE1 || key == KEY_SIDE2) {
        const uint8_t action =
            key == KEY_SIDE1 ? gEeprom.KEY_1_LONG_PRESS_ACTION : gEeprom.KEY_2_LONG_PRESS_ACTION;
        if (action == ACTION_OPT_SCANLIST_UP || action == ACTION_OPT_SCANLIST_DOWN)
            return 4;
        return action == ACTION_OPT_POWER || action == ACTION_OPT_SWITCH_DEMODUL || action == ACTION_OPT_WN
                   ? 1
                   : 0;
    }
    return 0;
}

void CheckKeys(void);
static uint8_t parameterRepeat; // current debounced key, including modal handoff

// The scanner has already sampled this key on two consecutive 10 ms ticks.
// Preserve its press even if released while restoring the foreground radio.
void APP_ResumeScanKey(KEY_Code_t key)
{
    if (key == KEY_PTT) {
        gPttDebounceCounter = 2;
        CheckKeys();
    } else if (key != KEY_INVALID) {
        gKeyReading0 = gKeyReading1 = key;
        gDebounceCounter = key_debounce_10ms;
        gKeyBeingHeld = false;
        parameterRepeat = ParameterRepeatKind(key);
        ProcessKey(key, true, false);
    }
}

// called every 10ms
void CheckKeys(void)
{
    // -------------------- PTT ------------------------
    Ptt2Restore();
    const bool serialConfigInProgress = SerialConfigInProgress();

#ifdef ENABLE_FEAT_F4HWN
    const bool isPressed = (APP_Ptt2Active() ? ptt2Down : GPIO_IsPttPressed()) && !serialConfigInProgress;
#else
    const bool isPressed = !GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT) && !serialConfigInProgress;
#endif

#ifdef ENABLE_FEAT_F4HWN
    if (gSetting_set_ptt_session && !APP_Ptt2Active()) {
        if ((isPressed && (gPttOnePushCounter == 0 || gPttOnePushCounter == 2)) ||
            (!isPressed && (gPttOnePushCounter == 1 || gPttOnePushCounter == 3)) || serialConfigInProgress) {
            if (++gPttDebounceCounter >= 3 || (serialConfigInProgress && gPttOnePushCounter > 0)) {
                gPttDebounceCounter = 0;

                if (gPttOnePushCounter == 0) { // start transmitting
                    boot_counter_10ms = 0;
                    gPttIsPressed = true;
                    gPttOnePushCounter = 1;
                    ProcessKey(KEY_PTT, true, false);
                } else if (gPttOnePushCounter == 3 || serialConfigInProgress) { // stop transmitting
                    StopTransmitting();
                    gPttOnePushCounter = 0;
                } else
                    gPttOnePushCounter++;
            }
        } else {
            gPttDebounceCounter = 0;
        }
    } else
#endif
    {
        if (gPttIsPressed) {
            if (!isPressed) { // PTT released or serial comms config in progress
                if (++gPttDebounceCounter >= 3 || serialConfigInProgress) // 30ms
                {                                                         // stop transmitting
                    gPttDebounceCounter = 0;
                    StopTransmitting();
                }
            } else {
                gPttDebounceCounter = 0;
            }
        } else if (isPressed) {             // PTT pressed
            if (++gPttDebounceCounter >= 3) // 30ms
            {                               // start transmitting
                boot_counter_10ms = 0;
                gPttDebounceCounter = 0;
                gPttIsPressed = true;
                ProcessKey(KEY_PTT, true, false);
            }
        } else {
            gPttDebounceCounter = 0;
        }
    }

    // --------------------- OTHER KEYS ----------------------------

    // scan the hardware keys
    KEY_Code_t Key = KEYBOARD_Poll();

    if (Key != KEY_INVALID)    // any key pressed
        boot_counter_10ms = 0; // cancel boot screen/beeps if any key pressed

    if (gKeyReading0 != Key) // new key pressed
    {

        if (gKeyReading0 != KEY_INVALID && Key != KEY_INVALID)
            ProcessKey(gKeyReading1, false, gKeyBeingHeld); // key pressed without releasing previous key

        parameterRepeat = 0;
        gKeyReading0 = Key;
        gDebounceCounter = 0;
        return;
    }

    gDebounceCounter++;

    if (gDebounceCounter == key_debounce_10ms) // debounced new key pressed
    {
        if (Key == KEY_INVALID) //all non PTT keys released
        {
            if (gKeyReading1 != KEY_INVALID) // some button was pressed before
            {
                ProcessKey(gKeyReading1, false, gKeyBeingHeld); // process last button released event
                gKeyReading1 = KEY_INVALID;
            }
        } else // process new key pressed
        {
            gKeyReading1 = Key;
            parameterRepeat = ParameterRepeatKind(Key);
            ProcessKey(Key, true, false);
        }

        gKeyBeingHeld = false;
        return;
    }

    if (gDebounceCounter < key_repeat_delay_10ms ||
        Key == KEY_INVALID) // the button is not held long enough for repeat yet, or not really pressed
        return;

    if (gDebounceCounter == key_repeat_delay_10ms) //initial key repeat with longer delay
    {
        if (Key != KEY_PTT) {
            gKeyBeingHeld = true;
            if (parameterRepeat == 2 && gScreenToDisplay == DISPLAY_MAIN &&
                gCurrentFunction != FUNCTION_TRANSMIT && !INPUTBOX_FrequencyIsActive() && !gInputBoxIndex)
                gWasFKeyPressed = true;
            ProcessKey(Key, true, true); // key held event
        }
    } else //subsequent fast key repeats
    {
        if (parameterRepeat == 3 && gScreenToDisplay == DISPLAY_CHANNEL && CHANNEL_CanRepeat(Key)) {
            if ((gDebounceCounter - key_repeat_delay_10ms) % 15 == 0)
                ProcessKey(Key, true, true);
        } else if (parameterRepeat && gScreenToDisplay == DISPLAY_MAIN &&
                   gCurrentFunction != FUNCTION_TRANSMIT && !INPUTBOX_FrequencyIsActive() &&
                   !gInputBoxIndex) {
            if ((gDebounceCounter - key_repeat_delay_10ms) % 40 == 0) {
                if (parameterRepeat == 2)
                    gWasFKeyPressed = true;
                ProcessKey(Key, true, true); // parameters and scanlists: 400 ms
            }
        } else if (!parameterRepeat && (Key == KEY_UP || Key == KEY_DOWN)) // original navigation cadence
        {
            gKeyBeingHeld = true;
            if ((gDebounceCounter % key_repeat_10ms) == 0)
                ProcessKey(Key, true, true); // key held event
        }

        if (gDebounceCounter < 0xFFFF)
            return;

        gDebounceCounter = key_repeat_delay_10ms + 1;
    }
}

typedef enum {
    SCAN_CSS_STATE_OFF,
    SCAN_CSS_STATE_SCANNING,
    SCAN_CSS_STATE_FOUND,
    SCAN_CSS_STATE_FAILED
} SCAN_CssState_t;

uint8_t gScanCssResultCode;
DCS_CodeType_t gScanCssResultType;
SCAN_CssState_t gScanCssState;
uint8_t scanHitCount;

void Tone_TimeSlice10ms(void)
{
    uint32_t cdcssFreq;
    uint16_t ctcssFreq;
    BK4819_CssScanResult_t scanResult = BK4819_GetCxCSSScanResult(&cdcssFreq, &ctcssFreq);
    if (scanResult == BK4819_CSS_RESULT_NOT_FOUND)
        return;

    if (scanResult == BK4819_CSS_RESULT_CDCSS) {
        const uint8_t Code = DCS_GetCdcssCode(cdcssFreq);
        if (Code != 0xFF) {
            gScanCssResultCode = Code;
            gScanCssResultType = CODE_TYPE_DIGITAL;
            gScanCssState = SCAN_CSS_STATE_FOUND;
            gUpdateStatus = true;
        }
    } else if (scanResult == BK4819_CSS_RESULT_CTCSS) {
        const uint8_t Code = DCS_GetCtcssCode(ctcssFreq);
        if (Code != 0xFF) {
            if (Code == gScanCssResultCode && gScanCssResultType == CODE_TYPE_CONTINUOUS_TONE) {
                if (++scanHitCount >= 2) {
                    gScanCssState = SCAN_CSS_STATE_FOUND;
                    gUpdateStatus = true;
                }
            } else
                scanHitCount = 0;

            gScanCssResultType = CODE_TYPE_CONTINUOUS_TONE;
            gScanCssResultCode = Code;
        }
    }
}

bool APP_IsScreenSaverDisplayed(void)
{
#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
    return gScreenSaverDisplayed;
#else
    return false;
#endif
}

/* Modal foreground loops (resident tools and overlay apps) bypass APP_Update()
 * and therefore also bypass the normal 10 ms fade and 500 ms BLTime service.
 * Keep that service resident so every overlay app gets identical timing without
 * extending the app ABI.  Only selected modal screens opt into the saver; the
 * others still fade from BLMax to BLMin when BLTime expires. */
void APP_ModalBacklightTick(bool allowScreenSaver)
{
    if (gNextTimeslice) {
        gNextTimeslice = false;
        BACKLIGHT_Update();

#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
        if (ScreenSaverAnimate())
            ScreenSaverUpdateViewer();
#endif
    }

    if (!gNextTimeslice_500ms)
        return;
    gNextTimeslice_500ms = false;

    if (gBacklightCountdown_500ms > 0 && gEeprom.BACKLIGHT_TIME < 61 && --gBacklightCountdown_500ms == 0)
        BACKLIGHT_TurnOff();

#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
    if (allowScreenSaver && gBacklightCountdown_500ms == 0)
        ScreenSaverTryDisplay(true);
#else
    (void)allowScreenSaver;
#endif
}

void APP_ModalScreenSaverExit(void)
{
#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
    ScreenSaverExit();
#endif
}

void APP_TimeSlice10ms(void)
{
    gNextTimeslice = false;

    SETTINGS_SaveVfoIndicesFlush();

    BACKLIGHT_Update();

    gFlashLightBlinkCounter++;

#ifdef ENABLE_UART
    if (UART_IsCommandAvailable(UART_PORT_UART)) {
        // SCHEDULER_Disable();
        UART_HandleCommand(UART_PORT_UART);
        // SCHEDULER_Enable();
    }
#endif

    if (gReducedService)
        return;

    if (gCurrentFunction != FUNCTION_POWER_SAVE || !gRxIdleMode)
        CheckRadioInterrupts();

    // Process a newly sampled VFO signal in this timeslice, using the existing
    // validation functions. Watch cadence and hardware squelch stay untouched.
    if (gScreenToDisplay == DISPLAY_MAIN &&
#ifdef ENABLE_FMRADIO
        !gFmRadioMode &&
#endif
        gCurrentFunction != FUNCTION_POWER_SAVE && APP_GetScanningVfo() == UINT8_MAX && g_SquelchLost) {
        if (gCurrentFunction == FUNCTION_FOREGROUND)
            CheckForIncoming();
        if (gCurrentFunction == FUNCTION_INCOMING)
            HandleIncoming();
    }
    FalconTick();

    if (gCurrentFunction == FUNCTION_TRANSMIT) { // transmitting
#if defined(ENABLE_AUDIO_BAR) && !defined(ENABLE_FEAT_F4HWN_AUDIO_SCOPE)
        if (gSetting_mic_bar && (gFlashLightBlinkCounter % (150 / 10)) == 0) // once every 150ms
            UI_DisplayAudioBar();
#endif
    }

#ifdef ENABLE_FEAT_F4HWN_AUDIO_SCOPE
    if (gSetting_mic_bar && (gFlashLightBlinkCounter % (150 / 10)) == 0)
        // Sample audio amplitude and refresh display during TX only (FM RX has no usable audio register)
        UI_DisplayAudioScope();
#endif

    bool gUpdateDisplayCurrent = gUpdateDisplay;
    bool gUpdateStatusCurrent = gUpdateStatus;

    if (gUpdateDisplayCurrent) {
        gUpdateDisplay = false;
        GUI_DisplayScreen();
    }

    if (gUpdateStatusCurrent) {
        UI_DisplayStatus();
    }

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && gFmRadioCountdown_500ms > 0) // 1of11
        return;
#endif

#ifdef ENABLE_FLASHLIGHT
    FlashlightTimeSlice();
#endif

#ifdef ENABLE_VOX
    if (gVoxResumeCountdown > 0)
        gVoxResumeCountdown--;

    if (gVoxPauseCountdown > 0)
        gVoxPauseCountdown--;
#endif

    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        // repeater tail tone elimination
        if (gRTTECountdown_10ms > 0) {
            if (--gRTTECountdown_10ms == 0) {
                //if (gCurrentFunction != FUNCTION_FOREGROUND)
                FUNCTION_Select(FUNCTION_FOREGROUND);

                gUpdateStatus = true;
                gUpdateDisplay = true;
            }
        }
    }

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && gFM_RestoreCountdown_10ms > 0) {
        if (--gFM_RestoreCountdown_10ms == 0) {
            FM_Start(); // switch back to FM radio mode
            GUI_SelectNextDisplay(DISPLAY_FM);
        }
    }
#endif
    Tone_TimeSlice10ms();
    CheckKeys();
#ifdef ENABLE_FMRADIO
    FM_TimeSlice10ms();
#endif
    if (gEeprom.RX_MODE == RX_MODE_ML && gScreenToDisplay == DISPLAY_MAIN &&
        gCurrentFunction == FUNCTION_FOREGROUND && !gPttIsPressed && !GPIO_IsPttPressed() &&
        !gFlagPrepareTX && !gFlagReconfigureVfos && !gRequestSaveSettings && !gRequestSaveVFO &&
        gKeyReading0 == KEY_INVALID && gKeyReading1 == KEY_INVALID && !gWasFKeyPressed && !gInputBoxIndex &&
        !INPUTBOX_FrequencyIsActive() && !SerialConfigInProgress()
#ifdef ENABLE_FMRADIO
        && !gFmRadioMode
#endif
    )
        APP_RunMLWatch();
}

void cancelUserInputModes(void)
{
    if (gWasFKeyPressed || gKeyInputCountdown > 0 || gInputBoxIndex > 0 || INPUTBOX_FrequencyIsActive()) {
        HideFKeyIcon();

        gInputBoxIndex = 0;
        gKeyInputCountdown = 0;
        INPUTBOX_FrequencyBegin();
        gUpdateDisplay = true;
    }
}

// this is called once every 500ms
void APP_TimeSlice500ms(void)
{
    if (gScanSqlPreview) {
        --gScanSqlPreview;
        gUpdateDisplay = true;
    }
    gNextTimeslice_500ms = false;


    // H4WHN: drawing never schedules itself; request the timer once a second.
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    if (gSetting_set_tmr && (gCurrentFunction == FUNCTION_TRANSMIT || FUNCTION_IsRx())) {
        const uint16_t timerCountdown =
            (gCurrentFunction == FUNCTION_TRANSMIT) ? gTxTimerCountdown_500ms : gRxTimerCountdown_500ms;
        if ((timerCountdown & 1u) != 0u)
            gUpdateStatus = true;
    }
#endif

    if (gKeypadLocked > 0)
        if (--gKeypadLocked == 0)
            gUpdateDisplay = true;

    if (gKeyInputCountdown > 0) {
        if (--gKeyInputCountdown == 0) {

            if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE) && (gInputBoxIndex > 0 && gInputBoxIndex < 4) &&
                (!gFmRadioMode)) {
                channelMoveSwitch();
            }

            cancelUserInputModes();
            gHasVfoBackup = false;
        }
    }

    // Menu stays open until an explicit key action; lighting has its own timer.
    if (gMenuCountdown > 0)
        --gMenuCountdown;

        // Skipped authentic device check

#ifdef ENABLE_FMRADIO
    if (gFmRadioCountdown_500ms > 0) {
        gFmRadioCountdown_500ms--;
        if (gFmRadioMode) // 1of11
            return;
    }
#endif

    const int m = UI_MENU_GetCurrentMenuId();

    if (gBacklightCountdown_500ms > 0 && !gAskToSave &&
        !gCssBackgroundScan
        // don't turn off backlight if user is in backlight menu option
        && !(gScreenToDisplay == DISPLAY_MENU && (m == MENU_ABR || m == MENU_ABR_MAX || m == MENU_ABR_MIN)) &&
        --gBacklightCountdown_500ms == 0 && gEeprom.BACKLIGHT_TIME < 61) {
        BACKLIGHT_TurnOff();
    }

    if (gReducedService) {
        BOARD_ADC_GetBatteryInfo(&gBatteryCurrentVoltage, &gBatteryCurrent);

        if (gBatteryCurrent > 500 || gBatteryCalibration[3] < gBatteryCurrentVoltage) {
#ifdef ENABLE_OVERLAY
            overlay_FLASH_RebootToBootloader();
#else
            NVIC_SystemReset();
#endif
        }

        return;
    }

    gBatteryCheckCounter++;

    // Skipped authentic device check

    if (gCurrentFunction != FUNCTION_TRANSMIT) {

        if ((gBatteryCheckCounter & 1) == 0) {
            BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryVoltageIndex++], &gBatteryCurrent);
            if (gBatteryVoltageIndex > 3)
                gBatteryVoltageIndex = 0;
            BATTERY_GetReadings(true);
        }
    }

    // regular display updates (once every 2 sec) - if need be
    if ((gBatteryCheckCounter & 3) == 0) {
        if (gChargingWithTypeC || gSetting_battery_text > 0)
            gUpdateStatus = true;
    }

    if (!gCssBackgroundScan

    ) {
        if (gEeprom.AUTO_KEYPAD_LOCK && gKeyLockCountdown > 0 && gScreenToDisplay != DISPLAY_MENU &&
            gScreenToDisplay != DISPLAY_CHANNEL && --gKeyLockCountdown == 0) {
            gEeprom.KEY_LOCK = true; // lock the keyboard
            gUpdateStatus = true;    // lock symbol needs showing
        }
    }

    if (!gPttIsPressed && gVFOStateResumeCountdown_500ms > 0 && --gVFOStateResumeCountdown_500ms == 0) {
        RADIO_SetVfoState(VFO_STATE_NORMAL);
#ifdef ENABLE_FMRADIO
        // Only if RestoreCountdown is no longer waiting; otherwise a second BK1080_Init hangs I2C
        if (gFmRadioMode && !FUNCTION_IsRx() && gFM_RestoreCountdown_10ms == 0) {
            FM_Start();
            GUI_SelectNextDisplay(DISPLAY_FM);
        }
#endif
    }

#ifdef ENABLE_FMRADIO
    FM_TimeSlice500ms();
#endif
    BATTERY_TimeSlice500ms();
    UI_MAIN_TimeSlice500ms();
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    RXTX_LOG_Tick500ms();
#endif
}

static KEY_Code_t falconConsumed = KEY_INVALID;

// A key still held when an app hands over to the main screen: its press,
// hold and release are ignored, whether it is released before or after.
void APP_ConsumeHeldKey(KEY_Code_t key)
{
    falconConsumed = gKeyReading0 = gKeyReading1 = key;
    gDebounceCounter = key_debounce_10ms + 1;
}

// Saves postponed while a key was held, done when it is released.
static void SavePostponed(void)
{
    if (flagSaveVfo) {
        SETTINGS_SaveVfoIndices();
        flagSaveVfo = false;
    }

    if (flagSaveSettings) {
        SETTINGS_SaveSettings();
        flagSaveSettings = false;
    }

#ifdef ENABLE_FMRADIO
    if (gFlagSaveFM) {
        SETTINGS_SaveFM();
        gFlagSaveFM = false;
    }
#endif

    if (flagSaveChannel) {
        SETTINGS_SaveChannel(gTxVfo->CHANNEL_SAVE, gEeprom.TX_VFO, gTxVfo, flagSaveChannel);
        flagSaveChannel = false;

        if (gVfoConfigureMode == VFO_CONFIGURE_NONE)
            // gVfoConfigureMode is so as we don't wipe out previously setting this variable elsewhere
            gVfoConfigureMode = VFO_CONFIGURE;
    }
}

static void ProcessKey(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Ptt2ProcessKey(&Key, bKeyPressed, bKeyHeld))
        return;
    if (Key == falconConsumed) {
        if (!bKeyPressed) {
            falconConsumed = KEY_INVALID;
            SavePostponed();
        }
        return;
    }
    if (Key == KEY_EXIT && bKeyPressed && bKeyHeld && gScreenToDisplay == DISPLAY_CHANNEL)
        falconConsumed = Key;
    if (APP_FalconMode()) {
        if (Key == KEY_EXIT) {
            if (!bKeyPressed && !bKeyHeld)
                APP_FalconDisable();
            return;
        }
        if (Key == KEY_7) {
            if (bKeyPressed && bKeyHeld) {
                APP_FalconDisable();
                falconConsumed = Key;
            }
            return;
        }
        if (Key != KEY_PTT && !(Key == KEY_2 && gEeprom.RX_MODE == RX_MODE_DUAL && bKeyHeld))
            return;
    }
    if (Key == KEY_PTT && bKeyPressed)
        APP_FalconTxEnd();
    if (Key == KEY_EXIT && !APP_FalconMode() && !BACKLIGHT_IsOn() &&
        gEeprom.BACKLIGHT_TIME > 0) { // just turn the light on for now so the user can see what's what
        BACKLIGHT_TurnOn();
        return;
    }

    if (gCurrentFunction == FUNCTION_POWER_SAVE)
        FUNCTION_Select(FUNCTION_FOREGROUND);

    gBatterySaveCountdown_10ms = battery_save_count_10ms;

    if (gEeprom.AUTO_KEYPAD_LOCK)
        gKeyLockCountdown = gEeprom.AUTO_KEYPAD_LOCK * 30; // 15 seconds step

    if (!bKeyPressed) { // key released
        SavePostponed();
    } else { // key pressed or held
        const int m = UI_MENU_GetCurrentMenuId();
        if ( //not when PTT and the backlight shouldn't turn on on TX
            !(Key == KEY_PTT && !(gSetting_backlight_on_tx_rx & BACKLIGHT_ON_TR_TX))
            // not in the backlight menu
            && !(gScreenToDisplay == DISPLAY_MENU &&
                 (m == MENU_ABR || m == MENU_ABR_MAX || m == MENU_ABR_MIN))) {
            BACKLIGHT_TurnOn();
        }

        if (Key == KEY_EXIT && bKeyHeld) { // exit key held pressed

            // cancel user input
            cancelUserInputModes();

            if (gMonitor)
                ACTION_Monitor(); //turn off the monitor
        }

        if (gScreenToDisplay == DISPLAY_MENU) // 1of11
            gMenuCountdown = menu_timeout_500ms;
    }

    bool lowBatPopup = gLowBattery && !gLowBatteryConfirmed && gScreenToDisplay == DISPLAY_MAIN;

#ifdef ENABLE_FEAT_F4HWN // Disable PTT if KEY_LOCK
    bool lck_condition = false;

    if (gSetting_set_lck)
        lck_condition = (gEeprom.KEY_LOCK || lowBatPopup) && gCurrentFunction != FUNCTION_TRANSMIT;
    else
        lck_condition =
            (gEeprom.KEY_LOCK || lowBatPopup) && gCurrentFunction != FUNCTION_TRANSMIT && Key != KEY_PTT;

    if (lck_condition)
#else
    if ((gEeprom.KEY_LOCK || lowBatPopup) && gCurrentFunction != FUNCTION_TRANSMIT && Key != KEY_PTT)
#endif
    { // keyboard is locked or low battery popup

        // close low battery popup
        if (Key == KEY_EXIT && bKeyPressed && lowBatPopup) {
            gLowBatteryConfirmed = true;
            gUpdateDisplay = true;
            return;
        }

        if (Key == KEY_F) { // function/key-lock key
            if (!bKeyPressed)
                return;

            if (!bKeyHeld) {       // keypad is locked, tell the user
                gKeypadLocked = 4; // 2 seconds
                gUpdateDisplay = true;
                return;
            }
        }
        // KEY_MENU has a special treatment here, because we want to pass hold event to ACTION_Handle
        // but we don't want it to complain when initial press happens
        // we want to react on realese instead
        else if (Key != KEY_SIDE1 && Key != KEY_SIDE2 && // pass side buttons
                 !(Key == KEY_MENU && bKeyHeld))         // pass KEY_MENU held
        {
            if ((!bKeyPressed || bKeyHeld ||
                 (Key == KEY_MENU && bKeyPressed)) && // prevent released or held, prevent KEY_MENU pressed
                !(Key == KEY_MENU && !bKeyPressed))   // pass KEY_MENU released
                return;

            // keypad is locked, tell the user
            gKeypadLocked = 4; // 2 seconds
            gUpdateDisplay = true;
            return;
        }
    }

    bool bFlag = false;
    if (Key == KEY_PTT) {
        if (gPttWasPressed) {
            bFlag = bKeyHeld;
            if (!bKeyPressed) {
                bFlag = true;
                gPttWasPressed = false;
            }
        }
    } else if (gPttWasReleased) {
        if (bKeyHeld)
            bFlag = true;
        if (!bKeyPressed) {
            bFlag = true;
            gPttWasReleased = false;
        }
    }

#ifdef ENABLE_FEAT_F4HWN // For F + SIDE1 or F + SIDE2
    if (gWasFKeyPressed && Key == KEY_EXIT && gScreenToDisplay == DISPLAY_MAIN && !gInputBoxIndex &&
        !INPUTBOX_FrequencyIsActive()) {
        if (bKeyPressed && !bKeyHeld) {
            HideFKeyIcon();
            MONITOR_ToggleCurrent();
        }
        goto Skip;
    }
    if (gWasFKeyPressed && (Key == KEY_PTT || Key == KEY_EXIT)) {
#else
    if (gWasFKeyPressed && (Key == KEY_PTT || Key == KEY_EXIT || Key == KEY_SIDE1 || Key == KEY_SIDE2)) {
#endif
        // cancel the F-key
        HideFKeyIcon();
    }

    if (bFlag) {
        goto Skip;
    }

    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        {
            // PTT is always handled during TX.
            if (Key == KEY_PTT) {
                GENERIC_Key_PTT(bKeyPressed);
                goto Skip;
            }

            // Side2 is the 1750 Hz tone key while transmitting.
            if (Key == KEY_SIDE2 && gEeprom.TONE_1750) {
                if (bKeyPressed && !bKeyHeld && !gTx1750Active) {
                    // The tone is heard in the speaker too, as in F4HWN.
                    GPIO_EnableAudioPath();
                    gEnableSpeaker = true;
                    BK4819_DisableScramble();
                    BK4819_TransmitTone(true, 1750);
                    gTx1750Active = true;
                    gUpdateDisplay = true;
                } else if (!bKeyPressed && gTx1750Active) {
                    TX1750_Off();

                    if (gEeprom.REPEATER_TAIL_TONE_ELIMINATION == 0)
                        FUNCTION_Select(FUNCTION_FOREGROUND);
                    else
                        gRTTECountdown_10ms = gEeprom.REPEATER_TAIL_TONE_ELIMINATION * 10;

                    gPttWasReleased = true;
                }
                goto Skip;
            }

            if (!bKeyPressed || bKeyHeld) {
                if (!bKeyPressed) {
                    GPIO_DisableAudioPath();
                    gEnableSpeaker = false;
                    if (gEeprom.SCRAMBLING_TYPE == 0)
                        BK4819_DisableScramble();
                    else
                        BK4819_EnableScramble(gEeprom.SCRAMBLING_TYPE - 1);
                }
            } else {
                BK4819_DisableScramble();
            }
        }
    } else if (gScreenToDisplay == DISPLAY_CHANNEL) {
        CHANNEL_ProcessKeys(Key, bKeyPressed, bKeyHeld);
    } else if (gScreenToDisplay != DISPLAY_INVALID &&
               ((Key != KEY_SIDE1 && Key != KEY_SIDE2)
#ifdef ENABLE_FEAT_F4HWN // For F + SIDE1 or F + SIDE2
                || (gWasFKeyPressed && (Key == KEY_SIDE1 || Key == KEY_SIDE2))
#endif
                    )) {
        if (Key == KEY_EXIT && !bKeyHeld && bKeyPressed && gScreenToDisplay != DISPLAY_MENU) {
            gRequestDisplayScreen = DISPLAY_MAIN;
        } else {
            ProcessKeysFunctions[gScreenToDisplay](Key, bKeyPressed, bKeyHeld);
        }
    } else
        ACTION_Handle(Key, bKeyPressed, bKeyHeld);


Skip:
    if (gFlagAcceptSetting) {
        gMenuCountdown = menu_timeout_500ms;

        MENU_AcceptSetting();

        gFlagRefreshSetting = true;
        gFlagAcceptSetting = false;
    }

    if (gRequestSaveSettings) {
        if (!bKeyHeld)
            SETTINGS_SaveSettings();
        else
            flagSaveSettings = 1;
        gRequestSaveSettings = false;
        gUpdateStatus = true;
    }

#ifdef ENABLE_FMRADIO
    if (gRequestSaveFM) {
        gRequestSaveFM = false;
        if (!bKeyHeld)
            SETTINGS_SaveFM();
        else
            gFlagSaveFM = true;
    }
#endif

    if (gRequestSaveVFO) {
        gRequestSaveVFO = false;
        if (!bKeyHeld)
            SETTINGS_SaveVfoIndices();
        else
            flagSaveVfo = true;
    }

    if (gRequestSaveChannel > 0) { // TODO: remove the gRequestSaveChannel, why use global variable for that??
        if ((!bKeyHeld && !bKeyPressed) ||
            (UI_MENU_GetCurrentMenuId() != 0 && gScreenToDisplay == DISPLAY_MENU)) {
            SETTINGS_SaveChannel(gTxVfo->CHANNEL_SAVE, gEeprom.TX_VFO, gTxVfo, gRequestSaveChannel);

            if (gVfoConfigureMode == VFO_CONFIGURE_NONE)
                // gVfoConfigureMode is so as we don't wipe out previously setting this variable elsewhere
                gVfoConfigureMode = VFO_CONFIGURE;
        } else { // this is probably so settings are not saved when up/down button is held and save is postponed to btn release
            flagSaveChannel = gRequestSaveChannel;

            if (gRequestDisplayScreen == DISPLAY_INVALID)
                gRequestDisplayScreen = DISPLAY_MAIN;
        }

        gRequestSaveChannel = 0;
    }

    if (gVfoConfigureMode != VFO_CONFIGURE_NONE) {
        if (gEeprom.RX_MODE != RX_MODE_SIMPLE) {
            RADIO_ConfigureChannel(0, gVfoConfigureMode);
            RADIO_ConfigureChannel(1, gVfoConfigureMode);
        } else {
            RADIO_ConfigureChannel(gEeprom.TX_VFO, gVfoConfigureMode);
        }

        if (gRequestDisplayScreen == DISPLAY_INVALID)
            gRequestDisplayScreen = DISPLAY_MAIN;

        gFlagReconfigureVfos = true;
        gVfoConfigureMode = VFO_CONFIGURE_NONE;
        gFlagResetVfos = false;
    }

    if (gFlagReconfigureVfos) {
        RADIO_SelectVfos();

        RADIO_SetupRegisters(true);

        gVFO_RSSI_bar_level[0] = 0;
        gVFO_RSSI_bar_level[1] = 0;

        gFlagReconfigureVfos = false;

        if (gMonitor)
            ACTION_Monitor(); // 1of11
    }

    if (gFlagRefreshSetting) {
        gFlagRefreshSetting = false;
        gMenuCountdown = menu_timeout_500ms;

        MENU_ShowCurrentSetting();
    }

    if (gFlagPrepareTX) {
        RADIO_PrepareTX();
        gFlagPrepareTX = false;
    }

    GUI_SelectNextDisplay(gRequestDisplayScreen);
    gRequestDisplayScreen = DISPLAY_INVALID;

    gUpdateDisplay = true;
}
