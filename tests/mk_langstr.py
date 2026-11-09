import struct, sys, os
d = sys.argv[1]
strs = ["", "Hello %s", "Zwei\nZeilen ä"]
enc = [s.encode("utf-16-be") for s in strs]
n = len(enc)
off = 12 + 8 * n
tab = b""
data = b""
for e in enc:
    tab += struct.pack(">II", off + len(data), len(e))
    data += e
open(os.path.join(d, "t.str"), "wb").write(struct.pack(">I", 0x43f93a00) + bytes([1, 0, 0, 0]) + struct.pack(">I", n) + tab + data)
