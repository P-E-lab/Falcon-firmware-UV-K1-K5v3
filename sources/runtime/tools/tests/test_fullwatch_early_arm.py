"""Actual ARM Full Watch early advance: a position is left early only after the
measurement time and two quiet RSSI readings; Dual and loud positions keep
their full dwell."""
from harness import *

probe = B / 'fullwatch_probe.c'
probe.write_text('#include <stddef.h>\n#include "radio.h"\n'
                 'const unsigned fullwatch_offsets[] = {offsetof(VFO_Info_t, SquelchOpenRSSIThresh)};\n')
cmd = out[:]
cmd[cmd.index('-c') + 1] = str(probe)
cmd[cmd.index('-o') + 1] = str(B / 'fullwatch_probe.o')
subprocess.run(cmd, cwd=c['directory'], check=True)
(thresh_off,) = struct.unpack('<I', ELFFile(open(B / 'fullwatch_probe.o', 'rb'))
                              .get_section_by_name('.rodata.fullwatch_offsets').data())

rssi = [0]
stub('BK4819_GetRSSI', lambda *a: ret(rssi[0]))
vfo = eep + vfooff                       # VfoInfo[0]
put8(vfo + thresh_off, 100)              # squelch opens at RSSI 100
put32(addr('gRxVfo'), vfo)
TICK = addr('gGlobalSysTickCounter')
get8 = lambda n: u.mem_read(addr(n), 1)[0]


def setup(mode=2, retune=1000):
    put8(eep + rxmode, mode)
    put32(addr('gCurrentFunction'), 0)   # FUNCTION_FOREGROUND
    put8(addr('gRxReceptionMode'), 0)
    put8(addr('g_SquelchLost'), 0)
    put8(addr('gDualVfoSchedule'), 0)
    put32(addr('fullWatchRetuneTick'), retune)
    put8(addr('fullWatchQuietReadings'), 0)


def step(tick, level):
    put32(TICK, tick)
    rssi[0] = level
    call('APP_FullWatchEarlyAdvance')
    return get8('gDualVfoSchedule')


setup()
assert step(1003, 10) == 0, 'no decision before the measurement time'
assert step(1004, 10) == 0 and step(1004, 10) == 0, 'one reading per tick'
assert step(1005, 10) == 1, 'two quiet readings: next position'
print('PASS quiet position left after 50 ms (two readings below threshold - 6 dB)')

setup()
assert step(1004, 10) == 0 and step(1005, 95) == 0 and step(1006, 10) == 0
assert step(1007, 10) == 1, 'a reading near the threshold restarts the count'
setup()
for t in range(1004, 1010):
    assert step(t, 120) == 0, 'signal at/above threshold: full dwell kept'
assert step(1010, 10) == 0, 'never after the normal dwell'
print('PASS near/above threshold keeps the full 100 ms dwell')

setup(mode=1)
for t in range(1004, 1010):
    assert step(t, 0) == 0
put8(eep + rxmode, 2); put8(addr('g_SquelchLost'), 1)
for t in range(1004, 1010):
    assert step(t, 0) == 0
print('PASS Dual mode and an open squelch are never shortened')
