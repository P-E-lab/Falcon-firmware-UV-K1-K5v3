"""Screens of the persistent popups (edit actions, keys infos), rendered by
the actual ARM code (PNG, x4)."""
import sys
from harness import *
from PIL import Image

OUTDIR = Path(sys.argv[1])
for f in ['ST7565_BlitStatusLine', 'ST7565_BlitFullScreen']:
    if f in sym or any(k.startswith(f + '.') for k in sym):
        stub(f, lambda *a: ret())
CE = addr('channelEditor')
STATE, ACTION, SURE, HELP, LIST, BAND = 44, 45, 62, 63, 64, 65


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


def background():
    # a busy background, to show the white margin around the frames
    u.mem_write(addr('gFrameBuffer'), b'\x55' * 7 * 128)
    u.mem_write(addr('gStatusLine'), b'\x55' * 128)


for i, (lst, band, action, sure) in enumerate([(0, 0, 0, 0), (1, 0, 1, 0), (0, 1, 2, 1)]):
    background()
    put8(CE + STATE, 1); put8(CE + ACTION, action); put8(CE + SURE, sure)
    put8(CE + LIST, lst); put8(CE + BAND, band)
    assert call('APP_NativeBegin', 2)
    call('ChannelDisplay', count=50000000)
    call('APP_NativeEnd')
    shot('5-actions-%u' % i)
put8(CE + STATE, 0)
background()
put8(addr('historyMessages'), 0)
put8(addr('historyListActive'), 1)
put8(addr('selectionHelp'), 1)
call(next(k for k in sym if k.startswith('RenderHistoryList.')), count=50000000)
shot('6-history-keys')
print('ok')
