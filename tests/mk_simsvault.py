#!/usr/bin/env python3
"""Synthetic VALT/VBIN vaults (SimAnimals / MySims) in OUT/v/: a.vlt+a.bin (v2/v4, SimAnimals Africa layout),
s.vlt+s.bin (v2/v4, SimAnimals layout: vault key in the header, 4 pad bytes before the entries) and k.vlt+k.bin
(v3/v5, MySims layout with a type table and a struct field). One class "player" with a float, a text, an int32 array,
a RefSpec, a vector3 and a uint32 array stored as separate "[%05d]" element entries; collections "base" and
"hero : base". OUT/sys/main.dol carries the names."""
import struct, sys, os

M = 0xffffffff


def fnvc(h, s):
    for c in s:
        h = (h * 0x01000193) & M
        h ^= c
    return h


def fnv(s):
    return fnvc(0x811c9dc5, s.lower().encode())


def raw(s):
    return fnvc(0x811c9dc5, s.encode())


be = lambda fmt, *a: struct.pack(">" + fmt, *a)
fbits = lambda x: struct.unpack(">I", struct.pack(">f", x))[0]

NAMES = ["player", "base", "hero", "speed", "title", "scores", "buddy", "pos", "tags", "cost", "kind", "kind.mkey",
         "mkey", "amount", "game.cost", "attrib.collectionkey", "ea.reflection.float", "ea.reflection.text",
         "ea.reflection.int32", "ea.reflection.uint32", "attrib.refspec", "attrib.types.vector3", "simvault"]

# fields: name, type, size, flags (0x10 = array)
FIELDS = [("speed", "ea.reflection.float", 4, 0), ("title", "ea.reflection.text", 4, 0),
          ("scores", "ea.reflection.int32", 4, 0x10), ("buddy", "attrib.refspec", 8, 0),
          ("pos", "attrib.types.vector3", 12, 0), ("tags", "ea.reflection.uint32", 4, 0x10)]
V3FIELDS = FIELDS + [("cost", "game.cost", 8, 0)]
TYPES = [("ea.reflection.float", 4, 4, []), ("ea.reflection.text", 4, 4, []), ("ea.reflection.int32", 4, 4, []),
         ("ea.reflection.uint32", 4, 4, []), ("attrib.refspec", 8, 4, [("mclasskey", 3), ("mcollectionkey", 3)]),
         ("attrib.types.vector3", 12, 4, []), ("attrib.collectionkey", 4, 4, [("mkey", 3)]),
         ("game.cost", 8, 4, [("kind", 6), ("kind.mkey", 3), ("amount", 2)])]
TIDX = {t[0]: i for i, t in enumerate(TYPES)}
STRINGS = ["Hello vault", "Second"]


def collection(fields, v3, base):
    """Entries and data of one collection."""
    E = []
    data = b""

    def put(b):
        nonlocal data
        off = len(data)
        data += b
        return off

    if base:
        E.append((fnv("speed"), fbits(1.5)))
        E.append((fnv("title"), raw("Hello vault")))
        off = put(be("3i", 7, -2, 300))
        E.append((fnv("scores"), 3 << 16 | off))
        E.append((fnv("buddy"), put(be("II", fnv("player"), fnv("base")))))
        E.append((fnv("pos"), put(be("3f", 1, 2, 3))))
    else:
        E.append((fnv("speed"), fbits(2.25)))
        E.append((fnv("tags"), 2 << 16))  # array header, superseded by the element entries
        E.append((fnvc(fnv("tags"), b"[00000]"), 11))
        E.append((fnvc(fnv("tags"), b"[00001]"), 12))
        if v3:
            E.append((fnv("cost"), put(be("Ii", 5, -3))))
    return E, data


def vlt(ver, fields):
    nc = 1
    if ver >= 3:
        body = b""
        mem = b""
        for name, size, al, members in TYPES:
            body += be("IHHHH", fnv(name), size, 0, al, len(members)) + bytes(12)
            mem += b"".join(be("I", fnv(m)) for m, _ in members)
            mem += b"".join(be("H", t) for _, t in members)
            mem += b"".join(be("H", 1) for _ in members)
        head = be("I4sIII", 1, b"VALT", ver, 0, nc) + be("I", len(TYPES)) + bytes(0x10) + body + mem
    else:
        head = be("I4sIII", 1, b"VALT", ver, 0, nc) + bytes(0x10)
    head += bytes(4 * nc)
    cls = be("IIH", fnv("player"), 0x20, len(fields)) + bytes(0x16)
    for name, typ, size, fl in fields:
        if ver >= 3:
            cls += be("IIHHHHHH", fnv(name), fnv(typ), size, fl, 1, 4, TIDX[typ], 0) + bytes(4)
        else:
            cls += be("IIHHHH", fnv(name), fnv(typ), size, fl, 1, 4)
    out = bytearray(head + cls)
    out[12:16] = be("I", len(out))
    return bytes(out)


def vbin(ver, style, v3):
    recs = b""
    for key, par, base in (("base", 0, True), ("hero", fnv("base"), False)):
        E, data = collection(None, v3, base)
        cap = len(E) + 1
        dcap = (len(data) + 7) & ~7
        counts = be("4H", len(E), cap, len(data), dcap)
        ent = b"".join(be("II", k, v) for k, v in E) + bytes(8 * (cap - len(E)))
        dat = data + bytes(dcap - len(data))
        if style == "a":  # ptr ptr key class parent flags 0 0 | counts
            hdr = be("8I", 0x80001000, 0x80002000, fnv(key), fnv("player"), par, 0x00010001, 0, 0)
            recs += hdr + counts + ent + dat
        elif style == "s":  # ptr ptr key vault class parent flags 0 0 | counts pad
            hdr = be("9I", 0x80001000, 0x80002000, fnv(key), fnv("simvault"), fnv("player"), par, 0x00010001, 0, 0)
            recs += hdr + counts + bytes(4) + ent + dat
        else:  # key vault class parent flags ...
            hdr = be("7I", fnv(key), fnv("simvault"), fnv("player"), par, 0x20010000, 0xffffffff, 0xffffffff)
            recs += hdr + counts + ent + dat
    pool = b""
    table = b""
    for s in STRINGS:
        table += be("II", raw(s), len(pool))
        pool += s.encode() + b"\0"
    start = 0x144 if style == "k" else 0x140
    head = be("I4sII", 1, b"VBIN", ver, 2) + bytes(0x10) + be("I", len(STRINGS))
    head += bytes(start - len(head))
    return head + recs + table + pool


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(os.path.join(out, "v"), exist_ok=True)
    os.makedirs(os.path.join(out, "sys"), exist_ok=True)
    for name, ver, bver, style, fields in (("a", 2, 4, "a", FIELDS), ("s", 2, 4, "s", FIELDS), ("k", 3, 5, "k", V3FIELDS)):
        open(os.path.join(out, "v", name + ".vlt"), "wb").write(vlt(ver, fields))
        open(os.path.join(out, "v", name + ".bin"), "wb").write(vbin(bver, style, ver >= 3))
    open(os.path.join(out, "sys", "main.dol"), "wb").write(bytes(0x100) + b"\0".join(n.encode() for n in NAMES) + b"\0")


main()
