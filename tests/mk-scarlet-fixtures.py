#!/usr/bin/env python3
"""Write compact, public-domain fixtures for Scarlet's Nintendo 3DS image wrappers."""

import argparse
import os
import struct


W = H = 8


def pica_rgba(r, g, b, a=255):
    # An 8x8 texture is one complete Morton tile.  A solid colour makes the
    # wrapper fixture concise while still exercising every byte of the tile.
    return bytes((r, g, b, a)) * (W * H)


def btga():
    header = bytearray(0x38)
    struct.pack_into('<IIIHHIHHI', header, 0, 1, 0x20, 0x20, W, H, 0, 0, 0, 1)
    return bytes(header) + pica_rgba(0xE0, 0x20, 0x10)


def dmpbm():
    return b'DMPBM' + bytes((3,)) + struct.pack('<II', W, H) + pica_rgba(0x10, 0xC0, 0x30)


def stex():
    header = bytearray(0x20)
    header[:4] = b'STEX'
    struct.pack_into('<IIIII', header, 0x0C, W, H, 0x1401, 0x6752, W * H * 4)
    # A non-0x80 pointer deliberately exercises the compact-header layout.
    return bytes(header) + pica_rgba(0x20, 0x40, 0xE0)


def cmb():
    tex_chunk = 0x50
    tex_data = tex_chunk + 0x30
    pixels = pica_rgba(0xD0, 0x90, 0x20)
    out = bytearray(tex_data + len(pixels))
    struct.pack_into('<4sIII16sI', out, 0, b'cmb ', len(out), 6, 0, b'synthetic\0', 0)
    # Revision 6: six chunk offsets followed by two raw-data offsets.
    struct.pack_into('<I', out, 0x24 + 2 * 4, tex_chunk)
    struct.pack_into('<I', out, 0x24 + 6 * 4 + 4, tex_data)
    struct.pack_into('<4sII', out, tex_chunk, b'tex ', 36, 1)
    struct.pack_into('<IHHHHHHI16s', out, tex_chunk + 12,
                     len(pixels), 0, 0, W, H, 0x6752, 0x1401, 0, b'synthetic\0')
    out[tex_data:] = pixels
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', required=True)
    args = ap.parse_args()
    os.makedirs(args.output, exist_ok=True)
    fixtures = {'scarlet_3ds.btga': btga(), 'scarlet_3ds.dmpbm': dmpbm(),
                'scarlet_3ds.stex': stex(), 'scarlet_3ds.cmb': cmb()}
    for name, data in fixtures.items():
        with open(os.path.join(args.output, name), 'wb') as fp:
            fp.write(data)


if __name__ == '__main__':
    main()
