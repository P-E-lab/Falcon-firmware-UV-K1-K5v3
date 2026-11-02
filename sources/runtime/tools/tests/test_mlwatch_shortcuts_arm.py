"""Actual ARM: an F shortcut leaves ML WATCH with the F modifier kept, so the
main screen runs it as in VFO (F+0 FM, F+5 apps, F+6..F+9 scanners, F+SK1/SK2
step, F+EXIT monitor); leaving without a key (no handoff) clears it."""
from harness import *

for name in sym:
    if name.startswith(('BK4819_', 'BK1080_')):
        stub(name, lambda *a: ret())
for name in ['ToggleRX', 'RestoreRegisters', 'RADIO_SetupRegisters', 'SYSTEM_DelayMs', 'ST7565_BlitFullScreen']:
    stub(name, lambda *a: ret())
KEYS = {name: i for i, name in enumerate(['KEY_0', 'KEY_1', 'KEY_2', 'KEY_3', 'KEY_4', 'KEY_5', 'KEY_6',
                                          'KEY_7', 'KEY_8', 'KEY_9', 'KEY_MENU', 'KEY_UP', 'KEY_DOWN',
                                          'KEY_EXIT', 'KEY_STAR', 'KEY_F', 'KEY_PTT', 'KEY_SIDE2',
                                          'KEY_SIDE1', 'KEY_INVALID'])}


def leave_ml(key, ml=True):
    put8(addr('vfoScan'), 1 if ml else 0)
    put8(addr('scanListOnly'), 0x80 if ml else 0)
    put8(addr('gWasFKeyPressed'), 1)
    put8(addr('kbd'), key)                 # kbd.current
    call('DeInitSpectrum')
    return u.mem_read(addr('gWasFKeyPressed'), 1)[0]


for key in list(range(KEYS['KEY_0'], KEYS['KEY_9'] + 1)) + [KEYS['KEY_SIDE1'], KEYS['KEY_SIDE2'],
                                                             KEYS['KEY_EXIT']]:
    assert leave_ml(key) == 1, key
assert leave_ml(KEYS['KEY_INVALID']) == 0
print('PASS ML WATCH: F shortcuts keep F for the main screen; other exits clear it')
