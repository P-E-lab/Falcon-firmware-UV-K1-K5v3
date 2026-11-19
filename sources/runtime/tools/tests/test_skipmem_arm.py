"""Actual ARM: SKIP MEM. In Spectre/Sentinel band and range scans a detection
on a memory frequency (within SkipMemHys) is passed over; list scans, the VFO
scans and the Monitor CH channels are never skipped; key 7 toggles it."""
from harness import *

fields = {'band': 'SCAN_BAND_MODE', 'range': 'SCAN_RANGE_MODE', 'list': 'CHANNEL_MODE', 'k7': 'KEY_7',
          'ch': 'ADRESS_CHANNELS', 'first': 'MR_CHANNEL_FIRST'}
probe = B / 'skipmem_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'skipmem_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'skipmem_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

popups = []
stub('ShowOSDPopup', lambda p, *a: (popups.append(bytes(u.mem_read(p, 24)).split(b'\0')[0]), ret()))
for flag in ['historyListActive', 'sentinelView', 'vfoScan', 'MonitorIndex']:
    put8(addr(flag), 0)
# Memory 5 on 145.500 MHz (10 Hz units), the rest empty.
base = Q['ch'] + 5 * 16
flash[base:base + 4] = struct.pack('<I', 14550000)

def skip(f):
    return call('SkipMemory', f, count=20000000) & 0xFF
def hys(i):
    put8(addr('skipMemHys'), i)

put8(addr('Spectrum_state'), Q['band'])
put8(addr('skipMem'), 0); hys(4)
assert not skip(14550000)                                  # OFF: nothing skipped
put8(addr('skipMem'), 1)
assert skip(14550000) and skip(14550625) and skip(14549375)  # ±6.25 kHz
assert not skip(14551250) and not skip(14560000)
hys(0); assert skip(14550000) and not skip(14550100)       # exact only
hys(6); assert skip(14552500) and not skip(14552600)       # ±25 kHz
put8(addr('Spectrum_state'), Q['range']); assert skip(14550000)
put8(addr('Spectrum_state'), Q['list']); assert not skip(14550000)   # list scan: never
put8(addr('Spectrum_state'), Q['band'])
put8(addr('vfoScan'), 1); assert not skip(14550000); put8(addr('vfoScan'), 0)
put8(addr('MonitorIndex'), 2); assert not skip(14550000); put8(addr('MonitorIndex'), 0)  # Monitor CH
put8(addr('sentinelView'), 1); assert skip(14550000)       # Sentinel too
put8(addr('sentinelView'), 0)
print('PASS SKIP MEM: band/range skip memory frequencies within 0..25 kHz, never list/VFO scans or Monitor CH')

put8(addr('skipMem'), 0); popups.clear()
for n in ['ToggleRX', 'ResetScanStats', 'NextScanStep', 'SaveSettings', 'BK4819_WriteRegister']:
    stub(n, lambda *a: ret())
call('HandleKeySpectrum', Q['k7'], count=3000000); call('HandleKeySpectrum', Q['k7'], count=3000000)
assert popups == [b'SKIP MEM ON', b'SKIP MEM OFF'], popups
print('PASS key 7 toggles SKIP MEM with its popup')
