"""Exercise actual optimized Cortex-M0 ELF with mocked external hardware."""
from harness import *

packets=[]
def send(p,n,*a):packets.append(bytes(u.mem_read(p,n)));ret()
stub('UART_Send',send);stub('VCP_SendAsync',send)
stub('usbd_ep_start_write',lambda ep,p,n,*a:send(p,n))
stub('BACKLIGHT_TurnOff',lambda *a:ret())
obfuscation=bytes.fromhex('16 6c 14 e6 2e 91 0d 40 21 35 d5 40 13 03 e9 80')
def request(port,cmd,data):
 command=addr('gCommand')  # one buffer shared by both links
 u.mem_write(command,struct.pack('<HH',cmd,len(data))+data)
 packets.clear();call('UART_HandleCommand',port,count=3000000)
 packet=b''.join(packets)
 assert packet[:2]==b'\xab\xcd' and packet[-2:]==b'\xdc\xba',(port,cmd,packet.hex())
 size=struct.unpack_from('<H',packet,2)[0]
 assert len(packet)==size+8
 body=bytes(c^obfuscation[i%16] for i,c in enumerate(packet[4:4+size]))
 reply,length=struct.unpack_from('<HH',body)
 assert size==length+4
 return reply,body[4:]
# Falcon 1K answers 3.1K and has no CHIRP 4K channel window (0x122000).
k1=B.name=='S1K'
reads=[]
def rec(a,p,n,*x):reads.append(a);rd(a,p,n)
stub('PY25Q16_ReadBuffer',rec)
writes=[]
stub('PY25Q16_WriteBuffer',lambda a,p,n,*x:(writes.append(a),ret()))
summary=[]
for port in [0,1]:
 reply,data=request(port,0x0514,struct.pack('<I',0x12345678))
 assert reply==0x0515
 name=data[:16].split(b'\0')[0].decode();assert name==('FALCON 7.0/3.1K' if k1 else 'FALCON 7.0/3.1'),name
 for offset,physical in [(0x000010,0x000010),(0x122010,0x122010 if k1 else 0x1DE010)]:
  reads.clear();reply,data=request(port,0x051B,struct.pack('<IBB',offset,16,0)+bytes(2)+struct.pack('<I',0x12345678))
  assert reply==0x051C and reads==[physical],(hex(offset),[hex(r) for r in reads])
 command=addr('gCommand');packets.clear();writes.clear()
 u.mem_write(command,struct.pack('<HH',0x051D,20)+struct.pack('<IBB',0x122000,8,0)+bytes(2)+struct.pack('<I',0x12345678)+bytes(8))
 call('UART_HandleCommand',port,count=3000000)
 assert bool(packets)!=k1 and writes==([] if k1 else [0x1DE000]),('CHIRP 4K window write: 4K maps it, 1K refuses it',[hex(w) for w in writes])
 assert u.mem_read(addr('gSerialConfigCountDown_500ms'),1)==b'\x0c'
 for slot in range(64):
  reply,data=request(port,0x0730,bytes([slot]))
  assert reply==0x0731 and len(data)==66 and data[0]==slot,(port,slot,reply,len(data))
 summary.append({'port':'USB' if port else 'UART','name':name,'slot_responses':64,'packet_lengths':'PASS'})
print(json.dumps({'firmware':ROOT.name,'protocol':summary}))
