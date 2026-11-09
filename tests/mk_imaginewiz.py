import struct, sys, os
d = sys.argv[1]
R = 0x60
pos = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
uv = [(0, 1), (1, 1), (1, 0), (0, 0)]
posb = b"".join(struct.pack(">3f", *p) for p in pos)
uvb = b"".join(struct.pack(">2f", *u) for u in uv)
rec = bytearray(0x100)
struct.pack_into(">4I", rec, 0, 0x100, 0, 0, 0x100 + len(posb) + 4)
rec[0x2c] = 2  # pos u8 index
rec[0x2e] = 2  # uv u8 index
struct.pack_into(">H", rec, 0x3a, len(pos))
dl = bytes([0x80, 0, 0, 0x90, 0, 6]) + bytes([0, 0, 1, 1, 2, 2, 0, 0, 2, 2, 3, 3]) + bytes(10)
rec[0x80:0x80 + len(dl)] = dl
body = bytes(rec[:0xfc]) + struct.pack(">I", len(posb)) + posb + struct.pack(">I", len(uvb)) + uvb
body = body[:0x100 - 4] + body[0x100 - 4:]
blob = R + len(body)
head = bytearray(R)
struct.pack_into(">II", head, 0, blob, 0x4c)
struct.pack_into(">II", head, 0x4c, 1, R)
tex_off = blob + 8 + 4 + 0x30
tbl = struct.pack(">III", 1, 1, blob + 12) + struct.pack(">IHHI", tex_off, 4, 4, 4) + bytes(0x30 - 12)
pix = struct.pack(">16H", *([0xf800] * 16))
open(os.path.join(d, "t.wiz"), "wb").write(bytes(head) + body + tbl + pix)
