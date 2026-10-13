#!/usr/bin/env python3
"""Synthetic Rebellion Asura resource file t.wii (AsuraZlb wrapper) with one RSCF chunk embedding an 8x4 I8 TPL named
"\\gfx\\t.tga" and one unrelated chunk. usage: mk_asura.py OUTDIR"""
import os, struct, sys, zlib

w, h = 8, 4
tpl = struct.pack('>III', 0x0020af30, 1, 0x0c) + struct.pack('>II', 0x14, 0)
tpl += struct.pack('>HHIIIIIIfBBBB', h, w, 1, 0x40, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
tpl = tpl.ljust(0x40, b'\0') + bytes(w * h)
name = b'\\gfx\\t.tga\0'
name = name.ljust((len(name) + 3) & ~3, b'\0')
data = struct.pack('>III', 2, 0, len(tpl)) + name + tpl
chunk = lambda tag, ver, inst, payload: tag + struct.pack('>III', 16 + len(payload), ver, inst) + payload
body = b'Asura   ' + chunk(b'FNFO', 1, 0, bytes(8)) + chunk(b'FCSR', 0, 2, data) + chunk(b'TXET', 3, 0, b'')
z = zlib.compress(body)
out = b'AsuraZlb' + struct.pack('<I', 1) + struct.pack('>II', len(z), len(body)) + z
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 't.wii'), 'wb').write(out)
