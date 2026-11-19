#include <string.h>

#include "app/action.h"
#include "app/fm.h"
#include "app/fkeys.h"
#include "app/main.h"
#include "app/spectrum.h"
#include "driver/py25q16.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

extern void (*action_opt_table[])(void);
void MONITOR_ToggleCurrent(void);

static uint8_t fkeyMap[2][FKEYS_KEYS]; // [0] long, [1] F+
static KEY_Code_t fkeyConsumed = KEY_INVALID;
static bool fkeyRunning, fkeyValid; // valid: a FKY1 table was read

void FKEYS_Load(void)
{
    uint8_t b[5 + sizeof fkeyMap];
    PY25Q16_ReadBuffer(ADRESS_FKEYS, b, sizeof b);
    fkeyValid = !memcmp(b, FKEYS_MAGIC, 4) && b[4] == FKEYS_VERSION;
    memcpy(fkeyMap, b + 5, sizeof fkeyMap);
}

static void Run(KEY_Code_t key, uint8_t v)
{
    gWasFKeyPressed = false;
    HideFKeyIcon();
    fkeyConsumed = key;
    fkeyRunning = true;
    if (v >= FKEYS_LONG) {
        const KEY_Code_t k = (KEY_Code_t)(v & 0x0Fu);
        if (v >= FKEYS_FPLUS && k == KEY_EXIT) {
            MONITOR_ToggleCurrent();
        } else if (v >= FKEYS_FPLUS) { // the native F+ press: press, then release
            gWasFKeyPressed = true;
            MAIN_ProcessKeys(k, true, false);
            gWasFKeyPressed = gWasFKeyPressed && k != KEY_STAR;
            MAIN_ProcessKeys(k, false, false);
        } else { // the native long press, then its release after hold
            gWasFKeyPressed = false;
            MAIN_ProcessKeys(k, true, true);
            MAIN_ProcessKeys(k, false, true);
        }
    } else if (v >= FKEYS_SENTINEL && v <= FKEYS_SENTINEL + 2) {
        static const Mode modes[] = {CHANNEL_MODE, SCAN_BAND_MODE, SCAN_RANGE_MODE};
        APP_RunScanner(modes[v - FKEYS_SENTINEL], true);
        gRequestDisplayScreen = DISPLAY_MAIN;
    } else if (v < ACTION_OPT_LEN) {
        action_opt_table[v]();
    }
    fkeyRunning = false;
    gUpdateStatus = gUpdateDisplay = true;
}

// True when FKeys owns this event: the rest of that press is then consumed.
__attribute__((noinline)) bool FKEYS_Intercept(KEY_Code_t key, bool pressed, bool held)
{
    if (key == fkeyConsumed) {
        if (!pressed)
            fkeyConsumed = KEY_INVALID;
        return true;
    }
    if (!fkeyValid || fkeyRunning || key >= FKEYS_KEYS || gScreenToDisplay != DISPLAY_MAIN || gInputBoxIndex ||
        INPUTBOX_FrequencyIsActive() || gEeprom.KEY_LOCK
#ifdef ENABLE_FMRADIO
        || gFmRadioMode
#endif
    )
        return false;
    uint8_t v;
    if (gWasFKeyPressed && pressed && !held && key != KEY_F)
        v = fkeyMap[1][key];
    else if (!gWasFKeyPressed && pressed && held)
        v = fkeyMap[0][key];
    else
        return false;
    if (v == FKEYS_DEFAULT)
        return false;
    Run(key, v);
    return true;
}
