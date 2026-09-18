"""Write a minimal SSBH MATL (.numatb) v1.6 with one material entry.

Entry "mat0" on shader "shd" with five attributes covering every XML value
shape: Float, Boolean, Vector4, String and Sampler. Every SSBH pointer is a
64-bit offset relative to the field holding it.

    mk_numatb.py OUT.numatb
"""
import struct, sys

def main(out):
    SUB = 0x10
    E = 0x40
    A = E + 0x20
    V = A + 5 * 0x18
    V_FLOAT, V_BOOL, V_VEC, V_STRF, V_SAMP = 0, 4, 8, 24, 32
    S = V + 88

    s_mat, s_shd, s_tex = S, S + 5, S + 9
    END = S + 17

    buf = bytearray(END)
    buf[0:4] = b"HBSS"
    struct.pack_into("<Q", buf, 4, 0x40)
    buf[SUB:SUB + 4] = b"LTAM"
    struct.pack_into("<HH", buf, SUB + 4, 1, 6)

    def rel(at, target):
        struct.pack_into("<Q", buf, at, target - at)

    rel(SUB + 8, E); struct.pack_into("<Q", buf, SUB + 16, 1)
    rel(E, s_mat)
    rel(E + 8, A); struct.pack_into("<Q", buf, E + 16, 5)
    rel(E + 0x18, s_shd)

    # (param id, data type, value offset inside V)
    attrs = [(192, 1, V_FLOAT), (233, 2, V_BOOL), (152, 5, V_VEC),
             (92, 11, V_STRF), (108, 14, V_SAMP)]
    for i, (pid, dtype, voff) in enumerate(attrs):
        a = A + i * 0x18
        struct.pack_into("<Q", buf, a, pid)
        rel(a + 8, V + voff)
        struct.pack_into("<Q", buf, a + 16, dtype)

    struct.pack_into("<f", buf, V + V_FLOAT, 1.5)
    struct.pack_into("<I", buf, V + V_BOOL, 1)
    struct.pack_into("<4f", buf, V + V_VEC, 1.0, 0.5, 0.0, 1.0)
    rel(V + V_STRF, s_tex)
    struct.pack_into("<6I4f2IfI", buf, V + V_SAMP,
                     0, 0, 0, 1, 1, 0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0.0, 1)

    buf[s_mat:s_mat + 5] = b"mat0\0"
    buf[s_shd:s_shd + 4] = b"shd\0"
    buf[s_tex:s_tex + 8] = b"tex_col\0"

    open(out, "wb").write(buf)

if __name__ == "__main__":
    main(sys.argv[1])
