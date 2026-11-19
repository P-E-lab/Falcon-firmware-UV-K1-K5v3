"""Actual ARM: the POCSAG journal (app/pocsaglog.c) and the history's POCSAG
page. Records in order, the chain back through the sectors, the wrap that
erases the oldest sector, a power-on again (rescan, ages unknown), delete,
clear, and the page drawn (list, open page, help). POCSAG_PNG=dir saves the
screens (x4)."""
import os
import struct
from harness import *
from PIL import Image

SECTOR = 0x1000
BASE, SECTORS = 0x182000, 8


def program(a, p, n, *unused):
    data = u.mem_read(p, n)
    for i in range(n):
        flash[a + i] &= data[i]
    ret()


def erase(a, *unused):
    a -= a % SECTOR
    flash[a:a + SECTOR] = b'\xff' * SECTOR
    ret()


stub('PY25Q16_ProgramRaw', program)
stub('PY25Q16_EraseRaw', erase)
for f in ['ST7565_BlitStatusLine', 'ST7565_BlitFullScreen']:
    if f in sym or any(k.startswith(f + '.') for k in sym):
        stub(f, lambda *a: ret())

BUF = 0x20003800
PLOG_SESSION, PLOG_SKIP, PLOG_NONE = -1, -2, -3


def pack(text):
    return text.encode()                                     # one byte a character


def page(ric, text, channel=0xFFFF, t=0, freq=17351250, rssi=-89, func=3):
    return struct.pack('<IIIhHBBH', ric, t, freq, rssi, channel, func, 0, len(text)) + pack(text)


def append(payload):
    u.mem_write(BUF, payload)
    return call('PLOG_Append', BUF, len(payload), count=20000000)


def read(a, n=2048):
    r = call('PLOG_Read', a, BUF, n)
    r = r - (1 << 32) if r & 0x80000000 else r
    return r, bytes(u.mem_read(BUF, max(r, 0))) if r > 0 else b''


def chain():
    out, a = [], call('PLOG_Prev', 0, count=20000000)
    while a:
        out.append(a)
        a = call('PLOG_Prev', a, count=20000000)
    return out


def native(name, *args, count=200000000):
    assert call('APP_NativeBegin', 14)
    r = call(name, *args, count=count)
    call('APP_NativeEnd')
    return r


def ric_of(a):
    r, data = read(a)
    return struct.unpack_from('<I', data)[0] if r > 0 else r


# empty
assert call('PLOG_Prev', 0) == 0
# a session of three pages: newest first, then the marker
put8(addr('pl') + 1, 1)  # PLOG_SessionStart (inlined in the loader)
first = [append(page(1000 + k, 'PAGE %d' % k)) for k in range(3)]
assert all(first) and first == sorted(first)
c = chain()
assert [ric_of(a) for a in c] == [1002, 1001, 1000, PLOG_SESSION], [ric_of(a) for a in c]
r, data = read(first[1])
assert r == len(page(1001, 'PAGE 1')) and data == page(1001, 'PAGE 1')
print('PASS three pages after a session marker, read back exactly, chain newest first', flush=True)

# a long page (400 characters) and the wrap: 2600 pages of 50 characters
LONG = ''.join(chr(0x20 + (i * 7) % 95) for i in range(400))
a = append(page(5000, LONG))
r, data = read(a)
assert r == 20 + len(pack(LONG)) and data == page(5000, LONG)
for k in range(2600):
    assert append(page(10000 + k, 'SDIS %04d INTERVENTION VSAV RUE DES LILAS 12 NORD' % k))
c = chain()
rics = [ric_of(a) for a in c]
assert rics[0] == 12599 and rics == list(range(12599, 12599 - len(rics), -1)), rics[:5]
used = sum(1 for s in range(SECTORS) if flash[BASE + s * SECTOR:BASE + s * SECTOR + 4] == b'PLG1')
assert used == SECTORS and 330 < len(rics) < 400, (used, len(rics))  # 79 B a 49-character page
print('PASS 2600 pages: the oldest sector erased in turn, %d kept, chain in order across %d sectors'
      % (len(rics), SECTORS), flush=True)

# power-on again: the journal is found back, ages of the old pages unknown
u.mem_write(addr('pl'), b'\0' * 20)
assert [ric_of(a) for a in chain()][:3] == [12599, 12598, 12597]
n1 = append(page(20000, 'AFTER POWER ON'))
assert ric_of(chain()[0]) == 20000
assert native('PLOG_SameBoot', n1) == 1 and native('PLOG_SameBoot', chain()[5]) == 0
print('PASS power-on again: newest found back, a new page follows it, old pages of another power-on', flush=True)

# delete and clear
native('PLOG_Delete', n1)
assert read(n1)[0] == PLOG_SKIP and ric_of(chain()[0]) == PLOG_SKIP
native('PLOG_Clear')
assert call('PLOG_Prev', 0) == 0
assert append(page(1, 'X')) and len(chain()) == 1
print('PASS delete (skipped), clear (empty, then a new journal)', flush=True)

# ---- the POCSAG page ----
native('PLOG_Clear')
for ch, nm in [(31, b'SDIS32'), (465, b'POCSAG466')]:   # a name longer than 8: cut to 8
    flash[NAME_BASE + 16 * ch:NAME_BASE + 16 * ch + 10] = nm.ljust(10, b'\xff')
    attrs[ch] = (1 << 8) | 5
tick = addr('gGlobalSysTickCounter')
put32(tick, 200000)                                        # 2000 s since power-on
put8(addr('pl') + 1, 1)  # PLOG_SessionStart (inlined in the loader)
append(page(1234567, 'ALERTE CO RUE HAUTE', 31, t=2000000 - 9000000 % 2000000))
put8(addr('pl') + 1, 1)  # PLOG_SessionStart (inlined in the loader)
append(page(2222222, 'RAPPEL GARDE 18H', 465, t=1500000))
append(page(1234567, 'INTERVENTION VSAV RUE DES LILAS 12 - SECTEUR NORD - VSAV 1 ET FPT 2 DEPART '
            'IMMEDIAT - CODE 3 - RAPPEL DU PERSONNEL D ASTREINTE', 31, t=1700000))
append(page(1234560, 'TEST 1 2 3', 31, t=1880000))


def shot(name):
    out = os.environ.get('POCSAG_PNG')
    if not out:
        return
    fb = u.mem_read(addr('gFrameBuffer'), 7 * 128)
    st = u.mem_read(addr('gStatusLine'), 128)
    img = Image.new('1', (128, 64), 1)
    for x in range(128):
        for y in range(8):
            if st[x] >> y & 1:
                img.putpixel((x, y), 0)
        for p in range(7):
            for y in range(8):
                if fb[p * 128 + x] >> y & 1:
                    img.putpixel((x, 8 + p * 8 + y), 0)
    img.resize((512, 256), Image.NEAREST).save(os.path.join(out, name + '.png'))


def render():
    u.mem_write(addr('gFrameBuffer'), b'\0' * 7 * 128)
    u.mem_write(addr('gStatusLine'), b'\0' * 128)
    assert call('APP_NativeBegin', 14)
    try:
        call('RenderJournalNative', count=200000000)
    except Exception:
        pc, lr = u.reg_read(UC_ARM_REG_PC), u.reg_read(UC_ARM_REG_LR)
        near = lambda a: min(((a - (v & ~1), k) for k, v in sym.items() if typ.get(k) == 'STT_FUNC' and 0 <= a - (v & ~1) < 0x3000), default=None)
        print('CRASH pc %08X %s lr %08X %s r0 %08X' % (pc, near(pc), lr, near(lr & ~1), u.reg_read(UC_ARM_REG_R0)))
        raise
    call('APP_NativeEnd')


put8(addr('historyMessages'), 1)
put8(addr('historyPocsag'), 1)
put8(addr('historyListActive'), 1)
put16(addr('msgIndex'), 1)
render(); shot('pocsag-1-list')
assert struct.unpack('<H', u.mem_read(addr('msgCount'), 2))[0] == 5          # 4 pages and a marker
fb = bytes(u.mem_read(addr('gFrameBuffer'), 7 * 128))
assert any(fb)
put8(addr('msgOpen'), 1); render(); shot('pocsag-2-open')
assert u.mem_read(addr('msgOpen'), 1)[0] == 1
put8(addr('plTop'), 50); render(); shot('pocsag-3-scrolled')
assert u.mem_read(addr('plTop'), 1)[0] == 3                                  # 7 lines, 4 shown
put8(addr('plTop'), 0); put8(addr('msgOpen'), 0); put8(addr('selectionHelp'), 1); render(); shot('pocsag-4-help')
print('PASS POCSAG page: list with a session line, open page scrolled to its last lines, help', flush=True)
