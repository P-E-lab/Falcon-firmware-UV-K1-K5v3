"""Actual ARM: the Spectre waterfall. One row per sweep, newest on top, in
frame-buffer pages 4-5: solid above the dBm squelch, one dot in four for a
weak signal 6 dB over the noise, nothing for the noise. Key 8 toggles it."""
from harness import *

fields = {'samples': 'offsetof(SpectrumActivity,samples)', 'noise': 'offsetof(SpectrumActivity,noiseDb)',
          'astart': 'offsetof(SpectrumActivity,start)', 'astop': 'offsetof(SpectrumActivity,stop)',
          'pf': 'offsetof(PeakInfo,f)',
          'init': 'offsetof(SpectrumActivity,initialized)', 'mode': 'SCAN_RANGE_MODE',
          'state': 'offsetof(SpectrumSettings,modulationType)'}
probe = B / 'waterfall_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned scanner_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'waterfall_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'waterfall_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

for n in ['ST7565_BlitFullScreen', 'ST7565_BlitLine', 'ST7565_BlitStatusLine', 'SYSTEM_DelayMs', 'BK4819_WriteRegister']:
    stub(n, lambda *a: ret())
act = addr('activity')
NOISE, SQL = -120, -90
put8(addr('gSpectrumSQL'), SQL + 130)
put8(addr('sentinelView'), 0); put8(addr('vfoScan'), 0); put8(addr('historyListActive'), 0)
put8(addr('historySession'), 0); put8(addr('gHistoryScan'), 0); put8(addr('isListening'), 0)
put8(addr('Spectrum_state'), Q['mode'])
u.mem_write(addr('waterfall'), bytes(256)); put8(addr('waterfallCount'), 0)


def level(row, x):
    if x == 127:
        return -60                                   # stale, never measured: must stay blank
    if abs(x - 60) <= 1:
        return -70                                   # continuous, strong
    if abs(x - 20) <= 1 and (row < 5 or 9 <= row < 13):
        return -75                                   # intermittent, strong
    if abs(x - 100) <= 2:
        return -110                                  # weak: over the noise, under the squelch
    return NOISE


def sweep(row):
    u.mem_write(act + Q['samples'], b''.join(struct.pack('<H', (level(row, x) + 192) * 2) for x in range(128)))
    u.mem_write(act + Q['noise'], struct.pack('<h', NOISE)); put8(act + Q['init'], 1)
    u.mem_write(addr('waterfallSeen'), b'\xff' * 15 + b'\x7f')  # the range's last column is never measured
    assert call('APP_NativeBegin', 7) == 1
    call('WaterfallPush')
    call('APP_NativeEnd')


ROWS = 16
for r in range(ROWS - 1, -1, -1):   # oldest first: row 0 ends on top
    sweep(r)
wf = bytes(u.mem_read(addr('waterfall'), 256))
def px(x, y): return wf[(y // 8) * 128 + x] >> (y % 8) & 1
assert all(px(60, y) for y in range(ROWS)), 'continuous signal: a full column'
assert [px(20, y) for y in range(ROWS)] == [1 if (y < 5 or 9 <= y < 13) else 0 for y in range(ROWS)]
weak = sum(px(x, y) for x in range(98, 103) for y in range(ROWS))
assert weak == 3 * ROWS // 2, weak  # even columns 98/100/102, every other row: one dot in four
assert not any(px(x, y) for x in list(range(0, 15)) + list(range(110, 128)) for y in range(ROWS)), 'noise and unmeasured columns stay blank'
assert bytes(u.mem_read(addr('waterfallSeen'), 16)) == bytes(16), 'the next row starts from nothing measured'
print('PASS waterfall rows: solid over the squelch, light dither for a weak signal, blank noise, newest on top')

# Rendered: with the waterfall on, pages 4-5 are the waterfall and the plot
# ends above it; key 8 toggles it, Sentinel ignores it.
from PIL import Image
for on in (0, 1):
    put8(addr('waterfallOn'), on)
    u.mem_write(addr('gFrameBuffer'), bytes(7 * 128))
    assert call('APP_NativeBegin', 7) == 1
    call('RenderSpectrumNative', count=5000000)
    call('APP_NativeEnd')
    fb = bytes(u.mem_read(addr('gFrameBuffer'), 7 * 128))
    if on:
        assert fb[4 * 128:6 * 128] == bytes(u.mem_read(addr('waterfall'), 256))
        assert u.mem_read(addr('activityBottom'), 1)[0] == 30
    else:
        assert u.mem_read(addr('activityBottom'), 1)[0] == 39
    im = Image.new('1', (128, 56), 1)
    for y in range(56):
        for x in range(128):
            if fb[(y // 8) * 128 + x] >> (y % 8) & 1:
                im.putpixel((x, y), 0)
    im.resize((512, 224), Image.NEAREST).save(B / ('waterfall-%s.png' % ('on' if on else 'off')))
print('PASS rendered: waterfall in pages 4-5 under a shorter plot; plot bottom 39 without it')

# During a reception the scan stops: each 500 ms row shows the frequency being
# received, and only it.
put32(act + Q['astart'], 14400000); put32(act + Q['astop'], 14600000)
u.mem_write(act + Q['samples'], bytes(256)); u.mem_write(addr('waterfallSeen'), bytes(16))
put32(addr('peak') + Q['pf'], 14500000); u.mem_write(addr('spectrumRxDbm'), struct.pack('<h', -70))
u.mem_write(addr('waterfall'), bytes(256))
assert call('APP_NativeBegin', 7) == 1
call('WaterfallListenRow')
call('APP_NativeEnd')
wf = bytes(u.mem_read(addr('waterfall'), 256))
top = [x for x in range(128) if wf[x] & 1]
assert top and all(60 <= x <= 67 for x in top), top
assert bytes(u.mem_read(addr('gFrameBuffer') + 4 * 128, 256)) == wf
print('PASS reception rows: only the received frequency, copied to the screen')
