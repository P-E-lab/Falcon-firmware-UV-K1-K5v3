"""Partial LCD refresh on the actual ARM build: only changed pages are sent,
every other LCD write path forces those pages again, and a complete frame is
still sent at least once per second."""
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[2]
B = ROOT / 'build/S4K'
fw = (B / 'falcon.data.bin').read_bytes()
elf = ELFFile(open(B / 'falcon.data.elf', 'rb'))
sym = {x.name: x['st_value'] for x in elf.get_section_by_name('.symtab').iter_symbols()}

def addr(name):
    keys = [k for k in sym if k == name or k.startswith(name + '.')]
    assert len(keys) == 1, (name, keys)
    return sym[keys[0]] & ~1

u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for a, n in [(0x08000000, 0x40000), (0x20000000, 0x10000), (0x40000000, 0x40000),
             (0x48000000, 0x10000), (0x50000000, 0x10000), (0xe000e000, 0x2000)]:
    u.mem_map(a, n)
u.mem_write(0x08002800, fw)
for sec in elf.iter_sections():
    if sec.name == '.data':
        u.mem_write(sec['sh_addr'], sec.data())

SPI1 = 0x40013000
GPIOA = 0x50000000
A0_MASK = 1 << 6
state = {'a0': 0, 'page': None, 'bytes': {}}

def mem_write(uc, access, address, size, value, _):
    if address == SPI1 + 0x0C:
        if state['a0']:
            state['bytes'][state['page']] = state['bytes'].get(state['page'], 0) + 1
        elif (value & 0xF0) == 0xB0:
            state['page'] = value & 0x0F
    elif address == GPIOA + 0x18 and value & A0_MASK:      # BSRR set
        state['a0'] = 1
    elif address == GPIOA + 0x28 and value & A0_MASK:      # BRR reset
        state['a0'] = 0

def mem_read(uc, access, address, size, value, _):
    if address == SPI1 + 0x08:
        uc.mem_write(SPI1 + 0x08, struct.pack('<I', 0x3))  # TXE | RXNE

u.hook_add(UC_HOOK_MEM_WRITE, mem_write, begin=SPI1, end=SPI1 + 0x20)
u.hook_add(UC_HOOK_MEM_WRITE, mem_write, begin=GPIOA, end=GPIOA + 0x40)
u.hook_add(UC_HOOK_MEM_READ, mem_read, begin=SPI1, end=SPI1 + 0x20)
STOP = 0x0803ff00

def call(name, *args):
    for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args):
        u.reg_write(reg, value)
    u.reg_write(UC_ARM_REG_SP, 0x20003ff0)
    u.reg_write(UC_ARM_REG_LR, STOP | 1)
    u.emu_start(addr(name) | 1, STOP, count=5_000_000)

FB = addr('gFrameBuffer')
TICK = addr('gGlobalSysTickCounter')

def tick(t):
    u.mem_write(TICK, struct.pack('<I', t))

def blit():
    state['bytes'] = {}
    call('ST7565_BlitFullScreen')
    return sorted(p - 1 for p in state['bytes'] if p)       # LCD page n = frame page n-1

def set_fb(page, col, value):
    u.mem_write(FB + page * 128 + col, bytes([value]))

tick(1000)
assert blit() == list(range(7)), 'first frame must be complete'
assert blit() == [], 'identical frame must send nothing'
set_fb(2, 10, 0xFF); set_fb(3, 10, 0xFF)
assert blit() == [2, 3], 'only the moved selection pages are sent'
assert all(n == 128 + 4 or n == 128 for n in state['bytes'].values())
print('PASS identical frame sends nothing; a moved selection sends only its two pages')

# A partial draw from the frame buffer, then the frame returns to its old
# content: the page must be sent again (its fingerprint is stale).
set_fb(5, 0, 0x55)
state['bytes'] = {}; call('ST7565_DrawLine', 0, 6, FB + 5 * 128, 22)
set_fb(5, 0, 0x00)
assert blit() == [5], 'page touched by a partial draw is refreshed'
assert blit() == []
# A raw page write (apps API blit_page) and a full raw write.
call('ST7565_BlitScreen', 4)
assert blit() == [3], 'raw page write invalidates that page'
call('ST7565_BlitScreen', 8)
assert blit() == list(range(7)), 'raw full write invalidates every page'
for fn in ['ST7565_FixInterfGlitch', 'ST7565_ContrastAndInv', 'ST7565_Init']:
    if not any(k == fn or k.startswith(fn + '.') for k in sym):
        continue                                    # inlined by LTO into its callers
    blit(); call(fn)
    assert blit() == list(range(7)), fn
call('ST7565_FillScreen', 0)
assert blit() == list(range(7)), 'fill invalidates'
print('PASS partial draws, raw page/full writes, fills and controller resets force a resend')

tick(1000 + 101)
assert blit() == list(range(7)), 'a complete frame at least once per second'
assert blit() == []
print('PASS complete frame at least once per second, partial in between')
