"""Screens of the MESSAGES page, rendered by the actual ARM code (PNG, x4)."""
import sys
from harness import *
from PIL import Image

OUTDIR = Path(sys.argv[1])
BASE, SECTOR = 0x012000, 0x1000
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
MSG = 0x20003800
def save(cs, text):
    u.mem_write(MSG, cs.encode() + b'\0\1' + text.encode())
    call('MSG_Save', MSG, len(text), count=20000000)
def shot(name):
    fb = u.mem_read(addr('gFrameBuffer'), 7 * 128)
    st = u.mem_read(addr('gStatusLine'), 128)
    img = Image.new('1', (128, 64), 1)
    for x in range(128):
        for y in range(8):
            if st[x] >> y & 1: img.putpixel((x, y), 0)
        for p in range(7):
            for y in range(8):
                if fb[p * 128 + x] >> y & 1: img.putpixel((x, 8 + p * 8 + y), 0)
    img.resize((512, 256), Image.NEAREST).save(OUTDIR / (name + '.png'))
def render():
    u.mem_write(addr('gFrameBuffer'), b'\0' * 7 * 128)
    u.mem_write(addr('gStatusLine'), b'\0' * 128)
    assert call('APP_NativeBegin', 14)
    call('RenderMessagesNative', count=50000000)
    call('APP_NativeEnd')

put8(addr('sessionPending'), 1)
save('AA', 'RDV AU COL 14H ON SE RETROUVE AU PARKING DU HAUT')
save('CD', 'OK BIEN RECU, J ARRIVE')
put8(addr('sessionPending'), 1)
save('EF', 'QRV SUR LE CANAL 8 ?')
save('GH', 'TEST 1 2 3')
put8(addr('historyMessages'), 1)
put8(addr('historyListActive'), 1)
put16(addr('msgIndex'), 1)
render(); shot('1-list')
put8(addr('msgOpen'), 1); render(); shot('2-open')
put8(addr('msgAsk'), 1); render(); shot('2b-delete')
put8(addr('msgOpen'), 0); put8(addr('msgAsk'), 2); render(); shot('3-clear')
put8(addr('msgAsk'), 0); put8(addr('selectionHelp'), 1); render(); shot('4-help')
print('ok')
