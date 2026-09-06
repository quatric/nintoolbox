"""Write a minimal single-texture PTLG container keyed by a given hash.

GLG/RLG models bind textures by this 32-bit key, so a test needs a container
that actually holds the hash the model's mesh table asks for.
"""
import struct, sys

path, hash_hex = sys.argv[1], sys.argv[2]
w = h = 8
pixels = bytes(64 * 2)                      # 8x8 RGB565, one mip
hdr = struct.pack(">IIBBBB", 1, 2, 5, 4, 5, 0)   # mips, unk, unk, format=4, unk
hdr += struct.pack(">HH", w, h)                  # GameCube: w/h at 0x0c
section = hdr + pixels
head = struct.pack(">4sIII", b"PTLG", 1, 0, 0)   # word at 8 == 0 -> GameCube
entry = struct.pack(">IIII", int(hash_hex, 16), 0, len(section), 0)
open(path, "wb").write(head + entry + section)
