"""Actual ARM: FGuide.app installed in an Apps slot and run by the firmware
loader. Home lists the 10 sections; arrows move, MENU or a digit opens a
section, arrows scroll it (held arrows repeat), EXIT goes back, EXIT quits.
Screens are rendered to PNG next to the build for review."""
import os
from harness import *

app = ROOT.parents[1] / 'sources/fguide/guide/FGuide.app'
h = install(app, 5)
assert call('app_read_validated_header', 5, 0x20006000) == 0
for n in ['BACKLIGHT_TurnOn', 'BACKLIGHT_Update', 'ST7565_BlitStatusLine', 'ST7565_BlitLine',
          'RADIO_SetupRegisters', 'RADIO_SelectVfos', 'RADIO_ConfigureChannel', 'FUNCTION_Select']:
    stub(n, lambda *a: ret())
for n in sym:
    if n.startswith(('BK4819_', 'BK1080_')):
        stub(n, lambda *a: ret(0))

KEY = {'0': 0, '1': 1, '3': 3, 'MENU': 10, 'UP': 11, 'DOWN': 12, 'EXIT': 13, 'PTT': 16, None: 19}
script = []      # (key, polls held)
shots, frames = {}, []
state = {'key': None, 'left': 0, 'i': 0}


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


statuses = []


def blit(*a):
    frames.append(bytes(u.mem_read(addr('gFrameBuffer'), 7 * 128)))
    statuses.append(bytes(u.mem_read(addr('gStatusLine'), 128)))
    ret()


stub('ST7565_BlitFullScreen', blit)


def press(key, polls=3):
    script.append((key, polls))
    script.append((None, 3))


def png(frame, name):
    out = os.environ.get('FGUIDE_PNG')
    if not out:
        return
    from PIL import Image
    full = statuses[frames.index(frame)] + frame  # the status line, then the framebuffer
    img = Image.new('1', (128, 64), 1)
    for p in range(8):
        for x in range(128):
            for y in range(8):
                if full[p * 128 + x] >> y & 1:
                    img.putpixel((x, p * 8 + y), 0)
    img.resize((512, 256)).save(os.path.join(out, name))


def lit(frame, x, y): return frame[(y >> 3) * 128 + x] >> (y & 7) & 1


# Home, DOWN x2, digit 0 (section 10), DOWN held to its end (text past the
# assets, read from the app code), EXIT, EXIT.
press('DOWN'); press('DOWN'); press('0')
script.append(('DOWN', 220)); script.append((None, 3))
press('EXIT'); press('EXIT')
put32(tick, 1000)
rc = call('app_launch', 5, count=2000000000)
assert rc == 0, rc
home = frames[0]
png(home, 'fguide_home.png')
png(frames[2], 'fguide_home_sel3.png')
# Selection bar: first row (y 0..6), then third row (y 14..20).
assert lit(home, 0, 1) and not lit(frames[2], 0, 1) and lit(frames[2], 0, 15)
distinct = []
for f in frames[3:-1]:
    if not distinct or f != distinct[-1]:
        distinct.append(f)
png(distinct[0], 'fguide_page10.png')
png(distinct[-1], 'fguide_page10_end.png')
assert len(distinct) == 23 - 8 + 1, len(distinct)  # every scroll step down to the last line
status = bytes(u.mem_read(addr('gStatusLine'), 128))
assert any(status[30:98]) and any(status[104:]), 'centered title and battery percent'
print('PASS FGuide: real loader, home list and selection, digit opens a section, held arrow scrolls to its '
      'last line, EXIT back and quit; %d frames' % len(frames))
