"""Write minimal Phive 2.2 static compound bodies (Mario Kart World .bphsc).

    mk_bphsc.py OUTDIR

OUTDIR/c.bphsc  Havok tag file with an hknpCompoundShape holding one instance (identity rotation, translation
                (100, 20, -50), unit scale) of a capsule from (0, 0, 0) to (0, 10, 0), radius 1, plus one free slot
OUTDIR/w.bphsc  2x2 water grid, box size (100, 200, 100) centred at (50, 0, 50); heights 0, 1000, 2000, 65535
"""
import os, struct, sys


def section(tag, body):
    return struct.pack('>I', (8 + len(body)) | 0x40000000) + tag + body


def varint(v):
    assert v < 0x80
    return bytes((v,))


def header(offsets, size):
    offs = list(offsets) + [size] * (14 - len(offsets))
    return b'Phive\0\x02\x02\xff\xfe\x02\xa0' + struct.pack('<14I', *offs)


def tagfile():
    data = bytearray(0x200)
    # compound at 0: instance array at 0x100 (relative to field 0x48), 2 instances
    struct.pack_into('<i', data, 0x48, 0x100 - 0x48)
    struct.pack_into('<I', data, 0x50, 2)
    data[0x18] = 9
    # instance 0 at 0x100: quaternion (0, 0, 0, 1), translation, scale, relptr to capsule at 0x180
    struct.pack_into('<4f4f4f', data, 0x100, 0, 0, 0, 1, 100, 20, -50, 0, 1, 1, 1, 0)
    struct.pack_into('<q', data, 0x130, 0x180 - 0x130)
    # instance 1: free slot (relptr 0)
    # capsule at 0x180: radius at +0x20, vertex relarray at +0x40 -> 0x1e0 (2 x hkFloat3)
    data[0x180 + 0x18] = 2
    struct.pack_into('<f', data, 0x180 + 0x20, 1.0)
    struct.pack_into('<iI', data, 0x180 + 0x40, 0x1e0 - (0x180 + 0x40), 2)
    struct.pack_into('<6f', data, 0x1e0, 0, 0, 0, 0, 10, 0)
    names = [b'hknpCompoundShape', b'hkFreeListArrayElement', b'hknpCapsuleShape', b'hkFloat3']
    tst = b''.join(n + b'\0' for n in names)
    tna = varint(len(names) + 1) + b''.join(varint(i) + varint(0) for i in range(len(names)))
    items = [(0, 0, 0), (1, 0, 1), (2, 0x100, 2), (3, 0x180, 1), (4, 0x1e0, 2)]
    item = b''.join(struct.pack('<III', (0x10 << 24 | t) if t else 0, o, c) for t, o, c in items)
    body = section(b'SDKV', b'20230200') + section(b'DATA', bytes(data))
    body += section(b'TYPE', section(b'TST1', tst + b'\0' * (-len(tst) % 4)) + section(b'TNA1', tna + b'\0' * (-len(tna) % 4)))
    body += section(b'INDX', section(b'ITEM', item))
    return struct.pack('>I', 8 + len(body)) + b'TAG0' + body


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    tag = tagfile()
    at = 0x50
    size = at + len(tag)
    with open(os.path.join(outdir, 'c.bphsc'), 'wb') as f:
        f.write(header([at] * 9, size).ljust(at, b'\0') + tag)

    w = h = 2
    hdr = struct.pack('<6f5I', 100, 200, 100, 50, 0, 50, w, h, w * h * 8, 0, w * h * 2)
    grid = b''.join(struct.pack('<4H', v, 0x7fff, 0x7fff, 1) for v in (0, 1000, 2000, 65535))
    hdr_at = 0x50
    grid_at = hdr_at + len(hdr)
    size = grid_at + len(grid)
    with open(os.path.join(outdir, 'w.bphsc'), 'wb') as f:
        f.write(header([0x50, hdr_at, hdr_at, hdr_at, hdr_at, hdr_at, hdr_at, hdr_at, grid_at], size).ljust(hdr_at, b'\0') + hdr + grid)


if __name__ == '__main__':
    main(sys.argv[1])
