"""Synthetic IO Interactive Glacier ".TEX" bank inside an IOI-style scene ".zip" (Mini Ninjas).

mk_glacier_tex.py <dir>        writes <dir>/t.zip (deflated member with 4 trailing bytes, "enuR" trailer after the EOCD)
mk_glacier_tex.py check <dir>  verifies the PNGs written by "wszst xx t.zip"
"""
import os
import struct
import sys
import zlib


def entry(fmt, idx, w, h, name, mips, extra=b""):
    body = fmt + fmt + struct.pack(">IHHIIfI", idx, w, h, len(mips), 0, 1.0, 0) + name + b"\0"
    for m in mips:
        body += struct.pack(">I", len(m)) + m
    body += extra
    return struct.pack(">I", 4 + len(body)) + body


def pad16(b):
    return b + b"\0" * (-len(b) % 16)


def make(d):
    # 4x2 RGB5A3: opaque red, opaque green, then translucent blue (alpha 3/7) and white
    t1 = struct.pack(">8H", 0xFC00, 0x83E0, 0x801F, 0xFFFF, 0x300F, 0x7FFF, 0xFC00, 0xFC00)
    # 4x4 DXT1, colour0 = pure red, colour1 = pure blue (big-endian words), indices: row 0 = 0, rest = 1
    t2 = struct.pack(">HH", 0xF800, 0x001F) + bytes([0x00, 0x55, 0x55, 0x55])
    # 2x2 PALN with a 2-entry {A,B,G,R} palette
    t3 = bytes([0, 1, 1, 0])
    pal = struct.pack(">I", 2) + bytes([0xFF, 0x00, 0x00, 0xFF, 0x80, 0x00, 0xFF, 0x00])
    data = b""
    data += pad16(entry(b"15A3", 0x80, 4, 2, b"Locations/Test/Red", [t1]))
    data += pad16(struct.pack(">IIII", 3, 0x80, 0x81, 0x82))  # id group
    data += pad16(entry(b"DXT1", 0x81, 4, 4, b"", [t2, t2[:8]]))
    data += pad16(entry(b"PALN", 0x82, 2, 2, b"Locations/Test/Red", [t3], pal))  # duplicate name
    table = 0x10 + len(data)
    tex = struct.pack(">IIII", table, table + 0x40, 0, 0x04000000) + data + b"\0" * 0x40

    name = b"Scenes/t.TEX"
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    stream = comp.compress(tex) + comp.flush() + b"\xde\xad\xbe\xef"  # IOI leaves 4 bytes after the stream
    crc = zlib.crc32(tex)
    local = struct.pack("<4sHHHHHIIIHH", b"PK\3\4", 20, 0, 8, 0, 0, crc, len(stream), len(tex), len(name), 0) + name
    cd = struct.pack("<4sHHHHHHIIIHHHHHII", b"PK\1\2", 20, 20, 0, 8, 0, 0, crc, len(stream), len(tex), len(name), 0, 0, 0, 0, 0, 0) + name
    off = len(local) + len(stream)
    eocd = struct.pack("<4sHHHHIIH", b"PK\5\6", 0, 0, 1, 1, len(cd), off, 0)
    trailer = b"enuR" + b"\0" * 12 + struct.pack("<I", off) + b"\0\0"
    open(os.path.join(d, "t.zip"), "wb").write(local + stream + cd + eocd + trailer)


def read_png(path):
    b = open(path, "rb").read()
    assert b[:8] == b"\x89PNG\r\n\x1a\n"
    p, idat, w, h = 8, b"", 0, 0
    while p < len(b):
        n, t = struct.unpack(">I4s", b[p:p + 8])
        c = b[p + 8:p + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", c[:10])
            assert depth == 8 and ctype == 6
        elif t == b"IDAT":
            idat += c
        p += 12 + n
    raw = zlib.decompress(idat)
    stride = 4 * w
    rows, prev = [], bytes(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            up = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + up) & 255
            elif f == 3:
                line[i] = (line[i] + (a + up) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = rows[-1]
    return w, h, [[tuple(r[4 * x:4 * x + 4]) for x in range(w)] for r in rows]


def check(d):
    base = os.path.join(d, "t.zip.d", "Scenes", "t.TEX.d")
    w, h, px = read_png(os.path.join(base, "Locations", "Test", "Red.png"))
    assert (w, h) == (4, 2), (w, h)
    assert px[0][:4] == [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255)], px[0]
    assert px[1][0] == (0, 0, 255, 109), px[1][0]
    w, h, px = read_png(os.path.join(base, "tex_129.png"))
    assert (w, h) == (4, 4) and px[0][0] == (255, 0, 0, 255) and px[1][3] == (0, 0, 255, 255), px
    w, h, px = read_png(os.path.join(base, "Locations", "Test", "Red_130.png"))
    assert px == [[(255, 0, 0, 255), (0, 255, 0, 128)], [(0, 255, 0, 128), (255, 0, 0, 255)]], px
    print("PASS")


if __name__ == "__main__":
    if sys.argv[1] == "check":
        check(sys.argv[2])
    else:
        make(sys.argv[1])
