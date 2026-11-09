import sys, os
d = sys.argv[1]
# 1024x1152 GX I8 sheet (8x4 tiles): first tile = 0..31, rest zero
data = bytes(range(32)) + bytes(1024 * 1152 - 32)
open(os.path.join(d, "font.bin"), "wb").write(data)
