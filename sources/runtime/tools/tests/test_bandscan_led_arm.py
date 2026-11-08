"""Actual ARM: the Spectre entry primes the audio path with an RX on/off but
lights no LED: opening BAND SCAN or the History from the VFO shows no green
flash."""
from harness import *

lit = []
for n in sym:
    if n.startswith(('BK4819_WriteRegister', 'BK4819_ReadRegister', 'SYSTEM_DelayMs', 'ST7565_', 'BK1080_', 'SYSTICK_')):
        stub(n, lambda *a: ret())
stub('BK4819_ToggleGpioOut', lambda pin, on, *a: ((lit.append(pin) if on else None), ret()))
for n in ['BK4819_RX_TurnOn', 'RADIO_SetupRegisters', 'SETTINGS_SaveVfoNow', 'SETTINGS_InitEEPROM', 'BK4819_Init',
          'APP_StartListening', 'BACKLIGHT_TurnOn', 'BACKLIGHT_Update']:
    if n in sym or any(k.startswith(n + '.') for k in sym):
        stub(n, lambda *a: ret())
stub('LoadActiveBands', lambda *a: (put8(addr('bandCount'), 2), ret()))
ticks = [0]
def tick(*a):  # the real Tick runs after this: the 10 ms, display and LED slices are all due
    ticks[0] += 1
    for f in ['gNextTimeslice_10ms', 'gNextTimeslice_listening', 'gNextTimeslice_SCAN_LED',
              'gNextTimeslice_SCAN_LED_OFF', 'gNextTimeslice_display', 'gNextTimeslice_500ms']:
        if f in sym or any(k.startswith(f + '.') for k in sym):
            put8(addr(f), 1)
    if ticks[0] > 40:
        put8(addr('isInitialized'), 0)
stub('Tick', tick)
stub('KEYBOARD_Poll', lambda *a: ret(19))  # no key
for entry in ['APP_RunBandScan', 'APP_RunHistory']:
    ticks[0] = 0; lit.clear(); put8(addr('isInitialized'), 0)
    call(entry, count=200000000)
    assert ticks[0] > 40 and not lit, (entry, lit)
print('PASS BAND SCAN and History from the VFO open without any LED')
