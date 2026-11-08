"""Actual ARM: the VFO memory arrows (KEY_UP/KEY_DOWN, left/right on the K1)
go through lists 1..25, then the memories without a list, then the monitor
memories. A memory without a list shows twice: in place (it follows the list
of the nearest listed memory below it) and again in the NONE step."""
from harness import *

ATTR = 0x008000 if K1 else 0x1FE000
# memory -> scanlist (0 = none, 51 = monitor)
lists = {1: 0, 10: 2, 11: 2, 12: 0, 20: 1, 21: 1, 22: 0, 30: 51, 31: 0, 40: 51}
for ch in range(MR_MAX):
    v = (lists[ch] << 8) | 0 if ch in lists else 0xFFFF   # band 7+: empty
    flash[ATTR + 2 * ch:ATTR + 2 * ch + 2] = struct.pack('<H', v)
    if ch in lists:
        attrs[ch] = lists[ch] << 8
stub('RADIO_CheckValidChannel', lambda ch, *a: ret(1 if ch in lists else 0))
u.mem_write(addr('memorySequential'), bytes(4))

u.mem_write(addr('memoryNone'), bytes(4))
def walk(ch, direction, n):
    seq = []
    for _ in range(n):
        ch = call('NextMemoryByList', ch, direction, count=50000000); seq.append(ch)
    return seq
# list 1: 20 21 + unlisted 22 and 31 in place (nearest listed below: 21); list 2: 10 11 + unlisted 12;
# NONE: 1 (nothing listed below), then 12, 22, 31 again; monitor: 30 40.
want = [21, 22, 31, 10, 11, 12, 1, 12, 22, 31, 30, 40, 20]
order = walk(20, 1, len(want))
assert order == want, order
back = walk(20, 0xffffffff, len(want))
assert back == list(reversed(want[:-1])) + [20], back
# Reached by the scanlist keys in the NONE step, the arrows go on in the NONE step.
u.mem_write(addr('memoryNone'), struct.pack('<H', 23))
assert walk(22, 1, 2) == [31, 30]
u.mem_write(addr('memoryNone'), bytes(2))
assert walk(22, 1, 1) == [31] and walk(31, 1, 1) == [10]
print('PASS arrows: lists with unlisted memories in place, NONE with all of them, then monitor; both ways; the scanlist step is kept')
