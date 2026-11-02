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
summary=[]
for port in [0,1]:
 reply,data=request(port,0x0514,struct.pack('<I',0x12345678))
 assert reply==0x0515
 name=data[:16].split(b'\0')[0].decode();assert name=='FALCON 7.0/3.0',name
 assert u.mem_read(addr('gSerialConfigCountDown_500ms'),1)==b'\x0c'
 for slot in range(64):
  reply,data=request(port,0x0730,bytes([slot]))
  assert reply==0x0731 and len(data)==66 and data[0]==slot,(port,slot,reply,len(data))
 summary.append({'port':'USB' if port else 'UART','name':name,'slot_responses':64,'packet_lengths':'PASS'})
print(json.dumps({'firmware':ROOT.name,'protocol':summary}))
