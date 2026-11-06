#!/usr/bin/env python3
"""Multiboot firmware slots over the radio's serial link (same commands as UV Studio).

  falcon_slots.py info  [--port P]                  read slot headers 0..4 (read only)
  falcon_slots.py write FILE.bin --slot N [--port P] erase, program and verify slot N (1..4)
  falcon_slots.py apps  [--port P]                  list the installed apps (read only)
  falcon_slots.py app-write FILE.app --slot N [--port P]  replace app slot N (0..63)
  falcon_slots.py app-erase --slot N [--port P]            remove the app of slot N (0..63)

Protocol: sources/runtime/App/app/uart.c (0x0514 hello, 0x0720..0x0727 slots)
and driver/mb_flash.h (slot layout, 64-byte FMB1 header). The image is written
first and the COMMITTED header last, so an interrupted write leaves an
uncommitted slot that the radio refuses to restore. Slot 0 (backup) is
refused by the radio and by this tool.

Apps (0x0730..0x0737, apps/app_overlay.h) need the radio running Falcon, not
the boot menu. A .app is the 64-byte header + code + assets: the code goes at
slot+0x1000, the assets at slot+0x100 and the header last. Erasing keeps the
app's own settings when the name matches (APP_SlotErase).
"""
import argparse
import glob
import os
import re
import struct
import sys
import time
import zlib

import serial

BAUDRATE = 38400
OBFUS = bytes([0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
               0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80])

SLOT_COUNT = 5
SLOT_STRIDE = 0x20000
IMG_OFFSET = 0x1000
INT_APP_SIZE = 0x1D800
SLOT_MAGIC = 0x31424D46            # "FMB1"
FLAG_COMMITTED = 1
CHUNK = 128                        # command buffer is 256 bytes

STATUS = ['OK', 'MAGIC', 'VERSION', 'NOT_COMMITTED', 'SIZE', 'CRC']

APP_SLOT_COUNT = 64
APP_MAGIC = b'FAP1'
APP_CODE_OFFSET = 0x1000
APP_ASSET_OFFSET = 0x100
APP_STATUS = ['OK', 'SLOT', 'MAGIC', 'ABI', 'NOT_COMMITTED', 'SIZE', 'CRC', 'VMA', 'AUTH', 'CAP',
              'ERR10', 'BUSY', 'SPI']


def crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
    return c


def xor(data):
    return bytes(b ^ OBFUS[i % 16] for i, b in enumerate(data))


class Radio:
    def __init__(self, port):
        self.ser = serial.Serial(port, BAUDRATE, timeout=2, write_timeout=2)
        self.ser.reset_input_buffer()
        self.timestamp = int(time.time()) & 0xFFFFFFFF

    def send(self, cmd_id, data=b''):
        body = struct.pack('<HH', cmd_id, len(data)) + data
        if len(body) % 2:
            body += b'\x00'
        payload = xor(body + struct.pack('<H', crc16(body)))
        self.ser.write(struct.pack('<HH', 0xCDAB, len(body)) + payload + struct.pack('<H', 0xBADC))

    def recv(self, want_id, timeout=5.0):
        end = time.time() + timeout
        buf = b''
        while time.time() < end:
            buf += self.ser.read(self.ser.in_waiting or 1)
            while True:
                start = buf.find(b'\xab\xcd')
                if start < 0 or len(buf) < start + 4:
                    break
                size = struct.unpack_from('<H', buf, start + 2)[0]
                if len(buf) < start + 8 + size:
                    break
                body = xor(buf[start + 4:start + 4 + size])
                buf = buf[start + 8 + size:]
                if struct.unpack_from('<H', body)[0] == want_id:
                    return body
        raise SystemExit('no reply 0x%04X from the radio' % want_id)

    def hello(self):
        self.send(0x0514, struct.pack('<I', self.timestamp))
        body = self.recv(0x0515)
        return body[4:20].split(b'\0')[0].decode('ascii', 'replace')

    def slot_info(self, slot):
        self.send(0x0720, bytes([slot, 0]))
        body = self.recv(0x0721)
        return body[5], body[6:6 + 64]

    def slot_validate(self, slot):
        self.send(0x0726, bytes([slot, 0]))
        body = self.recv(0x0727, timeout=10)
        crc, = struct.unpack_from('<I', body, 4)
        return body[9], crc

    def slot_erase(self, slot):
        self.send(0x0722, bytes([slot, 0]) + struct.pack('<I', self.timestamp))
        return self.recv(0x0723, timeout=15)[5]

    def slot_write(self, slot, offset, data):
        self.send(0x0724, bytes([slot, 0]) + struct.pack('<IHI', offset, len(data), self.timestamp) + data)
        return self.recv(0x0725)[5]

    def app_info(self, slot):
        self.send(0x0730, bytes([slot, 0]))
        body = self.recv(0x0731)
        return body[5], body[6:6 + 64]

    def app_erase(self, slot):
        self.send(0x0732, bytes([slot, 0]) + struct.pack('<I', self.timestamp))
        return self.recv(0x0733, timeout=15)[5]

    def app_write(self, slot, offset, data):
        self.send(0x0734, bytes([slot, 0]) + struct.pack('<IHI', offset, len(data), self.timestamp) + data)
        return self.recv(0x0735)[5]

    def app_validate(self, slot):
        self.send(0x0736, bytes([slot, 0]))
        return self.recv(0x0737)[5]


def status_name(s):
    return STATUS[s] if s < len(STATUS) else 'ERR%d' % s


def describe(hdr):
    magic, ver, flags, size, crc = struct.unpack_from('<IHHII', hdr)
    if magic != SLOT_MAGIC:
        return 'empty'
    name = hdr[16:32].split(b'\0')[0].decode('ascii', 'replace')
    fw = hdr[32:48].split(b'\0')[0].decode('ascii', 'replace')
    return '%-16s %-16s %6d bytes  crc %08X  flags %d' % (name, fw, size, crc, flags)


def find_port():
    ports = sorted(glob.glob('/dev/cu.usbserial*') + glob.glob('/dev/cu.usbmodem*'))
    if len(ports) != 1:
        raise SystemExit('pass --port (found: %s)' % (', '.join(ports) or 'none'))
    return ports[0]


def cmd_info(radio):
    for slot in range(SLOT_COUNT):
        status, hdr = radio.slot_info(slot)
        print('slot %d%s: %-13s %s' % (slot, ' (backup)' if slot == 0 else '', status_name(status),
                                      describe(hdr)))


def cmd_write(radio, slot, path, name, version):
    if not 1 <= slot < SLOT_COUNT:
        raise SystemExit('slot must be 1..4')
    image = open(path, 'rb').read()
    if not 0 < len(image) <= INT_APP_SIZE:
        raise SystemExit('image size %d out of range' % len(image))
    crc = zlib.crc32(image)
    hdr = struct.pack('<IHHII16s16s16s', SLOT_MAGIC, 1, FLAG_COMMITTED, len(image), crc,
                      name.encode()[:15], version.encode()[:15], b'\0' * 16)

    status = radio.slot_erase(slot)
    if status:
        raise SystemExit('erase failed: %s' % status_name(status))
    for off in range(0, len(image), CHUNK):
        status = radio.slot_write(slot, IMG_OFFSET + off, image[off:off + CHUNK])
        if status:
            raise SystemExit('write failed at %d: %s' % (off, status_name(status)))
        print('\r%3d%%' % ((off + CHUNK) * 100 // len(image)), end='', flush=True)
    print()
    status = radio.slot_write(slot, 0, hdr)
    if status:
        raise SystemExit('header write failed: %s' % status_name(status))
    status, radio_crc = radio.slot_validate(slot)
    print('validate: %s, crc %08X (file %08X)' % (status_name(status), radio_crc, crc))
    if status or radio_crc != crc:
        raise SystemExit('slot %d NOT valid' % slot)


def app_status_name(s):
    return APP_STATUS[s] if s < len(APP_STATUS) else 'ERR%d' % s


def app_describe(hdr):
    if hdr[:4] != APP_MAGIC:
        return 'empty'
    size, crc = struct.unpack_from('<II', hdr, 8)
    name = hdr[20:36].split(b'\0')[0].decode('ascii', 'replace')
    ver = hdr[36:52].split(b'\0')[0].decode('ascii', 'replace')
    return '%-16s %-8s code %4d bytes  crc %08X' % (name, ver, size, crc)


def cmd_apps(radio):
    for slot in range(APP_SLOT_COUNT):
        status, hdr = radio.app_info(slot)
        if hdr[:4] == APP_MAGIC:
            print('app %2d: %-13s %s' % (slot, app_status_name(status), app_describe(hdr)))


def cmd_app_write(radio, slot, path):
    if not 0 <= slot < APP_SLOT_COUNT:
        raise SystemExit('app slot must be 0..63')
    blob = open(path, 'rb').read()
    if len(blob) < 64 or blob[:4] != APP_MAGIC:
        raise SystemExit('%s is not a .app file' % path)
    header = blob[:64]
    code_size, = struct.unpack_from('<I', header, 8)
    asset_size, = struct.unpack_from('<H', header, 60)
    code, assets = blob[64:64 + code_size], blob[64 + code_size:]
    if len(code) != code_size or len(assets) != asset_size:
        raise SystemExit('%s: sizes do not match its header' % path)
    print('old: %s' % app_describe(radio.app_info(slot)[1]))

    status = radio.app_erase(slot)
    if status:
        raise SystemExit('erase failed: %s' % app_status_name(status))
    for base, data in ((APP_ASSET_OFFSET, assets), (APP_CODE_OFFSET, code), (0, header)):
        for off in range(0, len(data), CHUNK):
            status = radio.app_write(slot, base + off, data[off:off + CHUNK])
            if status:
                raise SystemExit('write failed at 0x%X: %s' % (base + off, app_status_name(status)))
    status = radio.app_validate(slot)
    print('new: %s  validate: %s' % (app_describe(header), app_status_name(status)))
    if status:
        raise SystemExit('app slot %d NOT valid' % slot)


def cmd_app_erase(radio, slot):
    if not 0 <= slot < APP_SLOT_COUNT:
        raise SystemExit('app slot must be 0..63')
    print('old: %s' % app_describe(radio.app_info(slot)[1]))
    status = radio.app_erase(slot)
    if status:
        raise SystemExit('erase failed: %s' % app_status_name(status))
    print('app slot %d erased' % slot)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=('info', 'write', 'apps', 'app-write', 'app-erase'))
    p.add_argument('file', nargs='?')
    p.add_argument('--port')
    p.add_argument('--slot', type=int)
    p.add_argument('--name', help='slot name; default from the file name: Falcon 1K or Falcon 4K')
    p.add_argument('--version', help='slot version; default from the file name, e.g. v3.1.1')
    a = p.parse_args()

    radio = Radio(a.port or find_port())
    print('radio:', radio.hello())
    if a.action == 'info':
        cmd_info(radio)
    elif a.action == 'apps':
        cmd_apps(radio)
    elif a.action == 'app-write':
        if a.slot is None or not a.file:
            p.error('app-write needs --slot and FILE')
        cmd_app_write(radio, a.slot, a.file)
    elif a.action == 'app-erase':
        if a.slot is None:
            p.error('app-erase needs --slot')
        cmd_app_erase(radio, a.slot)
    else:
        if a.slot is None or not a.file:
            p.error('write needs --slot and FILE')
        base = os.path.basename(a.file)
        name = a.name or ('Falcon 1K' if base.startswith('Falcon1K') else 'Falcon 4K')
        m = re.search(r'_v(\d+\.\d+\.\d+)', base)
        version = a.version or ('v' + m.group(1) if m else 'v0.0.0')
        cmd_write(radio, a.slot, a.file, name, version)


if __name__ == '__main__':
    sys.exit(main())
