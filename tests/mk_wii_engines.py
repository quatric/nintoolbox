"""Write minimal Planet-G and Eko Software engine files plus look-alikes that must be rejected.

    mk_wii_engines.py OUTDIR

Planet-G (Bermuda Triangle, Rock Blast, Veggy World, ...):
  t.MWT    GDATAVERSION envelope around an 8x4 I8 TPL (pixel (x, y) = 8 * (8y + x)), flags 1 -> 4-byte hit mask
  t.MWG    PLANETG_SPRITECOLLECTION version 2
  t.PKI    IMAGE_WII_COMPACT pack with one 4-byte entry "DATA\\A.MWT"
  t.pgf    "Tahoma" 16 pt, 96 glyphs of 8x12 in a 256x64 atlas; two-colour run tail with a 2x2 ink square at (10, 14)
  m.pgf    same, but with ink outside every glyph rectangle (a multi-colour atlas read as two colours): no PNG
Eko Software (Aqua Panic!, SPRay, Cyberbike, ...):
  t.rck    RKET header with the 0xAF fill and a 4-byte stored payload
  t.mat    MATF with one "MAT " and one "TEX " chunk
  t.mb2    BNAM with the names "Jaw" and "Root"
  t.vis    two objects; the first block {1, 12, {0, 12, ...}} also passes the Treasure MRG table check
  t.lit    two lights; light 1 at (1, 2, 3) with colour (255, 128, 0)
Look-alikes (must not get one of the names above):
  n.lit    "VBIN" header (Kevin VanDam Big Bass Challenge)
  n.mat    $1,000,000 Pyramid style 02 00 00 02 record table
  n.MWG    length 1 + "P" (accepted by the old prefix check)
  n.pgf    length-prefixed name and point size, then no glyph tables
"""
import os, struct, sys


def mwt():
    # big-endian TPL: one 8x4 I8 image, pixel (x, y) = 8 * (8y + x)
    tpl = bytes.fromhex('0020af30') + struct.pack('>III', 1, 0x0c, 0x14) + bytes(4)
    tpl += struct.pack('>HHII', 4, 8, 1, 0x40) + bytes(0x24 - 12)
    tpl = tpl.ljust(0x40, b'\0') + bytes(8 * i for i in range(32))
    hdr = b'GDATAVERSION' + struct.pack('<5I', 2, len(tpl), 8, 4, 1) + bytes(32)
    return hdr + tpl + bytes(((8 + 7) // 8) * 4)


def planetg():
    t = b'PLANETG_SPRITECOLLECTION'
    return struct.pack('<I', len(t)) + t + struct.pack('<II', 2, 1) + struct.pack('<I', 5) + b'BTN_N'


def pki():
    name = b'DATA\\A.MWT'
    table = b'IMAGE_WII_COMPACT_FILE_VERSION_1' + struct.pack('<I', 1) + struct.pack('<I', len(name)) + name
    at = len(table) + 8
    return table + struct.pack('<II', 4, at) + b'DATA'


def esc(n):
    return bytes([n]) if 0 < n < 256 else b'\0' + struct.pack('<I', n)


def pgf(ink_at=(10, 14)):
    # Runs alternate background / ink, starting with background; 0 + u32 is a long run.
    x, y = ink_at
    first = y * 256 + x
    tail = esc(first) + esc(2) + esc(254) + esc(2) + esc(256 * 64 - first - 258)
    return pgf_header() + tail


def pgf_header():
    name = b'Tahoma'
    xs, ys, ws, hs = [0] * 256, [0] * 256, [0] * 256, [0] * 256
    for i, c in enumerate(range(32, 128)):
        xs[c], ys[c], ws[c], hs[c] = (i % 32) * 8, (i // 32) * 12, 8, 12
    body = struct.pack('<I', len(name)) + name + struct.pack('<I', 16) + struct.pack('<4I', 1, 1, 3, 3)
    return body + struct.pack('<1024I', *(xs + ys + ws + hs)) + struct.pack('<II', 256, 64)


def rket():
    payload = b'EKO\0'
    hdr = b'RKET' + struct.pack('<IIHHII', 0, 0x12345678, 0x100, 0, 0, len(payload)) + bytes(12)
    hdr += struct.pack('<I', 3) + bytes(8) + b'\xaf' * 8
    assert len(hdr) == 56
    return hdr + payload


def matf():
    return b'MATF' + struct.pack('<III', 8, 1, 1) + b'MAT ' + struct.pack('<I', 72) + bytes(72) + b'TEX ' + struct.pack('<I', 8) + bytes(8)


def bnam():
    names = b''.join(struct.pack('<I', len(n) + 1) + n + b'\0' for n in (b'Jaw', b'Root'))
    body = struct.pack('<II', 3, 1) + b'\0' + names
    return b'BNAM' + struct.pack('<I', len(body)) + body


def vis():
    b0 = struct.pack('<II', 1, 12) + struct.pack('<II', 0, 12) + b''.join(struct.pack('<II', i, 0) for i in range(12))
    b1 = struct.pack('<II', 2, 3) + struct.pack('<II', 1, 1) + struct.pack('<II', 1, 1) + struct.pack('<II', 0, 2)
    b1 += struct.pack('<II', 0, 0) + struct.pack('<II', 1, 0)
    return b0 + b1


def lit():
    lights = struct.pack('<II3f3ff', 0x01000001, 0, 1, 2, 3, 255, 128, 0, 0)
    lights += struct.pack('<II3f3ff', 0x01000002, 0, -4, 5, -6, 10, 20, 30, 1)
    return struct.pack('<II', 0x20031126, 2) + lights + bytes(4)


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    files = {
        't.MWT': mwt(), 't.MWG': planetg(), 't.PKI': pki(), 't.pgf': pgf(), 'm.pgf': pgf((10, 50)),
        't.rck': rket(), 't.mat': matf(), 't.mb2': bnam(), 't.vis': vis(), 't.lit': lit(),
        'n.lit': b'VBIN' + struct.pack('<I', 0x10000) + bytes(4) + b'DAEH' + bytes(40),
        'n.mat': bytes.fromhex('02000002') + bytes(12) + bytes(range(64)),
        'n.MWG': struct.pack('<I', 1) + b'P' + bytes(11),
        'n.pgf': struct.pack('<I', 6) + b'Tahoma' + struct.pack('<I', 16) + bytes(40),
    }
    for name, data in files.items():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main(sys.argv[1])
