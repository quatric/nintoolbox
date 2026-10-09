#!/usr/bin/env python3
# Synthetic Rebellion Asura Wii archive ("wii\0" ARC) with a DSP sound and a raw type-18 member as t.arc.
import struct, sys, os
d = sys.argv[1]
frames = 4
dsp = bytearray(0x60)
struct.pack_into('>III', dsp, 0, frames * 14, frames * 16, 22050)
struct.pack_into('>III', dsp, 0x10, 2, frames * 16 - 1, 2)
dsp = bytes(dsp) + bytes([0x42]) * (frames * 8)
anim = b'ANIMDATA'
def ent(name, typ, size, off, idx):
    n = name.encode().ljust(0x40, b'\0')
    return n + struct.pack('>7I', typ, size, off, idx, 0, 0x20, 0)
hdr_len = 0xd4 + 2 * 0x5c
off1 = (hdr_len + 0x1f) & ~0x1f
off2 = (off1 + len(dsp) + 0x1f) & ~0x1f
body = bytearray(off2 + len(anim))
body[0:4] = b'wii\0'
struct.pack_into('>II', body, 4, len(body), 2)
body[0x14:0xd4] = (b'asurasnd.arc'.ljust(0x40, b'\0')) * 3
body[0xd4:0xd4 + 0x5c] = ent('sample', 0x15, len(dsp), off1, 1)
body[0xd4 + 0x5c:0xd4 + 0xb8] = ent('sample', 0x12, len(anim), off2, 2)
body[off1:off1 + len(dsp)] = dsp
body[off2:off2 + len(anim)] = anim
open(os.path.join(d, 't.arc'), 'wb').write(bytes(body))
