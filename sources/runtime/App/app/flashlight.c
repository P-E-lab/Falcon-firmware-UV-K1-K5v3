#include "app/app.h"
#ifdef ENABLE_FLASHLIGHT

#include "driver/gpio.h"
#include "app/flashlight.h"
#include "misc.h"
#include <stdbool.h>

static inline void Flashlight_TurnOn()
{
    GPIO_SetOutputPin(GPIO_PIN_FLASHLIGHT);
}
static inline void Flashlight_TurnOff()
{
    GPIO_ResetOutputPin(GPIO_PIN_FLASHLIGHT);
}
static inline void Flashlight_Toggle()
{
    GPIO_TogglePin(GPIO_PIN_FLASHLIGHT);
}

#if !defined(ENABLE_FEAT_F4HWN)
enum FlashlightMode_t gFlashLightState;

void FlashlightTimeSlice()
{
    if (gFlashLightState == FLASHLIGHT_BLINK && (gFlashLightBlinkCounter & 15u) == 0) {
        Flashlight_Toggle();
        return;
    }

    if (gFlashLightState == FLASHLIGHT_SOS) {
        const uint16_t u = 15;
        static uint8_t c;
        static uint16_t next;

        if (gFlashLightBlinkCounter - next > 7 * u) {
            c = 0;
            next = gFlashLightBlinkCounter + 1;
            return;
        }

        if (gFlashLightBlinkCounter == next) {
            if (c == 0) {
                Flashlight_TurnOff();
            } else {
                Flashlight_Toggle();
            }

            if (c >= 18) {
                next = gFlashLightBlinkCounter + 7 * u;
                c = 0;
            } else if (c == 7 || c == 9 || c == 11) {
                next = gFlashLightBlinkCounter + 3 * u;
            } else {
                next = gFlashLightBlinkCounter + u;
            }
            c++;
        }
    }
}

void ACTION_FlashLight(void)
{
    if (APP_FalconMode())
        return;
    // switch (gFlashLightState) {
    //     case FLASHLIGHT_OFF:
    //         Flashlight_TurnOn();
    //         break;
    //     case FLASHLIGHT_ON:
    //     case FLASHLIGHT_BLINK:
    //         break;
    //     case FLASHLIGHT_SOS:
    //     default:
    //         Flashlight_TurnOff();
    // }

    if (gFlashLightState == FLASHLIGHT_OFF) {
        Flashlight_TurnOn();
    } else if (gFlashLightState == FLASHLIGHT_SOS) {
        Flashlight_TurnOff();
    }

    gFlashLightState = (gFlashLightState + 1) % 4;
}
void ACTION_LightMenu(void)
{
    ACTION_FlashLight();
}
void FLASHLIGHT_ForceOff(void)
{
    gFlashLightState = FLASHLIGHT_OFF;
    Flashlight_TurnOff();
}
#else
static bool gFlashLightState = false;
static uint8_t gSosStep = 0;
static uint16_t gSosCountdown = 0;
static bool gBeaconOn = false;
static uint8_t gLightMode;
static uint16_t gLightCycleTicks;

void FlashlightTimeSlice(void)
{
    if (APP_FalconMode())
        return;
    if (gLightCycleTicks)
        --gLightCycleTicks;
    if (!gFlashLightState)
        return;

    if (gLightMode == 2) {
        // SOS: standard ... --- ... visual Morse pattern.
        if (gSosCountdown > 0) {
            --gSosCountdown;
            return;
        }

        static const uint16_t durations[] = {15, 15, 15, 15, 15, 45, 45, 15, 45,
                                             15, 45, 45, 15, 15, 15, 15, 15, 105};
        if (!(gSosStep & 1u))
            Flashlight_TurnOn();
        else
            Flashlight_TurnOff();

        gSosCountdown = durations[gSosStep] - 1;
        gSosStep = (uint8_t)((gSosStep + 1) % ARRAY_SIZE(durations));
        return;
    }

    if (gLightMode == 3) {
        // Beacon: one short 100 ms flash every 3 seconds.
        if (gSosCountdown > 0) {
            --gSosCountdown;
            if (gBeaconOn && gSosCountdown == 0) {
                Flashlight_TurnOff();
                gBeaconOn = false;
                gSosCountdown = 290;
            }
            return;
        }

        Flashlight_TurnOn();
        gBeaconOn = true;
        gSosCountdown = 10;
        return;
    }
}

static void LightStart(uint8_t mode)
{
    gLightMode = mode;
    gFlashLightState = mode != 0;
    gSosStep = 0;
    gSosCountdown = 0;
    gBeaconOn = mode == 3;
    if (mode == 1 || mode == 3)
        Flashlight_TurnOn();
    else
        Flashlight_TurnOff();
    if (mode == 3)
        gSosCountdown = 10;
}

void ACTION_FlashLight(void)
{
    if (APP_FalconMode())
        return;
    gLightCycleTicks = 0;
    LightStart(gFlashLightState ? 0 : gSetting_led_mode);
}

void ACTION_LightMenu(void)
{
    if (APP_FalconMode())
        return;
    if (gFlashLightState && !gLightCycleTicks) {
        ACTION_FlashLight();
        return;
    }
    LightStart(gFlashLightState ? gLightMode % 3 + 1 : gSetting_led_mode);
    if (gFlashLightState)
        gLightCycleTicks = 300;
}

void FLASHLIGHT_ForceOff(void)
{
    gLightCycleTicks = 0;
    LightStart(0);
}

#endif
#endif
