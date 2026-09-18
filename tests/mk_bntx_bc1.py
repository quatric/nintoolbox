#!/usr/bin/env python3
# Stdlib-only synthetic BC1 BNTX fixture for regress.sh (t_bntx_native).
#
# Reworks a wimgt-encoded 8x8 RGBA8 base container into a BC1_UNORM (0x1a01)
# texture: the format word is patched and the payload is replaced with
# GOB-swizzled BC1 blocks using the same equations mk_xtx.py cross-checks
# BntxDeswizzle() against. Sizes coincide (RGBA8 8x8 block-linear surface
# and BC1 8x8 block-linear surface are both 512 bytes), so the patch is
# in place and all container offsets stay valid.
# Layout (2x2 blocks of 4x4): TL red, TR green, BL blue, BR white.
# Usage: mk_bntx_bc1.py base.bntx(8x8 RGBA8) out.bntx
import struct
import sys


def swizzle(w, h, bpp, bh_log2, elems):
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
        return gob + ((xb % 64) // 32) * 256 + ((y % 8) // 2) * 64 \
            + ((xb % 32) // 16) * 32 + (y % 2) * 16 + (xb % 16)

    for y in range(h):
        for x in range(w):
            p = addr(x, y)
            out[p:p + bpp] = elems[y * w + x]
    return bytes(out)


def bc1_solid(rgb565):
    # c0 = colour, c1 = contrasting endpoint, all indices 0.
    # c0 > c1 selects 4-colour mode, so index 0 is exactly c0.
    c1 = 0x001F if rgb565 != 0x001F else 0xF800
    if rgb565 == 0xFFFF:
        c1 = 0x0000
    return struct.pack('<HHI', rgb565, c1, 0x00000000)


def u32(d, o):
    return struct.unpack('<I', d[o:o + 4])[0]


def u64(d, o):
    return struct.unpack('<Q', d[o:o + 8])[0]


def main():
    base_p, out_p = sys.argv[1:3]
    d = bytearray(open(base_p, 'rb').read())
    tc = 0x20
    assert u32(d, tc + 4) == 1, 'base must hold exactly 1 texture'
    blk = u64(d, u64(d, tc + 8))
    assert d[blk:blk + 4] == b'BRTI'
    ti = blk + 16
    assert u32(d, ti + 0x0c) == 0x0b01, 'base must be RGBA8_UNORM'
    w, h = u32(d, ti + 0x14), u32(d, ti + 0x18)
    assert (w, h) == (8, 8), 'base must be 8x8'
    assert u32(d, ti + 0x40) == 512, 'unexpected base image size'

    struct.pack_into('<I', d, ti + 0x0c, 0x1a01)  # BC1_UNORM

    blocks = [bc1_solid(0xF800), bc1_solid(0x07E0),
              bc1_solid(0x001F), bc1_solid(0xFFFF)]
    payload = swizzle(2, 2, 8, 0, blocks)
    assert len(payload) == 512

    ptrs = u64(d, ti + 0x60)
    data_addr = u64(d, ptrs)
    d[data_addr:data_addr + 512] = payload
    open(out_p, 'wb').write(bytes(d))
    print('wrote %s (%d bytes, BC1 8x8)' % (out_p, len(d)))


main()
