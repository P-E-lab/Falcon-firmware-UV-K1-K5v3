"""Actual ARM: the BAND SCAN shortcut (F1, F2, M long). It lists the band
memories alone, silent, title in the status line, the name and the limits of
each band; < > choose, M scans the chosen band on the active VFO, EXIT goes
back. The scan is the VFO frequency scan bounded to the band, with its step,
modulation and filter; started from memory mode, EXIT gives the memory back."""
from harness import *
from PIL import Image

fields = {'bsize': 'sizeof(bandparameters)', 'bname': 'offsetof(bandparameters,BandName)',
          'bstart': 'offsetof(bandparameters,Startfrequency)', 'bstop': 'offsetof(bandparameters,Stopfrequency)',
          'bstep': 'offsetof(bandparameters,scanStep)', 'bmod': 'offsetof(bandparameters,modulationType)',
          'bbw': 'offsetof(bandparameters,bandwidth)', 'blist': 'BAND_LIST_SELECT',
          'up': 'KEY_UP', 'down': 'KEY_DOWN', 'menu': 'KEY_MENU', 'exit': 'KEY_EXIT', 'k4': 'KEY_4',
          'ptt': 'KEY_PTT', 'kcur': 'offsetof(KeyboardState,current)', 'act': 'ACTION_OPT_BAND_SCAN',
          'freq': 'FREQ_CHANNEL', 'scr': 'offsetof(EEPROM_Config_t,ScreenChannel)',
          'tx': 'offsetof(EEPROM_Config_t,TX_VFO)', 'am': 'MODULATION_AM', 's833': 'STEP_8_33kHz',
          's125': 'STEP_12_5kHz', 'fm': 'MODULATION_FM'}
probe = B / 'bandscan_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned bandscan_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]; cmd[cmd.index('-c') + 1] = str(probe); cmd[cmd.index('-o') + 1] = str(B / 'bandscan_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'bandscan_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.bandscan_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

# The shortcut: action 30, offered in the side-key list before VFO A/B (so M long has it too).
assert get32(addr('action_opt_table') + 4 * Q['act']) == addr('APP_RunBandScan') | 1
n = [s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name == 'gSubMenu_SIDEFUNCTIONS'][0]['st_size'] // 8  # {char *name; uint8_t id;}
table = bytes(u.mem_read(addr('gSubMenu_SIDEFUNCTIONS'), n * 8))
entries = [(bytes(u.mem_read(struct.unpack_from('<I', table, i * 8)[0], 12)).split(b'\0')[0], table[i * 8 + 4])
           for i in range(n)]
assert entries[-2:] == [(b'BAND SCAN', Q['act']), (b'VFO A/B', entries[-1][1])], entries
print('PASS shortcut: BAND SCAN (action 30) for F1, F2 and M long')

for f in ['ST7565_BlitFullScreen', 'ST7565_BlitStatusLine', 'ST7565_BlitLine', 'SYSTEM_DelayMs']:
    stub(f, lambda *a: ret())
BP, CNT = addr('BParams'), addr('bandCount')
bands = [(b'AIR     ', 11800000, 13700000, Q['s833'], Q['am'], 0),
         (b'PMR446  ', 44600625, 44619375, Q['s125'], Q['fm'], 1),
         (b'MARINE', 16300000, 15600000, Q['s125'], Q['fm'], 0),  # limits stored backwards
         (b'2M', 14400000, 14600000, Q['s125'], Q['fm'], 0), (b'70CM', 43000000, 44000000, Q['s125'], Q['fm'], 0),
         (b'CB', 2696500, 2740500, Q['s125'], Q['am'], 0), (b'LPD', 43307500, 43477500, Q['s125'], Q['fm'], 1)]
def load_bands(k=len(bands)):
    for i, (name, a, b, step, mod, bw) in enumerate(bands[:k]):
        e = BP + i * Q['bsize']
        u.mem_write(e + Q['bname'], name.ljust(9, b'\0')[:9])
        put32(e + Q['bstart'], a); put32(e + Q['bstop'], b)
        put8(e + Q['bstep'], step); put8(e + Q['bmod'], mod); put8(e + Q['bbw'], bw)
    put8(CNT, k)
load_bands()
put8(addr('bandPickSession'), 1); put8(addr('currentState'), Q['blist']); put8(addr('selectionHelp'), 0)
put8(addr('bandListSelectedIndex'), 0); put32(addr('bandListScrollOffset'), 0)
texts, titles = [], []
stub('DrawListTitle', lambda t, c, *a: titles.append((bytes(u.mem_read(t, 12)).split(b'\0')[0],
                                                      bytes(u.mem_read(c, 8)).split(b'\0')[0])))
def text(p, x, y, n, *a):
    texts.append((bytes(u.mem_read(p, n)), x, y))
stub('UI_DrawText5x5', text)
def render(png=None):
    texts.clear()
    assert call('APP_NativeBegin', 8)  # NATIVE_LISTS
    call('RenderBandSelectNative', count=50000000)
    call('APP_NativeEnd')
    st = bytes(u.mem_read(addr('gStatusLine'), 128)); fb = bytes(u.mem_read(addr('gFrameBuffer'), 7 * 128))
    if png:
        img = Image.new('1', (128, 64), 1)
        for x in range(128):
            for y in range(8):
                if st[x] >> y & 1: img.putpixel((x, y), 0)
                for p in range(7):
                    if fb[p * 128 + x] >> y & 1: img.putpixel((x, 8 + p * 8 + y), 0)
        img.resize((512, 256), Image.NEAREST).save(B / png)
    return st, fb
st, fb = render('bandscan-list.png')
assert titles[-1] == (b'BAND SCAN', b''), titles
assert any(st) and not any(b & 1 for b in st), 'title in the status line, one pixel down'
rows = [(t, x, y) for t, x, y in texts if y < 48]
left = [t for t, x, y in rows if x == 2]; right = [t for t, x, y in rows if x != 2]
assert left == [b'AIR', b'PMR446', b'MARINE', b'2M', b'70CM', b'CB'], left
assert right == [b'118-137', b'446.0-446.2', b'156-163', b'144-146', b'430-440', b'26.9-27.5'], right
assert sorted({y for t, x, y in rows}) == [5 + 7 * i for i in range(6)], rows
print('PASS list: title up, 6 rows centred, names without numbers, limits in MHz (whole, or one decimal rounded outward)')

# Keys: < > move, 4 and * do nothing, M picks and leaves, EXIT leaves without a band.
deinit = []
stub('DeInitSpectrum', lambda *a: (deinit.append(1), ret()))
def key(k): call('HandleKeyBandList', k, count=3000000)
put8(addr('bandPick'), 255)
key(Q['down']); key(Q['down']); assert u.mem_read(addr('bandListSelectedIndex'), 1)[0] == 2
enabled = bytes(u.mem_read(addr('settings'), 64)); key(Q['k4'])
assert bytes(u.mem_read(addr('settings'), 64)) == enabled and not deinit
key(Q['menu']); assert u.mem_read(addr('bandPick'), 1)[0] == 2 and deinit
put8(addr('bandPick'), 255); deinit.clear(); key(Q['exit'])
assert u.mem_read(addr('bandPick'), 1)[0] == 255 and deinit
print('PASS keys: < > choose, M scans the band, EXIT goes back; 4 and * unused')

# The shortcut: the VFO scan bounded to the band, frequency mode, memory back on EXIT.
put8(addr('bandPickSession'), 0)
stub('LoadActiveBands', lambda *a: ret())
stub('RADIO_ConfigureChannel', lambda *a: ret()); stub('RADIO_SelectVfos', lambda *a: ret())
popups = []
stub('UI_DisplayPopup', lambda p, *a: (popups.append(bytes(u.mem_read(p, 16)).split(b'\0')[0]), ret()))
ee = addr('gEeprom'); put8(ee + Q['tx'], 1)
def screen(): return struct.unpack('<H', u.mem_read(ee + Q['scr'] + 2, 2))[0]
pick, scans, last = [None], [], [None]
def spectrum(*a):
    assert u.mem_read(addr('bandPickSession'), 1)[0] == 1
    put8(addr('bandPick'), 255 if pick[0] is None else pick[0]); ret()
stub('APP_RunSpectrum', spectrum)
VB = addr('vfoBand')
def scan(*a):
    scans.append((screen(), get32(VB), get32(VB + 4), bytes(u.mem_read(VB + 8, 3))))
    put8(addr('kbd') + Q['kcur'], last[0]); ret()
stub('APP_RunCurrentScanList', scan)
def run(start, p, k):
    u.mem_write(ee + Q['scr'] + 2, struct.pack('<H', start)); pick[0] = p; last[0] = k; scans.clear()
    call('APP_RunBandScan', count=5000000)
run(5, None, Q['exit']); assert not scans and screen() == 5  # EXIT in the list: nothing
run(5, 2, Q['exit'])                                         # MARINE from memory 6, then EXIT
assert scans == [(Q['freq'], 15600000, 16300000, bytes([Q['s125'], Q['fm'], 0]))], scans
assert screen() == 5 and get32(VB + 4) == 0
run(5, 0, Q['ptt'])                                          # AIR, stopped on a signal: frequency mode stays
assert scans[0][1:] == (11800000, 13700000, bytes([Q['s833'], Q['am'], 0])) and screen() == Q['freq']
run(Q['freq'], 1, Q['exit']); assert scans[0][0] == Q['freq'] and screen() == Q['freq']
put8(CNT, 0); popups.clear(); run(5, 0, Q['exit'])
assert not scans and popups == [b'NO BAND'], popups
print('PASS scan: the active VFO in frequency mode, the band limits, step, modulation and filter; EXIT gives the memory back; NO BAND without band')

# Inside the VFO frequency scan: the band bounds it, with its step, modulation and filter.
f2 = {'cs': 'offsetof(VFO_Info_t,CHANNEL_SAVE)', 'vsz': 'sizeof(VFO_Info_t)', 'vfo': 'offsetof(EEPROM_Config_t,VfoInfo)',
      'mod': 'offsetof(SpectrumSettings,modulationType)', 'step': 'offsetof(SpectrumSettings,scanStepIndex)',
      'bw': 'offsetof(SpectrumSettings,listenBw)', 'rx': 'offsetof(VFO_Info_t,freq_config_RX)',
      'prx': 'offsetof(VFO_Info_t,pRX)', 'vstep': 'offsetof(VFO_Info_t,STEP_SETTING)'}
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned bandscan_layout[]={' + ','.join(f2.values()) + '};')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'bandscan_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.bandscan_layout').data()
Q.update(zip(f2, struct.unpack('<%dI' % (len(data) // 4), data)))
del handlers[addr('APP_RunCurrentScanList')]
for n in ['LoadSettings', 'RADIO_SetupRegisters', 'FUNCTION_Select', 'BACKLIGHT_TurnOn', 'VfoScanSyncDisplay']:
    if n in sym or any(k.startswith(n + '.') for k in sym):
        stub(n, lambda *a: ret())
seen = []
stub('APP_RunSpectrum', lambda *a: (seen.append((get32(addr('RangeStart')), get32(addr('RangeStop')),
      u.mem_read(addr('settings') + Q['step'], 1)[0], u.mem_read(addr('settings') + Q['mod'], 1)[0],
      u.mem_read(addr('settings') + Q['bw'], 1)[0])), ret()))
v = ee + Q['vfo'] + Q['vsz']  # VFO B, the active one
u.mem_write(v + Q['cs'], struct.pack('<H', Q['freq'])); put32(v + Q['prx'], v + Q['rx']); put32(v + Q['rx'], 0)
put8(v + Q['vstep'], 0xEE)  # a VFO frequency and step a plain frequency scan would refuse
put32(addr('gTxVfo'), v)
put32(VB, 15600000); put32(VB + 4, 16300000); u.mem_write(VB + 8, bytes([Q['s125'], Q['am'], 1]))
put32(addr('RangeStart'), 1); put32(addr('RangeStop'), 2)
call('APP_RunCurrentScanList', 0x40, count=50000000)
assert seen == [(15600000, 16300000, Q['s125'], Q['am'], 1)], seen
assert (get32(addr('RangeStart')), get32(addr('RangeStop'))) == (1, 2)  # the Spectre range is kept
print('PASS VFO scan: bounded to the band with its step, modulation and filter; the Spectre range is kept')

# M long opens the band editor over the list: its own title, no BAND SCAN above it.
f3 = {'spec': 'SPECTRUM'}
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned bandscan_layout[]={' + ','.join(f3.values()) + '};')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'bandscan_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.bandscan_layout').data()
Q.update(zip(f3, struct.unpack('<%dI' % (len(data) // 4), data)))
shown = []
stub('CHANNEL_Display', lambda *a: (shown.append(1), ret()))
load_bands(); put8(addr('bandPickSession'), 1); put8(addr('currentState'), Q['blist'])
for f in ['vfoScan', 'historyListActive']:
    put8(addr(f), 0)
CE = addr('channelEditor') + 44  # channelEditor.state, as in shot_popups.py
put8(CE, 0); call('Render', count=50000000)
assert any(u.mem_read(addr('gStatusLine'), 128)) and not shown
put8(CE, 1); call('Render', count=50000000)
assert not any(u.mem_read(addr('gStatusLine'), 128)) and shown, 'BAND SCAN over the band editor'
put8(CE, 0); put8(addr('bandPickSession'), 0)
print('PASS band editor (M long): drawn without the BAND SCAN title over it')

# After BAND SCAN, Spectre opens on its scan again, not on the band list.
load_bands(); put8(addr('currentState'), Q['blist'])
def spectrum2(*a):
    put8(addr('currentState'), Q['blist']); put8(addr('bandPick'), 255); ret()
stub('APP_RunSpectrum', spectrum2)
put8(addr('scanEmpty'), 1)
call('APP_RunBandScan', count=5000000)
assert u.mem_read(addr('currentState'), 1)[0] == Q['spec'] and u.mem_read(addr('scanEmpty'), 1)[0] == 0
print('PASS after BAND SCAN: Spectre opens on its scan')

# The band scan goes back to the band start after its end (not after the radio's top).
f4 = {'sf': 'offsetof(ScanInfo,f)', 'ss': 'offsetof(ScanInfo,scanStep)', 'si': 'offsetof(ScanInfo,i)'}
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned bandscan_layout[]={' + ','.join(f4.values()) + '};')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'bandscan_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.bandscan_layout').data()
Q.update(zip(f4, struct.unpack('<%dI' % (len(data) // 4), data)))
SI = addr('scanInfo')
put8(addr('vfoScan'), 1); put8(addr('vfoRangeSession'), 1); put8(addr('scanEmpty'), 0)
put32(addr('RangeStart'), 14500000); put32(addr('RangeStop'), 14600000)
put32(SI + Q['sf'], 14500000); put32(SI + Q['ss'], 1250); put32(SI + Q['si'], 0)
put32(addr('vfoBand') + 4, 0)  # a frequency scan: in order
def steps(count):
    seq = []
    for _ in range(count):
        call('NextScanStep', count=3000000); seq.append(get32(SI + Q['sf']))
    return seq
seq = steps(82)  # the first call starts the scan at 145.0000
assert seq[0] == 14500000 and seq[79:82] == [14598750, 14600000, 14500000], seq[78:]
put8(addr('vfoScan'), 0); put8(addr('vfoRangeSession'), 0)
print('PASS band end: 145.9875, 146.0000, then back to 145.0000')

# While a VFO scan scans, only the scanned VFO is redrawn: title and frequency
# for a frequency/band scan, its block for a memory scan; whole frames at the
# start, during a reception and just after it.
modes = []
stub('UI_DisplayMain', lambda *a: (modes.append(u.mem_read(addr('gUiScanPartial'), 1)[0]), ret()))
stub('VfoScanSyncDisplay', lambda *a: ret())
put8(addr('vfoScan'), 1); put8(addr('vfoRangeSession'), 1); put8(addr('isListening'), 0)
put8(addr('vfoScanWholeFrame'), 1)
for _ in range(3):
    call('Render', count=5000000)
put8(addr('isListening'), 1); put8(addr('rxDisplayPending'), 1); call('Render', count=5000000)
put8(addr('isListening'), 0)
for _ in range(2):
    call('Render', count=5000000)
put8(addr('vfoRangeSession'), 0); call('Render', count=5000000)
assert modes == [0, 2, 2, 0, 0, 2, 1], modes
assert u.mem_read(addr('gUiScanPartial'), 1)[0] == 0
put8(addr('vfoScan'), 0)
print('PASS VFO scan display: only the scanned VFO while scanning, whole frames at start and around a reception')

# 8.33 kHz: each frequency from the scan start, on the 25/3 kHz grid (no drift).
put8(addr('vfoScan'), 1); put8(addr('vfoRangeSession'), 1); put8(addr('scanEmpty'), 0)
put32(addr('RangeStart'), 11797500); put32(addr('RangeStop'), 13700000)
put32(SI + Q['sf'], 11797500); put32(SI + Q['ss'], 833); put32(SI + Q['si'], 0)
seq = steps(2100)
assert seq[1:4] == [11798333, 11799167, 11800000], seq[:4]
assert seq[1299] == 12880000 and seq[2099] == 13546667, (seq[1299], seq[2099])  # 128.800, 135.46667

print('PASS 8.33 kHz: 117.98333, 117.99167, 118.00000 ... 128.80000, 135.46667: on the grid')

# BAND SCAN goes in the Spectre order: jumps of 25 kHz, then passes one step apart.
VBs = addr('vfoBand'); put32(VBs + 4, 1)
def order(start, stop, step, index, count):
    put32(addr('RangeStart'), start); put32(addr('RangeStop'), stop)
    put8(addr('settings') + Q['step'], index)
    put32(SI + Q['sf'], start); put32(SI + Q['ss'], step); put32(SI + Q['si'], 0)
    return steps(count)
grid = lambda n: 11797500 + (n * 2500 + 1) // 3
want = [grid(n) for n in [0, 3, 6, 9, 12, 15, 1, 4, 7, 10, 13, 2, 5, 8, 11, 14, 0, 3]]
got = order(11797500, 11810000, 833, Q['s833'], 18)  # 117.975-118.100: 16 channels
assert got == want, got
want = [14500000 + 1250 * n for n in [0, 2, 4, 6, 8, 1, 3, 5, 7, 0]]
assert order(14500000, 14510000, 1250, Q['s125'], 10) == want  # 12.5 kHz: two passes
put32(VBs + 4, 0)
print('PASS BAND SCAN order: 25 kHz jumps, then passes one step apart, all channels each lap')

# Every rise is confirmed 10 ms later against the dBm squelch (no pre-check: AM
# envelopes dip, a first reading under the squelch must not lose the signal).
waits, readings = [], []
stub('SYSTEM_DelayMs', lambda ms, *a: (waits.append(ms), ret()))
stub('SYSTICK_DelayUs', lambda us, *a: (waits.append(us), ret()))  # SYSTEM_DelayMs may be inlined
stub('GetRssi', lambda *a: ret(readings.pop(0)))
put8(addr('vfoScan'), 1); put8(addr('vfoRangeSession'), 1); put8(addr('sentinelView'), 0)
put8(addr('historyListActive'), 0); put8(addr('isListening'), 0); put8(addr('gVfoScanSQL'), 40)  # -90 dBm
put32(SI + Q['sf'], 14550000)  # 145.5 MHz: -10 dB band correction, so raw 160 = -90 dBm
def rise(first, confirm):
    waits.clear(); readings[:] = [first, confirm]
    put8(addr('gIsPeak'), 0); put8(addr('rssiReferenceValid'), 1); u.mem_write(addr('previousRssi'), struct.pack('<h', 100))
    call('Measure', count=3000000)
    return bool(waits), u.mem_read(addr('gIsPeak'), 1)[0]
assert rise(140, 170) == (True, 1)   # -100 dBm in a dip, -85 dBm confirmed: a stop
assert rise(156, 162) == (True, 1)   # -92 dBm, within 3 dB: confirmed above the squelch
assert rise(156, 150) == (True, 0)   # confirmed under it: no stop
assert rise(170, 170) == (True, 1)   # a clear signal
put8(addr('gIsPeak'), 0); put8(addr('vfoScan'), 0); put8(addr('vfoRangeSession'), 0)
print('PASS squelch: every rise confirmed 10 ms later against SQLSCN')

# A stale "empty" from another session (BAND SCAN's list) never keeps NO CH /
# LIST up: a new scan is initialised before the empty test.
for f in ['ST7565_BlitFullScreen', 'Render', 'RenderStatus', 'HandleUserInput', 'UpdateScan']:
    for k in list(sym):
        if k == f or k.startswith(f + '.'):
            stub(k, lambda *a: ret())
stub('GetRssi', lambda *a: ret(0x60))
a0 = addr('InitScan') if any(k.startswith('InitScan') for k in sym) else None
put8(addr('isInitialized'), 1); put8(addr('currentState'), 0); put8(addr('Spectrum_state'), 1)  # a range
put8(addr('historyListActive'), 0); put8(addr('SpectrumMonitor'), 0); put8(addr('vfoScan'), 0)
put8(addr('scanEmpty'), 1); put8(addr('newScanStart'), 1); put8(addr('bandPickSession'), 0); put8(addr('historySession'), 0)
put32(addr('RangeStart'), 14400000); put32(addr('RangeStop'), 14600000)
call('Tick', count=20000000)
assert u.mem_read(addr('scanEmpty'), 1)[0] == 0 and u.mem_read(addr('newScanStart'), 1)[0] == 0
print('PASS a new scan is initialised before NO CH / LIST is decided')

# BAND SCAN's list and the History from the VFO never write Spectre's settings
# (at power-on their lists are not loaded yet: a write would leave no list).
saved = []
for k in list(sym):
    if k.startswith('PY25Q16_WriteBuffer'):
        stub(k, lambda a, *x: (saved.append(hex(a)), ret()))
for flags in ({'bandPickSession': 1}, {'historySession': 1}, {}):
    for f in ('bandPickSession', 'historySession', 'scanListOnly', 'vfoRangeSession'):
        put8(addr(f), flags.get(f, 0))
    saved.clear(); call('SaveSettings', 0, count=5000000)
    assert bool(saved) == (not flags), (flags, saved)
for f in ('bandPickSession', 'historySession'):
    put8(addr(f), 0)
print('PASS Spectre settings: never written by BAND SCAN or the History from the VFO, written by Spectre')

# The VFO scans' message (BL ..., SKIPPED) lasts Spectre's popup time; 0 shows none.
P = addr('vfoPopup')
for ms, shown in ((0, False), (500, True)):
    u.mem_write(P, bytes(16)); u.mem_write(addr('vfoPopupMs'), struct.pack('<H', ms))
    T = 0x20005900; u.mem_write(T, b'SKIPPED\0')
    call('VfoScanPopup', T, count=100000)
    assert (u.mem_read(P, 1)[0] != 0) == shown, ms
end = get32(addr('vfoPopupEnd')) - get32(addr('gGlobalSysTickCounter'))
assert end == 50, end  # 500 ms in 10 ms ticks
print('PASS VFO scan message: Spectre popup time, none at 0')

# The message is drawn once; the screen stays still for its time, a new
# reception cancels it with a whole frame, its end redraws the whole screen.
shown_popups = []
for k in list(sym):
    if k == 'Render' or k.startswith('Render.'):
        handlers.pop(sym[k] & ~1, None)  # the real Render again (stubbed by the Tick test above)
stub('UI_DisplayPopup', lambda p, *a: (shown_popups.append(bytes(u.mem_read(p, 12)).split(b'\0')[0]), ret()))
put8(addr('vfoScan'), 1); put8(addr('vfoRangeSession'), 1); put8(addr('isListening'), 0)
put8(addr('rxDisplayPending'), 0); put8(addr('vfoScanWholeFrame'), 0); modes.clear()
call('VfoScanPopup', T, count=100000)
assert shown_popups == [b'SKIPPED']
call('Render', count=5000000); call('Render', count=5000000)
assert not modes and u.mem_read(P, 1)[0]                  # still, while it is shown
put8(addr('isListening'), 1); put8(addr('rxDisplayPending'), 1)
call('Render', count=5000000)
assert modes == [0] and not u.mem_read(P, 1)[0]            # a reception: cancelled, whole frame
put8(addr('isListening'), 0); modes.clear()
call('VfoScanPopup', T, count=100000)
put32(addr('gGlobalSysTickCounter'), get32(addr('vfoPopupEnd')) + 1)
call('Render', count=5000000)
assert modes == [0] and not u.mem_read(P, 1)[0]            # its end: the whole screen again
put8(addr('vfoScan'), 0); put8(addr('vfoRangeSession'), 0)
print('PASS VFO scan message: drawn once, still screen, a reception or its end redraws it all')

# Spectre list scan: the members are noted once, each step reads that table.
ATTR = 0x008000 if K1 else 0x1FE000
FAR = 900 if K1 else 3000  # a scattered member, inside the edition's memories
members = {5: 2, 100: 2, FAR: 2, 7: 0, FAR - 100: 3}
for ch in range(MR_MAX - 30):
    f = 14500000 + ch * 1250 if ch in members else 0xFFFFFFFF
    flash[CH_BASE + 16 * ch:CH_BASE + 16 * ch + 4] = struct.pack('<I', f)
    flash[ATTR + 2 * ch:ATTR + 2 * ch + 2] = struct.pack('<H', (members.get(ch, 0) << 8) if ch in members else 0xFFFF)
put8(addr('vfoScan'), 0); put8(addr('scanListOnly'), 0); put8(addr('Spectrum_state'), 0)
for k in list(sym):
    if k.startswith(('ScanListVisible', 'BuildActivityLists', 'Spectrum_Prepare_Tx')):
        stub(k, lambda *a: ret(1))
f5 = {'lists': 'offsetof(SpectrumSettings,scanListEnabled)'}
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned bandscan_layout[]={' + ','.join(f5.values()) + '};')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'bandscan_probe.o', 'rb') as fh:
    data = ELFFile(fh).get_section_by_name('.rodata.bandscan_layout').data()
Q.update(zip(f5, struct.unpack('<%dI' % (len(data) // 4), data)))
L = addr('settings') + Q['lists']
reads = [0]
def rd3(a, p, n, *x):
    reads[0] += 1; rd(a, p, n)
for k in ['PY25Q16_ReadBuffer', 'PY25Q16_ReadBufferSafe']:
    stub(k, rd3)
def scan(enabled):
    u.mem_write(L, bytes(1 if i + 1 in enabled else 0 for i in range(25)))
    call('LoadActiveScanFrequencies', count=200000000)
    seq, ch = [], 0
    reads[0] = 0
    for _ in range(4):
        ch = call('GetNextChannelInSelectedScanLists', ch, 1, count=5000000) & 0xFFFF; seq.append(ch)
    return seq, reads[0]
seq, n = scan({2})
assert seq == [5, 100, FAR, 5] and n <= 4, (seq, n)        # list 2, at most one read per step
seq, n = scan(set())
assert seq == [7, 7, 7, 7] and n <= 4, (seq, n)             # no list: the unlisted memory, as fast
print('PASS list scan: members noted once, steps read the table (list 2, or none checked)')
