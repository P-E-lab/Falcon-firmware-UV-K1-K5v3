"""Actual ARM: the received audio follows F4HWN v7 (no Sonic FM registers;
REG_28<8> is SetRxG, forced on by the RX expander; the compander only
switches REG_31<3>), SetRxG is saved in byte 0x00A0C0 as F4HWN v7, and the
FMESSAGE shortcut finds the app holding shortcut ID 15."""
from harness import *

# VFO field offsets, compiled with this build's definitions.
probe_c = B / 'audio_probe.c'
probe_c.write_text('#include "radio.h"\n#include <stddef.h>\n'
                   'const unsigned audio_layout[] __attribute__((section(".rodata.audio"))) = '
                   '{offsetof(VFO_Info_t, Modulation), offsetof(VFO_Info_t, Compander)};\n')
args = out[:-4] + ['-c', str(probe_c), '-o', str(B / 'audio_probe.o')]
subprocess.run(args, cwd=c['directory'], check=True)
MOD, COMP = struct.unpack('<2I', ELFFile(open(B / 'audio_probe.o', 'rb')).get_section_by_name('.rodata.audio').data())

regs = {}
def wreg(r, v, *unused):
    regs[r] = v
    ret()
stub('BK4819_WriteRegister', wreg)
stub('BK4819_ReadRegister', lambda r, *a: ret(regs.get(r, 0)))
for n in ['BK4819_SetAF', 'RADIO_SetupAGC', 'BK4819_SetFilterBandwidth', 'BK4819_SetRegValue']:
    if n in sym or any(k.startswith(n + '.') for k in sym):
        stub(n, lambda *a: ret())

FM, AM, USB = 0, 1, 2
vfo = eep + vfooff
u.mem_write(addr('gRxVfo'), struct.pack('<I', vfo))
u.mem_write(addr('gTxVfo'), struct.pack('<I', vfo))
rxg = addr('gSetting_set_rxg')

def modulate(mod, comp=0, gate=1):
    put8(vfo + MOD, mod); put8(vfo + COMP, comp); put8(rxg, gate)
    regs.clear(); regs[0x28] = 0x0B40  # the init value: HPF1 5, gate on, level 0x40
    call('RADIO_SetModulation', mod, count=5000000)
    return regs

for mod in (FM, AM, USB):
    r = modulate(mod)
    assert 0x2C not in r and 0x4A not in r, (mod, r)  # no Sonic FM registers
    assert r[0x28] == 0x0B40, hex(r[0x28])  # HPF1 and level kept, gate on
    assert modulate(mod, gate=0)[0x28] == 0x0A40  # SetRxG OFF: only bit 8 cleared
print('PASS FM, AM, USB: no Sonic FM registers; REG_28 keeps HPF1 and level, bit 8 = SetRxG', flush=True)
assert modulate(FM, comp=2, gate=0)[0x28] & 0x100  # RX expander: gate forced on
assert modulate(FM, comp=1, gate=0)[0x28] & 0x100 == 0  # TX compressor only: SetRxG decides
assert modulate(AM, comp=3, gate=0)[0x28] & 0x100 == 0  # the expander is FM only
print('PASS the RX expander (Compnd RX or TX+RX, FM) forces the gate on', flush=True)

# The compander only switches REG_31<3>, REG_28/29 untouched.
for mode in (0, 1, 2, 3):
    regs.clear(); regs[0x31] = 0x0004
    call('BK4819_SetCompander', mode)
    assert set(regs) == {0x31} and regs[0x31] == (0x000C if mode else 0x0004), (mode, regs)
print('PASS BK4819_SetCompander: REG_31<3> only (BK4829: REG_28/29 are gate and HPF1)', flush=True)

# SetRxG load/save: byte 0x00A0C0 as F4HWN v7, 0/1, anything else ON.
def wr(a, p, n, *unused):
    flash[a:a + n] = bytes(u.mem_read(p, n))
    ret()
stub('PY25Q16_WriteBuffer', wr)
for n in ['BK4819_SetCompander', 'RADIO_SetupRegisters', 'BACKLIGHT_SetBrightness', 'ST7565_SetContrast']:
    if n in sym or any(k.startswith(n + '.') for k in sym):
        stub(n, lambda *a: ret())
flash[0x00A160:0x00A170] = b'2.3.1.D1'.ljust(16, b'\0')  # Falcon settings already initialized
for stored, expect in [(0, 0), (1, 1), (0xFF, 1)]:
    flash[0x00A0C0] = stored
    call('SETTINGS_InitEEPROM', count=200000000)
    assert u.mem_read(rxg, 1)[0] == expect, (stored, u.mem_read(rxg, 1)[0])
put8(rxg, 0)
call('SETTINGS_SaveSettings', count=200000000)
assert flash[0x00A0C0] == 0
put8(rxg, 1)
call('SETTINGS_SaveSettings', count=200000000)
assert flash[0x00A0C0] == 1
print('PASS SetRxG read from and saved to 0x00A0C0 (0 OFF, 1 ON, anything else ON)', flush=True)

# FMESSAGE: the app holding shortcut ID 15 is launched; without it, a double beep.
launched, tones = [], []
stub('app_launch', lambda slot, *a: (launched.append(slot), ret(0)))
for n in ['BK4819_PlayToneRaw', 'BK4819_PrepareToPlayTone', 'RADIO_SetupRegisters']:
    if n in sym or any(k.startswith(n + '.') for k in sym):
        stub(n, (lambda name: lambda *a: (tones.append(name), ret()))(n))
call('APP_NotifySlotChanged')
call('ACTION_FMessage', count=200000000)
assert not launched and tones.count('BK4819_PlayToneRaw') == 2, (launched, tones)
install(Path('/tmp/claude-0/proto/fmessage/FMessage.app'), 3)
call('APP_NotifySlotChanged')
tones.clear()
call('ACTION_FMessage', count=200000000)
assert launched == [3] and not tones, (launched, tones)
print('PASS FMESSAGE: FMessage.app (header shortcut 15) launched from its slot; double beep without it', flush=True)
