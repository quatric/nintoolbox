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
  Asset/assets_b000.dat      PDAT pack 1: the PTEX HEAD flatbuffer + a PMSH quad mesh (zero HEAD, Main, GBuf)
                             and a 4x4x4 RGBA8 3D texture (colour LUT);
                             a PMTR material + Pfab prefab binding the quad's slot to a green BC1 texture
  plain.soundstream        Sygnus stream: 2 ch, 600 samples of 4-bit ADPCM (one full + one short block)
  enc.soundstream          the same file AES-192-ECB encrypted as the game ships it
  sfx.psnd                 phx PSND record embedding a 1-channel ADPCM source (Sygnus ASRC bank)
  plain.pcm / sfx.pcm      expected s16 samples from the reference decoder below
"""
import base64, hashlib, math, os, random, struct, subprocess, sys

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

def ptex_head(width, height, fmt, depth=1, dim=1):
    h = bytearray(0x48)
    struct.pack_into("<I", h, 0, 0x18)
    h[4:8] = b"PTEX"
    struct.pack_into("<HHHHHH", h, 0x08, 12, 8, 0, 0, 0, 4)  # root vtable: field 3 at +4
    struct.pack_into("<iI", h, 0x18, 0x18 - 0x08, 0x34 - 0x1C)
    struct.pack_into("<HHHHHHHHH", h, 0x20, 18, 20, 0, 4, 8, 12, 14, 16, 17)
    struct.pack_into("<iIIHHBB", h, 0x34, 0x34 - 0x20, width, height, depth, 1, dim, fmt)
    return bytes(h)


def flatbuffer(root_fields, ident):
    """Tiny forward-layout flatbuffer writer. A table is a list of (field, value) where value is
    ("u32", n), ("u8", n), ("u16", n), ("u64", n), ("raw", bytes) (inline struct), ("table", fields)
    or ("vec", [fields, ...])."""
    out = bytearray(8)
    out[4:8] = ident

    def table(fields):
        fields = sorted(fields)
        nf = max(f for f, _ in fields) + 1
        vt = len(out)
        body, offs, patches = bytearray(4), [0] * nf, []
        for f, (kind, val) in fields:
            if kind == "u8":
                offs[f] = len(body)
                body.append(val)
            elif kind == "u16":
                while len(body) % 2:
                    body.append(0)
                offs[f] = len(body)
                body += struct.pack("<H", val)
            elif kind == "raw":
                while len(body) % 4:
                    body.append(0)
                offs[f] = len(body)
                body += val
            elif kind == "u64":
                while len(body) % 4:
                    body.append(0)
                offs[f] = len(body)
                body += struct.pack("<Q", val)
            else:
                while len(body) % 4:
                    body.append(0)
                offs[f] = len(body)
                body += struct.pack("<I", val if kind == "u32" else 0)
                if kind != "u32":
                    patches.append((offs[f], kind, val))
        while len(body) % 4:
            body.append(0)
        vtab = struct.pack("<HH", 4 + 2 * nf, len(body)) + struct.pack("<%dH" % nf, *offs)
        while (len(out) + len(vtab)) % 4:
            vtab += b"\0"
        out.extend(vtab)
        tpos = len(out)
        struct.pack_into("<i", body, 0, tpos - vt)
        out.extend(body)
        for at, kind, val in patches:
            if kind == "table":
                child = table(val)
            else:
                elems = len(out)
                out.extend(struct.pack("<I", len(val)) + bytes(4 * len(val)))
                for k, v in enumerate(val):
                    c = table(v)
                    struct.pack_into("<I", out, elems + 4 + 4 * k, c - (elems + 4 + 4 * k))
                child = elems
            struct.pack_into("<I", out, tpos + at, child - (tpos + at))
        return tpos

    root = table(root_fields)
    struct.pack_into("<I", out, 0, root)
    return bytes(out)


PMSH_SLOT = 0x829C063347186A20
PMTR_ID, GREEN_TEX_ID, PFAB_ID = 0x38765C2F873169A0, 0x3344556677889900, 0x4455667788990011


def pmtr_material():
    """PMTR Main: f3.f2 texture refs {f1 asset id}: a missing 4x4 default first, then the colour map."""
    refs = [[(1, ("u64", 0x758EC0BBF3212217))], [(1, ("u64", GREEN_TEX_ID))]]
    return flatbuffer([(3, ("table", [(2, ("vec", refs))]))], b"PMTR")


def pfab_prefab():
    """Pfab: {u64 0x10, u64 offset of the slot hash, u64 material id} assigns a material to a slot."""
    d = bytearray(b"Pfab" + struct.pack("<I", 4) + bytes(0x38))
    d += struct.pack("<QQ", PMSH_SLOT, 0)  # slot hash at 0x40
    d += struct.pack("<QQQ", 0x10, 0x40, PMTR_ID)
    return bytes(d)


JOINT_ROOT, JOINT_TIP, PSKS_ID, SKIN_MESH_ID = 0x1111AAAA2222BBBB, 0x3333CCCC4444DDDD, 0x5566778899AABBCC, 0x0A0B0C0D0E0F1012


def psks_skeleton():
    """PSKS: f3.f1 joints {f2 {u64 name}, f4 u16 parent, f5 translation, f6 rotation, f7 scale}: tip 1 up z."""
    def joint(name, parent, t):
        f = [(2, ("table", [(0, ("u64", name))])), (4, ("u16", parent)),
             (5, ("table", [(i, ("raw", struct.pack("<f", v))) for i, v in enumerate(t) if v])),
             (6, ("table", [(3, ("raw", struct.pack("<f", 1.0)))])), (7, ("table", [(i, ("raw", struct.pack("<f", 1.0))) for i in range(3)]))]
        return [x for x in f if x[1][0] != "table" or x[1][1]]
    joints = [joint(JOINT_ROOT, 0xFFFF, (0, 0, 0)), joint(JOINT_TIP, 0, (0, 0, 1))]
    return flatbuffer([(3, ("table", [(1, ("vec", joints))]))], b"PSKS")


def pmsh_quad(skinned=False):
    """PMSH Main + GBuf: one 2-triangle quad (positions, unorm10 normals, half UVs, u16 indices).
    skinned: attribute 4 {u16 palette joint, u16 weight} x4 and the joint palette (geometry.f3),
    listed tip first so the exporter must remap palette indices onto the PSKS order."""
    pos = [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]
    gb = bytearray()
    streams = []

    def add(attr, data, fmt=None):
        while len(gb) % 16:
            gb.append(0)
        f = [(1, ("u32", attr)), (2, ("u32", len(data))), (3, ("u32", len(gb)))]
        if fmt is not None:
            f.append((4, ("u8", fmt)))
        streams.append(f)
        gb.extend(data)

    add(0, b"".join(struct.pack("<3f", *p) for p in pos))
    up = 512 | (1023 << 10) | (512 << 20)  # (0, 1, 0)
    add(1, struct.pack("<4I", *[up] * 4))
    add(8, b"".join(struct.pack("<2e", p[0], p[2]) for p in pos), 2)
    add(40, struct.pack("<6H", 0, 2, 1, 0, 3, 2), 1)
    geo = [(1, ("vec", streams)), (2, ("vec", [[(3, ("u32", 4)), (5, ("u32", 6))]])),
           (4, ("vec", [[(0, ("u64", PMSH_SLOT))]])), (11, ("u32", len(gb)))]
    if skinned:
        # z = 0 edge on the root (palette 1), z = 1 edge split between tip (0) and root
        add(4, struct.pack("<8H", 1, 65535, 0, 0, 0, 0, 0, 0) * 2 + struct.pack("<8H", 0, 32768, 1, 32767, 0, 0, 0, 0) * 2)
        ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        tip = ident[:14] + [-1, 1]  # column-major inverse bind of a joint at z = 1
        pal = [[(1, ("table", [(0, ("u64", JOINT_TIP))])), (2, ("raw", struct.pack("<16f", *tip)))],
               [(1, ("table", [(0, ("u64", JOINT_ROOT))])), (2, ("raw", struct.pack("<16f", *ident)))]]
        geo.append((3, ("vec", pal)))
    return flatbuffer([(3, ("table", geo))], b"PMSH"), bytes(gb)


def ptex_lut_body():
    """4x4x4 RGBA8 3D texture, Orin GOB layout, block height 1, block depth 4: texel = (60x, 60y, 60z)."""
    body = bytearray(2048)
    for z in range(4):
        for y in range(4):
            for x in range(4):
                xb = 4 * x
                o = z * 512 + ((xb % 64) // 32) * 256 + ((y % 8) // 4) * 128 + ((xb % 32) // 16) * 64 + (y % 4) * 16 + xb % 16
                body[o:o + 4] = bytes([60 * x, 60 * y, 60 * z, 255])
    return bytes(body)


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
    l_lhead = add(1, ptex_head(4, 4, 0x01, depth=4, dim=2), False)
    l_lbody = add(0, ptex_lut_body(), False)
    l_ghead = add(1, ptex_head(16, 16, 0x19), False)  # BC1 sRGB
    l_gbody = add(0, bytes([0xE0, 0x07, 0xE0, 0x07, 0, 0, 0, 0]) * 64, False)  # every block solid green
    l_pmtr = add(1, pmtr_material(), True)
    l_pfab = add(1, pfab_prefab(), False)
    msh_main, msh_gbuf = pmsh_quad()
    l_mhead = add(1, bytes(12), False)
    l_mmain = add(1, msh_main, False)
    l_mgbuf = add(1, msh_gbuf, True)
    sk_main, sk_gbuf = pmsh_quad(skinned=True)
    l_smain = add(1, sk_main, False)
    l_sgbuf = add(0, sk_gbuf, False)
    l_psks = add(1, psks_skeleton(), False)

    hashes = []
    for i, p in enumerate(packs):
        h = hashlib.sha256(b"pack%d" % i).digest()
        hdr = b"PDAT" + struct.pack("<IIIQ", 2, i, 0, len(p) - 0x40) + h + bytes(8)
        p[0:0x40] = hdr
        hashes.append(h)

    assets = [  # (id, blocks [(tag, sub, loc)])
        (tex_id, [("HEAD", 0, l_head), ("BODY", 0, l_body)]),
        (snd_id, [("Main", 0, l_snd)]),
        (0x2233445566778899, [("HEAD", 0, l_lhead), ("BODY", 0, l_lbody)]),
        (GREEN_TEX_ID, [("HEAD", 0, l_ghead), ("BODY", 0, l_gbody)]),
        (PMTR_ID, [("Main", 0, l_pmtr)]),
        (PFAB_ID, [("Main", 0, l_pfab)]),
        (0x0A0B0C0D0E0F1011, [("HEAD", 0, l_mhead), ("GBuf", 0, l_mgbuf), ("Main", 0, l_mmain)]),
        (SKIN_MESH_ID, [("HEAD", 0, l_mhead), ("GBuf", 0, l_sgbuf), ("Main", 0, l_smain)]),
        (PSKS_ID, [("Main", 0, l_psks)]),
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


# ---------------------------------------------------------------- Sygnus soundstream / PSND

def sygnus_adpcm(ch, ns, spb=512, seed=7):
    rnd = random.Random(seed)
    body = bytearray()
    done = 0
    while done < ns:
        n = min(spb, ns - done)
        for c in range(ch):
            b = struct.pack("<ff", 0.01 * (c + 1), 0.012 * (c + 1))
            if n >= 3:
                b += bytes([12 + c]) + bytes(rnd.randrange(256) for _ in range((n - 1) // 2))
            else:
                b = b[:4 * n]
            body += b
        done += n
    return struct.pack("<IIII", ch, 48000, ns, spb) + bytes(body)


def sygnus_stream(payload):
    strm = b"STRM" + struct.pack("<I", 0x10 + len(payload)) + struct.pack("<Q", 0x1234) + struct.pack("<i", len(payload))
    return b"SYGN" + bytes(4) + b"STRM" + bytes(4) + struct.pack("<II", 0, 0x14 + len(payload)) + strm + payload


def sygnus_reference(payload):
    """Decoder written from the game's ADPCMDecode (step table rows of 2^(row/2))."""
    m0 = [1, 3.25, 5.5, 8, 11, 15, 21, 31]
    m1 = [round(m * math.sqrt(2) * 256) / 256 for m in m0]
    adj = [-1, 0, 0, 0, 0, 1, 1, 4]
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    ch, rate, ns, spb = struct.unpack_from("<IIII", payload, 0)
    out = [[0] * ch for _ in range(ns)]
    pos, done = 16, 0
    while done < ns:
        n = min(spb, ns - done)
        bs = ((n - 1) >> 1) + 9 if n >= 3 else 4 * n
        for c in range(ch):
            blk = payload[pos:pos + bs]
            pos += bs
            p2 = struct.unpack_from("<f", blk, 0)[0]
            p1 = struct.unpack_from("<f", blk, 4)[0] if n >= 2 else 0.0
            row = min(blk[8], 31) if n >= 3 else 0
            for t in range(n):
                v = p2 if t == 0 else p1
                if t >= 2:
                    b = blk[9 + (t - 2) // 2]
                    code = b >> 4 if (t - 2) & 1 else b & 15
                    mag = code - 8 if code >= 8 else 7 - code
                    step = f32((m1 if row & 1 else m0)[mag] * 2.0 ** (row // 2 - 18))
                    row = max(0, min(31, row + adj[mag]))
                    v = f32(f32((step if code >= 8 else -step) + f32(2 * p1)) - p2)
                    p2, p1 = p1, v
                x = f32(v * 32767.0)
                out[done + t][c] = 32767 if x >= 32767 else -32768 if x <= -32768 else int(round(x))
        done += n
    return b"".join(struct.pack("<" + "h" * ch, *fr) for fr in out)


SYGNUS_ENC = base64.b64decode(
    "YKx93dO2BPMT8kyQPrlG9GgcZAybH2ogCglHhmkVdrAncOEw8fgFwnp8OtH+CKpKqsck1XMqTATTXyvKsSBeRH+IoZMSEJN55c1vM9D7fIVbFclK8psqeqnv6qQNMmyw93FSwxh5rHUcLGbfXZaxT72+uwZpHLlJe/87Z15PSShG2rdmUwvFcykiIrOjIc6+/D671KsTCwuEX9endORRLhKoUWhhXzyACXZKGIXnMWJz2rkHflPsixXpnBixBin03WJ3DG6wWHHhYFfORDUk/k4r1tcK3EWrYCCjKv4ORLlltN57bix+p/P164WuzJdMxthFK1330xtf+fVPVcqqtF6yoHCdeericXZwEAJ9ccqpUVIDTPF87fKr3r/pNRC+ZmGc3+ouem6+WXJdcEp6vyluk0A5YHX/qJBojROaUTBBpSrCTy2ByBucMeZWao9FpxKkqWMa83NCAOCpzBU8g4kY3PGwuZCMRq6gGzfhyLtuXFgaI5zuaE5OgW/o+pnBsHX/Z8lfLqTeNVipY07cO/WHFB1ompwZQykxQoI2qMKZC5LRSjdX2WZ4Vwy5SKvFN62C0nYkh2ge+rPgG0mLyXS0jTrlcvRV9aMLrftObLhrQiT4fFds3v6IkL7IZyOiZrMWUjhs+dGP7IPp+psulGOO4AkE8Flp1xlyuRu1Zchzp+4rnCm9TKdQbVAqnfD9MgqbGVBNYsXRw3tWp9OPc3G6vDsyOX+ii6ze1ZAcJBO4hXPKm7hCV7XrTveJKMzhnzYBr+BhoPInf7ViWYPsG7JDLjCJ8XQ1+q+qQm+W6zG8IqFfY+HtvjBXUjHovitgVIVPCE8vuvv++GPtFP2EFAMLhhJCejJ2CiN51q360rmyoqntxqHVgzjyuj+9zkbuPhAZOlHluSF7zOEl4mkgrGG/2w4=")


def build_sygnus(outdir):
    plain = sygnus_stream(sygnus_adpcm(2, 600))
    open(os.path.join(outdir, "plain.soundstream"), "wb").write(plain)
    assert len(SYGNUS_ENC) == len(plain)
    open(os.path.join(outdir, "enc.soundstream"), "wb").write(SYGNUS_ENC)
    open(os.path.join(outdir, "plain.pcm"), "wb").write(sygnus_reference(plain[0x2c:]))
    src = sygnus_adpcm(1, 1029, seed=11)
    bank = b"ASRC" + struct.pack("<II", 0x48 + len(src), 1) + bytes(0x40) + struct.pack("<I", len(src)) + src
    rec = (struct.pack("<I", 0x1c) + b"PSND" + bytes(0x18) + b"SYGN" + bytes(4) + b"ASRC" + bytes(8)
           + struct.pack("<I", len(bank)) + bank)
    open(os.path.join(outdir, "sfx.psnd"), "wb").write(rec)
    open(os.path.join(outdir, "sfx.pcm"), "wb").write(sygnus_reference(src))


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
    build_sygnus(outdir)


if __name__ == "__main__":
    main(sys.argv[1])
