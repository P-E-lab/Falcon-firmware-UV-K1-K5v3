"""Actual ARM installer rendering: title, gauge and status frames."""
from installer_harness import *

reset()
stub('SPI_WriteByte',lambda *_:ret())
def invoke(name,*args):
 for reg,value in zip(regs,args):u.reg_write(reg,value)
 u.reg_write(UC_ARM_REG_SP,0x20004000);u.reg_write(UC_ARM_REG_LR,STOP|1)
 u.emu_start(addr(name)|1,0,count=2000000)
 assert u.reg_read(UC_ARM_REG_PC)==STOP

def frame():return bytes(u.mem_read(addr('gStatusLine'),128))+bytes(u.mem_read(addr('gFrameBuffer'),896))
def ink(f,x,y):return bool(f[(y//8)*128+x]&(1<<(y%8)))
def bounds(f,y0,y1):
 xs=[x for x in range(128) for y in range(y0,y1) if ink(f,x,y)]
 assert xs
 return min(xs),max(xs)
from PIL import Image
images=[]
for stage,percent,error in [(1,0,0),(5,43,0),(8,100,0),(5,43,6)]:
 invoke('progress',stage,0)
 u.mem_write(addr('install_percent'),bytes([percent]));invoke('install_render',error)
 f=frame()
 for y0,y1 in [(16,23),(29,35),(41,48)]:
  l,h=bounds(f,y0,y1);assert abs(l+h-127)<=1,(stage,percent,y0,l,h)
 assert all(not ink(f,x,y) for x in range(128) for y in list(range(16))+list(range(48,64)))
 for x in range(10,118):
  expected=12 if x in (10,117) else 18 if x in (11,116) else 45 if x-12<percent*104//100 else 33
  assert sum(int(ink(f,x,29+y))<<y for y in range(6))==expected
 im=Image.new('1',(128,64),1)
 for y in range(64):
  for x in range(128):
   if ink(f,x,y):im.putpixel((x,y),0)
 images.append(im.resize((768,384),Image.Resampling.NEAREST))
canvas=Image.new('1',(768,384*4),1)
for i,im in enumerate(images):canvas.paste(im,(0,i*384))
canvas.save(ROOT/'validation/installer-preview.png')
print('PASS actual ARM installer rendering: title/gauge/status centered, exact Sentinel gauge, Powering/Punch/error frames',flush=True)
