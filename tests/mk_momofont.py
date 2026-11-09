import sys, os
d = sys.argv[1]
# 1024x1152 GX I8 sheet (8x4 tiles): first tile = 0..31, rest zero
data = bytes(range(32)) + bytes(1024 * 1152 - 32)
open(os.path.join(d, "font.bin"), "wb").write(data)
# Momotarou Dentetsu 16 variant: 1024x1024 GX I4 (8x8 tiles)
os.makedirs(os.path.join(d, "i4"), exist_ok=True)
open(os.path.join(d, "i4", "font.bin"), "wb").write(bytes([0x12, 0x34] * 16) + bytes(1024 * 1024 // 2 - 32))
