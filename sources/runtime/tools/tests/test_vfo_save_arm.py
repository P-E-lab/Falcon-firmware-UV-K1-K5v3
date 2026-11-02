"""Actual ARM: the VFO state is saved 3 s after its last change. A frequency-
mode record waits in RAM, where SETTINGS_ReadVfoRecord (RADIO_LoadChannel)
reads it; the channel numbers wait too. SETTINGS_SaveVfoNow (PTT, menu,
scanners, apps, UART) and the end of the 3 s countdown write both."""
from harness import *

FREQ_CHANNEL = 4096  # S4K: MR_CHANNELS_MAX
writes = []


def wr(a, p, n, *unused):
    data = bytes(u.mem_read(p, n))
    writes.append((a, data))
    flash[a:a + n] = data
    ret()


stub('PY25Q16_WriteBuffer', wr)
stub('MR_SetChannelAttributes', lambda *a: ret())
vfo1 = eep + vfooff + vfosize
OUT = 0x20003800


def record(vfo, offset=0, size=8):
    call('SETTINGS_ReadVfoRecord', vfo, offset, OUT, size)
    return bytes(u.mem_read(OUT, size))


flash[0x009010:0x009020] = struct.pack('<II', 14500000, 0) + b'\xff' * 8
assert struct.unpack('<I', record(1, 0, 4))[0] == 14500000

# Three steps: nothing written, the newest record read back from RAM.
for f in (14501250, 14502500, 14503750):
    put32(vfo1 + frx, f)
    call('SETTINGS_SaveChannel', FREQ_CHANNEL, 1, vfo1, 1)
assert not writes, writes
assert struct.unpack('<I', record(1, 0, 4))[0] == 14503750
assert struct.unpack('<I', flash[0x009010:0x009014])[0] == 14500000
assert u.mem_read(addr('gVfoSaveCountdown_10ms'), 2) == struct.pack('<H', 300)
print('PASS VFO steps wait in RAM (no flash write), read back from there; 3 s countdown', flush=True)

# A channel change waits as well.
call('SETTINGS_SaveVfoIndices')
assert not writes and u.mem_read(addr('gVfoSaveCountdown_10ms'), 2) == struct.pack('<H', 300)

# End of the countdown: the scheduler sets gScheduleVfoSave, the 10 ms slice flushes.
put8(addr('gScheduleVfoSave'), 1)
call('SETTINGS_SaveVfoIndicesFlush')
assert [a for a, _ in writes] == [0x009010, 0x00A010], [hex(a) for a, _ in writes]
assert struct.unpack('<I', flash[0x009010:0x009014])[0] == 14503750
assert struct.unpack('<I', record(1, 0, 4))[0] == 14503750  # from flash now
print('PASS after 3 s: one write of the VFO record and one of the channel numbers', flush=True)

# SaveVfoNow writes at once, and only when something waits.
writes.clear()
call('SETTINGS_SaveVfoNow')
assert not writes
put32(vfo1 + frx, 43300000)
call('SETTINGS_SaveChannel', FREQ_CHANNEL, 1, vfo1, 1)
call('SETTINGS_SaveVfoNow')
assert [a for a, _ in writes] == [0x009010] and struct.unpack('<I', flash[0x009010:0x009014])[0] == 43300000
print('PASS SaveVfoNow (PTT, menu, scanners, apps, UART) writes at once, only when something waits', flush=True)

# A memory channel is still written at once.
writes.clear()
call('SETTINGS_SaveChannel', 5, 1, vfo1, 2)
assert writes and all(a != 0x009010 for a, _ in writes)
print('PASS memory channels are written at once', flush=True)
