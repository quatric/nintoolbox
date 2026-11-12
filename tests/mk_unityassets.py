#!/usr/bin/env python3
# Synthetic Unity 2.x Wii SerializedFile (v6, big-endian): one IA8 Texture2D (LZ11 stored as literals),
# one stereo DSP AudioClip and one TextAsset (see extract/unityassets.inc).
import struct, sys, os
out = sys.argv[1]
def name(s):
    b = s.encode(); return struct.pack('>I', len(b)) + b + b'\0' * (-len(b) % 4)
def lz11_literal(data):
    o = bytearray(b'\x11' + struct.pack('<I', len(data))[:3]); i = 0
    while i < len(data):
        o.append(0); o += data[i:i + 8]; i += 8
    return bytes(o)
tex_raw = bytes([0x80 + i for i in range(32)]) * 1  # 4x4 IA8 = one 32-byte tile
tex_raw = bytes(sum(([0x00, i * 16] for i in range(16)), []))
lz = lz11_literal(tex_raw)
tex = name('tx') + struct.pack('>9I', 0, 1, 2, 4, 4, 32, 0x100000, 32, len(lz)) + lz
def dsp(rate, data):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, 14, len(data) * 2, rate)
    struct.pack_into('>III', h, 0x10, 2, len(data) * 2 - 1, 2)
    return bytes(h) + data
c0, c1 = dsp(32000, b'\x01' * 8), dsp(32000, b'\x02' * 8)
chunk = len(c0)
body = struct.pack('>fIIII', 1.0, 2, 0, 2, 2)[:0]
hdr = struct.pack('>f5I', 1.0, 2, 0, 2, 2, chunk) + b'\0' * 8   # 32 bytes before first dsp header
snd = name('sn') + hdr + c0
snd += b'\0' * (-(len(snd)) % 32 or 0) + c1
txt = name('tt') + struct.pack('>I', 5) + b'hello' + b'\0' * 3
objs = [(1, 28, tex), (2, 83, snd), (3, 49, txt)]
data = b''; table = b''
for pid, cls, b in objs:
    table += struct.pack('>iIIiHH', pid, 16 + len(data), len(b), cls, cls, 0)
    data += b + b'\0' * (-len(b) % 4)
meta = b'\x01' + struct.pack('>ii', 0, len(objs)) + table + struct.pack('>i', 0)
fsz = 16 + len(data) + len(meta)
f = struct.pack('>IIII', len(meta), fsz, 6, 0) + data + meta
open(os.path.join(out, 'sharedassets9.assets'), 'wb').write(f)

# Later layout: type trees present, Texture2D pixels in the scene's .ures stream. Bundle 3 << 16 = scene 3 = "level2",
# stream 0 -> level2-0.ures; an 8x4 I8 texture (format 0x0e, one GX tile) at offset 0x20.
def node(t, n, size, children=(), arr=0):
    b = t.encode() + b'\0' + n.encode() + b'\0' + struct.pack('>6i', size, 0, arr, 1, 0, len(children))
    return b + b''.join(children)
tree = struct.pack('>i', 28) + node('Texture2D', 'Base', -1, [node('int', 'm_Width', 4), node('int', 'm_Height', 4)])
i8 = bytes(range(0, 256, 8))
tex2 = name('tx2') + struct.pack('>4iB3x2i', 8, 4, 32, 0x0e, 0, 1, 2) + struct.pack('>iifi', 0, 0, 0.0, 0) \
    + struct.pack('>IIi', 0x20, 3 << 16, 0)
txt2 = name('tt2') + struct.pack('>I', 3) + b'abc' + b'\0'
objs = [(1, 28, tex2), (2, 49, txt2)]
data = b''; table = b''
for pid, cls, b in objs:
    table += struct.pack('>iIIiHH', pid, 16 + len(data), len(b), cls, cls, 0)
    data += b + b'\0' * (-len(b) % 4)
meta = b'\x01' + struct.pack('>i', 1) + tree + struct.pack('>i', len(objs)) + table + struct.pack('>i', 0)
fsz = 16 + len(data) + len(meta)
open(os.path.join(out, 'sharedassets3.assets'), 'wb').write(struct.pack('>IIII', len(meta), fsz, 6, 0) + data + meta)
open(os.path.join(out, 'level2-0.ures'), 'wb').write(b'\xff' * 0x20 + i8)
