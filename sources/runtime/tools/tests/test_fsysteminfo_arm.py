"""Actual ARM: FSystemInfo.app (Falcon System Info, rebuilt against the current
app API) installed in an Apps slot and run by the firmware loader: it opens,
draws its pages, scrolls with DOWN and quits with EXIT."""
import os
from harness import *

app = ROOT.parents[1] / 'sources/fsysteminfo/sysinfo/FSystemInfo.app'
h = install(app, 6)
assert call('app_read_validated_header', 6, 0x20006000) == 0
for n in ['BACKLIGHT_TurnOn', 'BACKLIGHT_Update', 'ST7565_BlitStatusLine', 'ST7565_BlitLine',
          'RADIO_SetupRegisters', 'RADIO_SelectVfos', 'RADIO_ConfigureChannel', 'FUNCTION_Select',
          'BOARD_ADC_GetBatteryInfo']:
    stub(n, lambda *a: ret())
for n in sym:
    if n.startswith(('BK4819_', 'BK1080_')):
        stub(n, lambda *a: ret(0))
KEY = {'DOWN': 12, 'EXIT': 13, None: 19}
script = [(None, 5)] + [('DOWN', 3), (None, 3)] * 6 + [('EXIT', 3)]
state = {'left': 0, 'i': 0, 'key': None}


def next_key():
    while state['left'] == 0:
        if state['i'] >= len(script):
            return KEY['EXIT']
        state['key'], state['left'] = script[state['i']]
        state['i'] += 1
    state['left'] -= 1
    return KEY[state['key']]


stub('KEYBOARD_GetKey', lambda *a: ret(next_key()))
tick = addr('gGlobalSysTickCounter')
stub('SYSTEM_DelayMs', lambda ms, *a: (put32(tick, get32(tick) + max(1, ms // 10)), ret()))
frames = []
stub('ST7565_BlitFullScreen', lambda *a: (frames.append(bytes(u.mem_read(addr('gFrameBuffer'), 7 * 128))), ret()))
put32(tick, 1000)
rc = call('app_launch', 6, count=2000000000)
assert rc == 0, rc
distinct = []
for f in frames:
    if any(f) and (not distinct or f != distinct[-1]):
        distinct.append(f)
assert len(distinct) >= 3, len(distinct)
out = os.environ.get('FSYS_PNG')
if out:
    from PIL import Image
    for k, f in enumerate([distinct[0], distinct[-1]]):
        img = Image.new('1', (128, 56), 1)
        for p in range(7):
            for x in range(128):
                for y in range(8):
                    if f[p * 128 + x] >> y & 1:
                        img.putpixel((x, p * 8 + y), 0)
        img.resize((512, 224)).save(os.path.join(out, 'fsys%d.png' % k))
print('PASS FSystemInfo: real loader, pages drawn, DOWN scrolls (%d distinct frames), EXIT quits' % len(distinct))
