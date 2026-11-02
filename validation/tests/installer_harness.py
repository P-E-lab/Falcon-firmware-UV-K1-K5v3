"""Shared emulation harness for the installer tests: the actual linked ARM
installer runs under Unicorn; the NOR flash and multiboot services are mocked."""
import struct
import zlib
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[2]
B = ROOT / 'sources/installer/build/S4K'
RUNTIME = ROOT / 'sources/runtime/build/S4K'
elf = ELFFile(open(B / 'falcon.installer.elf', 'rb'))
syms = {s.name: (s['st_value'], s['st_info']['type'])
        for s in elf.get_section_by_name('.symtab').iter_symbols()}
fw = (B / 'falcon.installer.bin').read_bytes()
runtime = (RUNTIME / 'runtime.raw').read_bytes()
store = (RUNTIME / 'store.raw').read_bytes()

STORE_ASSET_BASE, STORE_ASSET_SIZE = 0x1CA000, 0x4000
STORE_BANK_BASE, STORE_BANK_SIZE = 0x1A2000, 0x8000
STORE_JOURNAL = 0x3F00
SLOT_BASE, SLOT_STRIDE = 0x20000, 0x20000
APPS = (0x102000, 0x182000)


def addr(n):
    if n not in syms:
        ks = [k for k in syms if k.startswith(n + '.')]
        assert len(ks) == 1, (n, ks)
        n = ks[0]
    a, t = syms[n]
    return a & ~1 if t == 'STT_FUNC' else a


u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for a, n in [(0x08000000, 0x40000), (0x20000000, 0x10000), (0x40000000, 0x40000),
             (0x48000000, 0x10000), (0x50000000, 0x10000), (0xe000e000, 0x2000)]:
    u.mem_map(a, n)

flash = bytearray(b'\xff' * 0x200000)
handlers = {}
writes = []
regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
STOP = 0x0803ff00
sim = {'target': None, 'internal_matches': True, 'cut_after': None}
restores = []


class PowerCut(Exception):
    pass


def reset():
    u.mem_write(0x08002800, fw)
    u.mem_write(0x20000000, bytes(65536))
    for sec in elf.iter_sections():
        if sec.name == '.data':
            u.mem_write(sec['sh_addr'], sec.data())


def ret(v=0):
    u.reg_write(UC_ARM_REG_R0, v)
    u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))


def _hook(uc, pc, n, _):
    if pc == STOP:
        uc.emu_stop()
    elif pc in handlers:
        handlers[pc](*(uc.reg_read(r) for r in regs))


u.hook_add(UC_HOOK_CODE, _hook)


def stub(n, f):
    for key in syms:
        if key == n or key.startswith(n + '.'):
            handlers[addr(key)] = f


def store_physical(slot, offset):
    if offset < STORE_ASSET_SIZE:
        return STORE_ASSET_BASE + slot * STORE_ASSET_SIZE + offset
    return STORE_BANK_BASE + slot * STORE_BANK_SIZE + offset - STORE_ASSET_SIZE


def valid_range(a, n):
    slots_end = SLOT_BASE + 5 * SLOT_STRIDE
    store_end = STORE_ASSET_BASE + 5 * STORE_ASSET_SIZE
    return (SLOT_BASE <= a <= slots_end and n <= slots_end - a) or \
           (STORE_BANK_BASE <= a <= store_end and n <= store_end - a)


def read(a, p, n, *_):
    assert valid_range(a, n), hex(a)
    u.mem_write(p, bytes(flash[a:a + n]))
    ret()


def write(a, p, n, *_):
    assert valid_range(a, n), hex(a)
    if sim['cut_after'] is not None:
        sim['cut_after'] -= 1
        if sim['cut_after'] < 0:
            sim['cut_after'] = None
            raise PowerCut()
    data = bytes(u.mem_read(p, n))
    assert all((o & v) == v for o, v in zip(flash[a:a + n], data)), 'program without erase at ' + hex(a)
    flash[a:a + n] = data
    writes.append(('W', a, n))
    ret()


def erase(a, n, *_):
    assert valid_range(a, n) and ((a | n) & 4095) == 0
    flash[a:a + n] = b'\xff' * n
    writes.append(('E', a, n))
    ret()


def state(p, *_):
    data = bytearray(24)
    data[16] = sim['target'] or 0
    u.mem_write(p, bytes(data))
    ret(2 if sim['target'] is None else 0)          # MB_MARK_MISSING / MB_MARK_VALID


def validate(slot, p, crc, *_):
    base = SLOT_BASE + slot * SLOT_STRIDE
    h = flash[base:base + 64]
    magic, version, flags, size, check = struct.unpack_from('<IHHII', h)
    ok = magic == 0x31424d46 and flags == 1 and 0 < size <= 120832 and \
        zlib.crc32(flash[base + 4096:base + 4096 + size]) == check
    if ok and p:
        u.mem_write(p, bytes(h))
    ret(0 if ok else 1)


def restore(slot, *_):
    restores.append(slot)
    ret()


def installed(slot):
    """The runtime and the whole store are in place for `slot`."""
    base = SLOT_BASE + slot * SLOT_STRIDE
    if flash[base + 4096:base + 4096 + len(runtime)] != runtime:
        return False
    for off in range(0, len(store), 256):
        chunk = store[off:off + 256]
        if STORE_JOURNAL <= off < STORE_ASSET_SIZE:
            continue
        a = store_physical(slot, off)
        if flash[a:a + len(chunk)] != chunk:
            return False
    return True


stub('INS_Read', read)
stub('INS_Write', write)
stub('INS_Erase', erase)
stub('MB_ReadActiveState', state)
stub('MB_InternalMatchesState', lambda *_: ret(int(sim['internal_matches'])))
stub('MB_ValidateSlot', validate)
stub('MB_RestoreSlot', restore)
stub('MB_Crc32Bytes', lambda p, n, *_: ret(zlib.crc32(bytes(u.mem_read(p, n)))))


def run():
    u.reg_write(UC_ARM_REG_SP, 0x20004000)
    u.reg_write(UC_ARM_REG_LR, STOP | 1)
    u.emu_start(addr('INS_Run') | 1, 0, count=400000000)
    assert u.reg_read(UC_ARM_REG_PC) == STOP, hex(u.reg_read(UC_ARM_REG_PC))
    return u.reg_read(UC_ARM_REG_R0)
