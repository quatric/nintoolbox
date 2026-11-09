import struct, sys, os
d = sys.argv[1]
# two TA textures back to back: C4 16x8 (RGB5A3 palette), C8 8x4
def ta(fmt, w, h, pal, idx):
    body = b"TA\0\1" + struct.pack(">I", 0) + struct.pack(">5I", fmt, w, h, 0, 0)[:20] + b"\0" * 4
    body = b"TA\0\1" + struct.pack(">IIIII", 0, fmt, w, h, 0) + b"\0" * 8
    body += b"".join(struct.pack(">H", c) for c in pal) + idx + b"\0" * 0x58
    body = body[:4] + struct.pack(">I", len(body)) + body[8:]
    return body.ljust((len(body) + 0x1f) & ~0x1f, b"\0")
pal4 = [0x8000 | (i * 2) for i in range(16)]
t1 = ta(4, 16, 8, pal4, bytes(range(0, 64)))
t2 = ta(5, 8, 8, [0xffff] * 256, bytes(64))
open(os.path.join(d, "t.tex1"), "wb").write(t1 + t2)
