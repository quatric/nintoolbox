#!/usr/bin/env python3
"""Synthetic EA Attrib vault (t.vlt + t.bin): one class "player" with a float, a text, an int array and a RefSpec field, and two
collections ("base" and "hero" deriving from it)."""
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


d = sys.argv[1]
# ---- .bin: StrE pool, then the field table and an int array
names = ["Hello vault", "EA::Reflection::Float", "EA::Reflection::Text", "EA::Reflection::Int32", "Attrib::RefSpec",
         "player", "speed", "title", "scores", "buddy", "base", "hero"]
pool = b"".join(n.encode() + b"\0" for n in names)
pool += bytes(-len(pool) % 8)
binb = bytearray(b"StrE" + struct.pack(">I", 8 + len(pool)) + pool)
text_off = 8
T = {n: h(n) for n in names}
fields_off = len(binb)
fields = [("speed", "EA::Reflection::Float", 4, 1, 4, 2), ("title", "EA::Reflection::Text", 4, 1, 4, 2),
          ("scores", "EA::Reflection::Int32", 4, 0xffff, 5, 2), ("buddy", "Attrib::RefSpec", 24, 1, 4, 3)]
for n, t, sz, cnt, fl, al in fields:
    binb += struct.pack(">QQIHBB", T[n], T[t], sz, cnt, fl, al)
arr_off = len(binb)
binb += struct.pack(">HHHH", 3, 3, 4, 0) + struct.pack(">iii", 7, -2, 300)
ref_off = len(binb)
binb += struct.pack(">QQII", T["player"], T["base"], 0, 0)

# ---- .vlt
TC, TL = h("Attrib::ClassLoadData"), h("Attrib::CollectionLoadData")
dep = struct.pack(">Q", 2) + struct.pack(">QQ", h("t.vlt"), h("t.bin")) + struct.pack(">II", 0, 6) + b"t.vlt\0t.bin\0"
dep += bytes(-len(dep) % 8)
head = b"Vers" + struct.pack(">I", 16) + bytes(8) + b"StrN" + struct.pack(">I", 16) + bytes(8) + b"DepN" + struct.pack(">I", 8 + len(dep)) + dep
dat0 = len(head) + 8
recs = bytearray()
fix_vlt = []  # (location, target in .bin)
cls_off = dat0 + len(recs)
recs += struct.pack(">QII", T["player"], 2, len(fields)) + bytes(0x18)
fix_vlt.append((cls_off + 0x10, fields_off))


def coll(key, parent, entries):
    off = dat0 + len(recs)
    types = [T[t] for t in ("EA::Reflection::Float", "EA::Reflection::Text", "EA::Reflection::Int32", "Attrib::RefSpec")]
    body = struct.pack(">QQQIIIHHQ", T[key], T["player"], T[parent] if parent else 0, len(entries), 0, len(entries), 4, 4, 0)
    body += b"".join(struct.pack(">Q", t) for t in types)
    eo = off + len(body)
    for i, (f, val, ti, flags, tgt) in enumerate(entries):
        body += struct.pack(">QIHH", T[f], val, ti, flags)
        if tgt is not None: fix_vlt.append((eo + 16 * i + 8, tgt))
    recs.extend(body)
    return off, len(body)


b_off, b_sz = coll("base", None, [("speed", 0x3fc00000, 0, 0x4000, None), ("title", 0, 1, 0x4000, text_off)])
h_off, h_sz = coll("hero", "base", [("scores", 0, 2, 0x200, arr_off), ("buddy", 0, 3, 0, ref_off)])
recs += bytes(-len(recs) % 16)
datn = b"DatN" + struct.pack(">I", 8 + len(recs)) + recs
exps = [(T["player"], TC, 0x28, cls_off), (h("player/base"), TL, b_sz, b_off), (h("player/hero"), TL, h_sz, h_off)]
expn = struct.pack(">Q", len(exps)) + b"".join(struct.pack(">QQII", *e) for e in exps)
expn = b"ExpN" + struct.pack(">I", 8 + len(expn)) + expn
ptr = struct.pack(">IHHQ", 0, 2, 0, 0) + b"".join(struct.pack(">IHHQ", l, 3, 1, t) for l, t in fix_vlt) + struct.pack(">IHHQ", 0, 0, 0, 0)
ptrn = b"PtrN" + struct.pack(">I", 8 + len(ptr)) + ptr
open(os.path.join(d, "t.vlt"), "wb").write(head + datn + expn + ptrn)
open(os.path.join(d, "t.bin"), "wb").write(binb)
