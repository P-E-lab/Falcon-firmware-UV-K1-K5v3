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

#include <string.h>
#include "app/app.h"


#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif

#include "dcs.h"
#include "driver/backlight.h"
#if defined(ENABLE_FMRADIO)
#include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "driver/st7565.h"
#include "frequencies.h"
#include "functions.h"
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
#include "app/rxtx_log.h"
#endif
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/status.h"
#include "ui/main.h"
#include "ui/ui.h"

FUNCTION_Type_t gCurrentFunction;

bool FUNCTION_IsRx()
{
    return gCurrentFunction == FUNCTION_MONITOR || gCurrentFunction == FUNCTION_INCOMING ||
           gCurrentFunction == FUNCTION_RECEIVE;
}

void FUNCTION_Init(void)
{
    g_CxCSS_TAIL_Found = false;
    g_CDCSS_Lost = false;
    g_CTCSS_Lost = false;

    g_SquelchLost = false;

    gFlagTailNoteEliminationComplete = false;
    gTailNoteEliminationCountdown_10ms = 0;
    gFoundCTCSS = false;
    gFoundCDCSS = false;
    gFoundCTCSSCountdown_10ms = 0;
    gFoundCDCSSCountdown_10ms = 0;
    gEndOfRxDetectedMaybe = false;

    gCurrentCodeType = (gRxVfo->Modulation != MODULATION_FM) ? CODE_TYPE_OFF : gRxVfo->pRX->CodeType;

#ifdef ENABLE_VOX
    g_VOX_Lost = false;
#endif

    gUpdateStatus = true;
}

void FUNCTION_Foreground(const FUNCTION_Type_t PreviousFunction)
{

    if (PreviousFunction == FUNCTION_TRANSMIT) {
        ST7565_FixInterfGlitch();
        gVFO_RSSI_bar_level[0] = 0;
        gVFO_RSSI_bar_level[1] = 0;
    } else if (PreviousFunction != FUNCTION_RECEIVE && PreviousFunction != FUNCTION_INCOMING) {
        // INCOMING→FOREGROUND (squelch closed before full RX) also needs FM restore
        return;
    }

#if defined(ENABLE_FMRADIO)
    if (gFmRadioMode && gFM_RestoreCountdown_10ms == 0)
        gFM_RestoreCountdown_10ms = fm_restore_countdown_10ms;
#endif

    gUpdateStatus = true;
}

void FUNCTION_PowerSave()
{
    gPowerSave_10ms = gEeprom.BATTERY_SAVE * 10;
    gPowerSaveCountdownExpired = false;
    gRxIdleMode = true;
    gMonitor = false;
#ifdef ENABLE_VOX
    BK4819_DisableVox();
#endif
    BK4819_Sleep();
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
    gUpdateStatus = true;

    if (gScreenToDisplay != DISPLAY_MENU && gScreenToDisplay != DISPLAY_CHANNEL)
        // Standby must retain the open page and any unsaved draft.
        GUI_SelectNextDisplay(DISPLAY_MAIN);
}

void FUNCTION_Transmit()
{
    SETTINGS_SaveVfoNow(); // a VFO change waiting to be saved is kept from here
    // Keep the unused tone decoder disabled during transmit.
    BK4819_WriteRegister(BK4819_REG_24, 0);
#if defined(ENABLE_FMRADIO)
    if (gFmRadioMode)
        BK1080_Init0();
#endif

    gUpdateStatus = true;
    GUI_DisplayScreen();
    RADIO_SetTxParameters();
    APP_FalconTx();
    if (gTx1750Active) {
        BK4819_DisableScramble();
        BK4819_TransmitTone(true, 1750);
        SYSTEM_DelayMs(2);
        GPIO_EnableAudioPath();
        gEnableSpeaker = true;
        gVfoConfigureMode = VFO_CONFIGURE;
        return;
    }
    // turn the RED LED on
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    if (gEeprom.SCRAMBLING_TYPE > 0)
        BK4819_EnableScramble(gEeprom.SCRAMBLING_TYPE - 1);
    else
        BK4819_DisableScramble();
    if (gSetting_backlight_on_tx_rx & BACKLIGHT_ON_TR_TX) {
        BACKLIGHT_TurnOn();
    }
}


void FUNCTION_Select(FUNCTION_Type_t Function)
{
    const FUNCTION_Type_t PreviousFunction = gCurrentFunction;
    const bool bWasPowerSave = PreviousFunction == FUNCTION_POWER_SAVE;

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    const bool previousWasActive = PreviousFunction == FUNCTION_TRANSMIT ||
                                   PreviousFunction == FUNCTION_MONITOR || PreviousFunction == FUNCTION_RECEIVE;
    const bool previousWasTx = PreviousFunction == FUNCTION_TRANSMIT;
    const bool nextIsActive =
        Function == FUNCTION_TRANSMIT || Function == FUNCTION_MONITOR || Function == FUNCTION_RECEIVE;
    const bool nextIsTx = Function == FUNCTION_TRANSMIT;

    if (previousWasActive && (!nextIsActive || (previousWasTx != nextIsTx)))
        RXTX_LOG_EndActive();
#endif

    gCurrentFunction = Function;

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    if (nextIsActive && !nextIsTx && (!previousWasActive || previousWasTx))
        RXTX_LOG_BeginRx(gRxVfo, Function);
#endif
    if (!FUNCTION_IsRx())
        UI_MAIN_ClearReceivedCode();
#if defined(ENABLE_FMRADIO)
    if (gFmRadioMode) {
        if (Function == FUNCTION_FOREGROUND)
            FM_SetVfoAudio(false);
        else if (!gFM_No_Rx && !gFM_Mute &&
                 (Function == FUNCTION_INCOMING || Function == FUNCTION_RECEIVE ||
                  Function == FUNCTION_MONITOR))
            FM_SetVfoAudio(true);
    }
#endif

    if (bWasPowerSave && Function != FUNCTION_POWER_SAVE) {
        BK4819_Conditional_RX_TurnOn_and_GPIO6_Enable();
        gRxIdleMode = false;
        UI_DisplayStatus();
    }

    if (Function == FUNCTION_FOREGROUND) {
        FUNCTION_Foreground(PreviousFunction);
        return;
    }

    if (Function == FUNCTION_POWER_SAVE) {
        FUNCTION_PowerSave();
        return;
    }

    if (Function == FUNCTION_TRANSMIT) {
        FUNCTION_Transmit();
    } else if (Function == FUNCTION_MONITOR) {
        gMonitor = true;
    }

    gBatterySaveCountdown_10ms = battery_save_count_10ms;
    gSchedulePowerSave = false;

#if defined(ENABLE_FMRADIO)
    if (Function != FUNCTION_INCOMING)
        gFM_RestoreCountdown_10ms = 0;
#endif
}
