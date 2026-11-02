from pathlib import Path
p=Path(__file__).with_name('test_resources_arm.py');ns={'__file__':str(p)}
exec(compile(p.read_text().split('# Compile constant layout offsets')[0],str(p),'exec'),ns);globals().update(ns)
valid={0,17,4095};reads=[]
stub('RADIO_CheckValidChannel',lambda ch,*a:ret(int(ch in valid)))
def tracedread(a,p,n,*rest):reads.append((a,n));rd(a,p,n,0)
stub('PY25Q16_ReadBuffer',tracedread)
assert call('app_memory_read',65535,0)==4096
out=0x20005800
for channel in [0,17,4095]:
 flash[0x1de000+16*channel:0x1de004+16*channel]=struct.pack('<I',44601875)
 flash[0x1ee000+16*channel:0x1ee008+16*channel]=b'PMR 2   '
 u.mem_write(out,b'X'*16);assert call('app_memory_read',channel,out)==44601875
 assert bytes(u.mem_read(out,16))==b'PMR 2'+bytes(4)+b'X'*7,bytes(u.mem_read(out,16))
reads.clear()
for ch in [1,4096,65534]:assert call('app_memory_read',ch,out)==0
assert not reads
print('PASS actual resident memory bridge: first/last channel, name trimming, 4096 count and no reads for invalid/out-of-bounds channels')
