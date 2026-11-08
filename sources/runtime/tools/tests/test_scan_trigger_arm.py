"""Actual ARM: the scan trigger. Sentinel keeps its DS setting; Spectre, the
VFO scans and the History opened from Spectre or the VFO use a fixed rise of 5,
whatever DS is (DS 20 used to block weak signals there)."""
from harness import *

fields = {'trig': 'offsetof(SpectrumSettings,rssiTriggerLevelUp)', 'sf': 'offsetof(ScanInfo,f)'}
probe = B / 'scan_trigger_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'scan_trigger_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'scan_trigger_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

for n in ['SYSTICK_DelayUs', 'SYSTEM_DelayMs', 'BK4819_WriteRegister', 'BK4819_ToggleGpioOut']:
    stub(n, lambda *a: ret())
stub('BK4819_GetExNoiseIndicator', lambda *a: ret(0))
reads = []
stub('BK4819_GetRSSI', lambda *a: ret(reads.pop(0)))


def triggers(sentinel, vfo, session, ds, rise):
    """One Measure() after a reference of 100, rising by `rise`, confirmed well above."""
    put8(addr('sentinelView'), sentinel); put8(addr('vfoScan'), vfo); put8(addr('historySession'), session)
    put8(addr('historyListActive'), session); put16(addr('settings') + Q['trig'], ds)
    put8(addr('gIsPeak'), 0); put8(addr('isListening'), 0)
    put8(addr('rssiReferenceValid'), 1); put16(addr('previousRssi'), 100)
    put32(addr('scanInfo') + Q['sf'], 14512500)
    reads[:] = [100 + rise, 100 + rise + 40]
    call('Measure')
    return u.mem_read(addr('gIsPeak'), 1)[0] == 1


put8(addr('gSpectrumSQL'), 0); put8(addr('gVfoScanSQL'), 0)  # dBm squelch fully open
# Spectre, DS 20: a rise of 8 now triggers; the old rule needed 20.
assert triggers(0, 0, 0, 20, 8) and not triggers(0, 0, 0, 20, 4)
# VFO scan (ML WATCH / F+* / long *), DS 20: fixed 5 as well.
assert triggers(1, 1, 0, 20, 8)
# History from the VFO (saved view Sentinel), DS 20: fixed 5.
assert triggers(1, 0, 1, 20, 8)
# History from Spectre, DS 50 (record only in Sentinel): still triggers.
assert triggers(0, 0, 0, 50, 8)
# Sentinel keeps DS: rise 8 blocked at DS 20, accepted at DS 5.
assert not triggers(1, 0, 0, 20, 8) and triggers(1, 0, 0, 5, 8)
# History shown over a running Spectre scan keeps the Spectre dBm squelch:
# a weak rise (an internal spur) under the squelch line does not stop it.
def over_spectre(history_scan, rise):
    put8(addr('gHistoryScan'), history_scan)
    put8(addr('sentinelView'), 0); put8(addr('vfoScan'), 0); put8(addr('historySession'), 0)
    put8(addr('historyListActive'), 1); put16(addr('settings') + Q['trig'], 20)
    put8(addr('gIsPeak'), 0); put8(addr('isListening'), 0)
    put8(addr('rssiReferenceValid'), 1); put16(addr('previousRssi'), 100)
    put32(addr('scanInfo') + Q['sf'], 14512500)
    reads[:] = [100 + rise, 100 + rise + 40]
    call('Measure')
    return u.mem_read(addr('gIsPeak'), 1)[0] == 1
put8(addr('gSpectrumSQL'), 120)  # squelch line far above the signal
assert not over_spectre(0, 8)
assert triggers(0, 0, 0, 20, 8) is False  # same as Spectre without the History
# The History's own scan (long *) and the History from the VFO ignore it.
assert over_spectre(1, 8)
assert triggers(1, 0, 1, 20, 8)
put8(addr('gHistoryScan'), 0); put8(addr('gSpectrumSQL'), 0)
assert over_spectre(0, 8)  # squelch open: triggers as before
print('PASS scan trigger: Spectre, VFO scans and History from Spectre/VFO use 5; Sentinel keeps DS; History over Spectre keeps the dBm squelch')
