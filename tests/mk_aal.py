"""Write minimal AAL audio data files (Switch 2 layouts, little endian).

    mk_aal.py OUTDIR

OUTDIR/t.bars   BARS with one asset "snd": AMTA 5.0 (peak 0.5, envelope [[0, 0], [8, 0.25]], marker "Loop" at 4,
                tag "Tag", parameter "Grid:" = 4) + PCM16 BWAV (1 ch, 48000 Hz, samples 0, 1000, ..., 7000)
OUTDIR/t.baatn  attenuator: volume curve "v" (RollOff), the others "" (UnitDistance), occlusion on
OUTDIR/t.baroc  roll-off curve v2: model 2, ref 1, max 60, factor 1, start 0, increase on, culling start 5
OUTDIR/t.baacl  culling v2: distance 22, margin 12, priority down on
OUTDIR/t.bagst  group settings: tree Root -> {A, B}, three groups, string table
OUTDIR/t.dapg   2x2 probe grid; cell (1, 0) has no probe, the others records 1..3 with f0 = 10 * record
"""
import os, struct, sys, zlib


def amta():
    name = b'snd'
    strings = name + b'\0' + b'Loop\0Tag\0Grid:\0'
    b = bytearray(0x34)
    b[0:8] = b'AMTA\xff\xfe\x00\x05'
    b[0x30:0x34] = bytes((1, 1, 0, 4))
    data = len(b)
    b += struct.pack('<I4f', 0x6f, 0.5, 0.25, -12.0, -14.0)
    b += struct.pack('<HH', 2, 2) + struct.pack('<fI', 0.0, 0) + struct.pack('<fI', 0.25, 8)
    b += struct.pack('<f', 1.5)
    mark = len(b)
    b += struct.pack('<I', 1) + struct.pack('<IIII', 7, 0, 4, 0)
    tags = len(b)
    b += struct.pack('<II', 1, 0)
    prm = len(b)
    b += struct.pack('<III', 1, 0, 4)
    s = len(b)
    b += strings
    b += b'\0' * (-len(b) % 4)

    def rel(field, at):
        struct.pack_into('<I', b, field, at - field)
    struct.pack_into('<IIIIII', b, 0x10, data, mark, 0, tags, prm, 0)
    rel(0x24, s)
    rel(mark + 8, s + 4)
    rel(tags + 4, s + 9)
    rel(prm + 4, s + 13)
    struct.pack_into('<I', b, 0x28, zlib.crc32(name))
    struct.pack_into('<I', b, 8, len(b))
    return bytes(b)


def bwav():
    ns = 8
    rec = bytearray(0x4c)
    struct.pack_into('<HHIII', rec, 0, 0, 0, 48000, ns, ns)
    struct.pack_into('<I', rec, 0x34, 0x60)
    struct.pack_into('<I', rec, 0x38, 0x60)
    struct.pack_into('<iI', rec, 0x3c, -1, 0)
    head = b'BWAV\xff\xfe' + struct.pack('<HIHH', 1, 0, 0, 1) + bytes(rec)
    return head.ljust(0x60, b'\0') + struct.pack('<8h', *range(0, 8000, 1000))


def bars():
    a, w = amta(), bwav()
    amta_off = 0x40
    bwav_off = (amta_off + len(a) + 63) & ~63
    body = bytearray(bwav_off + len(w))
    body[0:16] = b'BARS' + struct.pack('<I', len(body)) + b'\xff\xfe' + struct.pack('<HI', 0x0102, 1)
    struct.pack_into('<III', body, 16, zlib.crc32(b'snd'), amta_off, bwav_off)
    body[amta_off:amta_off + len(a)] = a
    body[bwav_off:] = w
    return bytes(body)


def aatn():
    strings = b'v\0\0'
    b = bytearray(0x44)
    b[0:8] = b'AATN\xff\xfe\x01\x00'
    struct.pack_into('<I', b, 8, 0x44)
    for i in range(5):
        struct.pack_into('<II', b, 0x0c + 8 * i, 0 if i == 0 else 2, 0 if i == 0 else 2)
    struct.pack_into('<IIII', b, 0x34, 2, 2, 0, 1)
    return bytes(b) + strings + b'\0'


def agst():
    strings = b'Default\0Root\0A\0B\0'
    tree = struct.pack('<III', 0x24, 0, 2) + struct.pack('<III', 12, 1, 0) + struct.pack('<III', 12, 2, 0)
    tree = b'TREE' + struct.pack('<II', 8 + 4 + len(tree), 0) + tree
    tree = tree[:4] + struct.pack('<I', len(tree)) + tree[8:]
    hdr = 0x18
    grp_at = hdr + len(tree)
    groups = [struct.pack('<II', 8, 7), struct.pack('<II', 13, 8), struct.pack('<II', 15, 9)]
    table = 12 + 4 * len(groups)
    offs, at = [], grp_at + table
    for g in groups:
        offs.append(at)
        at += len(g)
    grp = b'GRP ' + struct.pack('<II', table + sum(map(len, groups)), len(groups)) + struct.pack('<%dI' % len(offs), *offs) + b''.join(groups)
    strg_at = grp_at + len(grp)
    strg = b'STRG' + struct.pack('<I', 8 + len(strings)) + strings
    total = strg_at + len(strg)
    head = b'AGST\xff\xfe' + struct.pack('<HIIII', 10, total, hdr, grp_at, strg_at)
    return head + tree + grp + strg


def dapg():
    n = 2
    idx = [1, 0, 2, 3]
    recs = b''
    for r in range(1, 4):
        rec = struct.pack('<2f2H4f', 10.0 * r, 2.0, 3, 4, 5.0, 6.0, 7.0, 8.0)
        rec += b''.join(struct.pack('<fI', float(k), 0x031e1c00 + k) for k in range(12))
        recs += rec
    return struct.pack('<I', n) + struct.pack('<4H', *idx) + recs


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    files = {
        't.bars': bars(),
        't.baatn': aatn(),
        't.baroc': b'AROC\xff\xfe\x02\x00' + struct.pack('<I4fIf', 2, 1.0, 60.0, 1.0, 0.0, 1, 5.0),
        't.baacl': b'AACL\xff\xfe\x02\x00' + struct.pack('<2fB3x', 22.0, 12.0, 1),
        't.bagst': agst(),
        't.dapg': dapg(),
    }
    for name, data in files.items():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main(sys.argv[1])
