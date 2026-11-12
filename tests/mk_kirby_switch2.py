#!/usr/bin/env python3
"""Synthetic fixtures for the Switch 2 Kirby formats.

Usage: mk_kirby_switch2.py OUTDIR

Writes:
  t.nup                    NUP container (see mk_nup.py)
  cam.bin.cmp              HAL ".cmp": u32 size + LZ4 frame around an XBIN YAML file
  y.bin                    XBIN 5.0 / YAML 6 (hash, array, int, float, bool, string)
  fdg.dat                  XBIN 5.0 / FDG 3 scene graph (2 scenes, 1 public, 1 dependency)
  Asset/assets.cat         PARC catalog for two assets:
  Asset/assets_a000.dat      PDAT pack 0: a PTEX BODY (4x4 BC1, solid red) + a zstd "PSND" record
  Asset/assets_b000.dat      PDAT pack 1: the PTEX HEAD flatbuffer
"""
import hashlib, os, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mk_nup  # noqa: E402


def zstd(data):
    try:
        import zstandard
        return zstandard.ZstdCompressor(level=3).compress(data)
    except ImportError:
        return subprocess.run(["zstd", "-q", "-c"], input=data, capture_output=True, check=True).stdout


def xxh32(data, seed=0):
    P1, P2, P3, P4, P5 = 2654435761, 2246822519, 3266489917, 668265263, 374761393
    M = 0xFFFFFFFF
    rotl = lambda x, r: ((x << r) | (x >> (32 - r))) & M
    n = len(data)
    i = 0
    if n >= 16:
        v = [(seed + P1 + P2) & M, (seed + P2) & M, seed & M, (seed - P1) & M]
        while i + 16 <= n:
            for k in range(4):
                v[k] = rotl((v[k] + struct.unpack_from("<I", data, i + 4 * k)[0] * P2) & M, 13) * P1 & M
            i += 16
        h = (rotl(v[0], 1) + rotl(v[1], 7) + rotl(v[2], 12) + rotl(v[3], 18)) & M
    else:
        h = (seed + P5) & M
    h = (h + n) & M
    while i + 4 <= n:
        h = rotl((h + struct.unpack_from("<I", data, i)[0] * P3) & M, 17) * P4 & M
        i += 4
    while i < n:
        h = rotl((h + data[i] * P5) & M, 11) * P1 & M
        i += 1
    h ^= h >> 15
    h = h * P2 & M
    h ^= h >> 13
    h = h * P3 & M
    h ^= h >> 16
    return h


def lz4_frame_stored(data):
    """A standard LZ4 frame whose single block is stored uncompressed."""
    desc = bytes([0x60, 0x40])  # version 01, independent blocks; 64 KB max block
    hc = (xxh32(desc) >> 8) & 0xFF
    return b"\x04\x22\x4d\x18" + desc + bytes([hc]) + struct.pack("<I", len(data) | 0x80000000) + data + b"\0\0\0\0"


# ---------------------------------------------------------------- XBIN / YAML 6

def xbin_wrap(payload, major=5):
    head = 0x14 if major > 2 else 0x10
    body = payload + b"RLOC" + bytes(8)
    size = head + len(body)
    h = b"XBIN" + b"\x34\x12" + bytes([major, 0]) + struct.pack("<II", size, 65001)
    if major > 2:
        h += struct.pack("<I", head + len(payload))
    return h + body


def yaml6(value):
    buf = bytearray(b"\0\0\0\0YAML" + struct.pack("<I", 6))
    patches = []  # (field position, python string)
    strings = {}

    def node(v):
        pos = len(buf)
        if isinstance(v, bool):
            buf.extend(struct.pack("<QI", 4, int(v)))
        elif isinstance(v, int):
            buf.extend(struct.pack("<Qi", 1, v))
        elif isinstance(v, float):
            buf.extend(struct.pack("<Qf", 3, v))
        elif isinstance(v, str):
            buf.extend(struct.pack("<Q", 5))
            patches.append((len(buf), v))
            buf.extend(b"\0\0\0\0")
        elif isinstance(v, dict):
            items = list(v.items())
            buf.extend(struct.pack("<QI", 6, len(items)))
            pairs = len(buf)
            buf.extend(bytes(8 * len(items)))
            buf.extend(struct.pack("<%dI" % len(items), *range(len(items))))
            for i, (k, val) in enumerate(items):
                patches.append((pairs + 8 * i, k))
                child = node(val)
                struct.pack_into("<I", buf, pairs + 8 * i + 4, child - (pairs + 8 * i + 4))
        elif isinstance(v, list):
            buf.extend(struct.pack("<QI", 7, len(v)))
            refs = len(buf)
            buf.extend(bytes(4 * len(v)))
            for i, val in enumerate(v):
                child = node(val)
                struct.pack_into("<I", buf, refs + 4 * i, child - (refs + 4 * i))
        else:
            raise TypeError(v)
        while len(buf) % 4:
            buf.append(0)
        return pos

    node(value)
    for _, s in patches:
        if s not in strings:
            strings[s] = len(buf)
            b = s.encode()
            buf.extend(struct.pack("<I", len(b)) + b + b"\0")
            while len(buf) % 4:
                buf.append(0)
    for field, s in patches:
        struct.pack_into("<I", buf, field, strings[s] - field)
    return bytes(buf)


def xbin_yaml(value):
    # offsets inside YAML 5+ are self-relative, so the payload can sit after the header as is
    return xbin_wrap(yaml6(value))


def xbin_fdg():
    strings = ["Scene_Title", "Scene_Main", ":SND:Title.bfsar", "gfx/Title.bfres.cmp"]
    head = 0x14
    body = bytearray(b"HGDF" + struct.pack("<I", 3) + bytes(12))  # offsets patched below
    base = head

    def here():
        return base + len(body)

    str_off = {}

    pub = here()
    body += struct.pack("<II", 1, 0)  # scene 0 is public
    scn = here()
    body += struct.pack("<I", 2) + bytes(24)
    deps0 = here()
    body += struct.pack("<I", 0)
    deps1 = here()
    body += struct.pack("<II", 1, 0)  # scene 1 depends on scene 0
    as0 = here()
    body += struct.pack("<II", 1, 0)
    as1 = here()
    body += struct.pack("<III", 2, 0, 1)
    ast = here()
    body += struct.pack("<II", 2, 0) + bytes(32)
    for s in strings:
        str_off[s] = here()
        b = s.encode()
        body += struct.pack("<I", len(b)) + b + b"\0"
        while len(body) % 4:
            body.append(0)
    struct.pack_into("<III", body, 8, pub, scn, ast)
    struct.pack_into("<III", body, scn - base + 4, str_off["Scene_Title"], deps0, as0)
    struct.pack_into("<III", body, scn - base + 16, str_off["Scene_Main"], deps1, as1)
    for i, s in enumerate(strings[2:]):
        struct.pack_into("<QII", body, ast - base + 8 + 16 * i, i, str_off[s], 0)
    return xbin_wrap(bytes(body))


# ---------------------------------------------------------------- PARC / PDAT / PTEX

def ptex_head(width, height, fmt):
    h = bytearray(0x48)
    struct.pack_into("<I", h, 0, 0x18)
    h[4:8] = b"PTEX"
    struct.pack_into("<HHHHHH", h, 0x08, 12, 8, 0, 0, 0, 4)  # root vtable: field 3 at +4
    struct.pack_into("<iI", h, 0x18, 0x18 - 0x08, 0x34 - 0x1C)
    struct.pack_into("<HHHHHHHHH", h, 0x20, 18, 20, 0, 4, 8, 12, 14, 16, 17)
    struct.pack_into("<iIIHHBB", h, 0x34, 0x34 - 0x20, width, height, 1, 1, 1, fmt)
    return bytes(h)


def build_parc(outdir):
    os.makedirs(outdir, exist_ok=True)
    tex_id, snd_id = 0x1122334455667788, 0x0102030405060708
    head = ptex_head(4, 4, 0x18)  # BC1
    body = bytearray(512)  # one GOB: block (0,0) at offset 0
    struct.pack_into("<HHI", body, 0, 0xF800, 0xF800, 0)  # solid red
    snd = struct.pack("<I", 8) + b"PSND" + b"phx sound record".ljust(24, b"\0")

    packs = [bytearray(0x40), bytearray(0x40)]
    locs = []

    def add(pack, data, compress):
        stored = zstd(data) if compress else data
        p = packs[pack]
        while len(p) % 16:
            p.append(0)
        off = len(p)
        p.extend(stored)
        locs.append((pack, 1 if compress else 0, off, len(stored), len(data), hashlib.sha256(stored).digest()))
        return len(locs) - 1

    l_head = add(1, head, False)
    l_body = add(0, bytes(body), False)
    l_snd = add(0, snd, True)

    hashes = []
    for i, p in enumerate(packs):
        h = hashlib.sha256(b"pack%d" % i).digest()
        hdr = b"PDAT" + struct.pack("<IIIQ", 2, i, 0, len(p) - 0x40) + h + bytes(8)
        p[0:0x40] = hdr
        hashes.append(h)

    assets = [  # (id, blocks [(tag, sub, loc)])
        (tex_id, [("HEAD", 0, l_head), ("BODY", 0, l_body)]),
        (snd_id, [("Main", 0, l_snd)]),
    ]
    recs = []
    for aid, blocks in assets:
        r = struct.pack("<Q", len(blocks)) + struct.pack("<Q", aid) * 2
        for tag, sub, li in blocks:
            r += bytes(16) + tag.encode().ljust(4, b"\0") + struct.pack("<IQQQ", sub, li, 0, locs[li][4])
        recs.append(r + bytes(16))

    out = bytearray(b"PARC" + struct.pack("<IQQ", 4, 0, 1000) + bytes(32))
    out += struct.pack("<III", 12 + 32 * len(packs), len(packs), 1) + b"".join(hashes)
    idmap = sorted((aid, i) for i, (aid, _) in enumerate(assets))
    out += struct.pack("<II", 8 + 16 * len(idmap), len(idmap))
    for aid, i in idmap:
        out += struct.pack("<QII", aid, 0xFFFFFFFF, i)
    offs, pos = [], 0
    for r in recs:
        offs.append(pos)
        pos += len(r)
    sec = struct.pack("<%dI" % len(offs), *offs) + b"".join(recs)
    out += struct.pack("<II", 8 + len(sec), len(assets)) + sec
    out += struct.pack("<II", 8 + 64 * len(locs), len(locs))
    for pack, codec, off, st, sz, h in locs:
        out += struct.pack("<IIQQQ", pack, codec, off, st, sz) + h
    struct.pack_into("<Q", out, 8, len(out) - 0x38)

    open(os.path.join(outdir, "assets.cat"), "wb").write(out)
    open(os.path.join(outdir, "assets_a000.dat"), "wb").write(packs[0])
    open(os.path.join(outdir, "assets_b000.dat"), "wb").write(packs[1])


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    mk_nup.build(os.path.join(outdir, "t.nup"))
    cam = xbin_yaml({"Kind": "Rail", "NodeCount": 2, "IsClose": False})
    open(os.path.join(outdir, "cam.bin.cmp"), "wb").write(struct.pack("<I", len(cam)) + lz4_frame_stored(cam))
    y = xbin_yaml({"type": "Scalable", "size": 64, "scale": 1.5, "flag": True,
                   "list": [1, "two", {"three": 3}], "empty": [], "quote me": 'a "b"'})
    open(os.path.join(outdir, "y.bin"), "wb").write(y)
    open(os.path.join(outdir, "fdg.dat"), "wb").write(xbin_fdg())
    build_parc(os.path.join(outdir, "Asset"))


if __name__ == "__main__":
    main(sys.argv[1])
