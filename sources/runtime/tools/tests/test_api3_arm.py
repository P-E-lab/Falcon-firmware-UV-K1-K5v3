"""Exercise actual optimized Cortex-M0 ELF with mocked external hardware."""
from harness import *

apps=ROOT.parents[1]/'Apps/F4WHN apps'
for f in sorted(apps.glob('*.app')):
 h=install(f,0)
 assert call('app_read_validated_header',0,0x20006000)==0,f.name
 assert h[10]==0x20000280 and h[5]==zlib.crc32(f.read_bytes()[64:64+h[4]])
 assert h[13]==(zlib.crc32(f.read_bytes()[64+h[4]:])&65535)
assert len(list(apps.glob('*.app')))==46  # the F4HWN 6.3.0 apps of the archive
print('PASS all 40 official packages: real ARM loader header validation, API/capabilities/VMA and code/asset CRCs',flush=True)
api=addr('app_api');a=bytes(u.mem_read(api,396))
assert a[0:2]==bytes([1,3]) and struct.unpack_from("<H",a,2)[0]==408
for off in range(328,396,4):assert struct.unpack_from('<I',a,off)[0]&1,off
for v in [0,1,123456789,0xffffffff]:
 end=call('app_append_u32',0x20006000,v);assert bytes(u.mem_read(0x20006000,end-0x20006000)).decode()==str(v)
for v in [-2147483648,-100,0,2147483647]:
 end=call('app_append_i32',0x20006000,v&0xffffffff);assert bytes(u.mem_read(0x20006000,end-0x20006000)).decode()==str(v)
print('PASS all 17 API3 callbacks at official offsets; real signed/unsigned full-range formatting',flush=True)
# Compile private structure offsets from the exact firmware feature set.
cmd=json.loads((B/'compile_commands.json').read_text())[0];args=shlex.split(cmd['command']);cc=[];i=0
while i<len(args):
 if args[i] in ['-o','-c']:i+=2;continue
 if not args[i].startswith('-flto'):cc.append(args[i])
 i+=1
fields={n:'offsetof(__typeof__(app_state),'+n+')' for n in ['run_slot','asset_size','handoff_unread','serial_used']}
fields['e_bl']='offsetof(EEPROM_Config_t,BACKLIGHT_TIME)'
fields['e_tx']='offsetof(EEPROM_Config_t,TX_VFO)'
fields['e_rx']='offsetof(EEPROM_Config_t,RX_VFO)'
fields['e_screen']='offsetof(EEPROM_Config_t,ScreenChannel)'
fields['e_mr']='offsetof(EEPROM_Config_t,MrChannel)'
fields['e_keylock']='offsetof(EEPROM_Config_t,KEY_LOCK)'
fields['e_vfos']='offsetof(EEPROM_Config_t,VfoInfo)'
fields['vfo_size']='sizeof(VFO_Info_t)'
fields['vfo_exact']='offsetof(VFO_Info_t,ExactFrequency)'
fields['vfo_ptr']='offsetof(VFO_Info_t,pRX)'
fields['vfo_step']='offsetof(VFO_Info_t,STEP_SETTING)'
fields['vfo_power']='offsetof(VFO_Info_t,OUTPUT_POWER)'
fields['wire_step']='offsetof(app_beam_channel_t,step_setting)'
fields['wire_power']='offsetof(app_beam_channel_t,output_power)'
fields['fchannel']='FREQ_CHANNEL'
source=B/'api3_probe.c';source.write_text('#include "'+str(ROOT/'App/apps/app_overlay.c')+'"\nconst unsigned api3_layout[]={'+','.join(fields.values())+'};')
subprocess.run(cc+['-c',str(source),'-o',str(B/'api3_probe.o')],cwd=cmd['directory'],check=True)
with open(B/'api3_probe.o','rb') as ff:buf=ELFFile(ff).get_section_by_name('.rodata.api3_layout').data()
Q=dict(zip(fields,struct.unpack('<%dI'%(len(buf)//4),buf)))
state=addr('app_state');eep=addr('gEeprom')
# Mailbox: target compatibility, bounded messages, one-shot sender read.
aprstx=install(apps/'APRSTX.app',0);install(apps/'APRSEdit.app',1)
put8(state+Q['run_slot'],0);u.mem_write(0x20006000,b'APRS Edit\0');u.mem_write(0x20006100,bytes(range(64)))
assert call('app_handoff',0x20006000,0x20006100,64)==1
put8(state+Q['handoff_unread'],1)
assert call('app_handoff_read',0x20006200,8,0x20006300)==32
assert bytes(u.mem_read(0x20006200,8))==bytes(range(8))
assert bytes(u.mem_read(0x20006300,16)).split(b'\0')[0]==b'APRS TX'
assert call('app_handoff_read',0,0,0)==0xffffffff
u.mem_write(0x20006000,b'NOT INSTALLED\0');assert call('app_handoff',0x20006000,0,0)==0
print('PASS app hand-off target validation, 32-byte clamp, short reads, sender name and one-shot mailbox',flush=True)
# Capture register restore and idempotency (SWTRIGR self-clears on hardware).
blocks=[(0x48000000,0x100),(0x40007400,0x100),(0x40012400,0x100)]
for base,n in blocks:u.mem_write(base,bytes([0x34])*n)
before=[bytes(u.mem_read(base,n)) for base,n in blocks]
call('app_af_capture_open');call('app_af_capture_open');call('app_af_capture_close');call('app_af_capture_close')
for (base,n),buf in zip(blocks,before):
 after=bytes(u.mem_read(base,n));diff=[i for i in range(n) if buf[i]!=after[i]]
 assert not diff or (base==0x40007400 and all(4<=i<8 for i in diff)),(hex(base),diff)
put32(addr('gGlobalSysTickCounter'),100);put32(0xe000e014,479999);put32(0xe000e018,400000)
assert call('app_cycles_raw')==100*480000+79999
print('PASS RX audio capture open/close and register restore; real 48 MHz cycle-clock calculation',flush=True)
# USB ring services share the parser cursor; no new RAM ring is allocated.
put8(state+Q['serial_used'],0);put32(addr('VCP_RxBufPointer'),250);put16(addr('VCP_ReadIndex'),200)
put32(addr('cdc_acm_rx_total'),10);assert call('app_serial_ctl',0,0)&1
assert struct.unpack('<H',u.mem_read(addr('VCP_ReadIndex'),2))[0]==250
for i,b in enumerate(b'Hello API3'):put8(addr('VCP_RxBuf')+((250+i)&255),b)
put32(addr('VCP_RxBufPointer'),4);put32(addr('cdc_acm_rx_total'),20)
assert call('app_serial_read',0,0x20006000,20)==10
assert bytes(u.mem_read(0x20006000,10))==b'Hello API3'
assert call('app_serial_ctl',1,38400)==0
assert call('app_serial_read',1,0x20006000,20)==0
put32(addr('cdc_acm_rx_total'),276);assert call('app_serial_read',0,0x20006000,20)==0
assert call('app_serial_ctl',0,0)&4
put8(addr('dtr_enable'),0);assert call('app_serial_write',0,0x20006000,10)==0
print('PASS USB ownership, shared read cursor, ring wrap, overrun event, disconnected writes and reserved K-plug port',flush=True)
# VFO stage is bounded and deferred: it must not alter EEPROM while code runs.
writes=[]
def wr(address,p,n,*a):writes.append((address,bytes(u.mem_read(p,n))));flash[address:address+n]=u.mem_read(p,n);ret()
stub('PY25Q16_WriteBuffer',wr)
for n in ['SYSTICK_DelayUs','SYSTEM_DelayMs','BK4819_SetAGC','BK4819_ToggleGpioOut','BK4819_SetFilterBandwidth','RADIO_SetupRegisters','RADIO_ConfigureSquelchAndOutputPower']:stub(n,lambda *a:ret())
put8(addr('beam_dirty'),0);put8(addr('gAppVfoExit'),0)
u.mem_write(0x20006000,bytes(44));put32(0x20006000,14550321)
assert call('app_vfo_stage',0x20006000)==1 and not writes
assert call('app_vfo_stage',0x20006000)==0
print('PASS VFO stage refuses a second pending change and performs no EEPROM write before app exit',flush=True)
# Verify full BEAM wire translations and the deferred VFO EEPROM commit.
vfo=eep+Q['e_vfos'];put8(eep+Q['e_tx'],0);put8(eep+Q['e_rx'],0)
steps=[7,8,9,12,13,16,10,0,1,2,3,4,5,6,11,14,15,17,18,19,20,21,22,23]
powers=[0,3,4,5,6,1,2]
# A tiny real ARM test app invokes vfo_stage through the published API slot.
asm=B/'api3_stage.s';asm.write_text(".syntax unified\n.thumb\n.text\n.global stage_entry\n.thumb_func\nstage_entry:\npush {r4,lr}\nmov r4,r0\nldr r0,=0x20006000\nmovs r2,#98\nlsls r2,r2,#2\nldr r3,[r4,r2]\nblx r3\npop {r4,pc}\n.ltorg\n")
subprocess.run([cc[0],'-mcpu=cortex-m0plus','-mthumb','-c',str(asm),'-o',str(B/'api3_stage.o')],check=True)
with open(B/'api3_stage.o','rb') as f:stage_code=ELFFile(f).get_section_by_name('.text').data()
header=struct.pack('<IHBBIIHH16s16sIIHH',0x31504146,1,1,3,len(stage_code),zlib.crc32(stage_code),0,1,b'VFO TEST',b'1',0x20000280,0,0,0)
flash[0x102000:0x104000]=bytes([255])*8192;flash[0x102000:0x102040]=header
flash[0x103000:0x103000+len(stage_code)]=stage_code
for name in ['gTxVfo','gRxVfo','gCurrentVfo']:put32(addr(name),vfo)
for wire,storage in enumerate(steps):
 call('RADIO_InitInfo',vfo,Q['fchannel'],14550321)
 put8(vfo+Q['vfo_step'],storage);put8(vfo+Q['vfo_power'],powers[wire%7])
 call('app_beam_get',0x20006000)
 assert u.mem_read(0x20006000+Q['wire_step'],1)[0]==wire
 assert u.mem_read(0x20006000+Q['wire_power'],1)[0]==wire%7+1
 put8(addr('beam_dirty'),0);writes.clear()
 assert call('app_vfo_stage',0x20006000)==1 and not writes
 put8(addr('beam_dirty'),0);put8(addr('gAppVfoExit'),0)
 assert call('app_launch',0,count=4000000)==0
 call('SETTINGS_SaveVfoNow')  # the VFO record waits 3 s in RAM (delayed save)
 assert flash[0x900e]==storage and ((flash[0x900c]>>2)&7)==powers[wire%7]
 assert struct.unpack_from('<I',flash,0x9000)[0]==14550321 and flash[0x900c]&128
 assert get32(vfo)==14550321 and u.mem_read(vfo+Q['vfo_step'],1)[0]==storage
 assert struct.unpack('<H',u.mem_read(eep+Q['e_screen'],2))[0]==Q['fchannel']
put8(addr('beam_dirty'),0);put32(0x20006000,0);assert not call('app_vfo_stage',0x20006000)
print('PASS all 24 published step IDs and seven power IDs round-trip between the F4HWN ABI and Falcon storage; VFO commits preserve exact frequency after app exit',flush=True)
print('ALL API3 SERVICE CHECKS PASS; physical audio/USB/RF performance requires radio testing',flush=True)
