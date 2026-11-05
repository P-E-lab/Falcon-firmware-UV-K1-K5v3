"""Check actual compiled release identity, API SystemInfo strings and boot label."""
from pathlib import Path
p=Path(__file__).with_name('test_resources_arm.py');ns={'__file__':str(p)}
exec(compile(p.read_text().split('golddata=')[0],str(p),'exec'),ns);globals().update(ns)
def cstring(p):return bytes(u.mem_read(p,32)).split(bytes([0]),1)[0]
assert cstring(addr('Version'))==b'FALCON 3.1.1'
probe=B/'branding_probe.c';probe.write_text('#include <stddef.h>\n#include "apps/app_api.h"\nconst unsigned branding_offsets[]={offsetof(app_api_t,sys_version),offsetof(app_api_t,sys_build_commit)};\n')
cmd=out[:];cmd[cmd.index('-c')+1]=str(probe);cmd[cmd.index('-o')+1]=str(B/'branding_probe.o');subprocess.run(cmd,cwd=c['directory'],check=True)
e=ELFFile(open(B/'branding_probe.o','rb'));a,b=struct.unpack('<II',e.get_section_by_name('.rodata.branding_offsets').data())
assert cstring(get32(addr('app_api')+a))==b'FALCON 3.1.1'
assert cstring(get32(addr('app_api')+b))==b'Falcon 3.1.1'
seen=[]
stub('UI_DrawCompactText5x5',lambda p,*a:(seen.append(cstring(p)),ret()))
stub('UI_PrintSmallAt',lambda *a:ret())
stub('BATTERY_VoltsToPercent',lambda *a:ret(97))
stub('SPI_WriteByte',lambda *a:ret())
assert call('APP_NativeBegin',10)==1
call('UI_DisplayWelcomeNative',count=3000000);call('APP_NativeEnd')
assert b'FALCON v3' in seen,seen
assert call('UI_CompactTextWidth',addr('Version'))<=124
print('PASS actual ARM Version/API sys_version/sys_build_commit and welcome label: Falcon v3.1.1')
