"""Write a minimal Hudson ATB (Animation Texture Bank) with one RGB565 texture.

Header is 20 bytes: num_banks, num_patterns, num_textures, num_references
(all u16), then bank_off, pattern_off, texture_off (all u32, big-endian).
A texture entry is 20 bytes: bpp, format (u8 each), palette_size, width,
height (u16 each), image_size (u32), palette_off, image_off (u32 each).

format=2 is Calling's "no palette, 16bpp" tag and must decode as RGB565
(bpp=16), not RGB5A3 (also 16bpp but with a very different bit layout):
every pixel here is raw halfword 0x0001, whose top bit is 0. Read as
RGB5A3 that selects the translucent 4/4/4/3 encoding with a 0 alpha
nibble, i.e. a fully transparent pixel; read correctly as RGB565 (no
alpha channel at all) it must come out fully opaque.

    mk_atb.py OUT.atb
"""
import struct, sys

def main(out):
    HEADER = 20
    TEX_ENTRY = 20
    tex_off = HEADER
    img_off = tex_off + TEX_ENTRY
    w = h = 4
    img_size = w * h * 2

    buf = bytearray(img_off + img_size)
    struct.pack_into(">HHHH", buf, 0, 0, 0, 1, 0)          # banks, patterns, textures, references
    struct.pack_into(">III", buf, 8, 0, 0, tex_off)        # bank_off, pattern_off, texture_off

    struct.pack_into(">BBHHHIII", buf, tex_off,
        16, 2,          # bpp, format
        0,              # palette_size
        w, h,
        img_size,
        0,              # palette_off (unused)
        img_off)

    for i in range(w * h):
        struct.pack_into(">H", buf, img_off + i * 2, 0x0001)

    with open(out, "wb") as f:
        f.write(buf)

if __name__ == "__main__":
    main(sys.argv[1])
