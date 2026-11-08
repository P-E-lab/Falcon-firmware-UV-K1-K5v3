#include "apps/app_overlay.h"
#include "app/monitor.h"
/* Copyright 2023 Dual Tachyon / Robzyl KA52
 * Licensed under the Apache License, Version 2.0
 */

#include <string.h>
#include "nav_invert.h"

#include "app/action.h"
#include "app/app.h"
#include "app/common.h"
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif
#include "app/generic.h"
#include "app/main.h"
#include "app/flashlight.h"

#ifdef ENABLE_SPECTRUM
#include "app/spectrum.h"
#endif


#include "board.h"
#include "driver/bk4819.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/helper.h"


#include "ui/inputbox.h"
#include "ui/ui.h"

/* Consumes the release event after a long press on 2. */
static bool gKey2LongHandled = false;
#include <stdlib.h>


// Direct priority shortcuts share the existing PRICH settings and save path.
static bool ListNamed(uint8_t slot);
static NATIVE_CODE(boot) __attribute__((long_call)) void TogglePriorityNative(uint8_t index)
{
    const uint16_t channel = gTxVfo->CHANNEL_SAVE;
    if (channel >= MR_CHANNELS_MAX - MAX_BANDS || gCurrentFunction == FUNCTION_TRANSMIT)
        return;
    uint16_t *priority = gEeprom.SCANLIST_PRIORITY_CH;
    if (priority[index] == channel)
        priority[index] = MR_CHANNELS_MAX;
    else {
        priority[index] = channel;
        for (uint8_t i = 0; i < 4; i++)
            if (i != index && priority[i] == channel)
                priority[i] = MR_CHANNELS_MAX;
        char text[] = "CH PRI 1";
        text[7] += index;
        UI_DisplayPopup(text);
        ST7565_BlitFullScreen();
        SYSTEM_DelayMs(450);
    }
    gRequestSaveSettings = true;
    gFlagReconfigureVfos = true;
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
}

static void TogglePriority(uint8_t index)
{
    if (APP_NativeBegin(NATIVE_BOOT)) {
        TogglePriorityNative(index);
        APP_NativeEnd();
    }
}

// Long 1: the main VFO leaves memory mode and keeps its settings. The history
// hands its selected entry over the same way.
void MAIN_FrequencyToVfo(void)
{
    const uint8_t vfo = gEeprom.TX_VFO;
    const uint32_t frequency = gTxVfo->pRX->Frequency;
    const uint8_t band = gTxVfo->Band;
    gTxVfo->CHANNEL_SAVE = FREQ_CHANNEL;
    gTxVfo->ExactFrequency = true;
    gEeprom.ScreenChannel[vfo] = FREQ_CHANNEL;
    SETTINGS_SaveVfoIndices();
    RADIO_SelectVfos();
    gTxVfo->pRX->Frequency = gTxVfo->pTX->Frequency = frequency;
    gTxVfo->Band = band;
    SETTINGS_SaveChannel(FREQ_CHANNEL, vfo, gTxVfo, 2);
    RADIO_ConfigureSquelchAndOutputPower(gTxVfo);
    RADIO_SetupRegisters(true);
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen = DISPLAY_MAIN;
}

// ── F-key functions ──────────────────────────────────────────────────────────

static void processFKeyFunction(const KEY_Code_t Key, const bool beep)
{
    if (gScreenToDisplay == DISPLAY_MENU) {
        return;
    }
    switch (Key) {
        case KEY_0:
#ifdef ENABLE_FMRADIO
            ACTION_FM();
#endif
            break;

        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
            TogglePriority(Key - KEY_1);
            break;


        case KEY_5:
            APP_MenuOpen();
            gRequestDisplayScreen = DISPLAY_MAIN;
            break;

        case KEY_9:
            APP_RunScanner(SCAN_RANGE_MODE, false);
            gRequestDisplayScreen = DISPLAY_MAIN;
            break;

        case KEY_7:
            APP_RunScanner(CHANNEL_MODE, false);
            gRequestDisplayScreen = DISPLAY_MAIN;
            break;

        case KEY_8:
            APP_RunScanner(SCAN_BAND_MODE, false);
            gRequestDisplayScreen = DISPLAY_MAIN;
            break;

        case KEY_6:
            APP_RunSentinel();
            gRequestDisplayScreen = DISPLAY_MAIN;
            break;
        case KEY_UP:
        case KEY_DOWN:
            APP_AdjustScanSQL(Key == KEY_UP ? -1 : 1);
            gWasFKeyPressed = false;
            break;

        case KEY_SIDE1:
        case KEY_SIDE2: {
            uint8_t step = FREQUENCY_GetSortedIdxFromStepIdx(gTxVfo->STEP_SETTING);
            if (Key == KEY_SIDE1) {
                if (step < STEP_N_ELEM - 1)
                    gTxVfo->STEP_SETTING = FREQUENCY_GetStepIdxFromSortedIdx(step + 1);
            } else if (step > 0)
                gTxVfo->STEP_SETTING = FREQUENCY_GetStepIdxFromSortedIdx(step - 1);
            gTxVfo->StepFrequency = gStepFrequencyTable[gTxVfo->STEP_SETTING];
            if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
                SETTINGS_SaveChannelParameters(gEeprom.TX_VFO, gTxVfo, CHANNEL_PARAMETER_STEP);
            else if (IS_FREQ_CHANNEL(gTxVfo->CHANNEL_SAVE))
                gRequestSaveChannel = 1;
            gVfoConfigureMode = VFO_CONFIGURE;
            gWasFKeyPressed = false;
            break;
        }

        default:
            gUpdateStatus = true;
            gWasFKeyPressed = false;
            break;
    }
}

// ── Channel move ─────────────────────────────────────────────────────────────

static uint16_t memorySequential[2];
// A memory without a list shows twice: in place and in the NONE step. This
// remembers the memory reached in the NONE step (+1), per VFO.
static uint16_t memoryNone[2];
void channelMove(uint16_t Channel)
{
    if (!RADIO_CheckValidChannel(Channel, false, 0)) {
        return;
    }

    memorySequential[gEeprom.TX_VFO] = MR_GetChannelAttributes(Channel)->scanlist == 0 ? Channel + 1u : 0;
    memoryNone[gEeprom.TX_VFO] = 0;
    gEeprom.MrChannel[gEeprom.TX_VFO] = (uint16_t)Channel;
    gEeprom.ScreenChannel[gEeprom.TX_VFO] = (uint16_t)Channel;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;

    RADIO_ConfigureChannel(gEeprom.TX_VFO, gVfoConfigureMode);
    SETTINGS_SaveVfoIndices();
}

void channelMoveSwitch(void)
{
    if (!IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
        return;

    uint16_t Channel = 0;
    for (uint8_t i = 0; i < gInputBoxIndex; i++)
        Channel = (Channel * 10) + gInputBox[i];

    if ((Channel == 0) && (gInputBoxIndex != 4))
        return;

    if (gInputBoxIndex == 4) {
        gInputBoxIndex = 0;
        gKeyInputCountdown = 1;
    }

    channelMove(Channel - 1);
    SETTINGS_SaveVfoIndices();
}

// ── Digit keys ───────────────────────────────────────────────────────────────

static void MAIN_CommitFrequencyInput(void)
{
    const uint32_t frequency = INPUTBOX_FrequencyValue();
    if (frequency <= frequencyBandTable[BAND_N_ELEM - 1].upper) {
        const FREQUENCY_Band_t band = FREQUENCY_GetBand(frequency);
        if (gTxVfo->Band != band) {
            gTxVfo->Band = band;
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = FREQ_CHANNEL;
            SETTINGS_SaveVfoIndices();
            RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
        }
        const uint32_t rounded = FREQUENCY_RoundToStep(frequency, gTxVfo->StepFrequency);
        gTxVfo->freq_config_RX.Frequency = rounded;
        gTxVfo->freq_config_TX.Frequency = rounded;
        gEeprom.ScreenChannel[gEeprom.TX_VFO] = FREQ_CHANNEL;
        gEeprom.FreqChannel[gEeprom.TX_VFO] = FREQ_CHANNEL;
        gRequestSaveVFO = true;
        gRequestSaveChannel = 1;
    }
    INPUTBOX_FrequencyBegin();
    gInputBoxIndex = 0;
    gRequestDisplayScreen = DISPLAY_MAIN;
}

static void MAIN_Key_DIGITS(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    /* Sonic's native long-press handling remains authoritative. Only KEY_2
     * in dual-VFO mode is consumed for the dedicated VFO toggle. */
    if (Key == KEY_2 && gEeprom.RX_MODE != RX_MODE_SIMPLE && gScreenToDisplay == DISPLAY_MAIN) {
        if (bKeyHeld && bKeyPressed) {
            if (!gKey2LongHandled) {
                gKey2LongHandled = true;
                ACTION_VfoAB();
            }
            return;
        }
        if (bKeyHeld && !bKeyPressed && gKey2LongHandled) {
            gKey2LongHandled = false;
            return;
        }
    }

    if (bKeyHeld) {
        if (bKeyPressed) {
            if (gScreenToDisplay == DISPLAY_MAIN) {
                if (gInputBoxIndex > 0) {
                    gInputBoxIndex = 0;
                    gRequestDisplayScreen = DISPLAY_MAIN;
                }
                if (INPUTBOX_FrequencyIsActive()) {
                    INPUTBOX_FrequencyBegin();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                }

                HideFKeyIcon();

                // Long 7: Falcon indicators in Single and Dual VFO.
                if (Key == KEY_7) {
                    APP_FalconToggle();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                    return;
                }
                // Long 9: backlight
                if (Key == KEY_9) {
                    if (gBackLight)
                        ACTION_BackLight();
                    else
                        ACTION_BackLightOnDemand();
                    return;
                }
                // Long 0: modulation
                if (Key == KEY_0) {
                    ACTION_SwitchDemodul();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                    return;
                }
                if (Key == KEY_3) {
                    gVfoConfigureMode = VFO_CONFIGURE;
                    COMMON_SwitchVFOMode();
                    gScheduleVfoSave = true;
                    SETTINGS_SaveVfoIndices();
                    return;
                }
                if (Key == KEY_1) {
                    if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
                        MAIN_FrequencyToVfo();
                    gWasFKeyPressed = false;
                    return;
                }
                if (Key == KEY_5) {
                    ACTION_Wn();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                    return;
                }
                if (Key == KEY_4) {
                    uint8_t step = FREQUENCY_GetSortedIdxFromStepIdx(gTxVfo->STEP_SETTING);
                    step = step < STEP_N_ELEM - 1 ? step + 1 : 0;
                    gTxVfo->STEP_SETTING = FREQUENCY_GetStepIdxFromSortedIdx(step);
                    gTxVfo->StepFrequency = gStepFrequencyTable[gTxVfo->STEP_SETTING];
                    SETTINGS_SaveChannelParameters(gEeprom.TX_VFO, gTxVfo, CHANNEL_PARAMETER_STEP);
                    gVfoConfigureMode = VFO_CONFIGURE;
                    gUpdateStatus = gUpdateDisplay = true;
                    gRequestDisplayScreen = DISPLAY_MAIN;
                    gWasFKeyPressed = false;
                    return;
                }

                // Long 6: power cycle L→M→H→U→L
                if (Key == KEY_6) {
                    ACTION_Power();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                    return;
                }

                // Long 8: cycle squelch 1..9, then back to 1.
                if (Key == KEY_8) {
                    gEeprom.SQUELCH_LEVEL++;
                    if (gEeprom.SQUELCH_LEVEL > 9)
                        gEeprom.SQUELCH_LEVEL = 1;
                    gVfoConfigureMode = VFO_CONFIGURE;
                    gUpdateStatus = true;
                    gUpdateDisplay = true;
                    gRequestDisplayScreen = DISPLAY_MAIN;
                    return;
                }

                // Long 2 in Single mode is not the short F+2 shortcut.
                if (Key != KEY_2)
                    processFKeyFunction(Key, true);
            }
        }
        return;
    }

    if (bKeyPressed) {
        return;
    }

    if (gWasFKeyPressed) {
        // F+digit: route to processFKeyFunction
        gWasFKeyPressed = false;
        HideFKeyIcon();
        processFKeyFunction(Key, true);
        return;
    }

    if (!gWasFKeyPressed) {
        gRequestDisplayScreen = DISPLAY_MAIN;

        if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
            INPUTBOX_Append(Key);
            gKeyInputCountdown = key_input_timeout_500ms;
            channelMoveSwitch();
            gKeyInputCountdown = key_input_timeout_500ms / 4;
            return;
        }

        /* Direct VFO frequency entry, while retaining Sonic's native input-box
         * behaviour and making it available in dual-VFO frequency mode. */
        if (!INPUTBOX_FrequencyIsActive()) {
            gInputBoxIndex = 0;
            INPUTBOX_FrequencyBegin();
        }
        INPUTBOX_FrequencyUpdate(Key);

        /* A direct frequency entry is complete after 6 numeric digits:
         * XXX.xxx MHz.  Validate it immediately in both simple and dual VFO
         * modes, without requiring MENU. */
        if (INPUTBOX_FrequencyComplete()) {
            MAIN_CommitFrequencyInput();
            gKeyInputCountdown = 1;
        } else {
            gKeyInputCountdown = key_input_timeout_500ms / 3;
        }
        return;
    }
}

// ── EXIT key ─────────────────────────────────────────────────────────────────

static void MAIN_Key_EXIT(bool bKeyPressed, bool bKeyHeld)
{
    if (!bKeyHeld && bKeyPressed) {
        return;
    }

    if (bKeyHeld) {
        if (bKeyPressed) {
            gInputBoxIndex = 0;
            INPUTBOX_FrequencyBegin();
            gRequestDisplayScreen = DISPLAY_MAIN;
        }
        return;
    }

    // released
    if (INPUTBOX_FrequencyIsActive()) {
        INPUTBOX_FrequencyUpdate(KEY_EXIT);
        gKeyInputCountdown = key_input_timeout_500ms;
        gRequestDisplayScreen = DISPLAY_MAIN;
        return;
    }
#ifdef ENABLE_FMRADIO
    if (!gFmRadioMode)
#endif
    {
        if (gInputBoxIndex == 0)
            return;
        gInputBox[--gInputBoxIndex] = 10;
        gKeyInputCountdown = key_input_timeout_500ms;

        gRequestDisplayScreen = DISPLAY_MAIN;
        return;
    }

#ifdef ENABLE_FMRADIO
    ACTION_FM();
#endif
}

// ── MENU key ─────────────────────────────────────────────────────────────────

static void MAIN_Key_MENU(bool bKeyPressed, bool bKeyHeld)
{
    if (gWasFKeyPressed && !bKeyPressed && !bKeyHeld) {
        gWasFKeyPressed = false;
        gUpdateStatus = true;
        CHANNEL_OpenVfo();
        return;
    }
    if (bKeyHeld) {
        if (bKeyPressed) {
            gWasFKeyPressed = false;
            if (gScreenToDisplay == DISPLAY_MAIN) {
                if (gInputBoxIndex > 0) {
                    gInputBoxIndex = 0;
                    gRequestDisplayScreen = DISPLAY_MAIN;
                }
                if (INPUTBOX_FrequencyIsActive()) {
                    INPUTBOX_FrequencyBegin();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                }
                gUpdateStatus = true;
                ACTION_Handle(KEY_MENU, bKeyPressed, bKeyHeld);
            }
        }
        return;
    }

    if (!bKeyPressed) {
        if (INPUTBOX_FrequencyIsActive()) {
            MAIN_CommitFrequencyInput();
            return;
        }
        gKeyInputCountdown = 1;
        channelMoveSwitch();
        const bool bFlag = !gInputBoxIndex;
        gInputBoxIndex = 0;

        if (bFlag) {
            gFlagRefreshSetting = true;
            gRequestDisplayScreen = DISPLAY_MENU;
        } else {
            gRequestDisplayScreen = DISPLAY_MAIN;
        }
    }
}


// ── UP/DOWN keys ─────────────────────────────────────────────────────────────

// Display navigation only: group by list without renumbering NOR memories.
// Lists 1..25, then the memories without a list, then the monitor memories,
// then the bands. A memory without a list stays in place: it follows the
// list of the nearest listed memory below it (`inherit`).
static uint8_t MemoryListGroup(uint16_t channel, const ChannelAttributes_t *a, uint8_t *inherit)
{
    if (a->scanlist >= 1 && a->scanlist <= MR_CHANNELS_LIST)
        return *inherit = a->scanlist;
    if (a->scanlist == 51)
        return MR_CHANNELS_LIST + 2;
    if (channel >= MR_CHANNELS_MAX - MAX_BANDS)
        return MR_CHANNELS_LIST + 3;
    return *inherit ? *inherit : MR_CHANNELS_LIST + 1;
}
void MAIN_SetMemoryStep(uint16_t channel, bool none)
{
    memorySequential[gEeprom.TX_VFO] = 0;
    memoryNone[gEeprom.TX_VFO] = none ? channel + 1u : 0;
}
static uint16_t NextMemoryByList(uint16_t current, int8_t direction)
{
    const uint8_t vfo = gEeprom.TX_VFO;
    if (memorySequential[vfo] == current + 1u) {
        uint16_t next = RADIO_FindNextChannel(current + direction, direction, false, vfo);
        memorySequential[vfo] = next + 1u;
        return next;
    }
    const bool forward = direction > 0;
    const uint32_t stride = MR_CHANNEL_LAST + 1u;
    const uint32_t reverse = forward ? 0 : UINT32_MAX;
    uint8_t none = 0;
    uint32_t currentKey = ((uint32_t)MemoryListGroup(current, MR_GetChannelAttributes(current), &none) * stride +
                           current) ^ reverse;
    uint16_t next = UINT16_MAX, wrap = UINT16_MAX;
    uint32_t nextKey = 0, wrapKey = 0;
    bool nextNone = false, wrapNone = false;
    // Read attributes in small batches: no channel-sized sorting buffer or
    // thousands of individual NOR transactions on every keypress. The first
    // pass stops at the current memory to know the list it follows.
    ChannelAttributes_t block[32];
    for (uint8_t pass = 0; pass < 2; ++pass) {
        uint8_t inherit = 0;
        for (uint16_t first = MR_CHANNEL_FIRST; first <= MR_CHANNEL_LAST; first += 32) {
            const uint16_t count = MR_CHANNEL_LAST - first + 1 < 32 ? MR_CHANNEL_LAST - first + 1 : 32;
            PY25Q16_ReadBuffer(ADRESS_CHANNELS_ATTRIBUTES + (uint32_t)first * sizeof *block, block,
                               count * sizeof *block);
            for (uint8_t i = 0; i < count; ++i) {
                const ChannelAttributes_t *a = &block[i];
                if (a->band > BAND7_470MHz)
                    continue;
                const uint16_t ch = first + i;
                const uint8_t group = MemoryListGroup(ch, a, &inherit);
                const bool unlisted = !a->scanlist && ch < MR_CHANNELS_MAX - MAX_BANDS;
                uint32_t key = ((uint32_t)group * stride + ch) ^ reverse;
                const uint32_t noneKey = ((uint32_t)(MR_CHANNELS_LIST + 1) * stride + ch) ^ reverse;
                if (!pass) {
                    if (ch == current) {
                        currentKey = unlisted && memoryNone[vfo] == current + 1u ? noneKey : key;
                        goto found;
                    }
                    continue;
                }
                // In place, then (in place elsewhere) once more in the NONE step.
                for (uint8_t twice = 0; twice < (unlisted && group != MR_CHANNELS_LIST + 1 ? 2 : 1); ++twice) {
                    if (wrap == UINT16_MAX || key < wrapKey) {
                        wrap = ch;
                        wrapKey = key;
                        wrapNone = twice;
                    }
                    if (key > currentKey && (next == UINT16_MAX || key < nextKey)) {
                        next = ch;
                        nextKey = key;
                        nextNone = twice;
                    }
                    key = noneKey;
                }
            }
        }
    found:;
    }
    const bool inNone = next != UINT16_MAX ? nextNone : wrapNone;
    if (next == UINT16_MAX)
        next = wrap;
    memoryNone[vfo] = inNone ? next + 1u : 0;
    return next;
}

static void MAIN_Key_UP_DOWN(bool bKeyPressed, bool bKeyHeld, int8_t Direction)
{
    if (INPUTBOX_FrequencyIsActive())
        return;
    if (!gEeprom.SET_NAV)
        Direction = -Direction;

#ifdef ENABLE_FEAT_F4HWN
    if (gWasFKeyPressed || gScanSqlPreview) {
        if (!bKeyPressed)
            return;
        processFKeyFunction(Direction == 1 ? KEY_UP : KEY_DOWN, true);
        return;
    }
#endif

    uint16_t Channel = gEeprom.ScreenChannel[gEeprom.TX_VFO];

    if (bKeyHeld || !bKeyPressed) {
        if (gInputBoxIndex > 0)
            return;
        if (!bKeyPressed) {
            if (!bKeyHeld || IS_FREQ_CHANNEL(Channel))
                return;
            return;
        }
    } else {
        if (gInputBoxIndex > 0) {
            return;
        }
    }
    {
        if (IS_FREQ_CHANNEL(Channel)) {
            const uint32_t frequency = APP_SetFrequencyByStep(gTxVfo, Direction);
            if (RX_freq_check(frequency) < 0) {
                return;
            }
            gTxVfo->freq_config_RX.Frequency = frequency;
            RADIO_ApplyOffset(gTxVfo);
            if (gEeprom.RX_MODE == RX_MODE_SIMPLE || gEeprom.RX_VFO == gEeprom.TX_VFO) {
                BK4819_SetFrequency(frequency);
                BK4819_RX_TurnOn();
            }
            gRequestSaveChannel = 1;
            return;
        }

        uint16_t Next = NextMemoryByList(Channel, Direction);
        if (Next == 0xFFFF)
            return;
        if (Channel == Next)
            return;
        gEeprom.MrChannel[gEeprom.TX_VFO] = Next;
        gEeprom.ScreenChannel[gEeprom.TX_VFO] = Next;
    }
    gRequestSaveVFO = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    return;
}

// ── Main dispatcher ──────────────────────────────────────────────────────────

void MAIN_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && Key != KEY_PTT && Key != KEY_EXIT) {
        if (!bKeyHeld && bKeyPressed)
            return;
    }
#endif

    switch (Key) {
#ifdef ENABLE_FEAT_F4HWN
        case KEY_SIDE1:
        case KEY_SIDE2:
#endif
        case KEY_0 ... KEY_9:
            MAIN_Key_DIGITS(Key, bKeyPressed, bKeyHeld);
            break;
        case KEY_MENU:
            MAIN_Key_MENU(bKeyPressed, bKeyHeld);
            break;
        case KEY_UP:
        case KEY_DOWN:
            MAIN_Key_UP_DOWN(bKeyPressed, bKeyHeld, NAV_DIR(Key == KEY_UP ? 1 : -1));
            break;
        case KEY_EXIT:
            MAIN_Key_EXIT(bKeyPressed, bKeyHeld);
            break;
        case KEY_STAR:
#ifdef ENABLE_SPECTRUM
            if (gWasFKeyPressed) {
                if (bKeyPressed && !bKeyHeld) {
                    gWasFKeyPressed = false;
                    HideFKeyIcon();
                    gInputBoxIndex = 0;
                    INPUTBOX_FrequencyBegin();
                    APP_RunAllMemoryScan();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                }
                break;
            }
            if (bKeyPressed && bKeyHeld) {
                const uint16_t channel = gEeprom.ScreenChannel[gEeprom.TX_VFO & 1u];
                if (IS_MR_CHANNEL(channel)) {
                    const ChannelAttributes_t *att = MR_GetChannelAttributes(channel);
                    if (att && att->scanlist >= 1 && att->scanlist <= MR_CHANNELS_LIST) {
                        const uint8_t list = att->scanlist;
                        gInputBoxIndex = 0;
                        INPUTBOX_FrequencyBegin();
                        APP_RunCurrentScanList(list);
                        gRequestDisplayScreen = DISPLAY_MAIN;
                    }
                } else if (gEeprom.RX_MODE != RX_MODE_SIMPLE && IS_FREQ_CHANNEL(gEeprom.ScreenChannel[0]) &&
                           IS_FREQ_CHANNEL(gEeprom.ScreenChannel[1])) {
                    gInputBoxIndex = 0;
                    INPUTBOX_FrequencyBegin();
                    APP_RunVfoScanRange();
                    gRequestDisplayScreen = DISPLAY_MAIN;
                }
                break;
            }
#endif
            if (!IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE) && INPUTBOX_FrequencyIsActive() && !bKeyPressed &&
                !bKeyHeld)
                INPUTBOX_FrequencyUpdate(KEY_STAR);
            break;
        case KEY_F:
            GENERIC_Key_F(bKeyPressed, bKeyHeld);
            break;
        case KEY_PTT:
            GENERIC_Key_PTT(bKeyPressed);
            break;
        default:
            break;
    }
}

#include "monitor.inc"
