import struct, sys, os
d = sys.argv[1]
def c(tag, body): return tag + struct.pack(">I", len(body)) + body
# NJTL: body {array offset 8, count 1}, entry {name offset 20, 0, 0}, name
names = b"TEXA\0"
body = struct.pack(">II", 8, 1) + struct.pack(">III", 20, 0, 0) + names
njtl = c(b"LTJN", body + bytes(-len(body) % 4))
# object data: node at 0, model at 0x34, vlist, plist
node = struct.pack(">II3f3i3fII", 6, 0x34, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0)
model_off = 0x34
vlist_off = 0x34 + 24
verts = struct.pack(">3f3f", 0, 0, 0, 0, 1, 0) + struct.pack(">3f3f", 10, 0, 0, 0, 1, 0) + struct.pack(">3f3f", 0, 10, 0, 0, 1, 0)
vchunk = bytes([0, 41]) + struct.pack(">HHH", 1 + len(verts) // 4, 0, 3) + verts
vend = bytes([0, 0xff, 0, 0])
plist_off = vlist_off + len(vchunk) + len(vend)
tiny = bytes([0, 8]) + struct.pack(">H", 0)
strip = bytes([0, 65]) + struct.pack(">HHH", 1 + 1 + 3 * 3, 1, 3) + b"".join(struct.pack(">Hhh", i, u, v) for i, (u, v) in enumerate([(0, 0), (256, 0), (0, 256)]))
pend = bytes([0, 0xff, 0, 0])
model = struct.pack(">II4f", vlist_off, plist_off, 0, 0, 0, 20.0)
data = node + model + vchunk + vend + tiny + strip + pend
assert 0x34 + len(model) == vlist_off
open(os.path.join(d, "t.nj"), "wb").write(njtl + c(b"MCJN", data))
