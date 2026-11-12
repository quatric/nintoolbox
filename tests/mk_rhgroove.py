#!/usr/bin/env python3
"""Synthetic Rhythm Heaven Groove fixtures and a Switch FLYT 9.0 layout.

OUTDIR/t.cwv    stereo DPCM, 4 frames: L bytes 1,1,3,255 -> 2,4,12,10; R 255,255,0,1 -> -2,-4,-4,-2;
                48000 Hz, loop start 1, length 2
OUTDIR/t.wmb    tempo map: 48000 Hz, one segment of 960 ticks over 48000 samples (120 bpm)
OUTDIR/t.tex    16x8 RGBA8 (format 99) block-linear surface, pixel (x, y) = (16x, 32y, 0, 255)
OUTDIR/t.spr    sprite set with two rectangles
OUTDIR/v9.bflyt FLYT 9.0, two textures, one material: colors + 2 texrefs + combiner mode 13 + flag 19 block
"""
import os, struct, sys


def cwv():
    left, right = [1, 1, 3, 255], [255, 255, 0, 1]
    data = bytes(b for pair in zip(left, right) for b in pair)
    footer = struct.pack('<6I2f', 3, 48000, 4, 2, 1, 2, 0.75, 1.0)
    return data + footer.ljust(0x100, b'\0')


def wmb():
    return struct.pack('<IIiIII', 0, 48000, -1, 0, 0, 0) + struct.pack('<II', 960, 48000) + struct.pack('<I', 0)


def gob_off(x, y):
    return ((x % 64) // 32) * 256 + ((y % 8) // 2) * 64 + ((x % 32) // 16) * 32 + (y % 2) * 16 + (x % 16)


def tex():
    surf = bytearray(512)
    for y in range(8):
        for x in range(16):
            o = gob_off(x * 4, y)
            surf[o:o + 4] = bytes((16 * x, 32 * y, 0, 255))
    trailer = struct.pack('<5H', 99, 4, 16, 8, 1).ljust(0x1000, b'\0')
    return bytes(surf) + trailer


def spr():
    counts = [0, 2] + [0] * 12
    rects = struct.pack('<8i', 1, 1, 6, 4, -40, 6, 37, 37)
    head = 0x38 + 8 * 27
    offs = [head] + [head + len(rects)] * 26
    return struct.pack('<14I', *counts) + struct.pack('<27Q', *offs) + rects


def section(magic, body):
    body += b'\0' * (-len(body) % 4)
    return magic + struct.pack('<I', 8 + len(body)) + body


def bflyt():
    lyt1 = section(b'lyt1', struct.pack('<B3x4f', 1, 1280, 720, 0, 0) + b't\0')
    names = [b'a.bflim\0', b'b.bflim\0']
    offs, blob = [], b''
    for n in names:
        offs.append(4 * len(names) + len(blob))
        blob += n
    txl1 = section(b'txl1', struct.pack('<HH', len(names), 0) + struct.pack('<%dI' % len(names), *offs) + blob)
    flags = 2 | (1 << 6) | (1 << 19)
    mat = b'M_Test'.ljust(28, b'\0') + struct.pack('<I', flags)
    mat += bytes((0, 2, 4, 8)) + bytes((255, 0, 0, 255)) + bytes((0, 0, 255, 255))
    mat += struct.pack('<HBB', 0, 4, 4) + struct.pack('<HBB', 1, 5, 5)  # texrefs
    mat += struct.pack('<BBH', 13, 0, 0)  # combiner, mode 13
    mat += bytes(range(28)) + bytes(range(100, 116))  # flag 19 block, one stage
    mat1 = section(b'mat1', struct.pack('<HHI', 1, 0, 16) + mat)
    secs = lyt1 + txl1 + mat1
    return b'FLYT' + struct.pack('<HHIIHH', 0xFEFF, 0x14, 0x09000000, 0x14 + len(secs), 3, 0) + secs


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    files = {'t.cwv': cwv(), 't.wmb': wmb(), 't.tex': tex(), 't.spr': spr(), 'v9.bflyt': bflyt()}
    for name, data in files.items():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)
        print(os.path.join(outdir, name))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
