#!/usr/bin/env python3
# Synthetic Opoona SL01 (LZO1X, literal-only) wrapper around an OPKG package
# holding a blanked-magic .tpl and a "TPLE" bundle .tpl, plus a raw package.
import struct, sys, os
out = sys.argv[1]

def tpl(fill):
    # blanked-magic TPL: 1 texture, 8x8 I8, data at 0x40
    h = struct.pack('>IIIII', 0, 1, 12, 20, 0) + struct.pack('>HHIIIIII', 8, 8, 1, 0x40, 0, 0, 1, 1)
    h = h.ljust(0x40, b'\0')
    return h + bytes([fill]) * 64

def pkg(members):
    n = len(members)
    off = 8 + 0x20 * n
    hdr = struct.pack('<IHH', n, 0x20, 0x20)
    body = b''
    for name, data in members:
        hdr += struct.pack('<II', off + len(body), len(data)) + name.ljust(24, b'\0')
        body += data
    return hdr + body

def sl01(data):
    assert len(data) <= 238
    stream = bytes([17 + len(data)]) + data + b'\x11\x00\x00'
    return b'SL01' + struct.pack('<II', len(data), len(stream)) + stream

tple = b'TPLE'.ljust(0x10, b'\0') + struct.pack('>III', 3, 0x80, 0x40) + b'eye'
tple = tple.ljust(0x40, b'\0') + tpl(0x20) + tpl(0x80) + tpl(0xe0)
open(os.path.join(out, 'raw.mdl'), 'wb').write(pkg([(b'x/model.mdl', b'DHDM-not-a-model'), (b'x/tex.bin', b'hello')]))
open(os.path.join(out, 'sample.btl'), 'wb').write(sl01(pkg([(b'dir/a.tpl', tpl(0x40))])))
open(os.path.join(out, 'eye.tpl'), 'wb').write(tple)
