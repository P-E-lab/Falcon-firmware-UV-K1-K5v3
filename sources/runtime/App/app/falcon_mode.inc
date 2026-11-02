// Falcon mode reuses the legacy option bit; radio/watch routing stays unchanged.
static uint32_t falconGreenTick, falconRedTick;
static uint8_t falconRedPhases, falconRedPeriod, falconGreenPhases;
static bool falconGreenOn, falconRxLatched;

bool APP_FalconMode(void)
{
    return gEeprom.FlashlightOnRX && gEeprom.RX_MODE < RX_MODE_FULL;
}

bool APP_FalconLedValue(unsigned pin, bool value)
{
    if (!APP_FalconMode())
        return value;
    if (pin == BK4819_GPIO6_PIN2_GREEN)
        return falconGreenOn;
    if (pin == BK4819_GPIO5_PIN1_RED)
        return (falconRedPhases & 1u) != 0;
    return value;
}

static void FalconRed(uint8_t phases, uint8_t period)
{
    falconRedPhases = phases;
    falconRedPeriod = period;
    falconRedTick = gGlobalSysTickCounter;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, (phases & 1u) != 0);
}

void APP_FalconDisable(void)
{
    if (!gEeprom.FlashlightOnRX)
        return;
    gEeprom.FlashlightOnRX = false;
    falconGreenOn = falconRxLatched = false;
    falconRedPhases = falconGreenPhases = 0;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, gCurrentFunction == FUNCTION_TRANSMIT);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, FUNCTION_IsRx() && g_SquelchLost);
    BACKLIGHT_TurnOn();
    gRequestSaveSettings = true;
    gUpdateStatus = true;
}

void APP_FalconToggle(void)
{
    if (gEeprom.RX_MODE >= RX_MODE_FULL)
        return;
    if (APP_FalconMode()) {
        APP_FalconDisable();
        return;
    }
    falconGreenOn = falconRxLatched = false;
    falconRedPhases = falconGreenPhases = 0;
    gEeprom.FlashlightOnRX = true;
#ifdef ENABLE_FLASHLIGHT
    FLASHLIGHT_ForceOff();
#endif
    BACKLIGHT_TurnOff();
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    falconGreenPhases = 5;
    falconGreenOn = true;
    falconGreenTick = gGlobalSysTickCounter;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    gRequestSaveSettings = true;
    gUpdateStatus = true;
}

void APP_FalconRx(void)
{
    if (!APP_FalconMode() || falconRxLatched || falconGreenPhases)
        return;
    falconRxLatched = falconGreenOn = true;
    falconGreenTick = gGlobalSysTickCounter;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
}

void APP_FalconTx(void)
{
    if (!APP_FalconMode())
        return;
    falconGreenOn = falconRxLatched = false;
    falconGreenPhases = 0;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    FalconRed(1, 100); // One second maximum; PTT release cancels it immediately.
}

void APP_FalconTxEnd(void)
{
    if (APP_FalconMode())
        FalconRed(0, 0);
}

void APP_FalconSwitch(void)
{
    if (APP_FalconMode())
        FalconRed(gEeprom.TX_VFO ? 3 : 1, 25);
}

static void FalconTick(void)
{
    if (!APP_FalconMode())
        return;
    if (falconGreenPhases) {
        const uint32_t elapsed = gGlobalSysTickCounter - falconGreenTick;
        if (elapsed >= 20u) {
            const uint32_t phases = elapsed / 20u;
            falconGreenTick += phases * 20u;
            falconGreenPhases = phases >= falconGreenPhases ? 0 : falconGreenPhases - phases;
            falconGreenOn = (falconGreenPhases & 1u) != 0;
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, falconGreenOn);
        }
    } else if (falconGreenOn && (!g_SquelchLost || gCurrentFunction != FUNCTION_RECEIVE ||
                                 (uint32_t)(gGlobalSysTickCounter - falconGreenTick) >= 10u)) {
        falconGreenOn = false;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    }
    if (!g_SquelchLost || !FUNCTION_IsRx())
        falconRxLatched = false;
    if (falconRedPhases) {
        const uint32_t elapsed = gGlobalSysTickCounter - falconRedTick;
        if (elapsed >= falconRedPeriod) {
            const uint32_t phases = elapsed / falconRedPeriod;
            falconRedTick += phases * falconRedPeriod;
            falconRedPhases = phases >= falconRedPhases ? 0 : falconRedPhases - phases;
            BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, (falconRedPhases & 1u) != 0);
        }
    }
}
