"""Actual ARM: in frequency mode the VFO title is the name of the first memory
on its frequency ("CH-0012" without a name, bands excluded, 20 Hz apart at
most), searched once per frequency and after a flash write, never while that
VFO is scanned. SK2 in a VFO frequency or band scan blacklists the frequency
in the Spectre blacklist, which those scans then skip."""
from harness import *

lookups, scanning = [], [255]
names = {7: b'RELAIS'}
stub('SETTINGS_FetchChannelName', lambda p, ch, *a: (u.mem_write(p, names.get(ch, b'').ljust(11, b'\0')), ret()))
stub('APP_GetScanningVfo', lambda *a: ret(scanning[0]))
def listening(on):  # APP_VfoScanListening: a VFO scan stopped on a signal
    put8(addr('vfoScan'), on); put8(addr('isListening'), on)
a0 = addr('FindMemoryChannel')  # count the searches, then let them run
u.hook_add(UC_HOOK_CODE, lambda uc, a, sz, d: lookups.append(uc.reg_read(UC_ARM_REG_R0)), begin=a0, end=a0)
BUF = 0x20005800
def title(vfo, f):
    ok = call('UI_VfoMemoryName', BUF, vfo, f, vfo * 4, count=50000000) & 0xFF
    return bytes(u.mem_read(BUF, 12)).split(b'\0')[0] if ok else None

# The search: first memory, 20 Hz tolerance, band memories excluded.
def mem(ch, f):
    flash[CH_BASE + 16 * ch:CH_BASE + 16 * ch + 4] = struct.pack('<I', f)
mem(3, 11800833); mem(4, 43300000); mem(7, 14550000); mem(9, 14550000); mem(MR_MAX - 30, 14500000)
assert title(0, 14550000) == b'RELAIS' and lookups == [14550000]  # memory 8 (index 7), not 10
assert title(0, 11800835) == b'CH-0004'                          # 20 Hz apart, no name: its number
assert title(0, 11800836) is None                                 # 30 Hz apart: no memory
assert title(1, 14500000) is None                                 # the band memory is not a VFO memory
print('PASS search: first memory, 20 Hz apart at most, bands excluded; CH-xxxx without a name')

# The title: cached per frequency, refreshed after a flash write, not during a scan.
lookups.clear()
assert title(0, 14550000) == b'RELAIS' and lookups == [14550000]
assert title(0, 14550000) == b'RELAIS' and len(lookups) == 1  # cached
put8(addr('gFlashWrites'), u.mem_read(addr('gFlashWrites'), 1)[0] + 1)
assert title(0, 14550000) == b'RELAIS' and len(lookups) == 2  # a write may have changed it
assert title(1, 43300000) == b'CH-0005'
scanning[0] = 0; lookups.clear()
assert title(0, 14550000) is None and title(0, 14562500) is None and not lookups  # scanned: no search
listening(1)  # the scan stops on a signal: its memory name (already known: no new search)
assert title(0, 14550000) == b'RELAIS' and not lookups
assert title(0, 43300000) == b'CH-0005' and lookups == [43300000]
listening(0); lookups.clear()
scanning[0] = 255
assert title(0, 14550000) == b'RELAIS'  # after the scan: its own frequency
print('PASS title: once per frequency and after a write; while scanned, only on a reception')

# BAND SCAN: the band name at the start, then its last reception (name, or the
# frequency off the memories) until the next one; its initials at the right.
VB = addr('vfoBand')
put32(VB + 4, 13700000); u.mem_write(VB + 11, b'AIR\0'); put32(VB + 20, 0)  # stop, name, lastRx
put8(addr('vfoScan'), 1); scanning[0] = 0; lookups.clear()
drawn = []
for k in list(sym):
    if k.startswith('UI_DrawText5x5'):
        stub(k, lambda p, x, y, n, *a: (drawn.append((bytes(u.mem_read(p, n)), x, y)), ret()))
assert title(0, 12880000) == b'AIR' and not lookups      # scanning, no reception yet: the band
assert (b'AIR', 106, 9) in drawn, drawn                  # its initials where the list's are, no pill
put32(VB + 20, 14550000)                                 # last reception on a memory
assert title(0, 12880000) == b'RELAIS'                    # kept while it scans
put32(VB + 20, 12882500)
assert title(0, 12880000) == b'128.82500'                   # off the memories: the frequency
listening(1)
assert title(0, 12890000) == b'128.90000'                   # a new reception replaces it
listening(0); put8(addr('vfoScan'), 1)
drawn.clear(); assert title(1, 43300000) == b'CH-0005' and not drawn  # the other VFO: no band
put32(VB + 4, 0)                                          # a plain frequency scan: VFO A
assert title(0, 12880000) is None
put8(addr('vfoScan'), 0); scanning[0] = 255
print('PASS BAND SCAN title: band at the start, then the last reception kept; band initials at the right')

# SK2 blacklist in a VFO frequency/band scan: Measure skips the frequency.
f = {'sf': 'offsetof(ScanInfo,f)'}
probe = B / 'vfo_name_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned vfo_name_layout[]={' + ','.join(f.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'vfo_name_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'vfo_name_probe.o', 'rb') as fh:
    data = ELFFile(fh).get_section_by_name('.rodata.vfo_name_layout').data()
Q = dict(zip(f, struct.unpack('<%dI' % (len(data) // 4), data)))
measured = []
stub('GetRssi', lambda *a: (measured.append(1), ret(100)))
stub('Skip', lambda *a: ret())
put32(addr('lastReceivingFreq'), 14551250); u.mem_write(addr('indexFs'), b'\0\0')
call('Blacklist.part.0', count=5000000)
put32(addr('scanInfo') + Q['sf'], 14551250)
put8(addr('vfoScan'), 1); put8(addr('vfoRangeSession'), 1)
call('Measure', count=5000000); assert not measured, 'blacklisted frequency measured'
put8(addr('vfoRangeSession'), 0)  # a memory scan keeps its own session blacklist
call('Measure', count=5000000); assert measured
print('PASS SK2: the band/frequency scan skips a frequency of the Spectre blacklist')
