/* V78: consume the entire wake gesture, preserving EXIT/PTT priority. */
#pragma once
static bool ScanWakeConsume(bool *waitingRelease, KEY_Code_t key, bool enabled)
{
    if (key == KEY_EXIT || key == KEY_PTT) {
        *waitingRelease = false;
        return false;
    }
    if (*waitingRelease) {
        if (key == KEY_INVALID)
            *waitingRelease = false;
        return true;
    }
    if (enabled && key != KEY_INVALID && !backlightOn && gEeprom.BACKLIGHT_TIME) {
        BACKLIGHT_TurnOn();
        *waitingRelease = true;
        return true;
    }
    return false;
}
