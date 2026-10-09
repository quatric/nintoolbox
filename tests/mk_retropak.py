#!/usr/bin/env python3
# Synthetic Retro PAK v2 (Metroid Prime 3): one LZO-compressed TXTR (8x8 I8) and one stored CSMP, as t.pak.
import struct, sys, os
d = sys.argv[1]
al = lambda x: (x + 0x3f) & ~0x3f
# TXTR: raw 12-byte header block + LZO block (literal run only) holding 64 pixel bytes
hdr = struct.pack('>IHHI', 1, 8, 8, 1)
pix = bytes(range(64))
lzo = bytes([17 + 64]) + pix + bytes([0x11, 0, 0])
chunk = struct.pack('>H', len(lzo)) + lzo
cmpd = b'CMPD' + struct.pack('>I', 2) + struct.pack('>II', 12, 12) + struct.pack('>II', 0xc0000000 | len(chunk), 64) + hdr + chunk
# CSMP: INFO + PAD + DATA with a 0x60 DSP-like header and one frame
dh = bytearray(0x60)
struct.pack_into('>I', dh, 0, 14)
struct.pack_into('>I', dh, 8, 32000)
csmp = b'CSMP' + struct.pack('>I', 1) + b'INFO' + struct.pack('>I', 12) + bytes(12) + b'DATA' + struct.pack('>I', 0x68) + bytes(dh) + bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77])
tid, cid = bytes.fromhex('0123456789abcdef'), bytes.fromhex('fedcba9876543210')
strg = struct.pack('>I', 1) + b'TXTR_Test\0TXTR' + tid
rshd = struct.pack('>I', 2) + struct.pack('>I4s8sII', 1, b'TXTR', tid, len(cmpd), 0) + struct.pack('>I4s8sII', 0, b'CSMP', cid, len(csmp), len(cmpd))
data = cmpd + csmp
f = bytearray(struct.pack('>II', 2, 0x40) + bytes(0x38))
f += struct.pack('>I', 3)
for tag, s in ((b'STRG', len(strg)), (b'RSHD', len(rshd)), (b'DATA', len(data))):
    f += tag + struct.pack('>I', s)
for b in (strg, rshd, data):
    f += bytes(al(len(f)) - len(f)) + b
open(os.path.join(d, 't.pak'), 'wb').write(bytes(f))
# STRG v3: one language (ENGL), one string "Hi" named "Greet"
nm = b'Greet\0'
nts = 8 + len(nm)
s = struct.pack('>IIIIII', 0x87654321, 3, 1, 1, 1, nts) + struct.pack('>II', 8, 0) + nm + b'ENGL'
s += struct.pack('>II', 7, 0) + struct.pack('>I', 3) + b'Hi\0'
open(os.path.join(d, 't.strg'), 'wb').write(s)
