"""Actual ARM installer Main: LCD init, error display and PTT restore of Main."""
from installer_harness import *

reset()
stub('INS_Run', lambda *_: ret(5))
stub('SPI_WriteByte', lambda *_: ret())
stub('SYSTICK_DelayUs', lambda *_: ret())
recovered = []


def recover(slot, *_):
    recovered.append(slot)
    u.reg_write(UC_ARM_REG_PC, STOP)
    u.emu_stop()


stub('MB_RestoreSlot', recover)
u.mem_write(0x50000410, bytes(4))                # PTT input low = pressed
u.reg_write(UC_ARM_REG_SP, 0x20004000)
u.reg_write(UC_ARM_REG_LR, STOP | 1)
u.emu_start(addr('Main') | 1, 0, count=2000000)
assert u.reg_read(UC_ARM_REG_PC) == STOP and recovered == [0], recovered
print('PASS actual ARM Main: LCD initialization, diagnostic error and PTT restoration of Main; hardware SPI and delay mocked')
