from pathlib import Path
p=Path(__file__).with_name('test_resources_arm.py');ns={'__file__':str(p)}
exec(compile(p.read_text().split('# Compile constant layout offsets')[0],str(p),'exec'),ns);globals().update(ns)
spi=[]
stub('mb_spi_byte',lambda b,*a:(spi.append(b&255),ret(0)))
cache=bytes((i*13)%256 for i in range(4096));u.mem_write(0x20000280,cache)
payload=bytes(range(40));u.mem_write(0x20005800,payload)
for masked in [0,1]:
 u.reg_write(UC_ARM_REG_PRIMASK,masked)
 spi.clear();assert call('MB_AppProgram',0x1020f0,0x20005800,40)==0
 expected=[6,2,0x10,0x20,0xf0]+list(payload[:16])+[5,255]+[6,2,0x10,0x21,0]+list(payload[16:])+[5,255]
 assert spi==expected,(spi,expected)
 assert bytes(u.mem_read(0x20000280,4096))==cache
 assert u.reg_read(UC_ARM_REG_PRIMASK)==masked
 spi.clear();assert call('MB_AppProgram',0x102000,0,4096)==0
 assert spi==[6,0x20,0x10,0x20,0,5,255],spi
 assert bytes(u.mem_read(0x20000280,4096))==cache
 assert u.reg_read(UC_ARM_REG_PRIMASK)==masked
 print('PASS actual NOR executor: sector erase and page-boundary program use existing SPI routines; no overlay RAM change; IRQ state',masked,'preserved')
handlers.pop(addr('mb_spi_byte'))
for masked in [0,1]:
 u.reg_write(UC_ARM_REG_PRIMASK,masked)
 assert call('MB_AppProgram',0x102000,0,4096,count=1000000)==6
 assert call('MB_AppProgram',0x102000,0x20005800,40,count=1000000)==6
 assert u.reg_read(UC_ARM_REG_PRIMASK)==masked
print('PASS bounded SPI failure on erase/program with IRQs enabled or disabled')
