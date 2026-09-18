"""Synthetic Metroid Prime: Federation Force fixtures.

Builds retail-layout .dict/.data pairs plus standalone FEDM/FEDS/FEDT
containers exercising the lib-fedforce parsers. All geometry uses the
"windowmaterial" (PositionNormalsUV1, stride 20) preset; the texture is an
8x8 RGBA8 PICA-tiled image. See lib-fedforce.h for format notes.

Usage: python3 tests/mk-fedforce-fixtures.py [outdir]
Defaults to tests/fixtures/.
"""
import os
import struct
import sys


def fed_hash(name, case_sensitive=False):
    h = 0xFFFFFFFF
    for c in name.encode():
        if case_sensitive and 65 <= c <= 90:
            c |= 0x20
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def morton8(x, y):
    return ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
            | ((x & 4) << 2) | ((y & 4) << 3))


def pica_tile_rgba8(w, h, px):
    tw, th = (w + 7) & ~7, (h + 7) & ~7
    out = bytearray(tw * th * 4)
    for y in range(h):
        for x in range(w):
            pos = ((y // 8) * (tw // 8) + (x // 8)) * 64 + morton8(x & 7, y & 7)
            r, g, b, a = px[y * w + x]
            out[pos * 4:pos * 4 + 4] = bytes((r, g, b, a))
    return bytes(out)


def fed_container(magic, parts):
    n = len(parts)
    hdr = magic + struct.pack("<HH", 1, n)
    th = b"".join(struct.pack("<HHI", t, 0, len(d)) for t, d in parts)
    return hdr + th + b"".join(d for _, d in parts)


# --- shared blobs -----------------------------------------------------------

MAT_HASH = fed_hash("windowmaterial")
MODEL_HASH = 0x12345678
PATH_MODEL = 0x11111111
PATH_TEX = 0x22222222

# B003: model header + one 40-byte mesh header (triangle).
MESH = struct.pack("<IIHHIIIII I HBB",
                   0, 0xAABBCCDD, 60, 3, 0, 0, MAT_HASH, 0, 0, 0, 3, 16, 40)
B003 = struct.pack("<IHHI", MODEL_HASH, 1, 52, 0) + MESH

# B004: vertex buffer start.
B004 = struct.pack("<I", 0)


def vpos(x, y, z, nx, ny, nz, u, v):
    return (struct.pack("<fff", x, y, z)
            + bytes((nx & 0xFF, ny & 0xFF, nz & 0xFF, 0))
            + struct.pack("<hh", u, v))


VERTS = (vpos(0.0, 0.0, 0.0, 0, 0, 127, 0, 0)
         + vpos(1.0, 0.0, 0.0, 0, 0, 127, 1024, 0)
         + vpos(0.0, 1.0, 0.0, 0, 0, 127, 0, 1024))
B005 = VERTS + struct.pack("<HHH", 0, 1, 2)

B008 = struct.pack("<f", 2.0)
B009 = struct.pack("<ffffff", -1.0, -1.0, -1.0, 2.0, 2.0, 2.0)
B001 = struct.pack("<16f", *[1.0 if i % 5 == 0 else 0.0 for i in range(16)])

# Skeleton: one bone, identity rotation, origin translation.
S101 = struct.pack("<7I", 1, 0, 0, 0, 0, 0, 0)
S102 = struct.pack("<Ih h HBB", 0xAAAAAAAA, -1, 0, 0, 0, 0)
S103 = struct.pack("<7f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0)

# Texture: B501 header (48 bytes) + tiled 8x8 RGBA8 pixels.
TEXELS = [(255, (i * 32) % 256, (i * 16) % 256, 255) for i in range(64)]
PIXELS = pica_tile_rgba8(8, 8, TEXELS)
B501 = (struct.pack("<IIII", 256, PATH_TEX, 0, 0x88888)
        + struct.pack("<HHHBB", 8, 8, 0, 1, 0x11)
        + struct.pack("<5I", 0, 0, 0, 0, 0)
        + bytes((0, 0, 0, 0)))

# NLOC version2 LE: 2 messages ("Hi", "Bye").
def nloc_v2():
    msgs = [(0x12345678, "Hi"), (0x9ABCDEF0, "Bye")]
    body = b""
    entries = b""
    off = 4  # u16-unit offset past the 8-byte entry area quirk (see lib)
    for mid, txt in msgs:
        u = txt.encode("utf-16-le") + b"\x00\x00"
        entries += struct.pack("<II", mid, off)
        body += u
        off += len(u) // 2
    hdr = struct.pack("<IIIII", 9, 1, 9, len(msgs), 0)
    return hdr + entries + body


NLOC = nloc_v2()

FONT = (b"NLG Font Description File\n"
        b"Version 1.1\n"
        b'Font "TestFont" 48 color 255 255 255\n'
        b"PageSize 256 PageCount 1 TextType color Distribution english\n"
        b"Height 25 RenderHeight 34 Ascent 26 RenderAscent 26 IL 10\n"
        b"CharSpacing 0 LineHeight 0 FallbackCharacter 63\n"
        b"Glyph A Width 10 12 1\n"
        b"Kern A B -1\n"
        b"END\n")


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "fixtures")
    os.makedirs(outdir, exist_ok=True)

    # Standalone containers.
    fedm = fed_container(b"FEDM", [
        (0xB008, B008), (0xB009, B009), (0xB001, B001), (0xB003, B003),
        (0xB004, B004), (0xB005, B005),
        (0x7101, S101), (0x7102, S102), (0x7103, S103),
    ])
    open(os.path.join(outdir, "fedforce_model_tri.fedmodel"), "wb").write(fedm)
    feds = fed_container(b"FEDS", [(0x7101, S101), (0x7102, S102), (0x7103, S103)])
    open(os.path.join(outdir, "fedforce_skel_1bone.fedskel"), "wb").write(feds)
    fedt = (b"FEDT" + struct.pack("<HHHHHHII", 1, 0, 8, 8, 1, 0, PATH_TEX, 256)
            + PIXELS)
    open(os.path.join(outdir, "fedforce_tex_8x8.fedtex"), "wb").write(fedt)

    # Retail-layout .dict/.data pair.
    # Buffer: header payloads, then leaf data.
    buf = bytearray()
    hoffs = []

    def put(b):
        off = len(buf)
        buf.extend(b)
        return off

    for ht, ph in [(0x30DB9D05, PATH_MODEL), (0, PATH_MODEL),
                   (0x50D377E9, PATH_TEX), (0, 0x33333333), (0, 0x44444444)]:
        hoffs.append(put(struct.pack("<II", ht, ph)))
    o008 = put(B008)
    o009 = put(B009)
    o001 = put(B001)
    o003 = put(B003)
    o004 = put(B004)
    o005 = put(B005)
    o101 = put(S101)
    o102 = put(S102)
    o103 = put(S103)
    o501 = put(B501)
    o502 = put(PIXELS)
    onloc = put(NLOC)
    ofont = put(FONT)

    LEAF = 0x10C0
    PAR = 0x10C0 | 0x8000
    entries = []
    # File headers + bodies (bodies reference child ranges below).
    entries.append((0x1301, LEAF, 8, hoffs[0]))   # 0 model hdr
    entries.append((0xB000, PAR, 6, 10))          # 1 model body
    entries.append((0x1301, LEAF, 8, hoffs[1]))   # 2 skel hdr
    entries.append((0x7100, PAR, 3, 16))          # 3 skel body
    entries.append((0x1301, LEAF, 8, hoffs[2]))   # 4 tex hdr
    entries.append((0xB500, PAR, 2, 19))          # 5 tex body
    entries.append((0x1301, LEAF, 8, hoffs[3]))   # 6 nloc hdr
    entries.append((0x7020, LEAF, len(NLOC), onloc))   # 7 nloc body
    entries.append((0x1301, LEAF, 8, hoffs[4]))   # 8 font hdr
    entries.append((0x7010, LEAF, len(FONT), ofont))   # 9 font body
    # Model children 10..15.
    entries.append((0xB008, LEAF, len(B008), o008))
    entries.append((0xB009, LEAF, len(B009), o009))
    entries.append((0xB001, LEAF, len(B001), o001))
    entries.append((0xB003, LEAF, len(B003), o003))
    entries.append((0xB004, LEAF, len(B004), o004))
    entries.append((0xB005, LEAF, len(B005), o005))
    # Skel children 16..18.
    entries.append((0x7101, LEAF, len(S101), o101))
    entries.append((0x7102, LEAF, len(S102), o102))
    entries.append((0x7103, LEAF, len(S103), o103))
    # Tex children 19..20.
    entries.append((0xB501, LEAF, len(B501), o501))
    entries.append((0xB502, LEAF, len(PIXELS), o502))
    assert len(entries) == 21
    table = b"".join(struct.pack("<HHII", t, f, s, o) for t, f, s, o in entries)

    data = table + bytes(buf)
    blo0 = (0, len(table), 0, 0)
    blo1 = (len(table), len(buf), 0, 0)
    largest = max(len(table), len(buf))
    d = struct.pack("<IHB B I BBBB", 0xA9F32458, 0, 0, 0, largest, 2, 1, 1, 0)
    d += struct.pack("<IHH8B", 0x297B947A, 21, 5, 0, 1, 0, 0, 0, 0, 0, 0)
    for off, ds, cs, fl in (blo0, blo1):
        d += struct.pack("<IIII", off, ds, cs, fl)
    d += b".data\x00"
    open(os.path.join(outdir, "fedforce_dict.dict"), "wb").write(d)
    open(os.path.join(outdir, "fedforce_dict.data"), "wb").write(data)
    print("wrote fixtures to", outdir)


main()
