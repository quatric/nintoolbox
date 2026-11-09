import struct, sys, os
d = sys.argv[1]
def bpe_store(raw):
    # Gage BPE with an identity pair table: one block per 65535 bytes, every byte literal
    out = bytearray()
    # identity pair table in run-skip form: 0xff skips 128 entries and then one entry follows (0x80 = literal 128),
    # 0xfe skips 127 more -> c == 256
    for i in range(0, len(raw), 60000):
        blk = raw[i:i + 60000]
        out += bytes([0xff, 0x80, 0xfe])
        out += struct.pack(">H", len(blk)) + blk
    return bytes(out)
def pvr(w, h, pix):
    return b"XIBG" + struct.pack(">I", 8) + bytes(8) + b"TRVP" + struct.pack(">IIHH", 8 + len(pix), 0x4501, w, h) + pix
tex = pvr(8, 8, bytes([0xf8, 0x00]) * 64)  # RGB565 red, 4x4 tiles
other = b"hello bpe"
members = [(b"STAGE\\TEX.PVR", tex), (b"NOTE.BIN", other)]
comp = [bpe_store(m) for _, m in members]
n = len(members)
hdr = b"GGXArchiver1.00\0" + struct.pack("<II", n, n) + bytes(8)
names = b"".join(nm.ljust(32, b"\0") for nm, _ in members)
recs = b""
off = 0
for i, ((nm, m), c) in enumerate(zip(members, comp)):
    recs += struct.pack("<6I", i, 1, len(m), len(c), 1, off)
    off += len(c)
open(os.path.join(d, "t.bin"), "wb").write(hdr + names + recs + b"".join(comp))
# loose toon-style paletted texture (kind 0x48): 16 entry ARGB1555 palette, 4 bit indices in 8x8 tiles
pal = b"".join(struct.pack(">H", v) for v in [0] + [0xfbde] * 14 + [0x8842])
idx = bytes([0xff]) * 32
open(os.path.join(d, "toon.pvr"), "wb").write(b"XIBG" + struct.pack(">I", 8) + bytes(8) + b"TRVP" + struct.pack(">IIHH", 0x28 + 32, 0x4800, 8, 8) + pal + idx)
