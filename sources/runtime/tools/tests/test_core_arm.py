"""Exercise actual optimized Cortex-M0 ELF with mocked external hardware."""
from harness import *

# Actual loader on the external store of three firmware slots.
for slot in (0, 1, 4):
    put8(addr('gRunningSlot'), slot)
    place_store(slot)
    for ident in range(1, 14):
        off, size, crc = store_entry(ident)
        call('APP_InvalidateNative')
        assert call('APP_NativeBegin', ident) == 1
        assert bytes(u.mem_read(0x20000280, size)) == store[off:off + size]
        # Same module nests; another module loads over it and the caller's
        # image is back after its End.
        other = ident % 13 + 1
        o_off, o_size, _ = store_entry(other)
        assert call('APP_NativeBegin', ident) == 1 and call('APP_NativeBegin', other) == 1
        assert bytes(u.mem_read(0x20000280, o_size)) == store[o_off:o_off + o_size]
        call('APP_NativeEnd')
        assert bytes(u.mem_read(0x20000280, size)) == store[off:off + size]
        call('APP_NativeEnd'); call('APP_NativeEnd')
        assert u.mem_read(addr('native_depth'), 1)[0] == 0
    print('PASS actual ARM loader (DMA read + hardware CRC), all 13 modules, nesting across modules, store of slot', slot, flush=True)
corrupt = store_physical(4, store_entry(1)[0])
flash[corrupt] ^= 1
call('APP_InvalidateNative'); assert call('APP_NativeBegin', 1) == 0
assert u.mem_read(addr('missing'), 1)[0] == 0, 'a damaged module is not a missing store'
flash[corrupt] ^= 1
binding = store_physical(4, 0x28)                 # store header runtime_crc
flash[binding] ^= 1
call('APP_InvalidateNative'); assert call('APP_NativeBegin', 1) == 0
assert u.mem_read(addr('missing'), 1)[0] == 1, 'another build is reported as missing'
print('PASS wrong runtime binding and corrupt module rejected; missing store reported', flush=True)

stub('SETTINGS_FetchChannelFrequency',lambda channel,*a:ret(14500000+channel*2500))
stub('RADIO_LoadChannel',lambda *a:ret())
attrs[10]=0;attrs[11]=0
put8(eep+txindex,0);put16(eep+vfooff+save,10);put16(eep+vfooff+vfosize+save,11)
for pri_a,pri_b,want in [(10,11,2),(11,10,2),(10,4096,1),(10,10,1),(4096,4096,0)]:
 put16(eep+prio,pri_a);put16(eep+prio+2,pri_b);call('APP_FullWatchReset')
 call('FullWatchInitialize')
 got=u.mem_read(addr('gFullWatchBackgroundCount'),1)[0];assert got==want,(pri_a,pri_b,got,want)
print('PASS actual ARM Full Watch: both priorities on displayed VFOs, swapped, one priority, duplicate rejection and none')
stub('RADIO_SelectVfos',lambda *a:ret())
stub('APP_FalconDisable',lambda *a:ret())
put16(eep+prio,10);put16(eep+prio+2,11);call('APP_FullWatchReset')
put8(addr('gScreenToDisplay'),0);put32(addr('gCurrentFunction'),0)
put8(addr('gInputBoxIndex'),0);put8(eep+rxmode,1)
call('ACTION_SingleDuo');assert u.mem_read(eep+rxmode,1)[0]==2
print('PASS actual mode-cycle action: Dual -> Full (Quad with both priorities on A/B), no Multiwatch skip')

flash[binding]^=1
call('APP_InvalidateNative');assert call('APP_NativeBegin',1)==1;call('APP_NativeEnd')
assert u.mem_read(addr('missing'),1)[0]==0
# Actual FM long-1 favorite storage: writes must occur after native exit.
for name in sym:
 if name.startswith(('BK1080_','BK4819_')):stub(name,lambda *a:ret())
stub('FM_CancelSeek',lambda *a:ret())
writes=[]
def write_guard(a,p,n,*_):
 writes.append((a,bytes(u.mem_read(p,n))));ret()
stub('PY25Q16_WriteBuffer',write_guard)
put8(addr('gFmRadioMode'),0);call('FM_ProcessKeys',1,1,1)
assert len(writes)==1 and writes[0][0]==0xA070,writes
assert bytes(u.mem_read(addr('pendingFavourite'),1))==bytes([0])
print('PASS actual FM long-1 save written once',flush=True)
# Actual bounded SPI error path with DMA IRQs unavailable.
for disabled in (0, 1):
    u.reg_write(UC_ARM_REG_PRIMASK, disabled)
    assert call('MB_StoreCRC', 0x182000, 16, 0x20005000, count=1000000) == 6
    u.reg_write(UC_ARM_REG_PRIMASK, 0)
for a, n in [(0x200001, 1), (0x1aa000, 0xffffffff)]:
    assert call('MB_StoreCRC', a, n, 0x20005000) == 4
print('PASS actual SPI timeouts with IRQ enabled/masked, and bounds rejection', flush=True)

# Module engine: whatever reuses the overlay RAM gives the active module back.
put8(addr('gRunningSlot'), 0)
place_store(0)
ident = 13
off, size, _ = store_entry(ident)
call('APP_InvalidateNative')
assert call('APP_NativeBegin', ident) == 1
handlers.pop(addr('PY25Q16_ReadPhysical'), None)
stub('PY25Q16_ReadPhysical', rd)
for h in [k for k in sym if k.startswith('PY25Q16_WriteBuffer')]:
    handlers.pop(addr(h), None)
# The real write path reuses the sector cache: the module must be back
# afterwards. SPI2 and DMA always report ready; the NOR reads back zeros.
SPI2, DMA1 = 0x40003800, 0x40020000
def ready(uc, access, address, size, value, _):
    if address == SPI2 + 8:
        uc.mem_write(SPI2 + 8, struct.pack('<I', 0x3))
    elif address == DMA1:
        uc.mem_write(DMA1, struct.pack('<I', 0xFFFFFFFF))
u.hook_add(UC_HOOK_MEM_READ, ready, begin=SPI2, end=SPI2 + 0x20)
u.hook_add(UC_HOOK_MEM_READ, ready, begin=DMA1, end=DMA1 + 4)
call('PY25Q16_WriteBuffer', 0xA000, 0x20005000, 16, 0, count=20000000)
assert bytes(u.mem_read(0x20000280, size)) == store[off:off + size], 'module resumed after a write'
call('APP_NativeEnd')
assert u.mem_read(addr('native_depth'), 1)[0] == 0
put8(addr('app_running'), 1)
assert call('APP_NativeBegin', 1) == 0, 'no module over a running app'
put8(addr('app_running'), 0)
print('PASS module resumed after a flash write from module code; refused while an app runs', flush=True)
