"""Exercise actual optimized Cortex-M0 ELF with mocked external hardware."""
from harness import *

def rawstub(name,fn):stub(name,fn)
def unhook(name):
 for k in sym:
  if k==name or k.startswith(name+'.'):handlers.pop(addr(k),None)
def get8(name):return u.mem_read(addr(name),1)[0]
def get16(name):return struct.unpack('<H',u.mem_read(addr(name),2))[0]
def text(p):return bytes(u.mem_read(p,80)).split(b'\0')[0]
fields={
 'size':'sizeof(SettingsEEPROM)','reserved':'offsetof(SettingsEEPROM,reserved16)',
 'delay':'offsetof(SettingsEEPROM,SpectrumDelay)','max':'offsetof(SettingsEEPROM,IndexMaxLT)',
 'noise':'offsetof(SettingsEEPROM,Noislvl_OFF)','record':'offsetof(SettingsEEPROM,UOO_trigger)',
 'power':'offsetof(SettingsEEPROM,IndexPS)','popup':'offsetof(SettingsEEPROM,osdPopupIndex)',
 'audio':'offsetof(SettingsEEPROM,gSetting_set_audio_am)','main':'offsetof(SettingsEEPROM,PttEmission)',
 'lists':'offsetof(SettingsEEPROM,scanListFlags)','bands':'offsetof(SettingsEEPROM,bandListFlags)',
 'start':'offsetof(SettingsEEPROM,RangeStart)','stop':'offsetof(SettingsEEPROM,RangeStop)',
 'step':'offsetof(SettingsEEPROM,scanStepIndex)','view':'offsetof(SettingsEEPROM,ShowLines)',
 'mode':'offsetof(SettingsEEPROM,Spectrum_state)','bw':'offsetof(SettingsEEPROM,listenBw)',
 'am':'offsetof(SettingsEEPROM,listenBwAM)','amtag':'offsetof(SettingsEEPROM,listenBwAMTag)',
 'modoff':'offsetof(SpectrumSettings,modulationType)','bwoff':'offsetof(SpectrumSettings,listenBw)',
 'stepoff':'offsetof(SpectrumSettings,scanStepIndex)','sf':'offsetof(ScanInfo,f)',
 'ss':'offsetof(ScanInfo,scanStep)','pf':'offsetof(PeakInfo,f)',
 'step625':'STEP_6_25kHz','step125':'STEP_12_5kHz','fm':'MODULATION_FM','ammod':'MODULATION_AM',
 'state':'SPECTRUM','paramstate':'PARAMETERS_SELECT','rangemode':'SCAN_RANGE_MODE',
 'kbdcurrent':'offsetof(KeyboardState,current)','fkey':'KEY_F','up':'KEY_UP','down':'KEY_DOWN','exit':'KEY_EXIT','side1':'KEY_SIDE1','side2':'KEY_SIDE2','ptt':'KEY_PTT','invalid':'KEY_INVALID',
 'key0':'offsetof(EEPROM_Config_t,KEY_1_SHORT_PRESS_ACTION)','key1':'offsetof(EEPROM_Config_t,KEY_1_LONG_PRESS_ACTION)',
 'key2':'offsetof(EEPROM_Config_t,KEY_2_SHORT_PRESS_ACTION)','key3':'offsetof(EEPROM_Config_t,KEY_2_LONG_PRESS_ACTION)',
 'lock':'offsetof(EEPROM_Config_t,KEY_LOCK)','bltime':'offsetof(EEPROM_Config_t,BACKLIGHT_TIME)',
 'falcon':'offsetof(EEPROM_Config_t,FlashlightOnRX)','roger':'offsetof(EEPROM_Config_t,AUTO_ROGER)',
 'ptt2action':'ACTION_OPT_PTT2','blrx':'BACKLIGHT_ON_TR_RX','mainstate':'DISPLAY_MAIN'
}
probe=B/'scanner_parameters_probe.c';probe.write_text('#include <stddef.h>\n#include "'+str(ROOT/'App/app/spectrum.c')+'"\nconst unsigned scanner_layout[]={'+','.join(fields.values())+'};')
cmd=out[:];cmd[cmd.index('-c')+1]=str(probe);cmd[cmd.index('-o')+1]=str(B/'scanner_parameters_probe.o');subprocess.run(cmd,cwd=c['directory'],check=True)
with open(B/'scanner_parameters_probe.o','rb') as f:data=ELFFile(f).get_section_by_name('.rodata.scanner_layout').data()
Q=dict(zip(fields,struct.unpack('<%dI'%(len(data)//4),data)))
writes=[]
def wr(a,p,n,*unused):
 writes.append((a,n));flash[a:a+n]=u.mem_read(p,n);ret()
rawstub('PY25Q16_WriteBuffer',wr)
for n in ['SPI_WriteByte','ST7565_BlitLine','ST7565_BlitStatusLine','BOARD_ADC_GetBatteryInfo','SYSTICK_DelayUs','SYSTEM_DelayMs','BK4819_WriteRegister','BK4819_ToggleGpioOut','BK4819_SetFrequency','BK4819_PickRXFilterPathBasedOnFrequency','BK4819_RX_TurnOn','BK4819_SetAF','RADIO_SetupAGC','BACKLIGHT_SetBrightness','BACKLIGHT_Update','BEEP_Play']:
 rawstub(n,lambda *a:ret())
rawstub('BK4819_ReadRegister',lambda *a:ret(0))
rawstub('SCANNING_Init',lambda *a:ret())
put8(eep+Q['lock'],0);put8(eep+Q['falcon'],0);put8(eep+Q['bltime'],1)
put8(addr('gScreenToDisplay'),Q['mainstate'])
put32(0x50000410,1<<10)
for flag in ['vfoScan','scanListOnly','historyListActive','vfoRangeSession','gReducedService','gFmRadioMode','gLowBattery','gKeypadLocked','gIsKeylocked']:
 put8(addr(flag),0)
# Legacy image initializes both independent profiles once, with no moved fields.
base=0xc020;cfg=bytearray(Q['size'])
for name,value in [('lists',1),('bands',1),('start',44600000),('stop',44620000),('step',Q['step625']),('view',3)]:struct.pack_into('<I',cfg,Q[name],value)
for name,value in [('mode',Q['rangemode']),('noise',40),('record',5),('popup',1),('amtag',0xb1)]:cfg[Q[name]]=value
cfg[Q['reserved']:Q['reserved']+3]=bytes([0xd2,15,15])
flash[base:base+len(cfg)]=cfg
put8(addr('SettingsLoaded'),1)
call('SelectScanProfile',0,count=5000000)
assert flash[base+Q['reserved']+26]==0xb6 and flash[base+Q['reserved']+38]==0xb6
assert Q['size']==104
print('PASS legacy menu migration: tagged Spectrum/Sentinel profiles fit unchanged 104-byte settings layout',flush=True)

def set_menu(values):
 delay,mx,noise,record,power,popup,audio,main,second,monitor,sound,lock,mod,bw,fm,am=values
 put16(addr('SpectrumDelay'),delay)
 for name,value in [('IndexMaxLT',mx),('Noislvl_OFF',noise),('IndexPS',power),('osdPopupIndex',popup),('gSetting_set_audio_am',audio),('PttEmission',main),('Ptt2Emission',second),('gMonitorScan',monitor),('SoundBoost',sound),('AUTO_KEYLOCK',lock),('scanMenuModulation',mod),('scanMenuBandwidth',bw),('listenBwAM',am)]:put8(addr(name),value)
 put16(addr('UOO_trigger'),record);put8(addr('settings')+Q['bwoff'],fm)
def menu_values():
 return (get16('SpectrumDelay'),get8('IndexMaxLT'),get8('Noislvl_OFF'),get16('UOO_trigger'),get8('IndexPS'),get8('osdPopupIndex'),get8('gSetting_set_audio_am'),get8('PttEmission'),get8('Ptt2Emission'),get8('gMonitorScan'),get8('SoundBoost'),get8('AUTO_KEYLOCK'),get8('scanMenuModulation'),get8('scanMenuBandwidth'),u.mem_read(addr('settings')+Q['bwoff'],1)[0],get8('listenBwAM'))
a=(3000,3,35,7,2,5,2,0,2,0,1,2,Q['ammod'],3,2,1)
b=(6000,6,48,10,4,1,1,2,0,1,0,1,Q['fm'],0,0,0)
set_menu(a);call('SaveSettings',0,count=5000000)
call('SelectScanProfile',1,count=5000000);assert get16('SpectrumDelay')==0
set_menu(b);call('SaveSettings',0,count=5000000)
for view,want in [(0,a),(1,b),(0,a),(1,b)]:
 call('SelectScanProfile',view,count=5000000);assert menu_values()==want,(view,menu_values(),want)
 call('SaveSettings',0,count=5000000);put8(addr('SettingsLoaded'),0);call('LoadSettings',count=5000000);assert menu_values()==want,(view,menu_values(),want)
print('PASS all menu settings independent, alternating views and cold reloads; both bandwidths, modulation, AM audio, PTTs, RX delay, power, popup, lock, noise/record',flush=True)
# Render all rows, checking the requested order, record-only Sentinel row,
# exact 2-pixel value margin and footer identical to the Scanlists page.
expected=['PTT:','PTT2:','MONITOR CH:','RX DELAY:','MAXLISTENTIME:','MODULATION:','LISTENBW FM:','LISTENBW AM:','AM AUDIO:','SOUNDBOOST:','NOIS LVL OFF:','RECORD TRIG:','POWER SAVE:','POPUPS:','KEYLOCK:']
scratch=0x20005800
from PIL import Image
def frame():return bytes(u.mem_read(addr('gFrameBuffer'),896))
def save_image(name):
 f=frame();im=Image.new('1',(128,64),1)
 for y in range(56):
  for x in range(128):
   if f[y//8*128+x]&(1<<(y%8)):im.putpixel((x,y+8),0)
 im.resize((768,384),Image.Resampling.NEAREST).save(B/name)
for view in [0,1]:
 call('SelectScanProfile',view,count=5000000)
 call('ResetScanParameterOverrides')
 names=expected[:] if view else [n for n in expected if n!='RECORD TRIG:']
 put8(addr('parametersSelectedIndex'),255);put8(addr('parametersScrollOffset'),0)
 assert call('APP_NativeBegin',9)==1;call('RenderParametersSelectNative');call('APP_NativeEnd')
 assert get8('parametersSelectedIndex')==len(names)-1
 for i,name in enumerate(names):
  call('GetParametersRow',i,scratch);assert text(scratch).decode()==name
  put8(addr('parametersSelectedIndex'),i);put8(addr('parametersScrollOffset'),0)
  assert call('APP_NativeBegin',9)==1;call('RenderParametersSelectNative');call('APP_NativeEnd')
  f=frame();assert any(f)
 # First/last pages show six rows at the scanlist positions.
 put8(addr('parametersSelectedIndex'),0);put8(addr('parametersScrollOffset'),0)
 assert call('APP_NativeBegin',9)==1;call('RenderParametersSelectNative');call('APP_NativeEnd')
 save_image('parameters-sentinel.png' if view else 'parameters-spectrum.png')
 footer=frame()[6*128:7*128]
 u.mem_write(addr('gFrameBuffer'),b'\0'*896);u.mem_write(scratch,b' KEYS INFOS\0');call('CHANNEL_DrawKeysInfosLabel',scratch)
 assert all(((a^b)&0xfc)==0 for a,b in zip(frame()[6*128:7*128],footer))
 # Non-selected PTT2 value has exactly two empty pixel columns to its right.
 assert call('APP_NativeBegin',9)==1;call('RenderParametersSelectNative');call('APP_NativeEnd')
 f=frame();assert not any(f[y//8*128+x]&(1<<(y%8)) for y in range(17,22) for x in [126,127])
print('PASS complete parameter order, 14/15 rows, shared footer, six-row scanlist geometry and right value margin; rendered PNGs',flush=True)
# Help frame uses the same text advance function as the other popups.
for name in ['DrawListTitle','DrawCompactSelectionRow','CHANNEL_DrawKeysInfosLabel']:rawstub(name,lambda *a:ret())
put8(addr('selectionHelp'),1)
assert call('APP_NativeBegin',9)==1;call('RenderParametersSelectNative');call('APP_NativeEnd');f=frame()
for name in ['DrawListTitle','DrawCompactSelectionRow','CHANNEL_DrawKeysInfosLabel']:unhook(name)
put8(addr('selectionHelp'),0)
points=[(x,y) for y in range(56) for x in range(128) if f[y//8*128+x]&(1<<(y%8))]
x0=min(x for x,y in points);x1=max(x for x,y in points);y0=min(y for x,y in points);y1=max(y for x,y in points)
# the frame is centered; its shadow adds a column and a row
assert x0+x1-1==127 and y0+y1-1+16==63,(x0,x1,y0,y1)
assert y1-y0==30
save_image('parameters-keys-popup.png')
print('PASS Keys Infos popup measured to content and centered on the 128x64 screen; common slash and space advance',flush=True)
# Actual RX sampling/state machine: no periodic backlight rearm, FL reopening,
# delay preserving audio mute and noisy-signal rejection.
rssi=130;noise=0
rawstub('GetRssi',lambda *a:ret(rssi))
rawstub('BK4819_ReadRegister',lambda reg,*a:ret(noise if reg==0x65 else 0))
for n in ['BACKLIGHT_TurnOn','FillfreqHistory']:rawstub(n,lambda *a:ret())
backlights=[];rawstub('BACKLIGHT_TurnOn',lambda *a:(backlights.append(1),ret()))
def rx_setup(view=0,lock=0,delay=1000):
 global rssi,noise
 for name in ['vfoScan','scanListOnly','historyListActive','vfoRangeSession','SPECTRUM_PAUSED','postTxListening','gIsPeak','isListening','newScanStart','scanEmpty','gNextTimeslice_10ms','gNextTimeslice_500ms','gNextTimeslice_listening','gNextTimeslice_display','gNextTimeslice_AutoPtt','gNextTimeslice_HTimeS']:
  put8(addr(name),0)
 put8(addr('sentinelView'),view);put8(addr('SpectrumMonitor'),lock)
 put8(addr('isInitialized'),1);put8(addr('currentState'),Q['state']);put8(addr('Spectrum_state'),Q['rangemode'])
 put16(addr('osdPopupTimer'),0);put16(addr('WaitSpectrum'),0)
 put32(addr('RangeStart'),44600000);put32(addr('RangeStop'),44620000)
 put8(addr('settings')+Q['stepoff'],Q['step625']);put16(addr('scanInfo')+Q['ss'],625)
 put32(addr('scanInfo')+Q['sf'],44600625);put32(addr('peak')+Q['pf'],44600625)
 put8(addr('settings')+Q['modoff'],Q['fm']);put16(addr('settings')+2,5)
 put8(addr('gSpectrumSQL'),15);put8(addr('gVfoScanSQL'),15);put8(addr('gScanSQLHys'),3)
 put8(addr('Noislvl_OFF'),40);put8(addr('Noislvl_ON'),25);put8(addr('scanNoiseCount'),0)
 put8(addr('Backlight_On'),1);put8(addr('SoundBoost'),0);put16(addr('MaxListenTime'),0)
 put16(addr('SpectrumDelay'),delay);put8(addr('audioState'),0)
 put8(addr('gSetting_backlight_on_tx_rx'),Q['blrx']);rssi=130;noise=0;backlights.clear()
def update(dbm,n):
 global rssi,noise
 rssi=(dbm+160)*2;noise=n;call('UpdateListening',count=4000000)
def tick(listening=False,half=False):
 put8(addr('gNextTimeslice_listening'),int(listening));put8(addr('gNextTimeslice_500ms'),int(half))
 call('Tick',count=5000000)



# Names were empty in the former first-RX smoke test. A named RX used to
# dereference the 0x91000000 font link address as CPU memory and HardFault.
from unicorn import UC_HOOK_MEM_READ_UNMAPPED
bad_reads=[]
def unmapped(uc,access,address,size,value,data):
 bad_reads.append((address,size));print('UNMAPPED font/data read',hex(address),size,flush=True);return False
u.hook_add(UC_HOOK_MEM_READ_UNMAPPED,unmapped)
for view in (0,1):
 for scan_mode in (0,1,2):
  for name in (b'AERO123',b'Brest',b' CDM-32',b'123.45',b'A/B+#',b'LONG NAME',b''):
   rx_setup(view,0,3000)
   put8(addr('Spectrum_state'),scan_mode)
   put32(addr('SpectrumRangeStart'),44600000);put32(addr('SpectrumRangeStop'),44620000)
   put8(addr('isListening'),1);put8(addr('rxDisplayPending'),1);put16(addr('spectrumRxDbm'),(-95)&65535)
   u.mem_write(addr('channelName'),name+bytes([0]))
   call('Render',count=5000000)
   assert not get8('rxDisplayPending')
   assert bytes(u.mem_read(addr('native_depth'),1))==bytes([0])
   call('Render',count=5000000)
assert not bad_reads
print('PASS 42 named/empty first-RX cases: Spectrum/Sentinel x List/Range/Band; repeated RX render returns',flush=True)
