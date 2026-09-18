"""Write a minimal SSBH ANIM (.nuanmb) v2.0 with decodable payloads.

One Transform group, one node ("Bone0"), four tracks:
  [0] "f0": Float, Direct, 2 frames [1.5, 2.5]
  [1] "t0": Transform, Constant, pos (1,2,3) rot (0,0,0,1) scale (1,1,1)
  [2] "b0": Boolean, Compressed, 3 frames [true, false, true]
  [3] "c0": Transform, Compressed, 2 frames; X lerps 0 -> 4 over 2 bits,
            everything else defaulted (pos X = 0 then 4)

Every SSBH pointer is a 64-bit offset relative to the field holding it.

    mk_nuanmb.py OUT.nuanmb
"""
import struct, sys

def main(out):
    F = 0x18
    G = 0x100
    N = G + 24
    T = N + 24
    BUF = T + 4 * 32
    # payload layout inside the buffer
    P_FLOAT, P_TRANS, P_BOOL, P_CT = 0, 8, 52, 69
    BUF_SIZE = P_CT + 205
    STR = BUF + BUF_SIZE

    s_test, s_bone = STR, STR + 5
    s_f0, s_t0, s_b0, s_c0 = STR + 11, STR + 14, STR + 17, STR + 20
    END = STR + 23

    buf = bytearray(END)
    buf[0:4] = b"HBSS"
    struct.pack_into("<Q", buf, 4, 0x40)
    buf[0x10:0x14] = b"MINA"
    struct.pack_into("<HH", buf, 0x14, 2, 0)

    def rel(at, target):
        struct.pack_into("<Q", buf, at, target - at)

    struct.pack_into("<f", buf, F, 3.0)
    struct.pack_into("<HH", buf, F + 4, 1, 3)
    rel(F + 8, s_test)
    rel(F + 0x10, G);  struct.pack_into("<Q", buf, F + 0x18, 1)
    rel(F + 0x20, BUF); struct.pack_into("<Q", buf, F + 0x28, BUF_SIZE)

    struct.pack_into("<Q", buf, G, 1)                       # Transform group
    rel(G + 8, N); struct.pack_into("<Q", buf, G + 16, 1)
    rel(N, s_bone)
    rel(N + 8, T); struct.pack_into("<Q", buf, N + 16, 4)

    def track(i, name, ttype, comp, frames, doff, dsize):
        e = T + i * 32
        rel(e, name)
        struct.pack_into("<BB", buf, e + 8, ttype, comp)
        struct.pack_into("<II", buf, e + 12, frames, 0)
        struct.pack_into("<I", buf, e + 20, doff)
        struct.pack_into("<Q", buf, e + 24, dsize)

    track(0, s_f0, 3, 1, 2, P_FLOAT, 8)
    track(1, s_t0, 1, 5, 1, P_TRANS, 44)
    track(2, s_b0, 8, 4, 3, P_BOOL, 17)
    track(3, s_c0, 1, 4, 2, P_CT, 205)

    struct.pack_into("<2f", buf, BUF + P_FLOAT, 1.5, 2.5)
    struct.pack_into("<10fI", buf, BUF + P_TRANS,
                     1, 1, 1, 0, 0, 0, 1, 1, 2, 3, 0)
    # compressed booleans: unk=4, flags=0, defaults@16, 1 bit/entry,
    # data@16, 3 frames; bits 101b
    struct.pack_into("<HHHHii", buf, BUF + P_BOOL, 4, 0, 16, 1, 16, 3)
    buf[BUF + P_BOOL + 16] = 0x05

    # compressed transform: unk=4, flags=0x0d (normal scale+rot+pos),
    # defaults@160, data@204, 2 frames. Only item 6 (pos X) carries bits.
    struct.pack_into("<HHHHii", buf, BUF + P_CT, 4, 0x0d, 160, 0, 204, 2)
    for k in range(9):
        if k == 6:
            struct.pack_into("<ffQ", buf, BUF + P_CT + 16 + k * 16, 0.0, 4.0, 2)
        else:
            struct.pack_into("<ffQ", buf, BUF + P_CT + 16 + k * 16, 0.0, 0.0, 0)
    struct.pack_into("<10fI", buf, BUF + P_CT + 160,
                     1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0)
    buf[BUF + P_CT + 204] = 0x18  # frame0: X=00 w=0; frame1: X=11 w=0

    buf[s_test:s_test + 5] = b"test\0"
    buf[s_bone:s_bone + 6] = b"Bone0\0"
    buf[s_f0:s_f0 + 3] = b"f0\0"
    buf[s_t0:s_t0 + 3] = b"t0\0"
    buf[s_b0:s_b0 + 3] = b"b0\0"
    buf[s_c0:s_c0 + 3] = b"c0\0"

    open(out, "wb").write(buf)

if __name__ == "__main__":
    main(sys.argv[1])

