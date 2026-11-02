"""Actual optimized ARM: Full Watch, four priorities and shared scan extras."""
from pathlib import Path
import struct,ast
from harness import *
get8=lambda n:u.mem_read(addr(n),1)[0]
freqs={};tunes=[]
for ch in range(4096):struct.pack_into('<I',flash,0x1DE000+ch*16,14500000+ch*2500)
stub('SETTINGS_FetchChannelFrequency',lambda ch,*a:ret(freqs.get(ch,14500000+ch*2500)))
stub('RADIO_LoadChannel',lambda *a:ret())
for n in ['RADIO_SetupRegisters','RADIO_ConfigureChannel','RADIO_SelectVfos','SETTINGS_SaveVfoIndices','ST7565_BlitFullScreen','SYSTEM_DelayMs','GPIO_DisableAudioPath','GPIO_EnableAudioPath','BK4819_WriteRegister','BK4819_ToggleGpioOut','BACKLIGHT_TurnOn','BACKLIGHT_Update','BEEP_Play']:
 stub(n,lambda *a:ret())
stub('BK4819_ReadRegister',lambda *a:ret(0))
for i,ch in enumerate([10,11]):
 attrs[ch]=0;call('RADIO_InitInfo',eep+vfooff+i*vfosize,ch,14500000+ch*2500);put16(eep+i*2,ch)
for n in ['gTxVfo','gRxVfo','gCurrentVfo']:put32(addr(n),eep+vfooff)
put8(eep+txindex,0);put8(eep+rxmode,2);put8(addr('gScreenToDisplay'),0);put32(addr('gCurrentFunction'),0)
for ch in [50,51,52,53]:attrs[ch]=0
for count in range(5):
 for i in range(4):put16(eep+prio+2*i,50+i if i<count else 4096)
 call('APP_FullWatchReset');call('FullWatchInitialize')
 assert get8('gFullWatchBackgroundCount')==count
 seen=[]
 for _ in range(count+2):
  call('DualVfoAlternate');seen.append(struct.unpack('<H',u.mem_read(get32(addr('gRxVfo'))+save,2))[0])
  assert struct.unpack('<H',u.mem_read(addr('gDualVfoCountdown_10ms'),2))[0]==10
 assert sorted(seen)==sorted([10,11]+list(range(50,50+count))),seen
print('PASS actual 100-ms cycle: A/B plus zero to four priorities, each visited once')
# Priorities already displayed remain valid; duplicate priority slots are ignored.
for vals,want in [([10,11,52,53],4),([50,50,52,52],2),([4096]*4,0)]:
 for i,ch in enumerate(vals):put16(eep+prio+2*i,ch)
 call('APP_FullWatchReset');call('FullWatchInitialize');assert get8('gFullWatchBackgroundCount')==want
# The mode action must enter OW while still in Dual (initialization precedes mode change).
for i in range(4):put16(eep+prio+2*i,50+i)
call('APP_FullWatchReset');put8(eep+rxmode,1)
stub('INPUTBOX_FrequencyIsActive',lambda *a:ret(0))
call('ACTION_SingleDuo');assert get8('gFullWatchBackgroundCount')==4 and u.mem_read(eep+rxmode,1)==b'\x02'
print('PASS mode selection from Dual; assigned A/B priorities and duplicate rejection')
# F+1..4 use the same native setter and toggle the same saved values.
put16(eep+vfooff+save,10);put32(addr('gTxVfo'),eep+vfooff)
for i in range(4):
 for j in range(4):put16(eep+prio+j*2,4096)
 put8(addr('gWasFKeyPressed'),1);call('MAIN_ProcessKeys',i+1,0,0)
 assert struct.unpack('<H',u.mem_read(eep+prio+i*2,2))[0]==10
 put8(addr('gWasFKeyPressed'),1);call('MAIN_ProcessKeys',i+1,0,0)
 assert struct.unpack('<H',u.mem_read(eep+prio+i*2,2))[0]==4096
 assert not get8('native_depth')
print('PASS F+1/F+2/F+3/F+4 toggle corresponding priority and return before deferred saves')
# Actual four-bubble layout, including four-digit IDs and mixed widths.
labels=[];bubbles=[]
def label(p,x,y,*a):labels.append(bytes(u.mem_read(p,20)).split(b'\0')[0]);ret()
def bubble(p,x,line,status):
 text=bytes(u.mem_read(p,5)).split(b'\0')[0];end=struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP)+4,4))[0]
 # Fifth/sixth arguments are at SP / SP+4.
 bubbles.append((text,x-2,end));ret()
stub('GUI_DisplaySmallest',label);stub('GUI_DisplaySmallestInverse',bubble)
for vals in [[50,51,52,53],[1000,2000,3000,4000],[50,1000,52,4000]]:
 for ch in vals:attrs[ch]=0
 for i,ch in enumerate(vals):put16(eep+prio+2*i,ch)
 call('APP_FullWatchReset');call('FullWatchInitialize');labels.clear();bubbles.clear()
 assert call('APP_NativeBegin',5);call('UI_MAIN_DrawFullWatchPriorities');call('APP_NativeEnd')
 four_digits=any(ch>=999 for ch in vals)
 assert (b'<<F WATCH>>' if four_digits else b'<< F WATCH >>') in labels,(vals,labels)
 assert len(bubbles)==4,bubbles
 pairs=sorted(bubbles,key=lambda x:x[1]);assert all(0<=start<=end<128 for _,start,end in pairs),pairs
 assert pairs[1][1]-pairs[0][2]-1==2 and pairs[3][1]-pairs[2][2]-1==2,pairs
print('PASS << F WATCH >>, tight <<F WATCH>> only for four-digit bubbles; four bubbles inside LCD, two blank pixels between neighbours')
# Hiding an inner priority bubble must not leave a hole beside F WATCH.
for hidden in [50,51,52,53]:
 for i,ch in enumerate([50,51,52,53]):put16(eep+prio+2*i,ch)
 put16(eep,hidden);put16(eep+2,11)
 call('APP_FullWatchReset');call('FullWatchInitialize');labels.clear();bubbles.clear()
 assert call('APP_NativeBegin',5);call('UI_MAIN_DrawFullWatchPriorities');call('APP_NativeEnd')
 pairs=sorted(bubbles,key=lambda x:x[1]);assert len(pairs)==3,pairs
 left=[b for b in pairs if b[1]<38];right=[b for b in pairs if b[1]>88]
 assert left[-1][2]==33 and right[0][1]==93,(hidden,pairs)
put16(eep,10)
print('PASS hidden priority bubble: visible neighbours pack against each F WATCH arrow')

# The real F+EXIT dispatcher must toggle Monitor once, and preserve editor exit.
monitor_events=[]
stub('MonitorAdd',lambda *a:(monitor_events.append(1),ret(1)))
stub('MonitorRemove',lambda *a:(monitor_events.append(1),ret(1)))
put8(addr('gWasFKeyPressed'),1);call('ProcessKey',13,1,0);call('ProcessKey',13,0,0)
assert monitor_events==[1],(monitor_events,get8('gWasFKeyPressed'))
put8(addr('frequencyInputIndex'),1);put8(addr('gWasFKeyPressed'),1);stub('INPUTBOX_FrequencyIsActive',lambda *a:ret(1))
stub('INPUTBOX_FrequencyUpdate',lambda *a:ret())
call('ProcessKey',13,1,0);call('ProcessKey',13,0,0)
assert monitor_events==[1],(monitor_events,get8('gWasFKeyPressed'))
put8(addr('frequencyInputIndex'),0);stub('INPUTBOX_FrequencyIsActive',lambda *a:ret(0))
print('PASS real F+EXIT adds/removes Monitor once; active frequency editor retains EXIT')
# Numeric browsing persists across list membership and does not leak to VFO B.
for ch,group in [(511,1),(512,0),(513,1),(514,1),(599,0)]:attrs[ch]=group<<8
put8(eep+txindex,0);u.mem_write(addr('gInputBox'),bytes([0,5,1,3]));put8(addr('gInputBoxIndex'),4);call('channelMoveSwitch')
assert call('NextMemoryByList',512,1)==513
assert call('NextMemoryByList',513,1)==514
assert call('NextMemoryByList',514,0xffffffff)==513
print('PASS direct unlisted CH513 -> CH514 -> CH515, and back, across list boundaries')
# All four menu settings save through the existing paths and survive reload.
for n in ['vfoScan','vfoRangeSession','scanListOnly','gWasFKeyPressed']:put8(addr(n),0)
resource=store_entry(0)[0];menu_offset=resource+addr('MenuList')-0x91000000
rows=[store[menu_offset+i*8:menu_offset+(i+1)*8] for i in range(menu_count)]
for i in range(4):
 at=next(n for n,row in enumerate(rows) if row[:7].split(b'\0')[0]==('PRICH'+str(i+1)).encode())
 put8(addr('gMenuCursor'),at);put32(addr('gSubMenuSelection'),50+i);call('MENU_AcceptSetting')
 assert struct.unpack('<H',u.mem_read(eep+prio+2*i,2))[0]==50+i
put8(addr('gCurrentFunction'),0)
def save_settings(a,p,n,*unused):
 assert get8('native_depth')==0
 flash[a:a+n]=u.mem_read(p,n);ret()
stub('PY25Q16_WriteBuffer',save_settings)
call('SETTINGS_SaveSettings')
assert flash[0xA131:0xA135]==struct.pack('<2H',50,51)
assert flash[0xA170:0xA174]==struct.pack('<2H',0xA034,0xA035)
flash[0xA160:0xA170]=b'2.3.1.D1'.ljust(16,b'\0')
u.mem_write(eep+prio,bytes(8));call('SETTINGS_InitEEPROM',count=6000000)
assert bytes(u.mem_read(eep+prio,8))==struct.pack('<4H',50,51,52,53)
print('PASS PRICH1..4 menu values, existing save path, and all four restored after cold settings reload')

flash[0xA170:0xA174]=struct.pack('<2H',52,53)
call('SETTINGS_InitEEPROM',count=6000000)
assert bytes(u.mem_read(eep+prio+4,4))==struct.pack('<2H',4096,4096)
print('PASS untagged legacy bytes do not enable phantom priorities 3/4')
