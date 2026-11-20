"""Synthetic Call of Duty WaW (Wii) geometry stream. Writes <dir>/t.xgm; "check <dir>" verifies t.xgm.d/model.obj."""
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        o = open(os.path.join(sys.argv[2], "t.xgm.d", "model.obj")).read().split("\n")
        assert o[0] == "o draw_0", o
        v = [l for l in o if l.startswith("v ")]
        assert v == ["v 0 0 0", "v 1 0 0", "v 1 1 0", "v 0 1 0"], v
        assert [l for l in o if l.startswith("vn ")] == ["vn 0 0 1"], o
        assert [l for l in o if l.startswith("vt ")] == ["vt 0 1", "vt 1 1", "vt 1 0", "vt 0 0"], o
        f = [l for l in o if l.startswith("f ")]
        assert f == ["f 1/1/1 2/2/1 3/3/1", "f 1/1/1 3/3/1 4/4/1"], f
        print("PASS")
        sys.exit(0)
    B = 0x100
    buf = bytearray(B + 0x120)
    fx = []

    def decl(at, count, stride, gqr, name_off, data_off):
        struct.pack_into(">HHHBBH", buf, at, 1, count, 0, stride, 1, gqr)
        struct.pack_into(">II", buf, at + 0x18, 0xFFBAABFF, 0xFFBAABFF)
        fx.append((at + 0x18, name_off))
        fx.append((at + 0x1C, data_off))

    decl(B + 0x00, 4, 6, 0x8707, B + 0x60, B + 0x90)
    decl(B + 0x20, 1, 6, 0x8E07, B + 0x70, B + 0xA8)
    decl(B + 0x40, 4, 4, 0x4E07, B + 0x80, B + 0xB0)
    buf[B + 0x60:B + 0x69] = b"Position\0"
    buf[B + 0x70:B + 0x77] = b"Normal\0"
    buf[B + 0x80:B + 0x89] = b"TexCoord\0"
    for i, p in enumerate([(0, 0, 0), (128, 0, 0), (128, 128, 0), (0, 128, 0)]):
        struct.pack_into(">3h", buf, B + 0x90 + 6 * i, *p)
    struct.pack_into(">3h", buf, B + 0xA8, 0, 0, 16384)
    for i, t in enumerate([(0, 0), (16384, 0), (16384, 16384), (0, 16384)]):
        struct.pack_into(">2h", buf, B + 0xB0 + 4 * i, *t)
    S = B + 0xC0
    dl = B + 0x100
    dlbytes = bytes([0x90, 0, 6]) + bytes([0, 0, 0, 1, 0, 1, 2, 0, 2, 0, 0, 0, 2, 0, 2, 3, 0, 3])
    struct.pack_into(">II", buf, S, 3, 0x18)
    struct.pack_into(">IIII", buf, S + 8, 0xFFBAABFF, 0, 0xFFBAABFF, len(dlbytes))
    at = B + 0xE0
    fx.append((S + 8, at))
    fx.append((S + 16, dl))
    for k, (aid, st, arr) in enumerate([(9, 6, B + 0x90), (10, 6, B + 0xA8), (13, 4, B + 0xB0)]):
        buf[at + 8 * k:at + 8 * k + 4] = bytes([aid, 0, 0, st])
        struct.pack_into(">I", buf, at + 8 * k + 4, 0xFFBAABFF)
        fx.append((at + 8 * k + 4, arr))
    buf[dl:dl + len(dlbytes)] = dlbytes
    hdr = struct.pack(">HHI", 0x4321, 1, len(fx)) + b"".join(struct.pack(">II", a, b) for a, b in fx)
    assert len(hdr) <= B
    buf[:len(hdr)] = hdr
    open(os.path.join(sys.argv[1], "t.xgm"), "wb").write(bytes(buf))
