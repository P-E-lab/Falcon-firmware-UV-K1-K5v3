"""Shared emulation harness for the runtime tests: the actual optimized
Cortex-M0+ ELF runs under Unicorn with the external NOR flash, the hardware
CRC unit and a few drivers mocked. Tests do `from harness import *`."""
import json
import os
import shlex
import struct
import subprocess
import zlib
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE,
                     UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ)
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[2]
B = Path(os.environ.get('FALCON_TEST_BUILD', str(ROOT / 'build/S4K')))
# Edition of the build under test: Falcon 1K (S1K) keeps 1024 channels in the
# config bank, Falcon 4K (S4K) 4096 in the Sonic 4K reserve.
K1 = B.name == 'S1K'
MR_MAX = 1024 if K1 else 4096
CH_BASE, NAME_BASE = (0x000000, 0x004000) if K1 else (0x1DE000, 0x1EE000)
fw = (B / 'falcon.data.bin').read_bytes()
elf = ELFFile(open(B / 'falcon.data.elf', 'rb'))
_symtab = list(elf.get_section_by_name('.symtab').iter_symbols())
sym = {x.name: x['st_value'] for x in _symtab}
typ = {x.name: x['st_info']['type'] for x in _symtab}


def addr(name):
    if name not in sym:
        keys = [k for k in sym if k.startswith(name + '.')]
        assert len(keys) == 1, (name, keys)
        name = keys[0]
    return sym[name] & ~1 if typ[name] == 'STT_FUNC' else sym[name]


u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for a, n in [(0x08000000, 0x40000), (0x20000000, 0x10000), (0x40000000, 0x40000),
             (0x48000000, 0x10000), (0x50000000, 0x10000), (0xe000e000, 0x2000),
             (0x1fff0000, 0x10000)]:
    u.mem_map(a, n)
u.mem_write(0x08002800, fw)
for sec in elf.iter_sections():
    if sec.name == '.data':
        u.mem_write(sec['sh_addr'], sec.data())

flash = bytearray(b'\xff' * 0x200000)
attrs = {}
popups = []
handlers = {}
regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
STOP = 0x0803ff00


def ret(value=0):
    u.reg_write(UC_ARM_REG_R0, value)
    u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))


def _hook(uc, pc, n, _):
    if pc == STOP:
        uc.emu_stop()
        return
    if pc in handlers:
        handlers[pc](*[uc.reg_read(r) for r in regs])


u.hook_add(UC_HOOK_CODE, _hook)


def stub(name, fn):
    for key in sym:
        if key == name or key.startswith(name + '.'):
            handlers[addr(key)] = fn


# --- PY32 hardware CRC unit (CRC-32/MPEG-2 over 32-bit words) ---------------
CRC_DR, CRC_CR = 0x40023000, 0x40023008
_crc = [0xFFFFFFFF]


def hw_crc_update(crc, word):
    crc ^= word
    for _ in range(32):
        crc = ((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else (crc << 1)
        crc &= 0xFFFFFFFF
    return crc


def _crc_write(uc, access, address, size, value, _):
    if address == CRC_CR and value & 1:
        _crc[0] = 0xFFFFFFFF
    elif address == CRC_DR:
        _crc[0] = hw_crc_update(_crc[0], value & 0xFFFFFFFF)


def _crc_read(uc, access, address, size, value, _):
    if address == CRC_DR:
        uc.mem_write(CRC_DR, struct.pack('<I', _crc[0]))


u.hook_add(UC_HOOK_MEM_WRITE, _crc_write, begin=CRC_DR, end=CRC_CR + 4)
u.hook_add(UC_HOOK_MEM_READ, _crc_read, begin=CRC_DR, end=CRC_DR + 4)


# --- external NOR flash ---------------------------------------------------
def rd(a, p, n, *_):
    assert 0 <= a <= len(flash) - n, (hex(a), n)
    u.mem_write(p, bytes(flash[a:a + n]))
    if 0x20000280 <= p < 0x20001280:
        u.ctl_remove_cache(p, p + n)
    ret()


stub('PY25Q16_ReadBuffer', rd)
stub('PY25Q16_ReadBufferSafe', rd)
stub('PY25Q16_ReadPhysical', rd)
stub('PY25Q16_ReadChecked', lambda a, p, n, *x: (rd(a, p, n), ret(1)))
stub('PY25Q16_EndBatch', lambda *a: ret())
stub('MB_Crc32Bytes', lambda p, n, *a: ret(zlib.crc32(bytes(u.mem_read(p, n)))))
stub('ST7565_BlitFullScreen', lambda *a: ret())
stub('UI_DisplayPopup', lambda p, *a: (popups.append(bytes(u.mem_read(p, 32)).split(b'\0')[0]), ret()))


def attr(ch, *a):
    u.mem_write(0x20005000, struct.pack('<H', attrs.get(ch, 7)))
    ret(0x20005000)


stub('MR_GetChannelAttributes', attr)


def call(name, *args, count=2000000):
    for reg, value in zip(regs, args):
        u.reg_write(reg, value)
    u.mem_write(0x20004000, struct.pack('<I', args[4] if len(args) > 4 else 0))
    u.reg_write(UC_ARM_REG_SP, 0x20004000)
    u.reg_write(UC_ARM_REG_LR, STOP | 1)
    pc = addr(name)
    for _ in range(8):
        u.emu_start(pc | 1, 0, count=count)
        pc = u.reg_read(UC_ARM_REG_PC)
        if pc == STOP:
            break
    assert u.reg_read(UC_ARM_REG_PC) == STOP, (name, hex(u.reg_read(UC_ARM_REG_PC)))
    return u.reg_read(UC_ARM_REG_R0)


def put8(a, v): u.mem_write(a, bytes([v]))
def put16(a, v): u.mem_write(a, struct.pack('<H', v))
def put32(a, v): u.mem_write(a, struct.pack('<I', v))
def get32(a): return struct.unpack('<I', u.mem_read(a, 4))[0]


# --- external store (store_layout.h) -----------------------------------------
STORE_ASSET_BASE, STORE_ASSET_SIZE = 0x1CA000, 0x4000
STORE_BANK_BASE, STORE_BANK_SIZE = 0x1A2000, 0x8000
store = (B / 'store.raw').read_bytes()


def store_physical(slot, offset):
    if offset < STORE_ASSET_SIZE:
        return STORE_ASSET_BASE + slot * STORE_ASSET_SIZE + offset
    return STORE_BANK_BASE + slot * STORE_BANK_SIZE + offset - STORE_ASSET_SIZE


def place_store(slot, image=store):
    asset, bank = image[:STORE_ASSET_SIZE], image[STORE_ASSET_SIZE:]
    flash[store_physical(slot, 0):store_physical(slot, 0) + len(asset)] = asset
    if bank:
        b = store_physical(slot, STORE_ASSET_SIZE)
        flash[b:b + len(bank)] = bank


def store_entry(index, image=store):
    return struct.unpack_from('<3I', image, 0x20 + 16 + index * 12)


place_store(0)
put8(addr('gRunningSlot'), 0)

# Compile constant layout offsets using this build's actual feature definitions.
commands = json.loads((B / 'compile_commands.json').read_text())
c = commands[0]
_args = shlex.split(c['command'])
out = []
_i = 0
while _i < len(_args):
    if _args[_i] in ['-o', '-c']:
        _i += 2
        continue
    if not _args[_i].startswith('-flto'):
        out.append(_args[_i])
    _i += 1
out += ['-c', str(Path(__file__).with_name('layout_probe.c')), '-o', str(B / 'layout_probe.o')]
subprocess.run(out, cwd=c['directory'], check=True)
probe = ELFFile(open(B / 'layout_probe.o', 'rb'))
L = struct.unpack('<11I', probe.get_section_by_name('.rodata.layout').data())
rxmode, vfooff, vfosize, prx, ptx, save, frx, ftx, txindex, rxindex, prio = L
eep = addr('gEeprom')

# Mirror the boot-time MenuList sentinel walk; menu tests must not run with
# the zero-initialized count left behind by skipping main().
_res_offset = store_entry(0)[0]
menu_count = 0
while store[_res_offset + addr('MenuList') - 0x91000000 + menu_count * 8]:
    menu_count += 1
assert 40 < menu_count < 100
put8(addr('gMenuListCount'), menu_count)


def install(module, slot):
    data = module.read_bytes()
    h = struct.unpack('<IHBBIIHH16s16sIIHH', data[:64])
    base = 0x102000 + 8192 * slot
    flash[base:base + 8192] = b'\xff' * 8192
    flash[base:base + 64] = data[:64]
    flash[base + 4096:base + 4096 + h[4]] = data[64:64 + h[4]]
    flash[base + 256:base + 256 + h[12]] = data[64 + h[4]:]
    return h
