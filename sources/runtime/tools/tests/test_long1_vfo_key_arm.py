"""Actual ARM: long 1 hands a history/journal row to the VFO while 1 is still
held; the main screen never sees that 1 (no digit typed), whether it is
released quickly or held on, and the next press of 1 works normally."""
from harness import *

KEY_1, KEY_INVALID = 1, 19
for name in sym:
    if name.startswith(('BK4819_', 'BK1080_', 'SETTINGS_', 'PY25Q16_', 'ST7565_', 'SPI_')):
        stub(name, lambda *a: ret())
for name in ['ToggleRX', 'RestoreRegisters', 'RADIO_SetupRegisters', 'SYSTEM_DelayMs', 'ST7565_BlitFullScreen',
             'FrequencyDraft', 'MAIN_FrequencyToVfo', 'BACKLIGHT_TurnOn']:
    if name in sym or any(k.startswith(name + '.') for k in sym):
        stub(name, lambda *a: ret())
seen = []
stub('MAIN_ProcessKeys', lambda k, p, h, *a: (k != 16 and seen.append((k, p, h)), ret()))  # PTT GPIO unmodelled
poll = [KEY_INVALID]
stub('KEYBOARD_Poll', lambda *a: ret(poll[0]))
put8(addr('gScreenToDisplay'), 0)  # DISPLAY_MAIN


def handover(held_ticks):
    seen.clear()
    put8(addr('vfoScan'), 0)
    put8(addr('scanListOnly'), 0)
    put32(addr('toVfoFrequency'), 14550000)
    call('DeInitSpectrum')
    poll[0] = KEY_1
    for _ in range(held_ticks):
        call('CheckKeys', count=2000000)
    poll[0] = KEY_INVALID
    for _ in range(10):
        call('CheckKeys', count=2000000)
    return list(seen)


for ticks in [0, 1, 5, 60, 200]:
    assert handover(ticks) == [], (ticks, seen)
poll[0] = KEY_1
for _ in range(5):
    call('CheckKeys', count=2000000)
poll[0] = KEY_INVALID
for _ in range(10):
    call('CheckKeys', count=2000000)
assert (KEY_1, 1, 0) in seen and (KEY_1, 0, 0) in seen, seen
print('PASS long 1 to VFO: the held 1 is never typed; the next 1 is a normal digit')
