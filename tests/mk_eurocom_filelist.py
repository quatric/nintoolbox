"""Synthetic Eurocom Filelist pack (GoldenEye 007 Wii). Writes <dir>/Filelist.bin and Filelist.000; "check <dir>" verifies Filelist.bin.d/0000.bin and 0001.bin.
"named <dir>" also writes the obfuscated name table (byte j of string i stored as plain + j + 0xea + i) and "checknamed <dir>" verifies the named files."""
import os
import struct
import sys

A = b"first file\n" * 3
B = b"second\n" * 9
if __name__ == "__main__":
    d = sys.argv[2] if sys.argv[1] in ("check", "named", "checknamed") else sys.argv[1]
    if sys.argv[1] == "checknamed":
        o = os.path.join(d, "Filelist.bin.d")
        assert open(os.path.join(o, "bondx.evn"), "rb").read() == A
        assert open(os.path.join(o, "sub", "b.edb"), "rb").read() == B
        print("PASS")
        sys.exit(0)
    if sys.argv[1] == "check":
        o = os.path.join(d, "Filelist.bin.d")
        assert open(os.path.join(o, "0000.bin"), "rb").read() == A
        assert open(os.path.join(o, "0001.bin"), "rb").read() == B
        assert sorted(os.listdir(o)) == ["0000.bin", "0001.bin"], os.listdir(o)
        print("PASS")
        sys.exit(0)
    data = bytearray(0x1000)
    data[0:len(A)] = A
    data[0x800:0x800 + len(B)] = B
    recs = struct.pack(">7I", 0, 0, len(A), 0x01B500A4, 0x107, 0x20000009, 1) + struct.pack(">7I", 0x800, 0, len(B), 0x01B500A4, 0x107, 0x20000009, 1)
    hdr = struct.pack(">6I", 7, 0, 3, 0x10000, 0x28 + len(recs), 5) + b"\0" * 16
    idx = bytearray(hdr + recs + b"\0" * 8)
    if sys.argv[1] == "named":
        names = [b"x:\\bondx\\binary\\bondx.evn", b"x:\\bondx\\binary\\_bin_wii\\sub\\b.edb", b"x:\\bondx\\binary\\_bin_wii\\extra.tpl"]
        tab = len(idx)
        struct.pack_into(">I", idx, 0x10, tab - 0x10)
        strs = [bytes((c + j + 0xEA + i) & 0xFF for j, c in enumerate(n + b"\0")) for i, n in enumerate(names)]
        pos = tab + 4 * len(names)
        for i, e in enumerate(strs):
            idx += struct.pack(">I", pos - (tab + 4 * i))
            pos += len(e)
        idx += b"".join(strs)
    struct.pack_into(">I", idx, 4, len(idx))
    open(os.path.join(d, "Filelist.bin"), "wb").write(bytes(idx))
    open(os.path.join(d, "Filelist.000"), "wb").write(bytes(data))
