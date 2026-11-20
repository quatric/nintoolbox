"""Synthetic Infinity Ward .iwi images (v7 World at War header 0x1c, v11 Black Ops header 0x20), 8x8 solid red GX CMPR. Writes <dir>/t7.iwi and t11.iwi; "check <dir>" verifies the PNGs."""
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        from PIL import Image
        for n in ("t7", "t11"):
            im = Image.open(os.path.join(sys.argv[2], n + ".iwi.png")).convert("RGBA")
            assert im.size == (8, 8), (n, im.size)
            r, g, b, a = im.getpixel((2, 6))
            assert r >= 248 and g <= 8 and b <= 8 and a == 255, (n, im.getpixel((2, 6)))
        print("PASS")
        sys.exit(0)
    block = struct.pack(">HHI", 0xF800, 0xF800, 0) * 4
    for n, ver, hdr in (("t7", 7, 0x1C), ("t11", 11, 0x20)):
        h = b"IWi" + bytes([ver, 11, 0]) + struct.pack("<HHH", 8, 8, 1)
        h += struct.pack("<I", hdr + len(block)) * ((hdr - 12) // 4)
        assert len(h) == hdr, len(h)
        open(os.path.join(sys.argv[1], n + ".iwi"), "wb").write(h + block)
