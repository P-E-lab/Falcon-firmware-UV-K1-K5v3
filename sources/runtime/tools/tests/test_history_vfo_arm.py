"""Actual ARM: the history screen. Listening on an entry uses the entry's own
modulation and filter and the scanner modulation comes back on exit; * swaps
the history and MESSAGES pages and silences RX; opened from the VFO, no scan
step runs; long 1 hands the entry (modulation, bandwidth, code) to the VFO."""
from harness import *

fields = {
    'modoff': 'offsetof(SpectrumSettings,modulationType)', 'down': 'KEY_DOWN', 'up': 'KEY_UP',
    'star': 'KEY_STAR', 'exit': 'KEY_EXIT', 'fm': 'MODULATION_FM', 'am': 'MODULATION_AM',
    'bwam': 'BK4819_FILTER_BW_AM', 'bwwide': 'BK4819_FILTER_BW_WIDE', 'bwnarrow': 'BK4819_FILTER_BW_NARROW',
    'mod': 'offsetof(VFO_Info_t,Modulation)', 'width': 'offsetof(VFO_Info_t,CHANNEL_BANDWIDTH)',
    'offset': 'offsetof(VFO_Info_t,TX_OFFSET_FREQUENCY)', 'ctype': 'offsetof(FREQ_Config_t,CodeType)',
    'code': 'offsetof(FREQ_Config_t,Code)', 'dcs': 'CODE_TYPE_DIGITAL', 'ctcss': 'CODE_TYPE_CONTINUOUS_TONE',
    'spectrum': 'SPECTRUM', 'range': 'SCAN_RANGE_MODE', 'sf': 'offsetof(ScanInfo,f)',
    'ss': 'offsetof(ScanInfo,scanStep)', 'stepoff': 'offsetof(SpectrumSettings,scanStepIndex)',
    'step625': 'STEP_6_25kHz', 'green': 'BK4819_GPIO6_PIN2_GREEN', 'red': 'BK4819_GPIO5_PIN1_RED',
}
probe = B / 'history_vfo_probe.c'
probe.write_text('#include <stddef.h>\n#include "' + str(ROOT / 'App/app/spectrum.c') +
                 '"\nconst unsigned history_layout[]={' + ','.join(fields.values()) + '};')
cmd = out[:]
cmd[cmd.index('-c') + 1] = str(probe)
cmd[cmd.index('-o') + 1] = str(B / 'history_vfo_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
with open(B / 'history_vfo_probe.o', 'rb') as f:
    data = ELFFile(f).get_section_by_name('.rodata.history_layout').data()
Q = dict(zip(fields, struct.unpack('<%dI' % (len(data) // 4), data)))

filters, tunes, rx = [], [], []
stub('BK4819_SetFilterBandwidth', lambda bw, *a: (filters.append(bw), ret()))
stub('BK4819_SetFrequency', lambda f, *a: (tunes.append(f), ret()))
for name in ['BK4819_WriteRegister', 'BK4819_ToggleGpioOut', 'BK4819_RX_TurnOn', 'BK4819_SetAF',
             'BK4819_PickRXFilterPathBasedOnFrequency', 'RADIO_SetModulation', 'RADIO_SetupAGC',
             'SYSTICK_DelayUs', 'SYSTEM_DelayMs', 'ST7565_BlitLine', 'ST7565_BlitStatusLine',
             'BACKLIGHT_TurnOn', 'BACKLIGHT_SetBrightness', 'GPIO_EnableAudioPath', 'GPIO_DisableAudioPath',
             'BOARD_ADC_GetBatteryInfo']:  # status bar redraw on leaving the MESSAGES page
    stub(name, lambda *a: ret())
stub('BK4819_ReadRegister', lambda *a: ret(0))
stub('BOARD_gMR_fetchChannel', lambda *a: ret(0xFFFF))  # frequencies outside the memories
stub('ToggleRX', lambda on, *a: (rx.append(on), put8(addr('isListening'), on), ret()))
for flag in ['vfoScan', 'scanListOnly', 'vfoRangeSession', 'gHistoryScan', 'historySession', 'historyMessages',
             'gForceModulation', 'isListening', 'SpectrumMonitor', 'gIsPeak']:
    put8(addr(flag), 0)
put8(addr('currentState'), Q['spectrum'])


def get8(name): return u.mem_read(addr(name), 1)[0]


def scan_step():
    """One scanner Tick on a valid range; returns the frequencies tuned."""
    for name in ['SPECTRUM_PAUSED', 'postTxListening', 'gIsPeak', 'isListening', 'newScanStart',
                 'gNextTimeslice_10ms', 'gNextTimeslice_500ms', 'gNextTimeslice_listening',
                 'gNextTimeslice_display', 'gNextTimeslice_AutoPtt', 'gNextTimeslice_HTimeS']:
        put8(addr(name), 0)
    put8(addr('isInitialized'), 1)
    put8(addr('Spectrum_state'), Q['range'])
    put16(addr('osdPopupTimer'), 0)
    put16(addr('WaitSpectrum'), 0)
    put32(addr('RangeStart'), 44600000)
    put32(addr('RangeStop'), 44620000)
    put32(addr('SpectrumRangeStart'), 44600000)
    put32(addr('SpectrumRangeStop'), 44620000)
    put8(settings + Q['stepoff'], Q['step625'])
    put16(addr('scanInfo') + Q['ss'], 625)
    put32(addr('scanInfo') + Q['sf'], 44600625)
    tunes.clear()
    call('Tick', count=5000000)
    return list(tunes)


def entry(i, freq, mod, bw, code=0, inverse=False):
    # RamHistory: HFreqs, HBlacklisted, code, HTimeS, record (profile + 1 in bits 10..13)
    record = 1 | (((mod | bw << 2) + 1) << 10) | (inverse << 14)
    u.mem_write(addr('history') + 10 * i, struct.pack('<IBBHH', freq, 0, code, 5, record))


settings = addr('settings')
entry(0, 12312500, Q['am'], 1)        # airband, AM narrow
entry(1, 14550000, Q['fm'], 0, 100 + 5)  # FM wide, DCS
put16(addr('indexFs'), 2)
put8(settings + Q['modoff'], Q['fm'])
assert scan_step()  # positive control: an ordinary scanner tick tunes the next step
call('HistoryOpen')
assert get8('historyListActive') and get8('historyModulation') == Q['fm']

# The list opens on entry 0; DOWN then UP locks on entry 0 with its own AM filter.
for key, want_freq, want_mod, want_bw in [(Q['down'], 14550000, Q['fm'], Q['bwwide']),
                                          (Q['up'], 12312500, Q['am'], Q['bwam'])]:
    filters.clear()
    tunes.clear()
    call('HandleKeySpectrum', key, count=5000000)
    assert get8('SpectrumMonitor') == 1 and get32(addr('lastReceivingFreq')) == want_freq
    assert u.mem_read(settings + Q['modoff'], 1)[0] == want_mod
    assert filters and filters[-1] == want_bw, (filters, want_bw)
print('PASS Frequency Lock on an entry uses its own modulation and filter (AM narrow airband)', flush=True)

rx.clear()
call('HandleKeySpectrum', Q['star'], count=5000000)
assert get8('historyMessages') and get8('SpectrumMonitor') == 0 and rx == [0]
assert not scan_step()  # the scan pauses on the MESSAGES page
call('HandleKeySpectrum', Q['star'], count=5000000)
assert not get8('historyMessages')
print('PASS * swaps history/MESSAGES pages; MESSAGES stops RX and the scan', flush=True)

for key in [Q['down'], Q['up']]:  # back on the AM entry
    call('HandleKeySpectrum', key, count=5000000)
assert u.mem_read(settings + Q['modoff'], 1)[0] == Q['am']
call('HandleKeySpectrum', Q['exit'], count=5000000)
assert not get8('historyListActive') and u.mem_read(settings + Q['modoff'], 1)[0] == Q['fm']
assert get8('historyBandwidth') == 0xFF
print('PASS leaving the history restores the scanner modulation and filter', flush=True)

# Opened from the VFO: no scan step, unless the history scan is asked for.
put8(addr('historySession'), 1)
call('HistoryOpen')
put8(addr('SpectrumMonitor'), 0)
assert not scan_step()
leds = []
stub('BK4819_ToggleGpioOut', lambda pin, on, *a: (leds.append(on), ret()))
for key in [Q['down'], Q['up']]:  # selecting entries: no Frequency Lock, no RX
    tunes.clear()
    rx.clear()
    call('HandleKeySpectrum', key, count=5000000)
    assert get8('SpectrumMonitor') == 0 and not tunes and 1 not in rx, (tunes, rx)
leds.clear()
put8(addr('isListening'), 0)
for _ in range(3):
    put8(addr('gNextTimeslice_SCAN_LED'), 1)
    put8(addr('gNextTimeslice_listening'), 1)
    call('Tick', count=5000000)
assert 1 not in leds, leds  # no green LED blink
put8(addr('gHistoryScan'), 1)
assert scan_step()  # long * still scans the history on request
put8(addr('gHistoryScan'), 0)
call('HistoryClose')
put8(addr('historySession'), 0)
print('PASS VFO history session: no scan, no RX on selection, no LED; long * still scans', flush=True)

# Long 1 settings: entry 1 (FM wide DCS 0x05) onto a draft holding an offset.
vfo = 0x20007000
u.mem_write(vfo, bytes(vfosize))
put8(vfo + Q['mod'], Q['am'])
put32(vfo + Q['offset'], 600000)
call('FrequencyDraft', vfo, 14550000, Q['fm'] | 0 << 2, (Q['dcs'] << 8) | 5)
assert get32(vfo + frx) == 14550000 and get32(vfo + ftx) == 14550000
assert get32(vfo + prx) == vfo + frx and get32(vfo + ptx) == vfo + ftx
assert u.mem_read(vfo + Q['mod'], 1)[0] == Q['fm'] and u.mem_read(vfo + Q['width'], 1)[0] == 0
assert get32(vfo + Q['offset']) == 0
assert u.mem_read(vfo + frx + Q['ctype'], 1)[0] == Q['dcs'] and u.mem_read(vfo + frx + Q['code'], 1)[0] == 5
assert u.mem_read(vfo + ftx + Q['ctype'], 1)[0] == Q['dcs']
# Journal row not found by the scanner: the VFO keeps its modulation and code.
put8(vfo + Q['mod'], Q['am'])
call('FrequencyDraft', vfo, 43312500, 0xFF, 0)
assert get32(vfo + frx) == 43312500 and u.mem_read(vfo + Q['mod'], 1)[0] == Q['am']
assert u.mem_read(vfo + frx + Q['ctype'], 1)[0] == Q['dcs']
print('PASS long 1 draft: entry modulation, bandwidth and code, simplex; journal-only rows keep VFO settings',
      flush=True)

# Scan blink: green and red together on the scan LED slot, both off after it.
pins = []
stub('BK4819_ToggleGpioOut', lambda pin, on, *a: (pins.append((pin, on)), ret()))
for flag, want in [('gNextTimeslice_SCAN_LED', {(Q['green'], 1), (Q['red'], 1)}),
                   ('gNextTimeslice_SCAN_LED_OFF', {(Q['green'], 0), (Q['red'], 0)})]:
    pins.clear()
    for name in ['SPECTRUM_PAUSED', 'gIsPeak', 'isListening', 'SpectrumMonitor', 'historyListActive',
                 'historySession', 'historyMessages', 'gHistoryScan', 'newScanStart', 'gNextTimeslice_10ms',
                 'gNextTimeslice_500ms', 'gNextTimeslice_listening', 'gNextTimeslice_display',
                 'gNextTimeslice_SCAN_LED', 'gNextTimeslice_SCAN_LED_OFF']:
        put8(addr(name), 0)
    put16(addr('WaitSpectrum'), 0)
    put8(addr('currentState'), Q['spectrum'])
    put8(addr(flag), 1)
    call('Tick', count=5000000)
    assert want <= set(pins), (flag, pins)
print('PASS scan LED blink: green and red together, both off at the end of the blink', flush=True)
