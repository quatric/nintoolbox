"""Write a minimal SSBH MESH (.numshb) v1.10, optionally with a .nusktb.

Mirrors the retail layout closely enough to exercise the reader's pointer
arithmetic, attribute table, rigging groups and bounds checks without
shipping cart data. Every pointer in SSBH is a 64-bit offset relative to the
field that holds it.

    mk_numshb.py OUT.numshb            geometry only
    mk_numshb.py OUT.numshb skinned    also writes OUT.nusktb beside it
"""
import struct, sys, os

HALF = {0.0: 0x0000, 1.0: 0x3C00, 0.5: 0x3800}

def build_mesh(skinned):
    SUB = 0x10
    OBJ = 0x100
    ATTR = OBJ + 0xD0
    VBT = ATTR + 3 * 0x30
    VB0 = VBT + 4 * 16
    VB1 = VB0 + 3 * 20
    IBUF = VB1 + 3 * 4
    RIG = IBUF + 3 * 2
    WGT = RIG + 0x28 + 0x18            # one group, one bone buffer
    STR = WGT + 3 * 6                  # strings last: every pointer is forward
    END = STR + 0x40

    buf = bytearray(END)
    buf[0:4] = b"HBSS"
    struct.pack_into("<Q", buf, 4, 0x40)
    buf[SUB:SUB+4] = b"HSEM"
    struct.pack_into("<HH", buf, SUB + 4, 1, 10)

    def relptr(at, target):
        struct.pack_into("<Q", buf, at, target - at)

    obj_name, bone_name = STR, STR + 0x20
    buf[obj_name:obj_name+8] = b"objectA\0"
    buf[bone_name:bone_name+6] = b"Bone0\0"

    relptr(SUB + 0x78, OBJ);  struct.pack_into("<Q", buf, SUB + 0x80, 1)
    relptr(SUB + 0xA0, VBT);  struct.pack_into("<Q", buf, SUB + 0xA8, 4)
    relptr(SUB + 0xB0, IBUF); struct.pack_into("<Q", buf, SUB + 0xB8, 3 * 2)
    if skinned:
        relptr(SUB + 0xC0, RIG); struct.pack_into("<Q", buf, SUB + 0xC8, 1)

    relptr(OBJ + 0x00, obj_name)
    relptr(OBJ + 0x10, bone_name)                      # parent bone
    struct.pack_into("<II", buf, OBJ + 0x18, 3, 3)     # vertex/index count
    struct.pack_into("<II", buf, OBJ + 0x24, 0, 0)     # buffer offsets
    struct.pack_into("<II", buf, OBJ + 0x34, 20, 4)    # strides
    struct.pack_into("<I", buf, OBJ + 0x44, 0)         # index offset
    struct.pack_into("<6f", buf, OBJ + 0x6C, -1, -1, -1, 2, 2, 2)
    relptr(OBJ + 0xC0, ATTR); struct.pack_into("<Q", buf, OBJ + 0xC8, 3)

    struct.pack_into("<IIII", buf, ATTR + 0x00, 0, 0, 0, 0)    # position float3
    struct.pack_into("<IIII", buf, ATTR + 0x30, 1, 5, 0, 12)   # normal half4
    struct.pack_into("<IIII", buf, ATTR + 0x60, 5, 2, 1, 0)    # texcoord half2

    relptr(VBT + 0x00, VB0); struct.pack_into("<Q", buf, VBT + 0x08, 3 * 20)
    relptr(VBT + 0x10, VB1); struct.pack_into("<Q", buf, VBT + 0x18, 3 * 4)
    relptr(VBT + 0x20, VB1); struct.pack_into("<Q", buf, VBT + 0x28, 0)
    relptr(VBT + 0x30, VB1); struct.pack_into("<Q", buf, VBT + 0x38, 0)

    for i, (x, y) in enumerate(((0.0, 0.0), (1.0, 0.0), (0.0, 1.0))):
        o = VB0 + i * 20
        struct.pack_into("<3f", buf, o, x, y, 0.0)
        struct.pack_into("<4H", buf, o + 12,
                         HALF[0.0], HALF[0.0], HALF[1.0], HALF[0.0])
        struct.pack_into("<2H", buf, VB1 + i * 4, HALF[x], HALF[y])

    struct.pack_into("<3H", buf, IBUF, 0, 1, 2)

    if skinned:
        # Rigging group naming the object, with one bone buffer.
        relptr(RIG + 0x00, obj_name)
        struct.pack_into("<Q", buf, RIG + 0x08, 0)          # sub index
        struct.pack_into("<Q", buf, RIG + 0x10, 0x100)      # flags
        relptr(RIG + 0x18, RIG + 0x28)
        struct.pack_into("<Q", buf, RIG + 0x20, 1)          # one bone buffer
        BB = RIG + 0x28
        relptr(BB + 0x00, bone_name)
        relptr(BB + 0x08, WGT)
        struct.pack_into("<Q", buf, BB + 0x10, 3 * 6)
        for i in range(3):                                   # u16 index, f32 weight
            struct.pack_into("<Hf", buf, WGT + i * 6, i, 1.0)
    return bytes(buf)

def build_skel():
    # Two bones, the second parented to the first. Matrices are 4x4
    # row-major with the translation in the last row.
    SUB, BONES, WORLD, INVW, LOCAL, INVL, STR = 0x10, 0x80, 0xC0, 0x140, 0x1C0, 0x240, 0x2C0
    buf = bytearray(STR + 0x20)
    buf[0:4] = b"HBSS"
    struct.pack_into("<Q", buf, 4, 0x40)
    buf[SUB:SUB+4] = b"LEKS"
    struct.pack_into("<HH", buf, SUB + 4, 1, 0)

    def relptr(at, target):
        struct.pack_into("<Q", buf, at, target - at)

    n0, n1 = STR, STR + 0x10
    buf[n0:n0+6] = b"Bone0\0"
    buf[n1:n1+6] = b"Bone1\0"
    relptr(0x18, BONES); struct.pack_into("<Q", buf, 0x20, 2)
    relptr(0x28, WORLD); struct.pack_into("<Q", buf, 0x30, 2)
    relptr(0x38, INVW);  struct.pack_into("<Q", buf, 0x40, 2)
    relptr(0x48, LOCAL); struct.pack_into("<Q", buf, 0x50, 2)
    relptr(0x58, INVL);  struct.pack_into("<Q", buf, 0x60, 2)

    relptr(BONES + 0x00, n0)
    struct.pack_into("<hhI", buf, BONES + 0x08, 0, -1, 1)
    relptr(BONES + 0x10, n1)
    struct.pack_into("<hhI", buf, BONES + 0x18, 1, 0, 1)

    ident = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    shift = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,1,0,1]     # translate +1 in Y
    unshift = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1]
    for base, a, b in ((WORLD, ident, shift), (INVW, ident, unshift),
                       (LOCAL, ident, shift), (INVL, ident, unshift)):
        struct.pack_into("<16f", buf, base, *a)
        struct.pack_into("<16f", buf, base + 64, *b)
    return bytes(buf)

out = sys.argv[1]
skinned = len(sys.argv) > 2 and sys.argv[2] == "skinned"
open(out, "wb").write(build_mesh(skinned))
if skinned:
    open(os.path.splitext(out)[0] + ".nusktb", "wb").write(build_skel())
