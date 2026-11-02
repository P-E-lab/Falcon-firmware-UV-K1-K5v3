"""Actual ARM: the Falcon messages (app/messages.c). FMessage's service writes
80-byte records at 0x012000 in two alternating 4 KiB sectors without the
sector cache; a session marker precedes the first message of an app session.
The MESSAGES page lists the 32 newest messages, newest first, with a marker
between two sessions, deletes one record by clearing a byte and clears both
sectors."""
from harness import *

BASE, SECTOR, SIZE, PER = 0x012000, 0x1000, 80, 51
erases, programs = [], []


def program(a, p, n, *unused):
    assert BASE <= a and a + n <= BASE + 2 * SECTOR, hex(a)
    data = u.mem_read(p, n)
    for i in range(n):
        flash[a + i] &= data[i]  # NOR programming only clears bits
    programs.append((a, n))
    ret()


def erase(a, *unused):
    a -= a % SECTOR
    erases.append(a)
    flash[a:a + SECTOR] = b'\xff' * SECTOR
    ret()


stub('PY25Q16_ProgramRaw', program)
stub('PY25Q16_EraseRaw', erase)
stub('PY25Q16_WriteBuffer', lambda *a: (_ for _ in ()).throw(AssertionError('no cached write')))

MSG = 0x20003800      # what FMessage hands over: callsign, 2 bytes, text
ROWS = 0x20003900     # MSG_Row_t rows[7]
OUT = 0x20003A00      # ordinal, messages
ROW = 21              # sizeof(MSG_Row_t)


def save(cs, text):
    u.mem_write(MSG, cs.encode() + b'\0\1' + text.encode())
    call('MSG_Save', MSG, len(text), count=20000000)


def call6(name, a0, a1, a2, a3, a4, a5):
    for reg, value in zip(regs, (a0, a1, a2, a3)):
        u.reg_write(reg, value)
    u.mem_write(0x20004000, struct.pack('<II', a4, a5))
    u.reg_write(UC_ARM_REG_SP, 0x20004000)
    u.reg_write(UC_ARM_REG_LR, STOP | 1)
    u.emu_start(addr(name) | 1, STOP, count=50000000)
    return u.reg_read(UC_ARM_REG_R0)


numbers = []


def fetch(first=0, count=7, selected=0xFFFF):
    u.mem_write(OUT, b'\0' * 4)
    assert call('APP_NativeBegin', 14)
    total = call6('MSG_Fetch', first, ROWS, count, selected, OUT, OUT + 2)
    call('APP_NativeEnd')
    rows = []
    for i in range(min(count, max(0, total - first))):
        slot, marker, cs, length, number, text = struct.unpack_from('<BB2sBB15s', u.mem_read(ROWS + i * ROW, ROW))
        rows.append('----' if marker else cs.decode() + ':' + text[:min(length, 15)].decode())
        if not marker:
            numbers.append(number)
    ordinal, messages = struct.unpack('<HH', u.mem_read(OUT, 4))
    return total, rows, ordinal, messages


# A blank zone: the first session starts with its marker, then the messages.
put8(addr('sessionPending'), 1)  # MSG_SessionStart, inlined into the app launch
save('AA', 'HELLO')
save('CD', 'RDV AU COL 14H ON SE RETROUVE AU PARKING DU HAUT, PRES DU REFUGE!')
rec = flash[BASE:BASE + 3 * SIZE]
assert rec[0] == ord('S') and rec[7] == 0xA5, rec[:8]
assert rec[SIZE] == ord('M') and rec[SIZE + 4:SIZE + 7] == b'AA\x05' and rec[SIZE + 7] == 0xA5
assert rec[2 * SIZE + 6] == 64 and bytes(rec[2 * SIZE + 8:2 * SIZE + 72]).startswith(b'RDV AU COL')
assert struct.unpack_from('<H', rec, SIZE + 2)[0] == 1 and struct.unpack_from('<H', rec, 2 * SIZE + 2)[0] == 2
assert not erases
total, rows, ordinal, messages = fetch(selected=1)
assert rows == ['CD:RDV AU COL 14H ', 'AA:HELLO'] and (total, ordinal, messages) == (2, 2, 2), rows
assert numbers[-2:] == [2, 1] and flash[BASE + 72] == 0 and flash[BASE + SIZE + 72] == 1, numbers
print('PASS records: marker, then messages, written outside the sector cache, commit byte last', flush=True)

# A second session: a marker between the two sessions, never above the newest.
put8(addr('sessionPending'), 1)  # MSG_SessionStart, inlined into the app launch
save('EF', 'SECOND SESSION')
total, rows, ordinal, messages = fetch(selected=0)
assert rows == ['EF:SECOND SESSION', '----', 'CD:RDV AU COL 14H ', 'AA:HELLO'], rows
assert (total, ordinal, messages) == (4, 1, 3)
total, rows, ordinal, messages = fetch(first=2, count=2, selected=3)
assert rows == ['CD:RDV AU COL 14H ', 'AA:HELLO'] and ordinal == 3, rows
print('PASS a dotted marker between two sessions; counter counts messages only', flush=True)

# Delete: one byte cleared, no erase; a marker left alone is not shown.
assert call('APP_NativeBegin', 14)
call('MSG_Delete', 4, count=20000000)  # EF: marker, AA, CD, marker, EF
call('APP_NativeEnd')
assert flash[BASE + 4 * SIZE + 1] == 0 and not erases
total, rows, ordinal, messages = fetch()
assert rows == ['CD:RDV AU COL 14H ', 'AA:HELLO'] and messages == 2, rows
save('XY', 'AFTER DELETE')  # the deleted EF keeps its number 3
numbers.clear()
assert fetch()[1][0] == 'XY:AFTER DELETE' and numbers[0] == 4, numbers
print('PASS delete clears one byte; an orphan marker is not listed', flush=True)

# Many messages: the sectors alternate, the newest 32 are listed.
for i in range(120):
    if i % 10 == 0:
        put8(addr('sessionPending'), 1)  # MSG_SessionStart, inlined into the app launch
    save('%02u' % (i % 100), 'MESSAGE %03u' % i)
assert erases == [BASE + SECTOR, BASE], [hex(e) for e in erases]  # 137 records: A, B, A again
total, rows, ordinal, messages = fetch(count=7, selected=0)
assert messages == 32 and ordinal == 1, (messages, ordinal)
for first in range(7, total, 7):  # the rows buffer holds seven rows
    rows += fetch(first=first, count=7)[1]
listed = [r for r in rows if r != '----']
assert listed == ['%02u:MESSAGE %03u' % (i % 100, i) for i in range(119, 87, -1)], listed[:3]
numbers.clear()
for first in range(0, total, 7):
    fetch(first=first, count=7)
assert numbers == [(4 + i) % 32 + 1 for i in range(119, 87, -1)], numbers  # 01..32, then 01 again
assert rows.count('----') == 3 and rows[0] != '----' and rows[-1] != '----', rows
print('PASS the sectors alternate (one erase each 51 records); the 32 newest, newest first', flush=True)

# CLEAR ALL erases both sectors.
assert call('APP_NativeBegin', 14)
call('MSG_Clear', count=20000000)
call('APP_NativeEnd')
assert flash[BASE:BASE + 2 * SECTOR] == b'\xff' * 2 * SECTOR
assert fetch() == (0, [], 0, 0)
save('ZZ', 'FIRST AGAIN')
numbers.clear()
assert fetch()[1] == ['ZZ:FIRST AGAIN'] and numbers == [1], numbers  # numbering starts again
assert call('APP_NativeBegin', 14)
call('MSG_Clear', count=20000000)
call('APP_NativeEnd')
print('PASS CLEAR ALL erases both sectors', flush=True)

# Foreign data in the zone is erased before use.
flash[BASE:BASE + 16] = b'NOT FALCON DATA!'
erases.clear()
put8(addr('sessionPending'), 1)  # MSG_SessionStart, inlined into the app launch
save('GH', 'AFTER')
assert erases and erases[0] == BASE and flash[BASE] == ord('S')
print('PASS foreign data in the zone is erased before the first record', flush=True)

# The MESSAGES page keys (HandleKeyMessages): M opens, 6 deletes and closes,
# EXIT closes; CLEAR ALL? clears on M, cancels on EXIT.
KEY_6, KEY_MENU, KEY_UP, KEY_DOWN, KEY_EXIT = 6, 10, 11, 12, 13
for f in ['ST7565_BlitStatusLine', 'ST7565_BlitFullScreen']:
    if f in sym or any(k.startswith(f + '.') for k in sym):
        stub(f, lambda *a: ret())
save('IJ', 'ONE')
save('KL', 'TWO')
put8(addr('historyMessages'), 1)
put16(addr('msgIndex'), 0)
assert call('APP_NativeBegin', 14)
call('RenderMessagesNative', count=50000000)
call('APP_NativeEnd')
assert u.mem_read(addr('msgCount'), 2) == b'\x03\x00'  # KL, IJ, GH (one session)
key = lambda k: call('HandleKeyMessages', k, count=50000000)
key(KEY_DOWN)
assert u.mem_read(addr('msgIndex'), 1)[0] == 1
key(KEY_MENU)
assert u.mem_read(addr('msgOpen'), 1)[0] == 1
key(KEY_DOWN)  # an open message ignores the arrows
assert u.mem_read(addr('msgIndex'), 1)[0] == 1
key(KEY_EXIT)
assert u.mem_read(addr('msgOpen'), 1)[0] == 0
ask = lambda: u.mem_read(addr('msgAsk'), 1)[0]
key(KEY_MENU)
key(KEY_6)  # DELETE? over the open message
assert ask() == 1 and u.mem_read(addr('msgOpen'), 1)[0] == 1
key(KEY_EXIT)  # cancelled: still open, nothing deleted
assert ask() == 0 and u.mem_read(addr('msgOpen'), 1)[0] == 1 and fetch()[3] == 3
key(KEY_6)
key(KEY_MENU)  # deleted (IJ), back to the list, Deleted bubble
assert ask() == 0 and u.mem_read(addr('msgOpen'), 1)[0] == 0
assert fetch()[1] == ['KL:TWO', 'GH:AFTER'], fetch()[1]
put8(addr('msgAsk'), 2)  # CLEAR ALL? (6 long)
key(KEY_EXIT)
assert ask() == 0 and fetch()[3] == 2
put8(addr('msgAsk'), 2)
key(KEY_MENU)
assert ask() == 0 and fetch()[3] == 0
print('PASS keys: arrows, M opens, EXIT returns; DELETE? and CLEAR ALL?: M confirms, EXIT cancels', flush=True)
