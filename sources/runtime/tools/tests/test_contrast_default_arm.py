"""Actual ARM: display settings never written (erased flash) or a contrast out
of range give the default contrast 10 (was 15); a saved contrast is kept."""
from harness import *

for n in sym:
    if n.startswith(('ST7565_', 'BK4819_', 'BK1080_', 'SYSTEM_DelayMs', 'SYSTICK_')):
        stub(n, lambda *a: ret())
for n in ['BOARD_ADC_GetBatteryInfo', 'PY25Q16_WriteBuffer', 'RADIO_ConfigureChannel', 'RADIO_SelectVfos',
          'BACKLIGHT_TurnOn', 'BACKLIGHT_SetBrightness']:
    for k in list(sym):
        if k == n or k.startswith(n + '.'):
            stub(k, lambda *a: ret())
def contrast(byte5):
    if byte5 is not None:
        flash[0xA158 + 5] = byte5
    call('SETTINGS_InitEEPROM', count=50000000)
    return u.mem_read(addr('gSetting_set_ctr'), 1)[0]
assert contrast(None) == 10   # erased: 0xFF
assert contrast(0x07) == 7    # saved
assert contrast(0x20) == 10   # 0: out of range
assert contrast(0x4F) == 15   # a saved 15 stays 15
print('PASS contrast: 10 on never-written settings or 0, a saved value kept')
