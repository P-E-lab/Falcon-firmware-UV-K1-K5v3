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
#include "app/common.h"

#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif

#include "app/generic.h"
#include "app/menu.h"

#include "driver/keyboard.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

void GENERIC_Key_F(bool bKeyPressed, bool bKeyHeld)
{
    if (gInputBoxIndex > 0 || gScreenToDisplay == DISPLAY_MENU) {
        return;
    }

    if (bKeyHeld) {
        if (bKeyPressed)
            COMMON_KeypadLockToggle();
        return;
    }
    if (bKeyPressed)
        return;
#ifdef ENABLE_FMRADIO
    if ((gFmRadioMode || gScreenToDisplay != DISPLAY_MAIN) && gScreenToDisplay != DISPLAY_FM)
        return;
#else
    if (gScreenToDisplay != DISPLAY_MAIN)
        return;
#endif
    gWasFKeyPressed = !gWasFKeyPressed;
    if (gWasFKeyPressed)
        gKeyInputCountdown = key_input_timeout_500ms;
    gUpdateStatus = true;
}

void GENERIC_Key_PTT(bool bKeyPressed)
{
    cancelUserInputModes();

    if (!bKeyPressed || SerialConfigInProgress()) { // PTT released
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            // we are transmitting .. stop
            APP_HandleEndTransmission();

#ifdef ENABLE_VOX
            gVOX_NoiseDetected = false;
#endif
            RADIO_SetVfoState(VFO_STATE_NORMAL);

            if (gScreenToDisplay != DISPLAY_MENU) // 1of11 .. don't close the menu
                gRequestDisplayScreen = DISPLAY_MAIN;
        }

        return;
    }

    // PTT pressed


#ifdef ENABLE_FMRADIO
    if (gScreenToDisplay == DISPLAY_FM)
        goto start_tx; // listening to the FM radio .. start TX'ing
#endif

    if (gCurrentFunction == FUNCTION_TRANSMIT && gRTTECountdown_10ms == 0) { // already transmitting
        gInputBoxIndex = 0;
        return;
    }

    if (gScreenToDisplay != DISPLAY_MENU) // 1of11 .. don't close the menu
        gRequestDisplayScreen = DISPLAY_MAIN;

start_tx:
    // request start TX (X power checked in RADIO_PrepareTX → VFO_STATE_TX_DISABLE)
    gFlagPrepareTX = true;
    gPttDebounceCounter = 0;
    if (gScreenToDisplay != DISPLAY_MENU
#ifdef ENABLE_FMRADIO
        && gRequestDisplayScreen != DISPLAY_FM
#endif
    ) {
        // 1of11 .. don't close the menu
        gRequestDisplayScreen = DISPLAY_MAIN;
    }

    gUpdateStatus = true;
    gUpdateDisplay = true;
}
