"""Exercise actual optimized Cortex-M0 ELF with mocked external hardware."""
from harness import *

golddata=json.loads(Path(__file__).with_name('resources_reference.json').read_text())
def gold(name):
 value=bytes.fromhex(golddata[name])
 if name=='MenuList':
  rows=[value[i:i+8] for i in range(0,len(value),8)]
  at=next(i for i,row in enumerate(rows) if row[:7].split(b'\0')[0]==b'PRICH2')
  rows.insert(at+1,b'PRICH3\0'+bytes([rows[at][7]+1]))
  rows.insert(at+2,b'PRICH4\0'+bytes([rows[at][7]+2]))
  for i,row in enumerate(rows):
   if row[:7].split(b'\0')[0] in [b'SQLSCN',b'SQLHYS']:rows[i]=row[:7]+bytes([row[7]+2])
  # VOX follows AUROG in the list and in the menu ids.
  at=next(i for i,row in enumerate(rows) if row[:7].split(b'\0')[0]==b'AUROG')
  vox=rows[at][7]+1
  rows=[row[:7]+bytes([row[7]+(row[7]>=vox and row[7]!=0xff)]) for row in rows]
  rows.insert(at+1,b'VOX\0\0\0\0'+bytes([vox]))
  # PONSCR follows POnMsg in the list and in the menu ids.
  at=next(i for i,row in enumerate(rows) if row[:7].split(b'\0')[0]==b'POnMsg')
  pon=rows[at][7]+1
  rows=[row[:7]+bytes([row[7]+(row[7]>=pon and row[7]!=0xff)]) for row in rows]
  rows.insert(at+1,b'PONSCR\0'+bytes([pon]))
  # SetRxG (F4HWN v7) follows SetRxA in the list and in the menu ids.
  at=next(i for i,row in enumerate(rows) if row[:7].split(b'\0')[0]==b'SetRxA')
  rxg=rows[at][7]+1
  rows=[row[:7]+bytes([row[7]+(row[7]>=rxg and row[7]!=0xff)]) for row in rows]
  rows.insert(at+1,b'SetRxG\0'+bytes([rxg]))
  value=b''.join(rows)
 return value
scratch=0x20005000
fb=addr('gFrameBuffer')
def clear():u.mem_write(fb,bytes(896))
def frame():return bytes(u.mem_read(fb,896))
for name,width in [('gFontSmall',6),('gFontSmallBold',6),('gFont3x5',3),('gFont5x5Upper',5),('gFont5x5Lower',5),('gFont5x5Digits',5),('MenuList',8),('gFontBigDigits',26),('fontBigGlyphs',14)]:
 expected=gold(name);call('FP_Read',addr(name),scratch,len(expected));assert bytes(u.mem_read(scratch,len(expected)))==expected,name
print('PASS all moved font and MenuList bytes match compiled resource reference',flush=True)
for ch in range(33,127):
 u.mem_write(scratch,bytes([ch,0]))
 for name,api in [('gFontSmall','UI_PrintStringSmallNormal'),('gFontSmallBold','UI_PrintStringSmallBold')]:
  clear();call(api,scratch,0,0,0);want=bytearray(896);want[1:7]=gold(name)[(ch-33)*6:(ch-32)*6];assert frame()==want,(api,ch)
 clear();call('GUI_DisplaySmallest',scratch,0,0,0,1);want=bytearray(896);want[:3]=bytes(v&63 for v in gold('gFont3x5')[(ch-32)*3:(ch-31)*3]);assert frame()==want,('tiny',ch)
for name,chars in [('gFont5x5Upper',range(65,91)),('gFont5x5Lower',range(97,123)),('gFont5x5Digits',range(48,58))]:
 for i,ch in enumerate(chars):
  clear();u.mem_write(scratch,bytes([ch,0]));call('UI_DrawText5x5',scratch,0,6,1);want=bytearray(896);rows=gold(name)[i*5:i*5+5]
  for row,v in enumerate(rows):
   for col in range(5):
    if v&(1<<(4-col)):want[(6+row)//8*128+col]|=1<<((6+row)%8)
  assert frame()==want,(name,ch)
print('PASS 344 glyph renders including page boundaries: exact TEST4 pixels',flush=True)
for index in range(menu_count):
 put8(addr('gMenuCursor'),index);assert call('UI_MENU_GetCurrentMenuId')==gold('MenuList')[index*8+7]
print('PASS all menu IDs read from external MenuList',flush=True)
# Valid VFOs for menu limits/formatting; no physical radio writes.
for i in range(2):call('RADIO_InitInfo',eep+vfooff+i*vfosize,4096,14550000+i*100000)
for name in ['gTxVfo','gRxVfo','gCurrentVfo']:put32(addr(name),eep+vfooff)
for name in ['SPI_WriteByte','ST7565_BlitFullScreen','BACKLIGHT_TurnOn','BK4819_WriteRegister','GPIO_DisableAudioPath','GPIO_EnableAudioPath']:stub(name,lambda *a:ret())
stub('BK4819_ReadRegister',lambda *a:ret(0))
# Every top-level menu row is rendered with a safe zero/default selection.
for index in range(menu_count):
 label=gold('MenuList')[index*8:index*8+7].split(b'\0')[0]
 put8(addr('gMenuCursor'),index);put32(addr('gSubMenuSelection'),2000 if label==b'BatCal' else 0)
 put8(addr('gIsInSubMenu'),0);put8(addr('gAskForConfirmation'),0);clear();call('UI_DisplayMenu',count=8000000)
 assert any(frame()),(index,label)
print('PASS all top-level menu screens execute with external resources',flush=True)

formatted=0
for index in range(menu_count):
 ident=gold('MenuList')[index*8+7]
 if call('MENU_GetLimits',ident,0x20005800,0x20005804)!=0:continue
 lo,hi=struct.unpack('<ii',u.mem_read(0x20005800,8))
 vals=list(range(lo,hi+1)) if hi-lo<=210 else [lo,(lo+hi)//2,hi]
 for v in vals:
  u.mem_write(0x20005a00,b'X'*80)
  assert call('APP_NativeBegin',1)==1
  call('MenuOption',ident,v&0xffffffff,0x20005a00,count=3000000);call('APP_NativeEnd')
  assert b'\0' in bytes(u.mem_read(0x20005a00,64)),(index,v)
  assert bytes(u.mem_read(0x20005a40,16))==b'X'*16,(index,v,'output overrun')
  formatted+=1
print('PASS',formatted,'valid menu values/extremes: formatted with external tables; bounded output',flush=True)

for ch in range(33,127):
 clear();u.mem_write(scratch,bytes([ch,0]));call('UI_PrintString',scratch,0,0,0,8)
 want=bytearray(896);g=gold('fontBigGlyphs')[(ch-33)*14:(ch-32)*14]
 want[:7]=g[:7];want[128:135]=g[7:]
 assert frame()==want,('big',ch)
print('PASS all 94 expanded big glyphs: exact TEST4 pixels',flush=True)
