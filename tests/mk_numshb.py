"""Write a minimal SSBH MESH (.numshb) version 1.10: one object, one triangle.

Mirrors the retail layout closely enough to exercise the reader's pointer
arithmetic, attribute table and bounds checks without shipping cart data.
Every pointer in SSBH is a 64-bit offset relative to the field holding it.
"""
import struct, sys

def half(f):
    # Only the handful of exact values this fixture uses.
    return {0.0: 0x0000, 1.0: 0x3C00, 0.5: 0x3800}[f]

SUB = 0x10                      # the MESH sub-file starts here
OBJ = 0x100                     # mesh object record
ATTR = OBJ + 0xD0               # attribute array
VBT = ATTR + 3 * 0x30           # vertex buffer table
VB0 = VBT + 4 * 16              # buffer 0 data
VB1 = VB0 + 3 * 20              # buffer 1 data
IBUF = VB1 + 3 * 4              # index buffer
END = IBUF + 3 * 2

buf = bytearray(END)
buf[0:4] = b"HBSS"
struct.pack_into("<Q", buf, 4, 0x40)
buf[SUB:SUB+4] = b"HSEM"
struct.pack_into("<HH", buf, SUB + 4, 1, 10)          # version 1.10

def relptr(at, target):
    struct.pack_into("<Q", buf, at, target - at)

# Mesh header: object array, vertex buffer array, index buffer.
relptr(SUB + 0x78, OBJ);   struct.pack_into("<Q", buf, SUB + 0x80, 1)
relptr(SUB + 0xA0, VBT);   struct.pack_into("<Q", buf, SUB + 0xA8, 4)
relptr(SUB + 0xB0, IBUF);  struct.pack_into("<Q", buf, SUB + 0xB8, 3 * 2)

# Mesh object: 3 vertices, 3 indices, stride 20 in buffer 0 and 4 in buffer 1.
struct.pack_into("<II", buf, OBJ + 0x18, 3, 3)
struct.pack_into("<II", buf, OBJ + 0x24, 0, 0)
struct.pack_into("<II", buf, OBJ + 0x34, 20, 4)
struct.pack_into("<I", buf, OBJ + 0x44, 0)
struct.pack_into("<6f", buf, OBJ + 0x6C, -1, -1, -1, 2, 2, 2)   # bbox
relptr(OBJ + 0xC0, ATTR);  struct.pack_into("<Q", buf, OBJ + 0xC8, 3)

# Attributes: position float3 @0, normal half4 @12 (buffer 0), uv half2 @0
# (buffer 1).
struct.pack_into("<IIII", buf, ATTR + 0x00, 0, 0, 0, 0)
struct.pack_into("<IIII", buf, ATTR + 0x30, 1, 5, 0, 12)
struct.pack_into("<IIII", buf, ATTR + 0x60, 5, 2, 1, 0)

# Vertex buffer table: pointer and size per buffer.
relptr(VBT + 0x00, VB0);  struct.pack_into("<Q", buf, VBT + 0x08, 3 * 20)
relptr(VBT + 0x10, VB1);  struct.pack_into("<Q", buf, VBT + 0x18, 3 * 4)
relptr(VBT + 0x20, VB1);  struct.pack_into("<Q", buf, VBT + 0x28, 0)
relptr(VBT + 0x30, VB1);  struct.pack_into("<Q", buf, VBT + 0x38, 0)

for i, (x, y) in enumerate(((0.0, 0.0), (1.0, 0.0), (0.0, 1.0))):
    o = VB0 + i * 20
    struct.pack_into("<3f", buf, o, x, y, 0.0)
    struct.pack_into("<4H", buf, o + 12, half(0.0), half(0.0), half(1.0), half(0.0))
    struct.pack_into("<2H", buf, VB1 + i * 4, half(x), half(y))

struct.pack_into("<3H", buf, IBUF, 0, 1, 2)
open(sys.argv[1], "wb").write(bytes(buf))
