"""Write minimal Mario Kart World terrain tiles (TARB) and a RESTBL size table.

    mk_mkw_tera.py OUTDIR

OUTDIR/t.slow.extm  4x4 R8, delta packed data block; pixel (x, y) = 10*y + 3*x
OUTDIR/t.mate       4x4 RGBA8, only a GPU block (Orin GOB, block height 1); pixel = (16x, 16y, 100, 0)
OUTDIR/t.rsizetable RESTBL v1: crc32("a/b.txt") = 111, crc32("c.bin") = 222, name "x/y.dat" = 333
"""
import os, struct, sys, zlib


def tarb(info, blocks):
    # info: 0x34 bytes with the three block fields at 0x1C/0x24/0x2C filled in here.
    payload = bytearray(info) + b''.join(b for b in blocks if b)
    at = 0x34
    for field, b in zip((0x1C, 0x24, 0x2C), blocks):
        if b:
            struct.pack_into('<iI', payload, field, at - field, len(b))
            at += len(b)
    head = b'TARB' + struct.pack('<IHHII', 1, 1, 0x14, len(payload), 0)
    return head + bytes(payload)


def info(grid, margin, fmt, flags, gfmt):
    b = bytearray(0x34)
    b[0], b[1], b[2], b[3] = grid >> 8, grid & 0xFF, margin, 0
    b[4:8] = bytes(fmt)
    b[8] = flags
    b[0x14:0x18] = bytes(gfmt)
    b[0x18], b[0x19] = 1, 1
    return b


def pack_delta8(px, grid):
    plane = grid * grid
    out = bytearray(plane)
    half = plane // 2
    for y in range(grid):
        prev = 0
        for x in range(0, grid, 2):
            i = y * grid + x
            v0, v1 = px[i], px[i + 1]
            even = (v0 - prev + 8) & 0xFF
            odd = (v1 - v0 + 8) & 0xFF
            prev = v1
            out[i >> 1] = ((even & 0x0F) << 4) | (odd & 0x0F)
            out[half + (i >> 1)] = (even & 0xF0) | (odd >> 4)
    return bytes(out)


def gob_orin(xb, y):
    return ((xb % 64) // 32) * 256 + ((y % 8) // 4) * 128 + ((xb % 32) // 16) * 64 + (y % 4) * 16 + (xb % 16)


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    g = 4
    px = [10 * y + 3 * x for y in range(g) for x in range(g)]
    data = pack_delta8(px, g)
    with open(os.path.join(outdir, 't.slow.extm'), 'wb') as f:
        f.write(tarb(info(g, 1, [6, 0, 0, 0], 0x81, [6, 0, 0, 0]), [data, b'', b'']))

    gpu = bytearray(512)
    for y in range(g):
        for x in range(g):
            o = gob_orin(4 * x, y)
            gpu[o:o + 4] = bytes((16 * x, 16 * y, 100, 0))
    with open(os.path.join(outdir, 't.mate'), 'wb') as f:
        f.write(tarb(info(g, 1, [6, 6, 6, 6], 0x01, [6, 6, 6, 6]), [b'', b'', bytes(gpu)]))

    crcs = sorted(((zlib.crc32(b'a/b.txt'), 111), (zlib.crc32(b'c.bin'), 222)))
    rt = b'RESTBL' + struct.pack('<IIII', 1, 0x20, len(crcs), 1)
    for c, s in crcs:
        rt += struct.pack('<II', c, s)
    rt += b'x/y.dat'.ljust(0x20, b'\0') + struct.pack('<I', 333)
    with open(os.path.join(outdir, 't.rsizetable'), 'wb') as f:
        f.write(rt)


if __name__ == '__main__':
    main(sys.argv[1])
