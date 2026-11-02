"""Actual ARM: PONSCR is stored in bits 3..5 of 0xA00E (F4HWN's TAIL_TONE /
NFM / VFO_OPEN bits 0..2 untouched), set from the menu, restored after a cold
settings reload, and an erased or out-of-range byte reads NORMAL. Changing the
RX mode ends a reception: ML WATCH stops and the receiver leaves RX."""
from harness import *
from pathlib import Path

fields = {'pon': 'offsetof(EEPROM_Config_t,POWER_ON_SCREEN)', 'count': 'POWER_ON_SCREEN_COUNT',
          'fm': 'POWER_ON_SCREEN_FM', 'fg': 'FUNCTION_FOREGROUND', 'rx': 'FUNCTION_RECEIVE',
          'inc': 'FUNCTION_INCOMING', 'single': 'RX_MODE_SIMPLE'}
probe = B / 'ponscr_probe.c'
probe.write_text('#include <stddef.h>\n#include "settings.h"\n#include "functions.h"\nconst unsigned ponscr_layout[]={' +
                 ','.join(fields.values()) + '};')
cmd = out[:]
cmd[cmd.index('-c') + 1] = str(probe)
cmd[cmd.index('-o') + 1] = str(B / 'ponscr_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'ponscr_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.ponscr_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))
assert Q['count'] == 6


def get8(a): return u.mem_read(a, 1)[0]


for n in ['RADIO_SetupRegisters', 'RADIO_ConfigureChannel', 'RADIO_SelectVfos', 'SETTINGS_SaveVfoIndices',
          'ST7565_BlitFullScreen', 'SYSTEM_DelayMs', 'GPIO_DisableAudioPath', 'GPIO_EnableAudioPath',
          'BK4819_WriteRegister', 'BK4819_ToggleGpioOut', 'BACKLIGHT_TurnOn', 'BACKLIGHT_Update']:
    stub(n, lambda *a: ret())
stub('BK4819_ReadRegister', lambda *a: ret(0))


def save(a, p, n, *unused):
    flash[a:a + n] = u.mem_read(p, n)
    ret()


stub('PY25Q16_WriteBuffer', save)

# Menu -> gEeprom -> flash bits 3..5, other bits of the byte preserved.
resource = store_entry(0)[0]
menu_offset = resource + addr('MenuList') - 0x91000000
rows = [store[menu_offset + i * 8:menu_offset + (i + 1) * 8] for i in range(menu_count)]
at = next(n for n, row in enumerate(rows) if row[:7].split(b'\0')[0] == b'PONSCR')
assert rows[at - 1][:6] == b'POnMsg'  # just below POnMsg
put8(addr('gCurrentFunction'), Q['fg'])
low = None  # the F4HWN bits 0..2 must not depend on PONSCR
flash[0xA00E] = 0x07
call('SETTINGS_InitEEPROM', count=6000000)
call('SETTINGS_SaveSettings')  # settle the settings mirrored in other blocks
for value in range(Q['count']):
    flash[0xA00E] = 0x07  # STE, NFM and VFO_OPEN set, PONSCR NORMAL
    call('SETTINGS_InitEEPROM', count=6000000)
    put8(addr('gMenuCursor'), at)
    put32(addr('gSubMenuSelection'), value)
    call('MENU_AcceptSetting')
    assert get8(eep + Q['pon']) == value
    call('SETTINGS_SaveSettings')
    low = flash[0xA00E] & 0x07 if low is None else low
    assert flash[0xA00E] & 0x07 == low and flash[0xA00E] >> 3 == value, hex(flash[0xA00E])
    put8(eep + Q['pon'], 0xEE)
    call('SETTINGS_InitEEPROM', count=6000000)
    assert get8(eep + Q['pon']) == value
print('PASS PONSCR: menu row below POnMsg, six values saved in 0xA00E bits 3..5 and restored', flush=True)

for raw in [0xFF, 0x30 | 0x07, 0x38]:  # erased, value 6, value 7
    flash[0xA00E] = raw
    call('SETTINGS_InitEEPROM', count=6000000)
    assert get8(eep + Q['pon']) == 0, hex(raw)
print('PASS erased or out-of-range PONSCR reads NORMAL', flush=True)

# RX mode change during a reception.
selected = []
stub('FUNCTION_Select', lambda f, *a: (selected.append(f), put8(addr('gCurrentFunction'), f), ret()))
for function in [Q['rx'], Q['inc']]:
    put8(addr('vfoScan'), 1)
    put8(addr('scanListOnly'), 0x80)  # ML WATCH
    put8(addr('isInitialized'), 1)
    put8(addr('isListening'), 1)
    put8(addr('gCurrentFunction'), function)
    selected.clear()
    call('ACTION_SetRxMode', Q['single'], count=6000000)
    assert get8(addr('isInitialized')) == 0, 'ML WATCH still running'
    assert get8(addr('gCurrentFunction')) == Q['fg'] and Q['fg'] in selected, selected
print('PASS RX mode change: ML WATCH stops and the reception ends', flush=True)

# Power-up dispatch: the actual main() after MainInit, up to the first loop step.
fields2 = {'list': 'CHANNEL_MODE', 'band': 'SCAN_BAND_MODE', 'range': 'SCAN_RANGE_MODE'}
probe.write_text('#include <stddef.h>\n#include "app/spectrum.h"\nconst unsigned ponscr_layout2[]={' +
                 ','.join(fields2.values()) + '};')
cmd[cmd.index('-o') + 1] = str(B / 'ponscr_probe2.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'ponscr_probe2.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.ponscr_layout2').data()
M = dict(zip(fields2, struct.unpack('<3I', data)))
events = []
# main() up to MainInit configures clocks and the stack watermark: start right
# after its call to MainInit, in main's own frame.
objdump = Path(cmd[0]).with_name(Path(cmd[0]).name.replace('gcc', 'objdump'))
listing = subprocess.run([str(objdump), '-d', '--no-show-raw-insn', str(B / 'falcon.data.elf')],
                         capture_output=True, text=True, check=True).stdout
body = listing[listing.index('<main>:'):]
body = body[:body.index('\n\n')]
line = next(l for l in body.splitlines() if '<MainInit' in l)
after_init = int(line.split(':')[0], 16) + 4



stub('MainInit', lambda *a: ret())
stub('SelectScanProfile', lambda sentinel, *a: (events.append(('profile', sentinel & 1)), ret()))
stub('APP_RunSpectrum', lambda *a: (events.append(('scan', get8(addr('Spectrum_state')))), ret()))
stub('ACTION_FM', lambda *a: (events.append(('fm',)), ret()))


def loop_reached(uc, access, address, size, value, data):
    events.append(('loop',))
    uc.emu_stop()


u.hook_add(UC_HOOK_MEM_READ, loop_reached, begin=addr('gNextTimeslice'), end=addr('gNextTimeslice'))
expected = [[], [('profile', 0), ('scan', M['list'])], [('profile', 0), ('scan', M['band'])],
            [('profile', 0), ('scan', M['range'])], [('profile', 1)], [('fm',)]]
for value in range(Q['count']):
    for held, reduced in [(0, 0), (1, 0), (0, 1)]:
        events.clear()
        put8(eep + Q['pon'], value)
        put8(addr('bootKeysHeld'), held)
        put8(addr('gReducedService'), reduced)
        put8(addr('Spectrum_state'), 0x55)
        put8(addr('gNextTimeslice'), 1)
        u.reg_write(UC_ARM_REG_SP, 0x20004000)
        u.emu_start(after_init | 1, 0, count=20000000)
        got = [e for e in events if e != ('loop',)]
        want = [] if held or reduced else expected[value]
        if value == 4 and want:  # Sentinel restores its own last mode
            assert got[:1] == want and got[1][0] == 'scan', got
        else:
            assert got == want, (value, held, reduced, got)
        assert events[-1] == ('loop',)
print('PASS power-up: PONSCR opens Spectre LIST/BAND/RANGE, Sentinel or FM; NORMAL, a held key or a low battery keep the VFO', flush=True)
