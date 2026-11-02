"""Actual ARM: the menu categories (ui/menu.c, app/menu.c). ALL lists the whole
MenuList; the other categories hold every other entry exactly once. EXIT goes
from the entries to the category screen, MENU opens the highlighted category,
each level keeps its cursor, digits pick a category, and leaving the menu
from the category screen reopens the last category opened on its last entry.
Writes the screens as PNG (x4) when given an output directory."""
import sys
from harness import *
from PIL import Image

for f in ['ST7565_BlitStatusLine', 'ST7565_BlitFullScreen', 'ST7565_SetContrast', 'BACKLIGHT_TurnOn',
          'BACKLIGHT_TurnOff', 'SETTINGS_SaveVfoNow']:
    if f in sym or any(k.startswith(f + '.') for k in sym):
        stub(f, lambda *a: ret())
for name in sym:
    if name.startswith(('BK4819_', 'BK1080_')):
        stub(name, lambda *a: ret())

KEY_MENU, KEY_UP, KEY_DOWN, KEY_EXIT = 10, 11, 12, 13
DISPLAY_MAIN, DISPLAY_MENU = 0, 1
CATS = ['ALL', 'CHANNELS', 'SCAN', 'KEYS', 'POWER', 'DISPLAY', 'TIMERS', 'AUDIO', 'RADIO', 'SERVICE']
EXPECTED = {
    'CHANNELS': ['Step', 'POWER', 'RxDCS', 'RxCTCS', 'TxDCS', 'TxCTCS', 'TxODir', 'TxOffs', 'BANDW', 'SetNFM',
                 'Scramb', 'Compnd', 'Mode', 'ChList', 'ChSave', 'ChDele', 'ChName'],
    'SCAN': ['PRICH1', 'PRICH2', 'PRICH3', 'PRICH4', 'SQLSCN', 'SQLHYS'],
    'KEYS': ['F1Shrt', 'F1Long', 'F2Shrt', 'F2Long', 'M Long', 'KeyLck', 'SetLck', 'BtnInv'],
    'POWER': ['BatSav', 'BatTxt'],
    'DISPLAY': ['POnMsg', 'PONSCR', 'LIGHT', 'BLTime', 'BLMin', 'BLMax', 'BLTxRx', 'SetCtr', 'SetInv', 'SetMet'],
    'TIMERS': ['TxTOut', 'SetTmr'],
    'AUDIO': ['Mic', 'MicBar', 'SetRxA', 'SetRxG', 'SetVol'],
    'RADIO': ['Sql', 'STE', 'RP STE', 'Roger', 'AUROG', 'Tone', 'VOX', 'RxMode'],
    'SERVICE': ['F Lock', 'BatCal', 'BatTyp', 'Reset'],  # FrCali: ENABLE_F_CAL_MENU is off
}

# MenuList as stored in the external resources: {name[7], menu_id}.
res = store_entry(0)[0] + addr('MenuList') - 0x91000000
menu = []
while store[res + len(menu) * 8]:
    e = store[res + len(menu) * 8:res + len(menu) * 8 + 8]
    menu.append((e[:7].split(b'\0')[0].decode(), e[7]))
name_of = {i: n for n, i in menu}
assert len(menu) == menu_count

get = lambda n: u.mem_read(addr(n), 1)[0]


def state():
    return get('gMenuCategories'), get('gMenuCategory'), get('gMenuCursor'), get('gMenuListCount')


def current():
    return name_of.get(call('UI_MENU_GetCurrentMenuId') & 0xFF)


def key(k):
    put8(addr('gScreenToDisplay'), DISPLAY_MENU)
    put8(addr('gRequestDisplayScreen'), 0xFF)
    call('MENU_ProcessKeys', k, 1, 0, count=20000000)


def leave_menu():
    put8(addr('gScreenToDisplay'), DISPLAY_MENU)
    call('GUI_SelectNextDisplay', DISPLAY_MAIN, count=20000000)


# Category contents: every entry once, in the agreed order.
put8(addr('gMenuCategories'), 0)
seen = []
for cat in range(1, len(CATS)):
    put8(addr('gMenuCategory'), cat)
    size = call('UI_MENU_CategorySize', cat)
    names = []
    for pos in range(size):
        put8(addr('gMenuCursor'), pos)
        names.append(current())
        assert name_of[menu[call('UI_MENU_ItemIndex', pos)][1]] == names[-1]
    assert names == EXPECTED[CATS[cat]], (CATS[cat], names)
    seen += names
assert sorted(seen) == sorted(n for n, _ in menu), set(n for n, _ in menu) ^ set(seen)
assert call('UI_MENU_CategorySize', 0) == menu_count
print('PASS categories: ALL = %u entries; the 9 others hold each entry once, in order' % menu_count)

# ALL, entry 06, EXIT: the category screen, on ALL.
put8(addr('gSetting_nav_invert'), 0)
put8(addr('gMenuCategory'), 0)
put8(addr('gMenuCursor'), 5)
put8(addr('gMenuListCount'), menu_count)
key(KEY_EXIT)
assert state() == (1, 0, 0, 10), state()
assert get('gRequestDisplayScreen') == DISPLAY_MENU
assert call('UI_MENU_GetCurrentMenuId') & 0xFF == 0xFF
# DOWN to CHANNELS, MENU: its first entry.
key(KEY_DOWN)
assert state()[2] == 1, state()
key(KEY_MENU)
assert state() == (0, 1, 0, 17) and current() == 'Step', (state(), current())
assert get('gFlagRefreshSetting')
key(KEY_DOWN)
key(KEY_DOWN)
assert current() == 'RxDCS'
# EXIT back to the categories on CHANNELS; ALL kept its entry 06.
key(KEY_EXIT)
assert state() == (1, 1, 1, 10), state()
key(KEY_UP)
key(KEY_MENU)
assert state() == (0, 0, 5, menu_count), state()
key(KEY_EXIT)
key(KEY_DOWN)
key(KEY_MENU)
assert state() == (0, 1, 2, 17) and current() == 'RxDCS', state()
print('PASS keys: EXIT shows the categories, MENU opens one, each keeps its cursor')

# Digits pick a category: 1 then 0 is SERVICE.
key(KEY_EXIT)
key(1)
key(0)
assert state()[:3] == (1, 1, 9), state()
key(KEY_MENU)
assert current() == 'F Lock' and state()[3] == 4, (current(), state())
print('PASS digits: 10 selects SERVICE on the category screen')

# EXIT from the categories leaves the menu; MENU reopens the last category
# opened (SERVICE), even after moving to another one before leaving.
key(KEY_EXIT)
key(KEY_UP)
key(KEY_EXIT)
assert get('gRequestDisplayScreen') == DISPLAY_MAIN
leave_menu()
assert state() == (0, 9, 0, 4) and current() == 'F Lock', state()
# Leaving from the entries keeps them.
key(KEY_DOWN)
leave_menu()
assert state() == (0, 9, 1, 4) and current() == 'BatCal', state()
print('PASS leaving: the menu reopens on the last category opened and its entry')

# Screens: category screen, entries of a category, battery text rules.
if len(sys.argv) > 1:
    OUTDIR = Path(sys.argv[1])
    OUTDIR.mkdir(parents=True, exist_ok=True)


def shot(name):
    if len(sys.argv) < 2:
        return
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


def header_gap():
    """Free columns between the counter and the battery text in the header."""
    st = u.mem_read(addr('gStatusLine'), 128)
    cols = [x for x in range(128) if st[x] & 0x3E != 0x3E]  # white ink inside the bar
    gaps = [b - a - 1 for a, b in zip(cols, cols[1:])]
    return max(gaps)


put16(addr('gBatteryVoltageAverage'), 812)
for mode in (3, 2):
    put8(addr('gSetting_battery_text'), mode)
    key(KEY_EXIT)                                 # category screen on SERVICE
    key(1)                                        # 01 ALL
    call('UI_DisplayMenu', count=50000000)
    shot('menu-categories-bat%u' % mode)
    st_all = bytes(u.mem_read(addr('gStatusLine'), 128))
    key(KEY_DOWN)                                 # CHANNELS
    key(KEY_MENU)
    call('UI_DisplayMenu', count=50000000)
    shot('menu-channels-bat%u' % mode)
    if mode == 3:
        assert header_gap() >= 6, header_gap()
print('PASS screens: header counter and battery never touch')
