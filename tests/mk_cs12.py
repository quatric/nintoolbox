import struct, sys, os
d = sys.argv[1]
page = bytearray(0x1000 * 4)
page[0:4] = b"CS12"
c = 0x1000
frames = bytes([0x12, 1, 2, 3, 4, 5, 6, 7]) * 4
blk = bytearray(0x800)
blk[0:4] = b"CHNK"
struct.pack_into("<III", blk, 4, 0x800, 1, 3)
blk[0x10:0x14] = b"SGSB"
blk[0x20:0x24] = b"MDWI"
struct.pack_into("<I", blk, 0x44, len(frames))
struct.pack_into("<H", blk, 0x48, 16000)
struct.pack_into(">16h", blk, 0x80, *([100] * 16))
blk[0xf0:0xf0 + len(frames)] = frames
data = bytes(page[:c]) + bytes(blk) + bytes(0x800)
open(os.path.join(d, "t.dat"), "wb").write(data)
