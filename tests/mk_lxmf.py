"""Synthetic Spectrobes .LXMF bundle. Writes <dir>/t.LXMF; "check <dir>" verifies the members under t.LXMF.d."""
import os
import struct
import sys

M = [(b"DATA/A/first.bin", b"hello world"), (b"DATA/B/second.dat", bytes(range(40)))]

if __name__ == "__main__":
    if sys.argv[1] == "check":
        d = os.path.join(sys.argv[2], "t.LXMF.d")
        for n, data in M:
            assert open(os.path.join(d, n.decode()), "rb").read() == data, n
        print("PASS")
        sys.exit(0)
    n = len(M)
    pos = 0x40 + 0x20 * n
    offs = []
    body = b""
    for _, data in M:
        offs.append(pos + len(body))
        body += data + b"\0" * (-len(data) % 0x20)
    npos = pos + len(body)
    names = b"".join(nm.ljust(0x40, b"\0") for nm, _ in M)
    out = b"FMXL\x01\x01\x01\x00" + struct.pack(">II", n, 0x20) + b"\0" * 0x30
    for i, (nm, data) in enumerate(M):
        out += struct.pack(">8I", 0, offs[i], 0, (len(data) + 0x1F) & ~0x1F, 0, npos + 0x40 * i, 0x40, len(data))
    out += body + names
    open(os.path.join(sys.argv[1], "t.LXMF"), "wb").write(out)
