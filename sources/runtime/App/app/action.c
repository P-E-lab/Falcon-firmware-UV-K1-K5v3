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
#include <string.h>

#include "app/action.h"
#include "app/app.h"
#include "app/common.h"
#include "app/monitor.h"
#include "app/spectrum.h"
#include "apps/app_overlay.h"
#include "driver/system.h"

#ifdef ENABLE_FLASHLIGHT
#include "app/flashlight.h"
#endif
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif

#ifdef ENABLE_FMRADIO
#include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/backlight.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"
static void ACTION_1750(void);
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
static void ACTION_FMessage(void);
#endif
static void ACTION_ScanCurrent(void)
{
    const uint8_t list = gTxVfo->SCANLIST_PARTICIPATION;
    if (IS_FREQ_CHANNEL(gTxVfo->CHANNEL_SAVE))
        APP_RunCurrentScanList(SCAN_VFO_FREQUENCY);
    else if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE) && list >= 1 && list <= MR_CHANNELS_LIST)
        APP_RunCurrentScanList(list);
}

void (*action_opt_table[])(void) = {
    [ACTION_OPT_NONE] = &FUNCTION_NOP,
    [ACTION_OPT_SCAN] = &ACTION_ScanCurrent,
    [ACTION_OPT_SCAN_ALL] = &APP_RunAllMemoryScan,
    [21] = &FUNCTION_NOP,
    [22] = &FUNCTION_NOP,
    [ACTION_OPT_PTT2] = &FUNCTION_NOP, // Momentary key handled before shortcut dispatch.
    [ACTION_OPT_POWER] = &ACTION_Power,
    [ACTION_OPT_MONITOR] = &ACTION_Monitor,
    [ACTION_OPT_KEYLOCK] = &COMMON_KeypadLockToggle,
    [ACTION_OPT_VFO_MR] = &COMMON_SwitchVFOMode,
    [ACTION_OPT_SWITCH_DEMODUL] = &ACTION_SwitchDemodul,

#ifdef ENABLE_FLASHLIGHT
    [ACTION_OPT_FLASHLIGHT] = &ACTION_FlashLight,
#else
    [ACTION_OPT_FLASHLIGHT] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_FMRADIO
    [ACTION_OPT_FM] = &ACTION_FM,
#else
    [ACTION_OPT_FM] = &FUNCTION_NOP,
#endif

    [ACTION_OPT_ALARM] = &FUNCTION_NOP,

    [ACTION_OPT_1750] = &ACTION_1750,

    [ACTION_OPT_BLMIN_TMP_OFF] = &FUNCTION_NOP,

#ifdef ENABLE_FEAT_F4HWN
    [ACTION_OPT_PTT] = &ACTION_Ptt,
    [ACTION_OPT_WN] = &ACTION_Wn,
    [ACTION_OPT_BACKLIGHT] = &ACTION_BackLight,
    [ACTION_OPT_RXA] = &ACTION_RxA,
#endif
    [ACTION_OPT_SCANLIST_UP] = &ACTION_ScanListUp,
    [ACTION_OPT_SCANLIST_DOWN] = &ACTION_ScanListDown,
    [ACTION_OPT_SINGLE_DUO] = &ACTION_SingleDuo,
    [ACTION_OPT_VFO_AB] = &ACTION_VfoAB,
    [ACTION_OPT_HISTORY] = &APP_RunHistory,
#ifdef ENABLE_VOX
    [ACTION_OPT_VOX] = &ACTION_Vox,
#else
    [ACTION_OPT_VOX] = &FUNCTION_NOP,
#endif
    [ACTION_OPT_REVERSE] = &ACTION_Reverse,
#ifdef ENABLE_FALCON_MESSAGES
    [ACTION_OPT_MESSAGES] = &APP_RunMessages,
#else
    [ACTION_OPT_MESSAGES] = &FUNCTION_NOP,
#endif
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    [ACTION_OPT_FMESSAGE] = &ACTION_FMessage,
#else
    [ACTION_OPT_FMESSAGE] = &FUNCTION_NOP,
#endif
};

static_assert(ARRAY_SIZE(action_opt_table) == ACTION_OPT_LEN);

// Use the exact Rxmode menu transition for both entry points.
void ACTION_SetRxMode(uint8_t mode)
{
    // A reception in progress does not survive the change: ML WATCH stops,
    // and the receiver is retuned to the selected VFOs below.
    APP_MLWatchStop();
    if (gCurrentFunction == FUNCTION_RECEIVE || gCurrentFunction == FUNCTION_INCOMING)
        FUNCTION_Select(FUNCTION_FOREGROUND);
    if (mode >= RX_MODE_FULL)
        APP_FalconDisable();
    gEeprom.RX_MODE = mode <= RX_MODE_ML ? mode : RX_MODE_SIMPLE;
    RADIO_SelectVfos();
    gRxVfoIsActive = false;
    gRxReceptionMode = RX_MODE_NONE;
    gDualVfoCountdown_10ms = 10; // 100 ms between VFO toggles
    gDualVfoSchedule = false;
    gFlagReconfigureVfos = true;
    gRequestSaveSettings = true;
    gUpdateStatus = true;
    gUpdateDisplay = true;
}

// Shared by long 2 and the programmable F1/F2 action.
void ACTION_VfoAB(void)
{
    if (gEeprom.RX_MODE == RX_MODE_SIMPLE || gScreenToDisplay != DISPLAY_MAIN ||
        gCurrentFunction == FUNCTION_TRANSMIT)
        return;
    cancelUserInputModes();
    COMMON_SwitchVFOs();
    APP_FalconSwitch();
    gScheduleVfoSave = true;
    SETTINGS_SaveVfoIndices();
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
}

void ACTION_SingleDuo(void)
{
    if (gScreenToDisplay != DISPLAY_MAIN || gCurrentFunction == FUNCTION_TRANSMIT)
        return;
    if (INPUTBOX_FrequencyIsActive() || gInputBoxIndex)
        return;
    uint8_t next = (gEeprom.RX_MODE + 1u) % 4u;
    if (next == RX_MODE_FULL) {
        uint8_t count;
        APP_GetFullWatchBackgroundVfos(&count);
        if (!count)
            next = RX_MODE_ML;
    }
    if (next == RX_MODE_ML && !APP_MLWatchEligible())
        next = RX_MODE_SIMPLE;
    ACTION_SetRxMode(next);
    gRequestDisplayScreen = DISPLAY_MAIN;
}

static void ACTION_ChangeScanList(int8_t direction)
{
    const uint8_t vfo = gEeprom.TX_VFO & 1u;
    const uint16_t currentChannel = gEeprom.ScreenChannel[vfo];
    if (!IS_MR_CHANNEL(currentChannel) || gCurrentFunction == FUNCTION_TRANSMIT)
        return;
    const ChannelAttributes_t *current = MR_GetChannelAttributes(currentChannel);
    const uint8_t currentList =
        (current && current->scanlist >= 1 && current->scanlist <= MR_CHANNELS_LIST) ? current->scanlist : 0;
    const uint8_t origin = currentList ? currentList : (direction > 0 ? MR_CHANNELS_LIST : 1);
    uint8_t best = MR_CHANNELS_LIST + 1;
    uint16_t next = UINT16_MAX;
    for (uint16_t ch = MR_CHANNEL_FIRST; ch <= MR_CHANNEL_LAST; ++ch) {
        if (!RADIO_CheckValidChannel(ch, false, 0))
            continue;
        const ChannelAttributes_t *att = MR_GetChannelAttributes(ch);
        if (!att || att->scanlist < 1 || att->scanlist > MR_CHANNELS_LIST)
            continue;
        int distance = direction > 0 ? att->scanlist - origin : origin - att->scanlist;
        if (distance <= 0)
            distance += MR_CHANNELS_LIST;
        if (distance < best) {
            best = distance;
            next = ch;
        }
    }
    if (next == UINT16_MAX)
        return;
    gEeprom.MrChannel[vfo] = next;
    gEeprom.ScreenChannel[vfo] = next;
    gRequestSaveVFO = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen = DISPLAY_MAIN;
}

void ACTION_ScanListUp(void)
{
    ACTION_ChangeScanList(1);
}
void ACTION_ScanListDown(void)
{
    ACTION_ChangeScanList(-1);
}

void ACTION_Power(void)
{
    gTxVfo->OUTPUT_POWER = POWER_FromIndex((POWER_ToIndex(gTxVfo->OUTPUT_POWER) + 1) % OUTPUT_POWER_LEN);

    // Save immediately to flash
    SETTINGS_SaveChannelParameters(gEeprom.TX_VFO, gTxVfo, CHANNEL_PARAMETER_POWER);

    // Apply new power level to hardware immediately (recalculates TXP_CalculatedSetting)
    RADIO_ConfigureSquelchAndOutputPower(gTxVfo);

    gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Monitor(void)
{
    if (gCurrentFunction != FUNCTION_MONITOR) { // enable the monitor
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);
        APP_StartListening(FUNCTION_MONITOR);
        return;
    }

    gMonitor = false;

    RADIO_SetupRegisters(true);

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode) {
        FM_Start();
        gRequestDisplayScreen = DISPLAY_FM;
    } else
#endif
        gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_SwitchDemodul(void)
{
    gRequestSaveChannel = 1;

    gTxVfo->Modulation++;

    if (gTxVfo->Modulation == MODULATION_UKNOWN)
        gTxVfo->Modulation = MODULATION_FM;

    if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
        SETTINGS_SaveChannelParameters(gEeprom.TX_VFO, gTxVfo, CHANNEL_PARAMETER_MODULATION);
}


void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    HideFKeyIcon();
    enum ACTION_OPT_t func = ACTION_OPT_NONE;
    switch (Key) {
        case KEY_SIDE1:
        case KEY_SIDE2:
            func = *SETTINGS_ShortcutSetting((Key == KEY_SIDE1 ? 0 : 2) + (bKeyHeld ? 1 : 0));
            break;
        case KEY_MENU:
            if (bKeyHeld)
                func = *SETTINGS_ShortcutSetting(4);
            break;
        default:
            break;
    }

    if (!bKeyHeld && bKeyPressed) // button pushed
    {
        return;
    }

    // held or released beyond this point

    if (bKeyHeld && !bKeyPressed) // button released after hold
    {
        return;
    }

    // held or released after short press beyond this point

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode) { // do not run these actions in FM radio mode
        switch (func) {
            case ACTION_OPT_POWER:
            case ACTION_OPT_MONITOR:
            case ACTION_OPT_VFO_MR:
            case ACTION_OPT_SWITCH_DEMODUL:
            case ACTION_OPT_SCANLIST_UP:
            case ACTION_OPT_SCANLIST_DOWN:
            case ACTION_OPT_SINGLE_DUO:
            case ACTION_OPT_SCAN:
            case ACTION_OPT_SCAN_ALL:
#ifdef ENABLE_FEAT_F4HWN
            case ACTION_OPT_WN:
            case ACTION_OPT_RXA:
#endif
                return;

            default:
                break;
        }
    }
#endif

    action_opt_table[func]();

    // A held key has done its action: the rest of that press (repeats and
    // release) is nobody's, whichever loop reads the keys next. Without it,
    // an action that changes the receiver during a reception (RX mode, which
    // ends ML WATCH, a scan, the history...) let the release of MENU open the
    // menu, or reach the screen that the action opened.
    // The 1750 Hz tone, PTT and monitor actions still need their release.
    if (bKeyHeld && func != ACTION_OPT_NONE && func != ACTION_OPT_1750 && func != ACTION_OPT_MONITOR
#ifdef ENABLE_FEAT_F4HWN
        && func != ACTION_OPT_PTT
#endif
    )
        APP_ConsumeHeldKey(Key);
}


#ifdef ENABLE_FMRADIO
void ACTION_FM(void)
{
    if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR) {
        gInputBoxIndex = 0;

        if (gFmRadioMode) {
            FM_TurnOff();
            gFlagReconfigureVfos = true;
            gRequestDisplayScreen = DISPLAY_MAIN;
#ifdef ENABLE_VOX
            gVoxResumeCountdown = 80;
#endif
            return;
        }
        gMonitor = false;
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);
        FM_Start();
        gRequestDisplayScreen = DISPLAY_FM;
    }
}
#endif

#ifdef ENABLE_VOX
void ACTION_Vox(void)
{
    gEeprom.VOX_SWITCH = !gEeprom.VOX_SWITCH;
    gRequestSaveSettings = true;
    gFlagReconfigureVfos = true;
    gUpdateStatus = true;
}
#endif

static void ACTION_1750(void)
{
    if (gEeprom.KEY_LOCK && gSetting_set_lck)
        return;

    gTx1750Active = true;
    gInputBoxIndex = 0;
    gFlagPrepareTX = true;

    if (gScreenToDisplay != DISPLAY_MENU)
        gRequestDisplayScreen = DISPLAY_MAIN;
}

void ACTION_RxA(void)
{
    if (gRxVfo->Modulation == MODULATION_AM)
        gSetting_set_audio_am = (gSetting_set_audio_am + 1) % 3;
    else if (gRxVfo->Modulation == MODULATION_FM)
        gSetting_set_audio_fm = (gSetting_set_audio_fm + 1) % 5;

    RADIO_SetModulation(gRxVfo->Modulation);
}

void ACTION_Ptt(void)
{
    gSetting_set_ptt_session = !gSetting_set_ptt_session;
}

void ACTION_Wn(void)
{
    const bool isRx = FUNCTION_IsRx();
    VFO_Info_t *pVfo = isRx ? gRxVfo : gTxVfo;

    pVfo->CHANNEL_BANDWIDTH = !pVfo->CHANNEL_BANDWIDTH;
    SETTINGS_SaveChannelParameters(pVfo == &gEeprom.VfoInfo[0] ? 0 : 1, pVfo, CHANNEL_PARAMETER_BANDWIDTH);

    if (pVfo->Modulation == MODULATION_AM) {
        BK4819_SetFilterBandwidth(
            pVfo->CHANNEL_BANDWIDTH == BANDWIDTH_WIDE ? BK4819_FILTER_BW_WIDE : BK4819_FILTER_BW_AM, true);
        return;
    }

    uint8_t bw = pVfo->CHANNEL_BANDWIDTH;

#ifdef ENABLE_FEAT_F4HWN_NARROWER
    if (bw == BANDWIDTH_NARROW && gSetting_set_nfm == 1) {
        bw++;
    }
#endif

    BK4819_SetFilterBandwidth(bw, false);
}

// REVERSE FQ: the VFO receives on its TX frequency and transmits on its RX
// frequency. Saved with the channel (memory or VFO), as the channel editor's
// REVERSE: line; the VFO is reconfigured from it.
void ACTION_Reverse(void)
{
    VFO_Info_t *pVfo = FUNCTION_IsRx() ? gRxVfo : gTxVfo;
    pVfo->FrequencyReverse = !pVfo->FrequencyReverse;
    SETTINGS_SaveChannelParameters(pVfo == &gEeprom.VfoInfo[0] ? 0 : 1, pVfo, CHANNEL_PARAMETER_REVERSE);
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen = DISPLAY_MAIN;
}

void ACTION_BackLight(void)
{
    if (APP_FalconMode())
        return;
    if (gBackLight) {
        gEeprom.BACKLIGHT_TIME = gBacklightTimeOriginal;
    }
    gBackLight = false;
    BACKLIGHT_TurnOn();
}

void ACTION_BackLightOnDemand(void)
{
    if (APP_FalconMode())
        return;
    if (gBackLight == false) {
        gBacklightTimeOriginal = gEeprom.BACKLIGHT_TIME;
        gEeprom.BACKLIGHT_TIME = 61;
        gBackLight = true;
    } else {
        if (gBacklightBrightnessOld == gEeprom.BACKLIGHT_MAX) {
            gEeprom.BACKLIGHT_TIME = 0;
        } else {
            gEeprom.BACKLIGHT_TIME = 61;
        }
    }

    BACKLIGHT_TurnOn();
}

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
// FMESSAGE: opens the app holding the FMessage shortcut; a double beep when
// none is installed, as F4HWN does for its app shortcuts.
static void ACTION_FMessage(void)
{
    const uint8_t rc = APP_LaunchOverlayShortcut(APP_SHORTCUT_FMESSAGE);
    if (rc == APP_OK || rc == APP_CHAIN_ABORTED)
        return;
    for (uint8_t i = 0; i < 2; ++i) {
        BK4819_PrepareToPlayTone(true);
        GPIO_EnableAudioPath();
        BK4819_PlayToneRaw(500, 60);
        GPIO_DisableAudioPath();
        SYSTEM_DelayMs(60);
    }
    RADIO_SetupRegisters(true);
}
#endif
