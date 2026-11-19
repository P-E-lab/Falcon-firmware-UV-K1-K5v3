"""Actual ARM: the SPECTRE VFO shortcut (action 31, F1/F2/M long, FKeys) opens
Spectre in range mode +-800 kHz around the VFO frequency with 25 kHz steps,
and puts the saved range, step and mode back afterwards."""
from harness import *

fields = {'range': 'SCAN_RANGE_MODE', 'band': 'SCAN_BAND_MODE', 's25': 'STEP_25kHz', 's125': 'STEP_12_5kHz',
          'step': 'offsetof(SpectrumSettings,scanStepIndex)', 'act': 'ACTION_OPT_SPECTRE_VFO',
          'prx': 'offsetof(VFO_Info_t,pRX)', 'freq': 'offsetof(FREQ_Config_t,Frequency)'}
probe = B / 'svfo_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'svfo_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'svfo_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

st = addr('settings') + Q['step']
def state(): return (get32(addr('RangeStart')), get32(addr('RangeStop')), u.mem_read(st, 1)[0],
                     u.mem_read(addr('Spectrum_state'), 1)[0])
seen, saved = [], []
stub('APP_RunSpectrum', lambda *a: (seen.append(state()), ret()))
stub('SaveSettings', lambda *a: (saved.append(state()), ret()))
def profile(*a):
    put32(addr('RangeStart'), 43000000); put32(addr('RangeStop'), 44000000)
    put8(st, Q['s125']); put8(addr('Spectrum_state'), Q['band']); ret()
stub('SelectScanProfile', profile)
VFO, FRQ = 0x20005c00, 0x20005d00
put32(VFO + Q['prx'], FRQ); put32(FRQ + Q['freq'], 14550000); put32(addr('gTxVfo'), VFO)

call('APP_RunSpectreVfo', count=20000000)
assert seen == [(14470000, 14630000, Q['s25'], Q['range'])], seen
assert saved[-1] == (43000000, 44000000, Q['s125'], Q['band']), saved
assert Q['act'] == 31
print('PASS SPECTRE VFO: range 144.700-146.300 (25 kHz) around 145.500, saved range/step/mode restored')
