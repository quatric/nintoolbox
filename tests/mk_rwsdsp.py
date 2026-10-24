# Synthetic RenderWare audio stream (GameCube ADPCM, Spyro: Dawn of the Dragon layout) with two mono segments as t.rws.
import struct, sys, os
d = sys.argv[1]
hdr_size = 0x340
f = bytearray(0x18 + hdr_size)
gi = 0x318                                               # file offset of "GCN ADPCM Header"
coef = gi - 50
smp = coef - 24 - 4                                      # nibble count sits 24 bytes in front of the coefficients
struct.pack_into('<III', f, 0, 0x80d, 0, 0x1c020065)
struct.pack_into('<III', f, 12, 0x80e, hdr_size, 0x1c020065)
struct.pack_into('>I', f, 0x18 + 0x20, 2)                # segment count
for i, (size, off) in enumerate(((0x40, 0), (0x20, 0x40))):
    struct.pack_into('>II', f, 0x18 + 0x60 + 0x20 * i + 0x18, size, off)
struct.pack_into('>I', f, smp - 52, 0x800)               # interleave
struct.pack_into('>I', f, smp - 44, 44000)               # sample rate
f[smp - 31] = 1                                          # channels
struct.pack_into('>II', f, smp, 100, 120)                # samples, nibbles
struct.pack_into('>16h', f, coef, *range(16))            # coefficients
f[gi:gi + 16] = b'GCN ADPCM Header'
data = bytes([0x01]) * 0x40 + bytes([0x02]) * 0x20
f += struct.pack('<III', 0x80f, len(data), 0x1c020065) + data
struct.pack_into('<I', f, 4, len(f) - 12)
open(os.path.join(d, 't.rws'), 'wb').write(bytes(f))
