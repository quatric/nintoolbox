#!/usr/bin/env python3
# Stdlib-only synthetic XTX fixture for regress.sh (t_xtx).
#
# Builds a minimal DFvN/HBvN container with two textures, both Tegra
# block-linear swizzled with the exact GOB equations lib-bntx.c's
# BntxDeswizzle() inverts (addr_block_linear), so the test is an
# independent cross-check of the parse/map/deswizzle/decode path, not a
# round trip through our own encoder (XTX has no encoder here):
#   tex 0: NVN RGBA8 (0x25), 16x16, 4px red/green checkerboard
#   tex 1: NVN DXT1  (0x42), 16x16, solid-red BC1 blocks
# Usage: mk_xtx.py OUT.xtx
#        mk_xtx.py --switch2 OUT.xtx
#   Switch 2 (NVN2) layout as in Mario Kart World's sead nvn2_font textures: one RGBA8 32x16 texture with
#   pixel (x, y) = (8x, 16y, 0, 255), Orin GOB layout, TextureLayout2 = 0xC9, and block headers whose size
#   field is only the 0x24-byte header (the next block follows the payload).
import struct
import sys


def swizzle(w, h, bpp, bh_log2, elems, orin=False):
    bh = 1 << bh_log2
    pitch = ((w * bpp + 63) // 64) * 64
    surf_h = ((h + bh * 8 - 1) // (bh * 8)) * (bh * 8)
    out = bytearray(pitch * surf_h)

    def addr(x, y):
        w_gobs = (w * bpp + 63) // 64
        gob = (y // (8 * bh)) * 512 * bh * w_gobs \
            + (x * bpp // 64) * 512 * bh \
            + ((y % (8 * bh)) // 8) * 512
        xb = x * bpp
        if orin:
            return gob + ((xb % 64) // 32) * 256 + ((y % 8) // 4) * 128 \
                + ((xb % 32) // 16) * 64 + (y % 4) * 16 + (xb % 16)
        return gob + ((xb % 64) // 32) * 256 + ((y % 8) // 2) * 64 \
            + ((xb % 32) // 16) * 32 + (y % 2) * 16 + (xb % 16)

    for y in range(h):
        for x in range(w):
            p = addr(x, y)
            out[p:p + bpp] = elems[y * w + x]
    return bytes(out)


def texinfo(w, h, nvn_fmt, data_len, layout2=0):
    mip_offsets = [0] * 17
    return struct.pack('<QIIIIIIII17IIII',
                       data_len, 512, w, h, 1, 1, nvn_fmt, 1,
                       data_len, *mip_offsets, 0, layout2, 0)


def retail_block(block_type, index, payload):
    # Size field = header size only; DataOffset 0x24, payload follows directly.
    return b'HBvN' + struct.pack('<IQqIII', 36, len(payload), 36, block_type, index, 0) + payload


def switch2(out_path):
    elems = [bytes((8 * x, 16 * y, 0, 255)) for y in range(16) for x in range(32)]
    data = swizzle(32, 16, 4, 0, elems, orin=True)
    blob = b'DFvN' + struct.pack('<III', 16, 1, 1)
    blob += retail_block(2, 0, texinfo(32, 16, 0x25, len(data), 0xc9))
    blob += retail_block(3, 1, data)
    blob += retail_block(5, 2, bytes(24))
    open(out_path, 'wb').write(blob)
    print('wrote %s (%d bytes, Switch 2 layout)' % (out_path, len(blob)))


def block(block_type, payload):
    # BlockSize strides to the next header: 36-byte header + payload.
    # (Retail files may pad data to 512; DataOffset still points at it and
    # the reader seeks absolutely, so unpadded stride is fine here.)
    hdr = b'HBvN' + struct.pack('<IQqIII', 36 + len(payload), len(payload),
                                36, block_type, 0, 0)
    return hdr + payload


def main():
    if sys.argv[1] == '--switch2':
        return switch2(sys.argv[2])
    out_path = sys.argv[1]
    # tex 0: RGBA8 checkerboard, 16x16 elements of 4 bytes
    elems = []
    for y in range(16):
        for x in range(16):
            red = ((x // 4) + (y // 4)) % 2 == 0
            elems.append(bytes((255, 0, 0, 255)) if red
                         else bytes((0, 255, 0, 255)))
    data0 = swizzle(16, 16, 4, 0, elems)
    # tex 1: DXT1 solid red, 4x4 blocks of 8 bytes
    # (c0=RGB565 red, c1=RGB565 blue, all indices 0; c0>c1 selects 4-colour mode)
    blk = struct.pack('<HHI', 0xF800, 0x001F, 0x00000000)
    elems1 = [blk] * 16
    data1 = swizzle(4, 4, 8, 0, elems1)
    blob = b'DFvN' + struct.pack('<III', 16, 1, 0)
    blob += block(2, texinfo(16, 16, 0x25, len(data0)))
    blob += block(3, data0)
    blob += block(2, texinfo(16, 16, 0x42, len(data1)))
    blob += block(3, data1)
    open(out_path, 'wb').write(blob)
    print('wrote %s (%d bytes, 2 textures)' % (out_path, len(blob)))


main()
