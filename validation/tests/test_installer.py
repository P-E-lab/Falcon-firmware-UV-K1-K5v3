"""Actual linked ARM installer, hardware/NOR mocked: slot selection, store
placement, Apps protection, power cuts and damaged payloads."""
from installer_harness import *

stub('progress', lambda *_: ret())
stub('install_render', lambda *_: ret())


def fresh(target):
    flash[:] = b'\xff' * len(flash)
    flash[APPS[0]:APPS[1]] = b'\xa5' * (APPS[1] - APPS[0])      # 64 Apps of any firmware
    flash[0x182000:0x1A2000] = b'\x5a' * 0x20000                 # v7 Apps reserve 64..79
    sim.update(target=target, internal_matches=True, cut_after=None)
    reset()
    writes.clear()
    restores.clear()


def untouched_outside(slot):
    keep = [(0, SLOT_BASE)] + [(SLOT_BASE + s * SLOT_STRIDE, SLOT_BASE + (s + 1) * SLOT_STRIDE)
                               for s in range(5) if s != slot]
    keep += [APPS, (0x182000, 0x1A2000), (0x1DE000, 0x200000)]
    for s in range(5):
        if s != slot:
            keep += [(store_physical(s, 0), store_physical(s, STORE_ASSET_SIZE)),
                     (store_physical(s, STORE_ASSET_SIZE), store_physical(s, STORE_ASSET_SIZE) + STORE_BANK_SIZE)]
    for kind, a, n in writes:
        for lo, hi in keep:
            assert not (a < hi and a + n > lo), (kind, hex(a), n, hex(lo))


# User slot (Firmware Slots tab) and Main (DFU, no multiboot marker yet).
for target, expected in [(4, 4), (1, 1), (None, 0)]:
    fresh(target)
    assert run() == 0
    assert restores == [expected] and installed(expected)
    untouched_outside(expected)
    hdr = SLOT_BASE + expected * SLOT_STRIDE
    assert bytes(flash[hdr + 32:hdr + 48]).split(b'\0')[0].startswith(b'v')
    print('PASS slot', expected, 'DFU Main' if target is None else 'Firmware Slots',
          ': runtime + store exact, Apps, reserve, other slots and stores intact', flush=True)

# Power cut while writing, then the marker no longer matches: resume from the journal.
fresh(2)
sim['cut_after'] = 300
try:
    run()
    raise AssertionError('expected power cut')
except PowerCut:
    pass
sim['internal_matches'] = False
reset()
assert run() == 0 and restores == [2] and installed(2)
print('PASS power cut during the write, journal resume into the same slot', flush=True)

# A journal left by an earlier install of another slot is ignored once cleared
# by the runtime (magic programmed to zero); DFU then installs into Main.
journal = store_physical(2, STORE_JOURNAL)
flash[journal:journal + 4] = bytes(4)
sim.update(target=None, internal_matches=False)
reset(); restores.clear()
assert run() == 0 and restores == [0] and installed(0)
print('PASS cleared journal ignored: DFU installs into Main', flush=True)

# Apps of another firmware in the store bank: refuse before erasing anything.
fresh(3)
bank = store_physical(3, STORE_ASSET_SIZE) + 0x2000
flash[bank:bank + 4] = struct.pack('<I', 0x31504146)                 # "FAP1"
assert run() == 0x10 and not writes and not restores
print('PASS Apps found in the store bank: installation refused, nothing erased', flush=True)

# Damaged embedded payload: refused before anything is erased.
fresh(3)
payload = addr('install_payload')
u.mem_write(payload + 100, bytes([u.mem_read(payload + 100, 1)[0] ^ 0x40]))
assert run() != 0 and not writes and not restores
print('PASS corrupt payload rejected before touching any slot or store', flush=True)
