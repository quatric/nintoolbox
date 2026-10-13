#!/usr/bin/env python3
"""Synthetic Trauma Team files: t.tpx (one 8x4 I8 TPL record "tex1") and t.bf (FLW0 flow script with one proc
label and a MSG2 block holding the message "msg0" with the Shift-JIS string "Hello"). usage: mk_trauma.py OUTDIR"""
import os, struct, sys

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
w, h = 8, 4
data_off = 0x40
tpl = struct.pack('>III', 0x0020af30, 1, 0x0c) + struct.pack('>II', 0x14, 0)
tpl += struct.pack('>HHIIIIIIfBBBB', h, w, 1, data_off, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
tpl = tpl.ljust(data_off, b'\0') + bytes(w * h)
rec = struct.pack('>III', 0, 0x40, len(tpl)) + b'tex1'.ljust(0x14, b'\0')
tpx = struct.pack('>IIII', 0x20060912, 2, 0x40, 1).ljust(0x20, b'\0') + rec + tpl
open(os.path.join(d, 't.tpx'), 'wb').write(tpx)

text = struct.pack('>II', 1, 8) + b'Hello\0\xff\0\0'
mrec = b'msg0'.ljust(0x20, b'\0') + struct.pack('>IIII', 0, len(text), 0x70, 0xffff0000) + bytes(0x10)
msg = (b'\0\0\0\0MSG2' + struct.pack('>II', 0x10000, 0)).ljust(0x24, b'\0') + struct.pack('>III', 0x30, 1, 0x30)
msg = msg.ljust(0x30, b'\0') + mrec + text
msg = msg[:12] + struct.pack('>I', len(msg)) + msg[16:]
lab = b'start'.ljust(0x30, b'\0') + struct.pack('>II', 0, 0)
secs = [(0, 0x38, 1, 0x20 + 16 * 4), (1, 0x38, 0, 0x20 + 16 * 4 + 0x38), (2, 4, 0, 0), (3, 1, len(msg), 0)]
base = 0x20 + 16 * 4 + 0x38
secs[2] = (2, 4, 0, base)
secs[3] = (3, 1, len(msg), base)
body = b''.join(struct.pack('>4I', *s) for s in secs)
bf = struct.pack('>HHI', 0, 0, 0) + b'FLW0' + struct.pack('>III', 0, 4, 0x10000)
bf = bf.ljust(0x20, b'\0') + body + lab + msg
bf = bf[:4] + struct.pack('>I', len(bf)) + bf[8:]
open(os.path.join(d, 't.bf'), 'wb').write(bf)
