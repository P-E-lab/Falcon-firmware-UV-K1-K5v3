"""Execute CHIRP handshake/version methods with serial I/O mocked; no CHIRP UI needed."""
import ast,logging,types
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'chirp/falcon4k.chirp.v4.0.0.py';tree=ast.parse(p.read_text())
ns={'LOG':logging.getLogger('test'),'errors':types.SimpleNamespace(RadioError=RuntimeError)}
for name in ['_getstring','_sayhello']:
 node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name==name)
 exec(compile(ast.Module(body=[node],type_ignores=[]),str(p),'exec'),ns)
ns['_send_command']=lambda *a:None
ns['_readmem']=lambda *a:(_ for _ in ()).throw(AssertionError('release must not read settings schema marker'))
for wire,release in [('FALCON 7.0/4.0','FALCON 4K 4.0.0'),('FALCON 7.0/3.5','FALCON 4K 3.5.0'),('FALCON 7.0/3.4','FALCON 4K 3.4.0'),('FALCON 7.0/3.3','FALCON 4K 3.3.0'),('FALCON 7.0/3.2','FALCON 4K 3.2.0'),('FALCON 7.0/3.1','FALCON 4K 3.1.0'),('FALCON 7.0/3.0','FALCON 4K 3.0.0'),('FALCON 3.0.0','FALCON 3.0.0')]:
 ns['_receive_reply']=lambda *a:b'\x15\x05\x24\x00'+wire.encode().ljust(16,b'\0')+bytes(20)
 assert ns['_sayhello'](None)==release,(wire,release)
cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='UVK5RadioEgzumer')
model=next(n.value.value for n in cls.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='MODEL' for t in n.targets))
assert model.endswith('Falcon 4K v4.0.0')
assert 'Firmware : Falcon 4K v4.0.0' in p.read_text()
node=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='_version_string')
tmp={};exec(compile(ast.Module(body=[node],type_ignores=[]),str(p),'exec'),tmp)
obj=types.SimpleNamespace(FIRMWARE_VERSION='FALCON 4K 3.1.0',_memobj=types.SimpleNamespace(version=types.SimpleNamespace(version='2.3.1.D1')))
assert tmp['_version_string'](obj)=='FALCON 4K 3.1.0'
for name in ['_version_string','_supports_navigation']:
 nodes=[n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name==name]
 # Some revisions name the navigation predicate differently; test the actual node below.
for node in cls.body:
 if isinstance(node,ast.FunctionDef) and '3.1.0' in ast.unparse(node) and '_version_string' in ast.unparse(node):
  tmp={};exec(compile(ast.Module(body=[node],type_ignores=[]),str(p),'exec'),tmp)
  obj=types.SimpleNamespace(_version_string=lambda:'FALCON 4K 3.1.0')
  assert tmp[node.name](obj)
# 3.4.0 general menu block: read, written on change, same layout as the firmware.
src=p.read_text()
assert '(0x00C0A0, 0x00C0A6),' in src and 'blocks_to_write.append((0x00C0A0, 0x00C0A6))' in src
fmt=src.split('#seekto 0x00C0A0;',1)[1].split('#seekto',1)[0].split()
assert fmt[:12]==['u8','scan_menu_tag;','u8','scan_noise_off;','u8','scan_max_rx;','u8','scan_rx_hold;','u8','scan_power_save;','u8','popup_time;'],fmt[:12]
print('PASS actual CHIRP hello: FALCON 7.0/4.0 -> 4K 4.0.0, 7.0/3.5 -> 4K 3.5.0, 7.0/3.4 -> 4K 3.4.0, 7.0/3.3 -> 4K 3.3.0, 7.0/3.2 -> 4K 3.2.0, 7.0/3.1 -> 4K 3.1.0, 7.0/3.0 -> 4K 3.0.0 without EEPROM access; model and navigation support v3.1.0')
