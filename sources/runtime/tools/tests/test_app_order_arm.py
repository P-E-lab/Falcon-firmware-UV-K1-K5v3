from pathlib import Path
p=Path(__file__).with_name('test_protocol_arm.py');ns={'__file__':str(p)}
exec(compile(p.read_text(),str(p),'exec'),ns);globals().update(ns)
changes=[]
def program(a,p,n,*args):
 assert 0x102000<=a<0x182000
 assert u.mem_read(addr('native_depth'),1)==b'\x01'
 if p:
  assert 0<n<=128
  old=flash[a:a+n];new=bytes(u.mem_read(p,n));assert all((x&y)==y for x,y in zip(old,new))
  flash[a:a+n]=new;changes.append(('write',a,n))
 else:
  assert a%4096==0
  flash[a:a+4096]=b'\xff'*4096;changes.append(('erase',a))
 ret(0)
def crc(a,n,p,*args):
 assert 0<=a<=0x200000-n
 put32(p,zlib.crc32(flash[a:a+n]));ret(0)
stub('MB_AppProgram',program)
stub('PY25Q16_ReadPhysical',rd);stub('MB_StoreCRC',crc)
def invalidate(*a):
 assert u.mem_read(addr('native_depth'),1)==b'\0'
 put8(addr('loaded_native'),0);u.mem_write(0x20000280,bytes(4096));u.ctl_remove_cache(0x20000280,0x20001280);ret()
stub('PY25Q16_InvalidateCache',invalidate);stub('APP_NotifySlotChanged',lambda *a:ret())
ts=0x12345678
# Real app package (FSystemInfo, in the archive); full 8K image includes app config and arbitrary assets tail.
package=(ROOT.parents[1]/'Apps/Falcon apps/FSystemInfo.app').read_bytes()
def image(name,fill):
 data=bytearray(bytes([fill])*8192);data[:64]=package[:64];data[20:36]=name.ljust(16,b'\0')
 size=struct.unpack_from('<I',package,8)[0];data[4096:4096+size]=package[64:64+size];return bytes(data)
A=image(b'ORDER A',23);B=image(b'ORDER B',91)
for port in [0,1]:
 request(port,0x0514,struct.pack('<I',ts))
 base1=0x102000;base2=0x180000
 flash[base1:base1+8192]=A;flash[base2:base2+8192]=B
 before=bytes(flash[:0x102000])+bytes(flash[0x182000:])
 images=[]
 for base in [base1,base2]:
  blob=bytearray()
  for off in range(0,8192,128):
   reply,data=request(port,0x0738,struct.pack('<IHI',base+off,128,ts))
   assert reply==0x0739 and data[6]==0;blob+=data[7:]
  images.append(bytes(blob))
 # CRC capability probe made by UVStudio, outside Apps but read-only.
 reply,data=request(port,0x073E,struct.pack('<III',0,4096,ts));assert reply==0x073F and data[12]==0
 for base,img in [(base1,images[1]),(base2,images[0])]:
  for sector in [0,4096]:
   reply,data=request(port,0x073A,struct.pack('<II',base+sector,ts));assert reply==0x073B and data[4]==0
   for off in range(0,4096,128):
    chunk=img[sector+off:sector+off+128]
    reply,data=request(port,0x073C,struct.pack('<IHI',base+sector+off,len(chunk),ts)+chunk)
    assert reply==0x073D and data[6]==0 and struct.unpack_from('<H',data,4)[0]==128
   reply,data=request(port,0x073E,struct.pack('<III',base+sector,4096,ts))
   assert reply==0x073F and data[12]==0 and struct.unpack_from('<I',data,8)[0]==zlib.crc32(img[sector:sector+4096])
 assert bytes(flash[base1:base1+8192])==B and bytes(flash[base2:base2+8192])==A
 assert bytes(flash[:0x102000])+bytes(flash[0x182000:])==before
 for slot in [0,63]:
  reply,data=request(port,0x0736,bytes([slot]));assert reply==0x0737 and data[1]==0
 print('PASS actual UART handler permutation of slots 0/63 on port',port,': all 8192 bytes (code, assets, config) follow; CRC and headers valid')
 # Refuse writes outside Apps, auth failures, unaligned erase, overflow, excess payload.
 count=len(changes)
 for command,payload,index in [
  (0x073A,struct.pack('<II',base1,ts+1),4),
  (0x073A,struct.pack('<II',0x182000,ts),4),
  (0x073A,struct.pack('<II',base1+1,ts),4),
  (0x073C,struct.pack('<IHI',0x181fff,2,ts)+b'xx',6),
  (0x073C,struct.pack('<IHI',0,1,ts)+b'x',6),
  (0x073C,struct.pack('<IHI',base1,129,ts)+bytes(129),6),
  (0x073C,struct.pack('<IHI',base1,2,ts)+b'x',6),
  (0x073E,struct.pack('<III',0x1fffff,0xffffffff,ts),12)]:
  reply,data=request(port,command,payload);assert data[index]!=0,(command,payload)
 assert len(changes)==count
 put8(addr('bHasCustomAesKey'),1)
 reply,data=request(port,0x073A,struct.pack('<II',base1,ts));assert data[4]!=0 and len(changes)==count
 put8(addr('bHasCustomAesKey'),0)
 print('PASS timestamp/lock, bounds, alignment, malformed length and overflow refusals without flash mutation')
# SPI failure is returned to UVStudio rather than acknowledged as a success.
stub('MB_AppProgram',lambda *a:ret(6))
count=len(changes)
for cmd,payload,index in [(0x073A,struct.pack('<II',0x102000,ts),4),(0x073C,struct.pack('<IHI',0x102000,1,ts)+b'x',6)]:
 reply,data=request(1,cmd,payload);assert data[index]==6 and len(changes)==count
# Never load over a running app (its code cannot be reloaded).
put8(addr('app_running'),1)
u.mem_write(addr('gCommand'),struct.pack('<HHII',0x073A,8,0x102000,ts));packets.clear()
call('UART_HandleCommand',1);assert not packets and len(changes)==count
put8(addr('app_running'),0)
print('PASS SPI errors propagated; a running app blocks mutations; invalidation happens only after external return')
