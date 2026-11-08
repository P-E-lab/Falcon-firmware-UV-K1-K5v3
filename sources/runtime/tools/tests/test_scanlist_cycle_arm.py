"""Actual ARM: scanlist up/down in the VFO memory mode go through lists 1..25,
then the memories without a list, then the monitor memories (list 51), then
the band memories (told apart by their number), and back to list 1; empty
groups are skipped. The VFO screen shows NONE, MONI and BAND for them."""
from harness import *

fields = {'scr': 'offsetof(EEPROM_Config_t,ScreenChannel)', 'mr': 'offsetof(EEPROM_Config_t,MrChannel)',
          'tx': 'offsetof(EEPROM_Config_t,TX_VFO)'}
probe = B / 'scanlist_cycle_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/settings.h') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'scanlist_cycle_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'scanlist_cycle_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

# memory -> scanlist: lists 1, 2 and 25, no list, monitor; list 3 is empty.
BAND0 = MR_MAX - 30
lists = {10: 2, 11: 1, 12: 25, 13: 0, 14: 51, 15: 1, 16: 51, 17: 0, BAND0: 0, BAND0 + 3: 0}
stub('RADIO_CheckValidChannel', lambda ch, *a: ret(1 if ch in lists else 0))
mem = 0x20005000
def attr(ch, *a):
    u.mem_write(mem, struct.pack('<H', (lists.get(ch, 0) << 8) | 0)); ret(mem)
stub('MR_GetChannelAttributes', attr)
ee = addr('gEeprom')
put8(ee + Q['tx'], 0)
def screen(): return struct.unpack('<H', u.mem_read(ee + Q['scr'], 2))[0]
def press(name, ch):
    u.mem_write(ee + Q['scr'], struct.pack('<H', ch)); u.mem_write(ee + Q['mr'], struct.pack('<H', ch))
    call(name, count=5000000)
    return screen()

# Up from list 1 (11): list 2 (10), list 25 (12), no list (13), monitor (14), list 1 (11).
seq, ch = [], 11
for _ in range(6):
    ch = press('ACTION_ScanListUp', ch); seq.append(ch)
assert seq == [10, 12, 13, 14, BAND0, 11], seq
# Down retraces it: list 1 -> monitor -> no list -> list 25 -> list 2 -> list 1.
seq, ch = [], 11
for _ in range(6):
    ch = press('ACTION_ScanListDown', ch); seq.append(ch)
assert seq == [BAND0, 14, 13, 12, 10, 11], seq
# Landing in the NONE step tells the arrows to go on in the NONE step.
def none_mark(): return struct.unpack('<H', u.mem_read(addr('memoryNone'), 2))[0]
assert press('ACTION_ScanListUp', 12) == 13 and none_mark() == 14
assert press('ACTION_ScanListUp', 13) == 14 and none_mark() == 0
print('PASS scanlist cycle: lists, NONE, MONI, BAND, back to list 1; both ways; empty lists skipped')

# The VFO screen: a list number at x 109, or NONE / MONI / BAND against the right edge.
stub('UI_DrawScanListBubble', lambda *a: ret())
def label(ch):
    u.mem_write(addr('gFrameBuffer'), bytes(7 * 128))
    call('UI_DrawMemoryList', ch, 2, count=5000000)
    fb = bytes(u.mem_read(addr('gFrameBuffer'), 7 * 128))
    return [x for x in range(128) if fb[2 * 128 + x] or fb[3 * 128 + x]]
for ch in (13, 14, BAND0):
    cols = label(ch)
    assert cols and cols[-1] >= 126 and cols[0] >= 104, (ch, cols)  # the narrow I of MONI ends at 126
cols = label(11)
assert cols and cols[0] >= 109 and cols[-1] < 127, cols
from PIL import Image
im = Image.new('1', (128, 24), 1)
for i, ch in enumerate((11, 13, 14)):
    u.mem_write(addr('gFrameBuffer'), bytes(7 * 128)); call('UI_DrawMemoryList', ch, 0, count=5000000)
    fb = bytes(u.mem_read(addr('gFrameBuffer'), 128))
    for x in range(128):
        for y in range(8):
            if fb[x] >> y & 1: im.putpixel((x, i * 8 + y), 0)
im.resize((512, 96)).save(B / 'vfo-list-labels.png')
print('PASS VFO screen: list number at x 109; NONE, MONI, BAND end on the right edge')
