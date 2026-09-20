#!/usr/bin/env python3
"""Synthetic BombShell engine data packs (see project/src/lib-bombshell.h): a
Wii .xwi (big-endian, CMPR colour + alpha texture, FSB3 sound) and a PC .xdx9
(little-endian, DXT1 texture, RIFF sound), one directory each.
usage: mk_bombshell.py OUTDIR -> OUTDIR/T.xwi, OUTDIR/T.xdx9"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def build(be, sound, pix, alpha, fmt, log2):
    e = '>' if be else '<'
    hdr = 192 if be else 188
    al = (lambda v: (v + 31) & ~31) if be else (lambda v: v)
    dp = 16 + 24
    name = b'Content\\Sounds\\beep.wav\0'
    sbytes = 0x20 + len(sound) + len(name)
    e1 = dp + hdr + al(8)
    E = al(e1 + sbytes)
    size = E + 0xa0 + len(alpha)
    f = bytearray(size)
    struct.pack_into(e + '4I', f, 0, 0x020100a0, 0x040100af, 0x138, 1)
    struct.pack_into(e + '6I', f, 16, 0x138, 4, 0, 0, size - dp, 0)
    struct.pack_into(e + 'I', f, dp + 4, sbytes)
    struct.pack_into(e + 'I', f, dp + 8, 2)
    struct.pack_into(e + 'I', f, dp + 20, 1)
    struct.pack_into(e + 'I', f, dp + 32, 1)
    struct.pack_into(e + '2I', f, dp + hdr, 0, 0)                  # sound table: record at e1 + 0
    struct.pack_into(e + '4I', f, e1, 0x20, len(sound), 22050, 1)  # {offset, size, rate, flags}
    f[e1 + 0x20:e1 + 0x20 + len(sound)] = sound
    f[e1 + 0x20 + len(sound):e1 + sbytes] = name
    struct.pack_into(e + 'I', f, E, 0x20)                          # texture list
    r = E + 0x20
    f[r + 4:r + 8] = bytes([fmt, 0, log2, log2])
    struct.pack_into(e + '4I', f, r + 8, 0x60, 0, 0x40, 0x80 if alpha else 0)
    f[E + 0x40:E + 0x48] = b'tex_a\0\0\0'
    f[E + 0x60:E + 0x60 + len(pix)] = pix
    f[E + 0x80:E + 0x80 + len(alpha)] = alpha
    return f


def cmpr_tile(c0, c1):
    return b''.join(struct.pack('>HH', c0, c1) + bytes(4) for _ in range(4))


wii = build(True, b'FSB3' + bytes(60), cmpr_tile(0xF800, 0x001F), cmpr_tile(0x8410, 0x8410), 0xc5, 3)
open(os.path.join(out, 'T.xwi'), 'wb').write(wii)
pc = build(False, b'RIFF' + bytes(40), struct.pack('<HH', 0xF800, 0x001F) + bytes(4), b'', 0x45, 2)
open(os.path.join(out, 'T.xdx9'), 'wb').write(pc)
