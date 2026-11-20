"""Synthetic Green Lantern .gaftx texture (8x8 GX CMPR, solid red). Writes <dir>/t.gaftx; "check <dir>" verifies t.png."""
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        from PIL import Image
        im = Image.open(os.path.join(sys.argv[2], "t.png")).convert("RGBA")
        assert im.size == (8, 8), im.size
        for p in ((0, 0), (7, 7), (3, 5)):
            r, g, b, a = im.getpixel(p)
            assert r >= 248 and g <= 8 and b <= 8 and a == 255, (p, im.getpixel(p))
        print("PASS")
        sys.exit(0)
    block = struct.pack(">HHI", 0xF800, 0xF800, 0) * 4
    hdr = struct.pack(">8I", 0x00080008, 0x00080008, 0x10001, 18, 32, 0, 0x60, 0x10000) + struct.pack(">4I", 0x38, 0x60, 32, 32)
    hdr += struct.pack(">2I", 0, 0) + struct.pack(">I", 1) + b"\0" * 4 + struct.pack(">HHII", 8, 8, 32, 0)
    hdr += b"\0" * (0x60 - len(hdr))
    open(os.path.join(sys.argv[1], "t.gaftx"), "wb").write(hdr + block)
