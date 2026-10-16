#!/usr/bin/env python3
# Synthetic JAudio archive t.baa with one WSYS (id 0) holding one wave group "t.aw" with two waves, plus Waves/t.aw:
# wave 0 is AFC 4 bit (2 frames, 32 samples), wave 1 is PCM16 big-endian (4 samples). Writes the expected PCM of wave 0
# from an independent AFC decoder to t.expect (little-endian s16).
import struct, sys, os
d = sys.argv[1]
COEF = [(0, 0), (2048, 0), (0, 2048), (1024, 1024), (4096, -2048), (3584, -1536), (3072, -1024), (4608, -2560),
        (4200, -2248), (4800, -2300), (5120, -3072), (2048, -2048), (1024, -1024), (-1024, 1024), (-1024, 0), (-2048, 0)]
frames = [bytes([0x13]) + bytes([0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0]),
          bytes([0x32]) + bytes([0x0f, 0xed, 0xcb, 0xa9, 0x87, 0x65, 0x43, 0x21])]
afc = b''.join(frames)
h1 = h2 = 0
exp = []
for fr in frames:
    scale = 1 << (fr[0] >> 4)
    c1, c2 = COEF[fr[0] & 15]
    for i in range(16):
        code = (fr[1 + i // 2] >> (0 if i & 1 else 4)) & 15
        if code >= 8:
            code -= 16
        v = ((code * scale) << 11) + c1 * h1 + c2 * h2 >> 11
        v = max(-32768, min(32767, v))
        exp.append(v)
        h2, h1 = h1, v
open(os.path.join(d, 't.expect'), 'wb').write(b''.join(struct.pack('<h', v) for v in exp))
pcm = struct.pack('>4h', 100, -200, 300, -400)
aw = afc + bytes(0x20 - len(afc) % 0x20 if len(afc) % 0x20 else 0)
pcm_off = len(aw)
aw += pcm
os.makedirs(os.path.join(d, 'Waves'), exist_ok=True)
open(os.path.join(d, 'Waves', 't.aw'), 'wb').write(aw)
# WSYS layout: 0x20 header, wave records, WINF, WBCT (empty)
w0 = struct.pack('>BBBBf', 0xcc, 0, 0x3c, 0, 8000.0) + struct.pack('>IIIIIII', 0, len(afc), 0, 0, 32, 32, 0) + bytes(8)
w1 = struct.pack('>BBBBf', 0xcc, 3, 0x3c, 0, 11025.0) + struct.pack('>IIIIIII', pcm_off, 8, 0, 0, 4, 4, 0) + bytes(8)
assert len(w0) == 0x2c and len(w1) == 0x2c
body = bytearray(0x20) + w0 + w1
winf_off = len(body)
group = b't.aw'.ljust(0x70, b'\0') + struct.pack('>III', 2, 0x20, 0x4c)
body += b'WINF' + struct.pack('>II', 1, winf_off + 12) + group
body[0:4] = b'WSYS'
struct.pack_into('>IIIII', body, 4, len(body), 0, 0x20, winf_off, len(body))
body += b'WBCT' + bytes(8)
hdr = b'AA_<' + b'ws  ' + struct.pack('>III', 0, 0x28, 0) + b'>_AA' + bytes(0x10)
hdr = hdr.ljust(0x28, b'\0')
open(os.path.join(d, 't.baa'), 'wb').write(hdr + bytes(body))
