import struct, sys, os
d = sys.argv[1]
s3 = 0x100
buf = bytearray(0x1000)
struct.pack_into("<II", buf, 0, 0xb060ffed, 0x7ff)
for i in range(11):
    struct.pack_into("<III", buf, 8 + 12 * i, s3 if i == 3 else 0x90, 1, 0xffffffff)
struct.pack_into("<i", buf, s3 + 8, 2)  # 2 points per line
for i in range(19):
    po = 0x200 + i * 0x40
    struct.pack_into("<II", buf, s3 + 0x5c + 8 * i, po, 0)
    struct.pack_into("<8f", buf, s3 + po, 1.0 + i, 0, 2.0, 0, 0, 0, 0, 0)
    struct.pack_into("<8f", buf, s3 + po + 0x20, 1.0 + i, 0, 5.0, 0, 0, 0, 0, 0)
open(os.path.join(d, "t.btd"), "wb").write(bytes(buf))
