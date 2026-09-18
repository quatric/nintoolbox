"""Synthetic Next Level Games LM2 / LM3 / SANIM fixtures.

Builds spec-layout .dict/.data pairs (upstream LM2/LM3 DICT_Parser +
ChunkTable + DATA_Parser block roles) plus a Mario Strikers SANIM stream,
exercising the lib-nlg-lm parsers: models -> GLB, textures -> PNG,
skeletons, animations/scripts -> text, NLOC/font/config passthrough.
Path hashes come from the upstream Hashes/ wordlists so filenames resolve
to real names (see lib-nlg-names.inc).

Usage: python3 tests/mk-nlg-lm-fixtures.py [outdir]
Defaults to tests/fixtures/.
"""
import os
import struct
import sys


def nlg_hash(name):
    h = -1
    for c in name.encode():
        h = (h * 33 + c) & 0xFFFFFFFF
    return h & 0xFFFFFFFF


def morton8(x, y):
    z = 0
    for i in range(3):
        z |= ((x >> i) & 1) << (2 * i)
        z |= ((y >> i) & 1) << (2 * i + 1)
    return z


def pica_tile_rgba8(w, h, px):
    tw, th = (w + 7) & ~7, (h + 7) & ~7
    out = bytearray(tw * th * 4)
    for y in range(h):
        for x in range(w):
            pos = ((y // 8) * (tw // 8) + (x // 8)) * 64 + morton8(x & 7, y & 7)
            out[pos * 4:pos * 4 + 4] = bytes(px[y * w + x])
    return bytes(out)


def tegra_swizzle_addr(x, y, w, bpp, bh):
    wgobs = (w * bpp + 63) // 64
    gob = (y // (8 * bh)) * 512 * bh * wgobs + (x * bpp // 64) * 512 * bh \
        + ((y % (8 * bh)) // 8) * 512
    xb = x * bpp
    return gob + ((xb % 64) // 32) * 256 + ((y % 8) // 2) * 64 \
        + ((xb % 32) // 16) * 32 + (y % 2) * 16 + (xb % 16)


def tegra_tile_rgba8(w, h, px):
    pitch = ((w * 4 + 63) // 64) * 64
    surf = pitch * (((h + 8 - 1) // 8) * 8)
    out = bytearray(surf)
    for y in range(h):
        for x in range(w):
            o = tegra_swizzle_addr(x, y, w, 4, 1)
            out[o:o + 4] = bytes(px[y * w + x])
    return bytes(out)


P_MODEL = nlg_hash("00.nlg")
P_TEX = nlg_hash("02.nlg")
P_SKELB = nlg_hash("03.nlg")
P_ANIM = nlg_hash("04.nlg")
P_SCRIPT = nlg_hash("05.nlg")
P_FONT = nlg_hash("06.nlg")
P_MSG = nlg_hash("07.nlg")
P_CFG = nlg_hash("1.nlg")
H_BIP = nlg_hash("bip01")
H_EASY = nlg_hash("Easy.script")
H_MED = nlg_hash("Medium.script")

GRAD = [(x * 32, y * 32, 128, 255) for y in range(8) for x in range(8)]

FONT_TEXT = b"NLG Font Description File\nVersion 1.1\nFont \"T\" 8 color 1 2 3\nEND\n"


def be_nloc():
    nloc = b"NLOC" + struct.pack(">4I", 1, 0x1234, 1, 0)
    nloc += struct.pack(">2I", 100, 0)
    nloc += "Test".encode("utf-16be") + b"\x00\x00"
    return nloc


def lm2_pair(outdir):
    H_TEX = 0xA11CE001
    pay = bytearray()

    def place(b):
        off = len(pay)
        pay.extend(b)
        return off

    o_b002 = place(struct.pack("<4I", 0x11111111, 1, 0, 0))
    mesh = struct.pack("<IHHHHIIIIIHHI",
                       0, 3, 0, 0, 0,
                       0x4821B2DF, 0xDEADBEEF, 0, 0, 0,
                       3, 0, 0xAABBCCDD)
    assert len(mesh) == 40
    o_b003 = place(mesh)
    o_b004 = place(struct.pack("<I", 6))

    def l4vert(x, y, z):
        return struct.pack("<fff", x, y, z) + bytes([0, 0, 127, 0]) \
            + struct.pack("<hh", 0, 0)

    b005 = struct.pack("<HHH", 0, 1, 2) + l4vert(0, 0, 0) \
        + l4vert(1, 0, 0) + l4vert(0, 1, 0)
    assert len(b005) == 66
    o_b005 = place(b005)
    o_b006 = place(struct.pack("<I", H_TEX))
    o_b007 = place(struct.pack("<I", 0))
    o_b001 = place(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0,
                               0, 0, 1, 0, 0, 0, 0, 1))

    b501 = bytearray(48)
    struct.pack_into("<I", b501, 0, 256)
    struct.pack_into("<I", b501, 4, H_TEX)
    struct.pack_into("<H", b501, 16, 8)
    struct.pack_into("<H", b501, 18, 8)
    b501[44] = 0
    o_b501 = place(bytes(b501))
    o_b502 = place(pica_tile_rgba8(8, 8, GRAD))

    s101 = struct.pack("<7I", 1, 0, 0, 0, 0, 0, 0)
    s102 = struct.pack("<I", H_BIP) + struct.pack("<h", -1) \
        + struct.pack("<hHBB", 0, 0, 0, 0)
    assert len(s102) == 12
    s103 = struct.pack("<10f", 0, 0, 0, 1, 1, 2, 3, 1, 1, 1)
    o_s101 = place(s101)
    o_s102 = place(s102)
    o_s103 = place(s103)

    o_5014 = place(struct.pack("<IIHH", H_EASY, 0, 0, 0))
    sdata = struct.pack("<4I", 0, 2, 4, 3) + struct.pack("<I", P_MODEL) \
        + bytes([0x38, 0]) + b"hi\x00"
    o_5012 = place(sdata)
    o_7011 = place(FONT_TEXT)

    anim = struct.pack("<IHHfI", 0, 1, 4, 1.0, 0)
    anim += struct.pack("<IBBBBI", H_BIP, 0, 0, 1, 0x0B, 28)
    anim += struct.pack("<fff", 7, 8, 9)
    assert len(anim) == 40
    o_anim = place(anim)
    nloc = be_nloc()
    o_msg = place(nloc)
    o_cfg = place(b"quality=high\n")
    PAY = bytes(pay)

    files = [
        (0xB000, P_MODEL, [(0xB001, o_b001, 64), (0xB002, o_b002, 16),
                           (0xB003, o_b003, 40), (0xB004, o_b004, 4),
                           (0xB005, o_b005, 66), (0xB006, o_b006, 4),
                           (0xB007, o_b007, 4)]),
        (0xB500, P_TEX, [(0xB501, o_b501, 48), (0xB502, o_b502, 256)]),
        (0x7100, P_MODEL, [(0x7101, o_s101, 28), (0x7102, o_s102, 12),
                           (0x7103, o_s103, 40)]),
        (0x7100, P_SKELB, [(0x7101, o_s101, 28), (0x7102, o_s102, 12),
                           (0x7103, o_s103, 40)]),
        (0x7000, P_ANIM, None, o_anim, 40),
        (0x5000, P_SCRIPT, [(0x5014, o_5014, 12), (0x5012, o_5012, len(sdata))]),
        (0x7010, P_FONT, [(0x7011, o_7011, len(FONT_TEXT))]),
        (0x7020, P_MSG, None, o_msg, len(nloc)),
        (0x0031, P_CFG, None, o_cfg, 13),
    ]
    entries = []
    for f in files:
        entries.append((0x1301, 0, 8, None))
        if len(f) == 3:
            entries.append((f[0], 0x3000, len(f[2]), None))
        else:
            entries.append((f[0], 0, f[4], f[3]))
    child_base = len(entries)
    for f in files:
        if len(f) == 3:
            for (ct, off, sz) in f[2]:
                entries.append((ct, 0x1000, sz, off))
    for fi, f in enumerate(files):
        if len(f) == 3:
            cs = child_base + sum(len(g[2]) for g in files[:fi] if len(g) == 3)
            ty, fl, sz, _ = entries[fi * 2 + 1]
            entries[fi * 2 + 1] = (ty, fl, sz, cs)
        ty, fl, sz, _ = entries[fi * 2]
        entries[fi * 2] = (ty, fl, sz, fi * 8)
    headers = b"".join(struct.pack("<II", 0, f[1]) for f in files)
    table = b"".join(struct.pack("<HHII", *e) for e in entries)

    nblocks = 4
    blocks = [(0, len(table), 0, 0), (0, 0, 0, 0),
              (len(table), len(headers), 0, 0),
              (len(table) + len(headers), len(PAY), 0, 0)]
    hdr = struct.pack(">I", 0x5824F3A9)
    hdr += struct.pack("<H", 0x4001) + bytes([0, 0])
    hdr += struct.pack("<II", nblocks, 0x800)
    hdr += bytes([1, 0, 1, 1])
    hdr += bytes(12)
    hdr += bytes(nblocks)
    for (off, dec, comp, fl) in blocks:
        hdr += struct.pack("<4I", off, dec, comp, fl)
    hdr += b"x\x00"
    open(os.path.join(outdir, "lm2_dict.dict"), "wb").write(hdr)
    open(os.path.join(outdir, "lm2_dict.data"), "wb").write(table + headers + PAY)
    print("lm2: dict %d data %d" % (len(hdr), len(table) + len(headers) + len(PAY)))


def lm3_pair(outdir):
    H_TEX = 0xA11CE003
    b53 = bytearray()
    b65 = bytearray()
    b69 = bytearray()

    def place(buf, b):
        off = len(buf)
        buf.extend(b)
        return off

    o_b002 = place(b53, struct.pack("<IIHH", 0x11111111, 0, 1, 0))
    mesh = struct.pack("<IIIIBBHHHIIIIHHIIIII",
                       0xAAAAAAAA, 0, 3, 3, 0, 1, 0, 0, 0,
                       0xDEADBEEF, 0, 0, 0xFFFFFF, 0, 0,
                       0, 0, 0, 0, 0)
    assert len(mesh) == 64
    o_b003 = place(b53, mesh)
    o_b004 = place(b53, struct.pack("<3I", 6, 0, 0))

    def lm3vert(x, y, z, u, v):
        return struct.pack("<fff", x, y, z) + struct.pack("<f", u) \
            + struct.pack("<fff", 0, 0, 1) + struct.pack("<f", v) \
            + struct.pack("<ffff", 1, 0, 0, 1)

    b005 = struct.pack("<HHH", 0, 1, 2) + lm3vert(0, 0, 0, 0, 0) \
        + lm3vert(1, 0, 0, 1, 0) + lm3vert(0, 1, 0, 0, 1)
    assert len(b005) == 150
    o_b005 = place(b53, b005)
    o_b006 = place(b53, struct.pack("<I", H_TEX))
    o_b007 = place(b53, struct.pack("<I", 0))
    o_b001 = place(b53, struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0,
                                    0, 0, 1, 0, 0, 0, 0, 1))

    b501 = struct.pack("<IHHBBBBBBH", H_TEX, 8, 8, 0, 0, 1, 0, 0, 0, 0)
    assert len(b501) == 16
    o_b501 = place(b53, b501)
    o_b502 = place(b65, tegra_tile_rgba8(8, 8, GRAD))

    def skel_chunks():
        s101 = struct.pack("<8I", 0, 0, 0, 0, 0, 1, 0, 0)
        s102 = struct.pack("<IhBB", H_BIP, 0, 0, 0)
        assert len(s102) == 8
        s103 = struct.pack("<7f", 0, 0, 0, 1, 4, 5, 6)
        s106 = struct.pack("<h", -1)
        return [(0x7101, place(b53, s101), 32), (0x7102, place(b53, s102), 8),
                (0x7103, place(b53, s103), 28), (0x7106, place(b53, s106), 2)]

    skelA = skel_chunks()
    skelB = skel_chunks()

    o_5014 = place(b53, struct.pack("<IIHH", H_MED, 0, 0, 0))
    sdata = struct.pack("<5I", 0, 0, 4, 2, 3) + struct.pack("<I", P_MODEL) \
        + bytes([0x38, 0]) + b"hi\x00"
    o_5012 = place(b53, sdata)
    o_7011 = place(b53, FONT_TEXT)

    anim = struct.pack("<IHHfI", 0, 1, 2, 2.0, 0)
    anim += struct.pack("<IBBBBI", H_BIP, 0, 0, 3, 0x0B, 28)
    anim += struct.pack("<fff", 10, 11, 12)
    assert len(anim) == 40
    o_anim = place(b53, anim)
    o_cfg = place(b53, b"quality=high\n")

    nloc = be_nloc()
    o_mleaf = place(b69, nloc)
    B53, B65, B69 = bytes(b53), bytes(b65), bytes(b69)

    files = [
        (0xB000, P_MODEL, 52, [(0xB002, o_b002, 12), (0xB003, o_b003, 64),
                               (0xB004, o_b004, 12), (0xB005, o_b005, 150),
                               (0xB006, o_b006, 4), (0xB007, o_b007, 4),
                               (0xB001, o_b001, 64)]),
        (0xB500, P_TEX, 63, [(0xB501, o_b501, 16), (0xB502, o_b502, len(B65))]),
        (0x7100, P_MODEL, 52, skelA),
        (0x7100, P_SKELB, 52, skelB),
        (0x7000, P_ANIM, 52, None, o_anim, 40),
        (0x5000, P_SCRIPT, 63, [(0x5014, o_5014, 12), (0x5012, o_5012, len(sdata))]),
        (0x7010, P_FONT, 52, [(0x7011, o_7011, len(FONT_TEXT))]),
        (0x7020, P_MSG, 52, None, o_mleaf, len(nloc)),
        (0x0031, P_CFG, 52, None, o_cfg, 13),
    ]
    entries = []
    for f in files:
        entries.append((0x1301, 0, 8, None))
        if len(f) == 4:
            entries.append((f[0], 0x3000, len(f[3]), None))
        else:
            entries.append((f[0], 0, f[5], f[4]))
    child_base = len(entries)
    for f in files:
        if len(f) == 4:
            for (ct, off, sz) in f[3]:
                fl = 0 if ct == 0xB502 else 192
                entries.append((ct, fl, sz, off))
    for fi, f in enumerate(files):
        if len(f) == 4:
            cs = child_base + sum(len(g[3]) for g in files[:fi] if len(g) == 4)
            ty, fl, sz, _ = entries[fi * 2 + 1]
            entries[fi * 2 + 1] = (ty, fl, sz, cs)
    used52 = used63 = 0
    for fi, f in enumerate(files):
        ty, fl, sz, _ = entries[fi * 2]
        if f[2] == 52:
            entries[fi * 2] = (ty, fl, sz, used52)
            used52 += 8
        elif f[2] == 63:
            entries[fi * 2] = (ty, fl, sz, used63)
            used63 += 8
        else:
            entries[fi * 2] = (ty, fl, sz, 0)
    headers52 = b"".join(struct.pack("<II", 0, f[1]) for f in files if f[2] == 52)
    headers63 = b"".join(struct.pack("<II", 0, f[1]) for f in files if f[2] == 63)
    assert len(headers52) == used52 and len(headers63) == used63
    table = b"".join(struct.pack("<HHII", *e) for e in entries)

    NB = 70
    off52 = len(table)
    off63 = off52 + len(headers52)
    off53 = off63 + len(headers63)
    off65 = off53 + len(B53)
    off69 = off65 + len(B65)
    blocks = [(0, 0, 0, 0)] * NB
    blocks[0] = (0, len(table), 0, 0)
    blocks[52] = (off52, len(headers52), 0, 0)
    blocks[63] = (off63, len(headers63), 0, 0)
    blocks[53] = (off53, len(B53), 0, 0)
    blocks[65] = (off65, len(B65), 0, 0)
    blocks[69] = (off69, len(B69), 0, 0)
    largest = max(b[1] for b in blocks)
    hdr = struct.pack(">I", 0x5824F3A9)
    hdr += struct.pack("<H", 0x4001) + bytes([0, 0])
    hdr += struct.pack("<I", largest)
    hdr += bytes([NB, 1, 1, 0])
    hdr += bytes(24)
    for (off, dec, comp, fl) in blocks:
        hdr += struct.pack("<4I", off, dec, comp, fl)
    hdr += b"x\x00"
    data = table + headers52 + headers63 + B53 + B65 + B69
    open(os.path.join(outdir, "lm3_dict.dict"), "wb").write(hdr)
    open(os.path.join(outdir, "lm3_dict.data"), "wb").write(data)
    print("lm3: dict %d data %d" % (len(hdr), len(data)))


def sanim_stream(outdir):
    def chunk(flags, magic, payload):
        pad = (-len(payload)) % 4
        return struct.pack(">HHI", flags, magic, len(payload)) + payload \
            + b"\x00" * pad

    rot = struct.pack(">6h", 100, 0, 0, 200, 0, 0)
    tr = struct.pack(">3f", 1.0, 2.0, 3.0)
    track = chunk(0, 0x7101, rot) + chunk(0, 0x7102, tr)
    inner = chunk(0, 0x7001, struct.pack(">4I", 0, 0x12345678, 30, 2))
    inner += chunk(0, 0x7002, b"run")
    inner += chunk(0, 0x7003, struct.pack(">2I", 0, 0))
    inner += chunk(0, 0x7100, track)
    blob = chunk(0, 0x7000, inner)
    open(os.path.join(outdir, "strikers_test.sanim"), "wb").write(blob)
    print("sanim: %d bytes" % len(blob))


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures"
    os.makedirs(outdir, exist_ok=True)
    lm2_pair(outdir)
    lm3_pair(outdir)
    sanim_stream(outdir)


main()
