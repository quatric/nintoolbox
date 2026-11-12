#!/usr/bin/env python3
"""Synthetic EA Attrib vaults: t.vlt + t.bin (64-bit keys) and t32.vlt + t32.bin (32-bit keys), each with one class "player"
(a float, a text, an int array and a RefSpec field) and two collections ("base" and "hero" deriving from it)."""
import struct, sys, os

M = (1 << 64) - 1


def mix(a, b, c):
    for sa, sb, sc in ((43, 9, 8), (38, 23, 5), (35, 49, 11), (12, 18, 22)):
        a = (a - b - c) & M; a ^= c >> sa
        b = (b - c - a) & M; b ^= (a << sb) & M
        c = (c - a - b) & M; c ^= b >> sc
    return a, b, c


def h(s):
    k = s.encode(); a = b = 0xABCDEF0011223344; c = 0x9e3779b97f4a7c13; i = 0
    while len(k) - i >= 24:
        a, b, c = [(x + int.from_bytes(k[i + 8 * j:i + 8 * j + 8], "little")) & M for j, x in enumerate((a, b, c))]
        a, b, c = mix(a, b, c); i += 24
    c = (c + len(k)) & M
    for j in range(len(k) - i - 1, -1, -1):
        v = k[i + j]
        if j >= 16: c = (c + (v << (8 * (j - 15)))) & M
        elif j >= 8: b = (b + (v << (8 * (j - 8)))) & M
        else: a = (a + (v << (8 * j))) & M
    return mix(a, b, c)[2]


def h32(s):
    m = 0xffffffff; k = s.encode(); a = b = 0x9e3779b9; c = 0xABCDEF00; i = 0

    def mix(a, b, c):
        for sa, sb, sc in ((13, 8, 13), (12, 16, 5), (3, 10, 15)):
            a = (a - b - c) & m; a ^= c >> sa
            b = (b - c - a) & m; b ^= (a << sb) & m
            c = (c - a - b) & m; c ^= b >> sc
        return a, b, c
    while len(k) - i >= 12:
        a, b, c = [(x + int.from_bytes(k[i + 4 * j:i + 4 * j + 4], "little")) & m for j, x in enumerate((a, b, c))]
        a, b, c = mix(a, b, c); i += 12
    c = (c + len(k)) & m
    for j in range(len(k) - i - 1, -1, -1):
        v = k[i + j]
        if j >= 8: c = (c + (v << (8 * (j - 7)))) & m
        elif j >= 4: b = (b + (v << (8 * (j - 4)))) & m
        else: a = (a + (v << (8 * j))) & m
    return mix(a, b, c)[2]


def build(d, stem, W):
    H = h if W == 8 else h32
    Q = ">Q" if W == 8 else ">I"
    k = lambda v: struct.pack(Q, v)
    names = ["Hello vault", "EA::Reflection::Float", "EA::Reflection::Text", "EA::Reflection::Int32", "Attrib::RefSpec",
             "player", "speed", "title", "scores", "buddy", "base", "hero"]
    pool = b"".join(n.encode() + b"\0" for n in names)
    pool += bytes(-len(pool) % 8)
    binb = bytearray(b"StrE" + struct.pack(">I", 8 + len(pool)) + pool)
    text_off = 8
    T = {n: H(n) for n in names}
    fields_off = len(binb)
    fields = [("speed", "EA::Reflection::Float", 4, 1, 4, 2), ("title", "EA::Reflection::Text", 4, 1, 4, 2),
              ("scores", "EA::Reflection::Int32", 4, 0xffff, 5, 2), ("buddy", "Attrib::RefSpec", 2 * W + 8, 1, 4, 3)]
    for n, t, sz, cnt, fl, al in fields:
        binb += k(T[n]) + k(T[t]) + struct.pack(">IHBB", sz, cnt, fl, al)
    arr_off = len(binb)
    binb += struct.pack(">HHHH", 3, 3, 4, 0) + struct.pack(">iii", 7, -2, 300)
    ref_off = len(binb)
    binb += k(T["player"]) + k(T["base"]) + struct.pack(">II", 0, 0)

    TC, TL = H("Attrib::ClassLoadData"), H("Attrib::CollectionLoadData")
    dep = k(2) + k(H(stem + ".vlt")) + k(H(stem + ".bin")) + struct.pack(">II", 0, len(stem) + 5) + (stem + ".vlt\0" + stem + ".bin\0").encode()
    dep += bytes(-len(dep) % 8)
    head = b"Vers" + struct.pack(">I", 16) + bytes(8) + b"StrN" + struct.pack(">I", 16) + bytes(8) + b"DepN" + struct.pack(">I", 8 + len(dep)) + dep
    dat0 = len(head) + 8
    recs = bytearray()
    fix_vlt = []
    cls_off = dat0
    recs += k(T["player"]) + struct.pack(">II", 2, len(fields)) + bytes(0x28 - W - 8)
    fix_vlt.append((cls_off + W + 8, fields_off))

    def coll(key, parent, entries):
        off = dat0 + len(recs)
        types = [T[t] for t in ("EA::Reflection::Float", "EA::Reflection::Text", "EA::Reflection::Int32", "Attrib::RefSpec")]
        body = k(T[key]) + k(T["player"]) + k(T[parent] if parent else 0) + struct.pack(">IIIHH", len(entries), 0, len(entries), 4, 4) + k(0)
        body += b"".join(k(t) for t in types)
        eo = off + len(body)
        for i, (f, val, ti, flags, tgt) in enumerate(entries):
            body += k(T[f]) + struct.pack(">IHH", val, ti, flags)
            if tgt is not None: fix_vlt.append((eo + (W + 8) * i + W, tgt))
        recs.extend(body)
        return off, len(body)

    b_off, b_sz = coll("base", None, [("speed", 0x3fc00000, 0, 0x4000, None), ("title", 0, 1, 0x4000, text_off)])
    h_off, h_sz = coll("hero", "base", [("scores", 0, 2, 0x200, arr_off), ("buddy", 0, 3, 0, ref_off)])
    recs += bytes(-len(recs) % 16)
    datn = b"DatN" + struct.pack(">I", 8 + len(recs)) + recs
    exps = [(T["player"], TC, 0x28, cls_off), (H("player/base"), TL, b_sz, b_off), (H("player/hero"), TL, h_sz, h_off)]
    expn = k(len(exps)) + b"".join(k(a) + k(b) + struct.pack(">II", c, o) for a, b, c, o in exps)
    expn = b"ExpN" + struct.pack(">I", 8 + len(expn)) + expn
    pr = lambda l, kind, f, t: struct.pack(">IHH", l, kind, f) + k(t)
    ptr = pr(0, 2, 0, 0) + b"".join(pr(l, 3, 1, t) for l, t in fix_vlt) + pr(0, 0, 0, 0)
    ptrn = b"PtrN" + struct.pack(">I", 8 + len(ptr)) + ptr
    open(os.path.join(d, stem + ".vlt"), "wb").write(head + datn + expn + ptrn)
    open(os.path.join(d, stem + ".bin"), "wb").write(binb)


build(sys.argv[1], "t", 8)
build(sys.argv[1], "t32", 4)
