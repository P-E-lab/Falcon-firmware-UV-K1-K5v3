#!/usr/bin/env python3
"""Build the external store and the one-file installer payload.

Inputs : the runtime ELF/BIN (sources/runtime/build/<preset>, S4K by default).
Outputs: sources/installer/App/install_payload.c (compressed runtime + store),
         build/S4K/store.raw, runtime.raw and onefile-measurements.json.

The store layout is described in sources/runtime/App/store_layout.h; its
constants are read from that header so both sides always agree.
"""
import argparse
import json
import re
import struct
import subprocess
import zlib
from pathlib import Path

import zopfli.zlib

# Module ids follow NATIVE_MENU (1) .. NATIVE_JOURNAL (14) in apps/app_overlay.h.
MODULES = ('menu', 'editor', 'rf', 'fm', 'vfo', 'scan', 'plot', 'lists', 'options', 'boot',
           'menukeys', 'menuset', 'editkeys', 'messages')
INTERNAL_FLASH = 120832            # 118 KiB application area
INSTALL_MAGIC = 0x31534946         # "FIS1"


def layout_constants(header):
    values = {}
    for name, value in re.findall(r'#define\s+(STORE_\w+|INSTALL_\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\b',
                                  header.read_text()):
        values[name] = int(value, 0)
    values['STORE_SIZE'] = values['STORE_ASSET_SIZE'] + values['STORE_BANK_SIZE']
    return values


def _crc_table():
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
        table.append(c & 0xFFFFFFFF)
    return table


CRC_TABLE = _crc_table()


def hw_crc(data):
    """PY32 CRC unit: CRC-32/MPEG-2 over little-endian 32-bit words."""
    assert len(data) % 4 == 0
    crc = 0xFFFFFFFF
    for i in range(0, len(data), 4):
        for byte in data[i:i + 4][::-1]:        # each word is fed MSB first
            crc = ((crc << 8) & 0xFFFFFFFF) ^ CRC_TABLE[(crc >> 24) ^ byte]
    return crc


def section(objcopy, elf, name, scratch):
    subprocess.run([objcopy, '-O', 'binary', '--only-section=' + name, str(elf), str(scratch)],
                   check=True)
    return scratch.read_bytes()


def thumb_filter(data, to_absolute):
    """Thumb BL filter (as xz's ARM-Thumb BCJ): BL offsets become absolute
    targets, which compress better. A BL starts at an even position i with
    data[i+1] = 0xF0..0xF7 and data[i+3] = 0xF8..0xFF; the filter keeps those
    bits, so both sides find the same BLs, and two BLs never overlap. The
    installer reverses it while inflating (sources/installer/App/main.c)."""
    out = bytearray(data)
    i = 0
    while i + 4 <= len(out):
        if out[i + 1] & 0xF8 == 0xF0 and out[i + 3] & 0xF8 == 0xF8:
            v = (out[i + 1] & 7) << 19 | out[i] << 11 | (out[i + 3] & 7) << 8 | out[i + 2]
            v = (v + ((i + 4) >> 1) if to_absolute else v - ((i + 4) >> 1)) & 0x3FFFFF
            out[i:i + 4] = bytes((v >> 11 & 0xFF, 0xF0 | v >> 19, v & 0xFF, 0xF8 | (v >> 8 & 7)))
            i += 4
        else:
            i += 2
    return bytes(out)


def pad4(data):
    return data + bytes(-len(data) % 4)


def build_store(c, firmware, entries):
    """Place entries in the virtual store; returns (image, placements)."""
    image = bytearray(c['STORE_DATA_OFFSET'])
    placements = []
    zones = [(c['STORE_DATA_OFFSET'], c['STORE_JOURNAL']),
             (c['STORE_ASSET_SIZE'], c['STORE_SIZE'])]
    cursor = list(start for start, _ in zones)
    for data in entries:
        data = pad4(data)
        for zone, (_, end) in enumerate(zones):
            if cursor[zone] + len(data) <= end:
                offset = cursor[zone]
                cursor[zone] += len(data)
                break
        else:
            raise SystemExit('external store full')
        if len(image) < offset + len(data):
            image.extend(b'\xff' * (offset + len(data) - len(image)))
        image[offset:offset + len(data)] = data
        placements.append((offset, len(data), hw_crc(data)))

    header = struct.pack('<4I', c['STORE_MAGIC'], len(firmware), hw_crc(firmware), len(placements))
    header += b''.join(struct.pack('<3I', *p) for p in placements)
    assert c['STORE_HEADER_OFFSET'] + len(header) <= c['STORE_DATA_OFFSET']
    image[c['STORE_HEADER_OFFSET']:c['STORE_HEADER_OFFSET'] + len(header)] = header
    for gap in range(c['STORE_HEADER_OFFSET'] + len(header), c['STORE_DATA_OFFSET']):
        image[gap] = 0xFF

    # FWA1 header: the asset-zone part of the store, journal page excluded.
    payload_end = min(len(image), c['STORE_JOURNAL'])
    payload = bytes(image[32:payload_end])
    build_id = struct.pack('<8sII', b'FALCON', len(firmware), hw_crc(firmware))
    image[0:32] = struct.pack('<IHHII16s', c['STORE_FWA1_MAGIC'], 1, 32, len(payload),
                              zlib.crc32(payload), build_id)
    used_asset = cursor[0]
    used_bank = cursor[1] - c['STORE_ASSET_SIZE']
    return bytes(image), placements, used_asset, used_bank


def main():
    p = argparse.ArgumentParser()
    p.add_argument('runtime', type=Path)
    p.add_argument('installer', type=Path)
    p.add_argument('--objcopy', default='arm-none-eabi-objcopy')
    p.add_argument('--name', default='falcon.data')
    p.add_argument('--preset', default='S4K', help='runtime build preset: S4K or S1K')
    a = p.parse_args()

    c = layout_constants(a.runtime / 'App/store_layout.h')
    build = a.runtime / 'build' / a.preset
    elf = build / (a.name + '.elf')
    firmware = (build / (a.name + '.bin')).read_bytes()
    assert len(firmware) % 4 == 0 and len(firmware) <= INTERNAL_FLASH

    scratch = build / 'section.raw'
    resources = section(a.objcopy, elf, '.fp_resources', scratch)
    modules = [section(a.objcopy, elf, '.native_' + key, scratch) for key in MODULES]
    for key, data in zip(MODULES, modules):
        assert 0 < len(data) <= c['STORE_MODULE_MAX'], key
    store, placements, used_asset, used_bank = build_store(c, firmware, [resources] + modules)

    raw = firmware + store
    compressed = zopfli.zlib.compress(thumb_filter(raw, True), numiterations=15)[2:-4]
    assert thumb_filter(zlib.decompress(compressed, -15), False) == raw
    meta = [INSTALL_MAGIC, len(firmware), zlib.crc32(firmware), len(store), zlib.crc32(store),
            len(compressed), zlib.crc32(raw), zlib.crc32(compressed)]
    rows = ''.join(','.join(map(str, compressed[i:i + 32])) + ',\n'
                   for i in range(0, len(compressed), 32))
    (a.installer / 'App/install_payload.c').write_text(
        '/* Generated by tools/build_payload.py: compressed runtime + external store. */\n'
        '#include <stdint.h>\n'
        'const uint32_t install_meta[8] = {' + ','.join('%uu' % x for x in meta) + '};\n'
        'const uint8_t install_payload[] = {\n' + rows + '};\n')

    (build / 'store.raw').write_bytes(store)
    (build / 'runtime.raw').write_bytes(firmware)
    report = {
        'runtime': len(firmware),
        'runtime_percent': round(len(firmware) * 100 / INTERNAL_FLASH, 2),
        'store': len(store),
        'store_asset_used': used_asset,
        'store_asset_percent': round(used_asset * 100 / c['STORE_JOURNAL'], 2),
        'store_bank_used': used_bank,
        'store_bank_percent': round(used_bank * 100 / c['STORE_BANK_SIZE'], 2),
        'resources': len(resources),
        'modules': {k: len(m) for k, m in zip(MODULES, modules)},
        'compressed': len(compressed),
    }
    (build / 'onefile-measurements.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
