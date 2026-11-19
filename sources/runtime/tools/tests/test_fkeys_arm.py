"""Actual ARM: FKeys. The VFO screen's long and F+ presses follow the table in
the slot's FKeys sector (0xE000): a shortcut action, a native long/F+ function
of another key, or DEFAULT (the Falcon function); the rest of a reassigned
press is consumed; nothing changes with an erased sector, a locked keypad or
another screen."""
from harness import *

fields = {'sq': 'offsetof(EEPROM_Config_t,SQUELCH_LEVEL)', 'vox': 'offsetof(EEPROM_Config_t,VOX_SWITCH)',
          'lock': 'offsetof(EEPROM_Config_t,KEY_LOCK)', 'main': 'DISPLAY_MAIN', 'menu': 'DISPLAY_MENU',
          'fk': 'ADRESS_FKEYS'}
probe = B / 'fkeys_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/settings.h') + '"\n#include "' +
                 str(ROOT / 'App/app/spectrum.h') + '"\n#include "' + str(ROOT / 'App/ui/ui.h') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'fkeys_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'fkeys_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

E = addr('gEeprom')
def sq(): return u.mem_read(E + Q['sq'], 1)[0]
def vox(): return u.mem_read(E + Q['vox'], 1)[0]
def fk(key, pressed, held): return call('FKEYS_Intercept', key, pressed, held, count=20000000) & 0xFF
def load(long_, fplus):
    t = bytearray(b'\xff' * 32)
    for k, v in long_.items(): t[k] = v
    for k, v in fplus.items(): t[16 + k] = v
    flash[Q['fk']:Q['fk'] + 37] = b'FKY1\x01' + bytes(t)
    call('FKEYS_Load', count=2000000)

for n in ['SETTINGS_SaveSettings', 'BK4819_WriteRegister', 'RADIO_ConfigureSquelchAndOutputPower']:
    stub(n, lambda *a: ret())
put8(addr('gScreenToDisplay'), Q['main']); put8(addr('gInputBoxIndex'), 0); put8(E + Q['lock'], 0)
put8(addr('gWasFKeyPressed'), 0)

flash[Q['fk']:Q['fk'] + 37] = b'\xff' * 37; call('FKEYS_Load', count=2000000)
assert not fk(7, 1, 1) and not fk(1, 1, 0)
print('PASS erased FKeys sector: every press keeps its Falcon function')

load({7: 0x88, 5: 0xFF}, {2: 26})
put8(E + Q['sq'], 3)
assert fk(7, 1, 1) and sq() == 4           # long 7 -> the native long 8 (squelch +1)
assert fk(7, 1, 1) and fk(7, 0, 1)         # held repeats and the release are consumed
assert sq() == 4
assert not fk(7, 1, 0)                     # next short press of 7 is not FKeys'
assert not fk(5, 1, 1)                     # DEFAULT entry: native long 5
v = vox(); put8(addr('gWasFKeyPressed'), 1)
assert fk(2, 1, 0) and vox() == (v ^ 1)    # F+2 -> VOX action
assert u.mem_read(addr('gWasFKeyPressed'), 1)[0] == 0
assert fk(2, 0, 0)                         # its release consumed
print('PASS long 7 = native squelch, F+2 = VOX action, DEFAULT untouched, rest of the press consumed')

put8(E + Q['lock'], 1); assert not fk(7, 1, 1); put8(E + Q['lock'], 0)
put8(addr('gScreenToDisplay'), Q['menu']); assert not fk(7, 1, 1); put8(addr('gScreenToDisplay'), Q['main'])
put8(addr('gInputBoxIndex'), 2); assert not fk(7, 1, 1); put8(addr('gInputBoxIndex'), 0)
print('PASS locked keypad, other screens and a digit entry keep the Falcon keys')
