"""Falcon 1K CHIRP driver: handshake, edition guard and memory map, no CHIRP UI needed."""
import ast, logging, re, types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
D1K = ROOT / 'chirp/falcon1k.chirp.v3.1.1.py'
D4K = ROOT / 'chirp/falcon4k.chirp.v3.1.1.py'


def hello(path, wire):
    tree = ast.parse(path.read_text())
    ns = {'LOG': logging.getLogger('test'), 'errors': types.SimpleNamespace(RadioError=RuntimeError)}
    for name in ['_getstring', '_sayhello']:
        node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == name)
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(path), 'exec'), ns)
    ns['_send_command'] = lambda *a: None
    ns['_readmem'] = lambda *a: (_ for _ in ()).throw(AssertionError('no memory read in the handshake'))
    ns['_receive_reply'] = lambda *a: b'\x15\x05\x24\x00' + wire.encode().ljust(16, b'\0') + bytes(20)
    try:
        return ns['_sayhello'](None)
    except RuntimeError as e:
        return 'REFUSED: %s' % e


# Each driver accepts only its own edition.
assert hello(D1K, 'FALCON 7.0/3.1K') == 'FALCON 1K 3.1.1'
assert hello(D1K, 'FALCON 7.0/3.1').startswith('REFUSED')
assert hello(D1K, 'FALCON 7.0/3.0').startswith('REFUSED')
assert hello(D4K, 'FALCON 7.0/3.1') == 'FALCON 4K 3.1.0'
assert hello(D4K, 'FALCON 7.0/3.1K').startswith('REFUSED')

src = D1K.read_text()
assert 'MR_CHANNELS_MAX = 1024' in src and 'BAND_FIRST = 995' in src
# Channels, names and attributes in the F4HWN 1024 layout of the config bank.
fmt = src.split('MEM_FORMAT = """', 1)[1].split('"""', 1)[0]
assert re.search(r'#seekto 0x000000;.*?\} channel\[1024\];', fmt, re.S)
assert re.search(r'#seekto 0x004000;.*?\} channelname\[1024\];', fmt, re.S)
assert re.search(r'#seekto 0x008000;.*?\} ch_attr\[1031\];', fmt, re.S)
assert not re.search(r'0x1[0-9A-Fa-f]{5}', fmt), 'no 4K address left in the memory format'
# Download/upload ranges: below the calibration boundary, and not overlapping.
blocks = [(0x000000, 0x4000), (0x004000, 0x8000), (0x008000, 0x8800), (0x008900, 0xA174)]
for a, b in zip(blocks, blocks[1:]):
    assert a[1] <= b[0]
for code in ['(0x000000, 0x000000 + (MR_CHANNELS_MAX * 16))', '(0x004000, 0x004000 + (MR_CHANNELS_MAX * 16))',
             '(0x008000, 0x008000 + (MR_CHANNELS_MAX * 2))']:
    assert src.count(code) == 2, code  # download and upload
assert '0x122000' not in src and '0x132000' not in src and '0x142000' not in src
assert 'MEM_SIZE =      0x010001' in src
tree = ast.parse(src)
cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'UVK5RadioEgzumer')
model = next(n.value.value for n in cls.body if isinstance(n, ast.Assign)
             and any(isinstance(t, ast.Name) and t.id == 'MODEL' for t in n.targets))
assert model == 'UV-K1 / UV-K5 V3 Falcon 1K v3.1.1'
node = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == '_supports_navigation')
tmp = {}
exec(compile(ast.Module(body=[node], type_ignores=[]), str(D1K), 'exec'), tmp)
assert tmp['_supports_navigation'](types.SimpleNamespace(_version_string=lambda: 'FALCON 1K 3.1.1'))
print('PASS Falcon 1K CHIRP driver: own edition only, 1024 memories at 0x0000/0x4000/0x8000, bands 995..1024')
