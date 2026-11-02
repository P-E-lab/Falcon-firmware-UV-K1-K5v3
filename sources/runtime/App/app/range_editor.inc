#include "apps/app_overlay.h"
#include "ui/inputbox.h"
/* Scanrange-only editor. Included by spectrum.c; no build-system change. */
typedef struct {
    uint32_t frequency[2];
    const char *error;
    uint8_t field, modulation, step, bandwidth, length, dot;
    char digits[9];
    uint32_t idleTick;
    uint16_t savedR30, savedR37;
    bool dirty, sleeping, savedRxEnable;
} RangeEditor;
static RangeEditor rangeEditor;

static void RangeEditorWake(void)
{
    rangeEditor.idleTick = gGlobalSysTickCounter;
    if (!rangeEditor.sleeping)
        return;
    // Restore the exact suspended scanner state, including its muted audio.
    BK4819_WriteRegister(BK4819_REG_37, rangeEditor.savedR37);
    BK4819_WriteRegister(BK4819_REG_30, rangeEditor.savedR30);
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, rangeEditor.savedRxEnable);
    rangeEditor.sleeping = false;
}

static void RangeEditorTick(void)
{
    // This page already suspends scanning and reception: RF can stay asleep
    // until a key wakes it. The draft and keyboard remain in RAM and active.
    if (!rangeEditor.sleeping && gEeprom.BATTERY_SAVE && !last_ptt_state &&
        (uint32_t)(gGlobalSysTickCounter - rangeEditor.idleTick) >= battery_save_count_10ms) {
        rangeEditor.savedR30 = BK4819_ReadRegister(BK4819_REG_30);
        rangeEditor.savedR37 = BK4819_ReadRegister(BK4819_REG_37);
        rangeEditor.savedRxEnable = (BK4819_ReadRegister(BK4819_REG_33) & 0x40u) != 0;
        BK4819_Sleep();
        BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
        rangeEditor.sleeping = true;
    }
    // Keep the normal backlight timeout active while the scan is suspended.
    if (gNextTimeslice_500ms) {
        gNextTimeslice_500ms = false;
        if (gBacklightCountdown_500ms > 0)
            --gBacklightCountdown_500ms;
        if (gEeprom.BACKLIGHT_TIME < 61 && gBacklightCountdown_500ms == 0)
            BACKLIGHT_TurnOff();
    }
    if (rangeEditor.dirty) {
        Render();
        rangeEditor.dirty = false;
    }
}

static void RangeEditorOpen(void)
{
    if (Spectrum_state != SCAN_RANGE_MODE || currentState != SPECTRUM || historyListActive || gIsKeylocked ||
        last_ptt_state)
        return;
    memset(&rangeEditor, 0, sizeof(rangeEditor));
    rangeEditor.frequency[0] = RangeStart;
    rangeEditor.frequency[1] = RangeStop;
    rangeEditor.modulation =
        settings.modulationType < MODULATION_UKNOWN ? settings.modulationType : MODULATION_FM;
    rangeEditor.step =
        settings.scanStepIndex < ARRAY_SIZE(scanStepValues) ? settings.scanStepIndex : STEP_12_5kHz;
    rangeEditor.bandwidth = settings.modulationType == MODULATION_AM ? listenBwAM : settings.listenBw;
    rangeEditor.dirty = true;
    rangeEditor.idleTick = gGlobalSysTickCounter;
    osdPopupTimer = 0;
    osdPopupText[0] = '\0';
    SpectrumMonitor = 0;
    SPECTRUM_PAUSED = false;
    SpectrumPauseCount = 0;
    SetState(RANGE_EDIT);
    RenderStatus();
}

static uint32_t RangeEditorInputValue(void)
{
    return INPUTBOX_ParseFrequency(rangeEditor.digits, rangeEditor.length, rangeEditor.dot);
}

static bool RangeEditorAcceptInput(void)
{
    if (!rangeEditor.length)
        return true;
    const uint32_t value = RangeEditorInputValue();
    if (value < FMIN || value > FMAX || RX_freq_check(value) < 0) {
        rangeEditor.error = "INVALID FREQ";
        return false;
    }
    rangeEditor.frequency[rangeEditor.field] = value;
    rangeEditor.length = rangeEditor.dot = 0;
    return true;
}

static void RangeEditorClose(void)
{
    RangeEditorWake();
    rangeEditor.length = rangeEditor.dot = 0;
    WaitSpectrum = 0;
    ResetModifiers();
    SetState(SPECTRUM);
    RenderStatus();
    Render();
}

static void HandleKeyRangeEditor(uint8_t key)
{
    RangeEditorWake();
    rangeEditor.error = NULL;
    rangeEditor.dirty = true;
    if (key == KEY_PTT) {
        RangeEditorClose(); // Cancel the draft before using the native PTT route.
        SpectrumTransmit();
        return;
    }
    if (key == KEY_EXIT) {
        if (rangeEditor.field >= 2 || !INPUTBOX_Backspace(&rangeEditor.length, &rangeEditor.dot))
            RangeEditorClose();
        return;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        if (!RangeEditorAcceptInput())
            return;
        rangeEditor.field = key == KEY_DOWN ? (rangeEditor.field + 1) % 5 : (rangeEditor.field + 4) % 5;
        return;
    }
    if (rangeEditor.field < 2) {
        if (key >= KEY_0 && key <= KEY_9) {
            const uint8_t max = rangeEditor.dot ? rangeEditor.dot + 5 : 8;
            if (rangeEditor.length < max && rangeEditor.length < sizeof(rangeEditor.digits))
                rangeEditor.digits[rangeEditor.length++] = '0' + key - KEY_0;
            return;
        }
        if (key == KEY_STAR) {
            if (rangeEditor.length && rangeEditor.length <= 4 && !rangeEditor.dot)
                rangeEditor.dot = rangeEditor.length;
            return;
        }
    } else if (key == KEY_1 || key == KEY_3) {
        uint8_t *value = rangeEditor.field == 2   ? &rangeEditor.step
                         : rangeEditor.field == 3 ? &rangeEditor.modulation
                                                  : &rangeEditor.bandwidth;
        const uint8_t count = rangeEditor.field == 2                    ? ARRAY_SIZE(scanStepValues)
                              : rangeEditor.field == 3                  ? MODULATION_UKNOWN
                              : rangeEditor.modulation == MODULATION_AM ? 2
                                                                        : 3;
        *value = key == KEY_3 ? (*value + 1) % count : (*value ? *value - 1 : count - 1);
        rangeEditor.bandwidth =
            rangeEditor.modulation == MODULATION_AM ? !!rangeEditor.bandwidth : rangeEditor.bandwidth;
        return;
    }
    if (key != KEY_MENU || !RangeEditorAcceptInput())
        return;
    for (uint8_t row = 0; row < 2; ++row) {
        const uint32_t f = rangeEditor.frequency[row];
        if (f < FMIN || f > FMAX || RX_freq_check(f) < 0) {
            rangeEditor.error = row ? "INVALID STOP" : "INVALID START";
            return;
        }
    }
    if (rangeEditor.frequency[0] >= rangeEditor.frequency[1]) {
        rangeEditor.error = "START MUST BE < STOP";
        return;
    }
    if (rangeEditor.frequency[1] - rangeEditor.frequency[0] < scanStepValues[rangeEditor.step]) {
        rangeEditor.error = "RANGE < STEP";
        return;
    }
    // Draft editing does not write flash. Commit once, only if anything changed.
    const bool changed =
        RangeStart != rangeEditor.frequency[0] || RangeStop != rangeEditor.frequency[1] ||
        settings.scanStepIndex != rangeEditor.step || !rangeModConfigured ||
        rangeModulation != rangeEditor.modulation ||
        rangeEditor.bandwidth != (rangeEditor.modulation == MODULATION_AM ? listenBwAM : settings.listenBw);
    RangeStart = rangeEditor.frequency[0];
    RangeStop = rangeEditor.frequency[1];
    settings.scanStepIndex = rangeEditor.step;
    settings.modulationType = rangeModulation = rangeEditor.modulation;
    rangeModConfigured = true;
    if (scanMenuModulation < MODULATION_UKNOWN && !vfoRangeSession)
        scanMenuModulation = rangeEditor.modulation;
    if (rangeEditor.modulation == MODULATION_AM)
        listenBwAM = rangeEditor.bandwidth;
    else
        settings.listenBw = rangeEditor.bandwidth;
    savedScanStep = settings.scanStepIndex;
    savedListenBw = settings.listenBw;
    savedListenBwAM = listenBwAM;
    if (changed) {
        sentinelIdleTicks = 0;
        sentinelTimerTick = gGlobalSysTickCounter;
        scanOptionsChanged = true;
        SaveSettings(false);
    }
    RangeEditorClose();
}

static NATIVE_CODE(options) void RenderRangeEditorNative(void)
{
    UI_PrintStringSmallBold("SCANRANGE", 0, 127, 0);
    static const char *const labels[] = {"FSTART:", "FSTOP:", "STEP:", "MOD:", "BANDW:"};
    // Five seven-pixel rows centred between title y0..7 and footer y50..54.
    for (uint8_t row = 0; row < 5; ++row) {
        char text[24];
        if (row < 2) {
            if (row == rangeEditor.field && rangeEditor.length) {
                const uint8_t whole = rangeEditor.dot ? rangeEditor.dot : 3;
                uint8_t out = 0;
                for (uint8_t i = 0; i < rangeEditor.length; ++i) {
                    if (i == whole)
                        text[out++] = '.';
                    text[out++] = rangeEditor.digits[i];
                }
                if (rangeEditor.dot && rangeEditor.length == whole)
                    text[out++] = '.';
                if (rangeEditor.length <= whole || rangeEditor.length < whole + 5)
                    text[out++] = '_';
                text[out] = 0;
            } else {
                CHANNEL_FormatFrequency(text, rangeEditor.frequency[row]);
            }
        } else if (row == 2)
            FP_CopyString(text, scanStepNames[rangeEditor.step], 6);
        else
            strcpy(text, row == 3 ? gModulationStr[rangeEditor.modulation]
                         : rangeEditor.modulation == MODULATION_AM
                             ? (rangeEditor.bandwidth ? "Narrow" : "Wide")
                             : bwNames[rangeEditor.bandwidth]);
        CHANNEL_DrawRangeField(labels[row], text, 12 + row * 7, row == rangeEditor.field);
    }
    if (rangeEditor.error)
        GUI_DisplaySmallest(rangeEditor.error, 2, 45, false, true);
    static const char fields[] = "< >: NEXT FIELD", values[] = "1/3:VALUES";
    const uint8_t valuesX = 126 - (sizeof(values) - 1) * 4;
    GUI_DisplaySmallest(fields, valuesX - 7 - (sizeof(fields) - 1) * 4, 50, false, true);
    GUI_DisplaySmallest(values, 128, 50, false, true);
}
static void RenderRangeEditor(void)
{
    if (APP_NativeBegin(NATIVE_OPTIONS)) {
        RenderRangeEditorNative();
        APP_NativeEnd();
    }
}
