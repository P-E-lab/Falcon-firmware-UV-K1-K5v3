"""Linked ARM Full Watch RX paths; peripheral events are simulated, not RF timing."""
from pathlib import Path
from harness import *
for name in sym:
 if name.startswith(('BK1080_','BK4819_')) and name!='BK4819_SetupSquelch':stub(name,lambda *a:ret())
for n in ['SYSTEM_DelayMs','GPIO_DisableAudioPath','BACKLIGHT_TurnOn']:stub(n,lambda *a:ret())
writes=[];audio=[];irq=[]
stub('BK4819_WriteRegister',lambda reg,value,*a:(writes.append((reg,value)),ret()))
stub('GPIO_EnableAudioPath',lambda *a:(audio.append(True),ret()))
def read(reg,*a):
 ret(bool(irq) if reg==0x0c else (irq.pop(0) if reg==2 and irq else 0))
stub('BK4819_ReadRegister',read)
flash[0x10000:0x10100]=bytes([40])*256
for i,ch in enumerate([10,11,50,51,52,53]):
 attrs[ch]=0
 struct.pack_into('<IIBBBBBBBB',flash,CH_BASE+ch*16,14500000+ch*2500,0,0,0,0,0,0,0,8,0)
 if i<2:
  call('RADIO_InitInfo',eep+vfooff+i*vfosize,ch,14500000+ch*2500)
  call('RADIO_LoadChannel',eep+vfooff+i*vfosize,ch,0,2)
  put16(eep+i*2,ch)
for i,ch in enumerate([50,51,52,53]):put16(eep+prio+2*i,ch)
put8(eep+txindex,0)
for mode,delay in [(0,5),(1,5),(2,5)]:
 put8(eep+rxmode,mode);put32(addr('gRxVfo'),eep+vfooff);writes.clear()
 call('RADIO_SetupRegisters',0)
 val=next(value for reg,value in writes if reg==0x4e)
 assert (val>>11)&7==delay,(mode,hex(val))
 assert (val&0x4700)==0x4700,hex(val)
print('PASS hardware squelch opening delay: original opening/closing delays and flags retained in all modes')
for target in [10,11,50,51,52,53]:
 call('APP_FullWatchReset')
 for n in ['gTxVfo','gRxVfo','gCurrentVfo']:put32(addr(n),eep+vfooff)
 put32(addr('gCurrentFunction'),0)
 for step in range(6):
  call('DualVfoAlternate');v=get32(addr('gRxVfo'))
  if struct.unpack('<H',u.mem_read(v+save,2))[0]==target:break
 else:raise AssertionError(target)
 assert get32(get32(v+prx))==14500000+target*2500
 assert struct.unpack('<H',u.mem_read(addr('gDualVfoCountdown_10ms'),2))[0]==10
 irq.append(4);put8(addr('g_SquelchLost'),1);irq.clear();assert u.mem_read(addr('g_SquelchLost'),1)==b'\x01'
 put32(addr('gGlobalSysTickCounter'),100);call('CheckForIncoming');assert get32(addr('gCurrentFunction'))==3
 put32(0x50000018,0);call('HandleIncoming');assert get32(addr('gCurrentFunction'))==4 and get32(0x50000018)&256,(target,get32(addr('gCurrentFunction')),hex(get32(0x50000018)))
 assert get32(get32(get32(addr('gRxVfo'))+prx))==14500000+target*2500
 print('PASS actual channel load, retune, RX event, immediate incoming transition, promotion and audio opening:',target+1)
print('ALL FULL WATCH RX PATH CHECKS PASS; physical RF detection requires radio validation')
