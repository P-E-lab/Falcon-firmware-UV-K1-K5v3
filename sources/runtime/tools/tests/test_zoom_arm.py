"""Actual ARM: the Spectre zoom on key 4. Short presses go ±800, 400, 200,
100, 50 kHz around the last received frequency in a loop, from the first level
narrower than the scan shown; EXIT goes back to the scan that was running;
7 and the exit never save the zoom."""
from harness import *

fields = {'step': 'offsetof(SpectrumSettings,scanStepIndex)', 'range': 'SCAN_RANGE_MODE',
          'band': 'SCAN_BAND_MODE', 's25': 'STEP_25kHz', 's125': 'STEP_12_5kHz', 's625': 'STEP_6_25kHz',
          's25b': 'STEP_2_5kHz', 's1': 'STEP_1kHz', 's833': 'STEP_8_33kHz', 'k4': 'KEY_4', 'k7': 'KEY_7',
          'k6': 'KEY_6', 'kx': 'KEY_EXIT', 'mod': 'offsetof(SpectrumSettings,modulationType)', 'list': 'CHANNEL_MODE', 'am': 'MODULATION_AM'}
probe = B / 'zoom_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'zoom_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'zoom_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

popups = []
stub('ShowOSDPopup', lambda p, *a: (popups.append(bytes(u.mem_read(p, 24)).split(b'\0')[0]), ret()))
saved = []
stub('SaveSettings', lambda *a: (saved.append((get32(addr('RangeStart')), get32(addr('RangeStop')),
                                               u.mem_read(addr('Spectrum_state'), 1)[0])), ret()))
for n in ['ToggleRX', 'ResetScanStats', 'NextScanStep', 'BK4819_WriteRegister', 'SYSTEM_DelayMs']:
    stub(n, lambda *a: ret())
for flag in ['historyListActive', 'sentinelView', 'vfoScan', 'isListening', 'zoomLevel']:
    put8(addr(flag), 0)
st = addr('settings') + Q['step']
def zoom(): return u.mem_read(addr('zoomLevel'), 1)[0]
def rng(): return get32(addr('RangeStart')), get32(addr('RangeStop'))
def key(k): call('HandleKeySpectrum', k, count=3000000)

# Running a BAND scan at 8.33 kHz; last reception 118.700 MHz.
put8(addr('Spectrum_state'), Q['band']); put8(st, Q['s833'])
put32(addr('RangeStart'), 11800000); put32(addr('RangeStop'), 13700000)
put32(addr('SpectrumRangeStart'), 11800000); put32(addr('SpectrumRangeStop'), 13700000)
put32(addr('lastReceivingFreq'), 11870000); put8(addr('zoomRx'), 1)  # a reception in this session
put8(addr('settings') + Q['mod'], Q['am']); put8(addr('rangeModConfigured'), 0)
want = [(80000, Q['s25']), (40000, Q['s125']), (20000, Q['s625']), (10000, Q['s25b']), (5000, Q['s1'])]
for level, (half, step) in enumerate(want, 1):
    popups.clear(); key(Q['k4'])
    assert popups == ([b'ZOOM'] if level == 1 else []), popups  # the popup only on entry
    assert zoom() == level and rng() == (11870000 - half, 11870000 + half), (level, rng())
    assert u.mem_read(addr('Spectrum_state'), 1)[0] == Q['range'] and u.mem_read(st, 1)[0] == step
    # the zoomed range keeps AM (an unconfigured range would take the VFO's FM)
    assert u.mem_read(addr('rangeModConfigured'), 1)[0] == 1 and u.mem_read(addr('rangeModulation'), 1)[0] == Q['am']
# 7 in the zoom saves the band scan, not the zoom.
key(Q['k7'])
assert saved[-1] == (11800000, 13700000, Q['band']), saved
assert zoom() == 5 and rng() == (11865000, 11875000)
key(Q['k4'])  # after ±50: around again at ±800, still zoomed
assert zoom() == 1 and rng() == (11790000, 11950000) and u.mem_read(st, 1)[0] == Q['s25']
key(Q['kx'])  # EXIT: back to the band scan
assert zoom() == 0 and rng() == (11800000, 13700000)
assert u.mem_read(addr('Spectrum_state'), 1)[0] == Q['band'] and u.mem_read(st, 1)[0] == Q['s833']
assert u.mem_read(addr('rangeModConfigured'), 1)[0] == 0  # the range's own setting is back
# A narrow range: 446.000-446.200 (200 kHz) starts at ±50, the first narrower level, then loops.
put8(addr('Spectrum_state'), Q['range'])
put32(addr('RangeStart'), 44600000); put32(addr('RangeStop'), 44620000)
put32(addr('SpectrumRangeStart'), 44600000); put32(addr('SpectrumRangeStop'), 44620000)
put32(addr('lastReceivingFreq'), 44610000)
key(Q['k4'])
assert zoom() == 5 and rng() == (44605000, 44615000) and u.mem_read(st, 1)[0] == Q['s1'], (zoom(), rng())
key(Q['k4'])
assert zoom() == 1 and rng() == (44530000, 44690000)
key(Q['kx']); assert zoom() == 0 and rng() == (44600000, 44620000)
# A 400 kHz range starts at ±100 (200 kHz wide).
put32(addr('SpectrumRangeStop'), 44640000); key(Q['k4'])
assert zoom() == 4 and rng() == (44600000, 44620000), (zoom(), rng())
key(Q['kx'])
put8(addr('Spectrum_state'), Q['band'])
put32(addr('RangeStart'), 11800000); put32(addr('RangeStop'), 13700000)
put32(addr('SpectrumRangeStart'), 11800000); put32(addr('SpectrumRangeStop'), 13700000)
put32(addr('lastReceivingFreq'), 11870000)
# 6 leaves the zoom first, then changes mode as before.
key(Q['k4']); key(Q['k6'])
assert zoom() == 0 and rng() == (11800000, 13700000)
# EXIT leaves the zoom, back to the band scan, and stays in Spectre.
exits = []
stub('DeInitSpectrum', lambda *a: (exits.append(1), ret()))
before = u.mem_read(addr('Spectrum_state'), 1)[0], rng()
key(Q['k4']); key(Q['k4']); key(Q['kx'])
assert zoom() == 0 and (u.mem_read(addr('Spectrum_state'), 1)[0], rng()) == before and not exits
key(Q['kx']); assert exits  # a second EXIT leaves Spectre
# No reception yet: no zoom.
put32(addr('lastReceivingFreq'), 0); popups.clear(); key(Q['k4'])
assert zoom() == 0 and popups[-1] == b'NO SIGNAL YET', popups
# A remembered frequency from before this session is not a signal either.
put32(addr('lastReceivingFreq'), 11870000); put8(addr('zoomRx'), 0); popups.clear(); key(Q['k4'])
assert zoom() == 0 and popups[-1] == b'NO SIGNAL YET', popups
put8(addr('zoomRx'), 1)
# List scan: key 4 does nothing (the waterfall stays available there).
put32(addr('lastReceivingFreq'), 11870000); put8(addr('Spectrum_state'), Q['list']); key(Q['k4'])
assert zoom() == 0 and u.mem_read(addr('Spectrum_state'), 1)[0] == Q['list']
put8(addr('Spectrum_state'), Q['band'])
# Sentinel: key 4 does nothing.
put32(addr('lastReceivingFreq'), 11870000); put8(addr('sentinelView'), 1); key(Q['k4'])
assert zoom() == 0
print('PASS zoom: ±800/400/200/100/50 kHz around the last RX with 25/12.5/6.25/2.5/1 kHz in a loop, from the first level narrower than the scan; EXIT back to the scan; ZOOM popup on entry; EXIT leaves the zoom; 7 and 6 keep the real scan; none in Sentinel, in the list scan or without a reception in this session')
