#!/usr/bin/env python3
"""Build a tiny NUP container (decrypted Switch package format, "NUP\\x01").

Usage: mk_nup.py OUT.nup

Two packages: a base (exefs/main + romfs/data/hello.txt stored zstd-compressed,
romfs/empty.bin zero-length) and an update (romfs/data/hello.txt raw, different
content). The base carries a meta block: title name, display version and a
fake 4-byte JPEG icon.
"""
import hashlib, struct, subprocess, sys


def zstd(data):
    try:
        import zstandard
        return zstandard.ZstdCompressor(level=3, write_content_size=True).compress(data)
    except ImportError:
        return subprocess.run(["zstd", "-q", "-c"], input=data, capture_output=True, check=True).stdout


def build(out):
    pool = bytearray()
    names = {}

    def name(s):
        b = s.encode()
        if b not in names:
            names[b] = len(pool)
            pool.extend(b)
        return names[b], len(b)

    dirs, files, slots = [], [], []
    hello_base = b"Hello from the base package!\n" * 8
    hello_upd = b"Patched text\n"
    blobs = []  # (file index, stored bytes)

    def add_file(nm, data, compress):
        stored = zstd(data) if compress else data
        files.append([nm, data, stored, 1 if compress and data else 0])
        return len(files) - 1

    # node ids are assigned after all dirs are known; build trees as nested lists
    trees = []
    def tree_base():
        return ("", [("exefs", [("main", add_file("main", b"NSO0" + bytes(60), False))]),
                     ("romfs", [("data", [("hello.txt", add_file("hello.txt", hello_base, True))]),
                                ("empty.bin", add_file("empty.bin", b"", False))])])
    def tree_upd():
        return ("", [("romfs", [("data", [("hello.txt", add_file("hello.txt", hello_upd, False))])])])
    trees = [tree_base(), tree_upd()]

    # flatten dirs in pre-order
    dir_nodes = []
    def collect(node):
        nm, kids = node
        idx = len(dir_nodes)
        dir_nodes.append([nm, kids])
        for k in kids:
            if isinstance(k[1], list):
                collect(k)
        return idx
    roots = [collect(t) for t in trees]
    dir_index = {id(d[1]): i for i, d in enumerate(dir_nodes)}
    nd = len(dir_nodes)
    dir_recs = []
    for nm, kids in dir_nodes:
        first = len(slots)
        for k in sorted(kids, key=lambda k: k[0].encode()):
            slots.append(dir_index[id(k[1])] if isinstance(k[1], list) else nd + k[1])
        dir_recs.append((name(nm) if nm else (0, 0), first, len(kids)))

    def find(node, nm):
        return [k for k in node[1] if k[0] == nm][0]
    pkg_dirs = []
    for t, r in zip(trees, roots):
        ex = find(t, "exefs") if any(k[0] == "exefs" for k in t[1]) else None
        ro = find(t, "romfs")
        pkg_dirs.append((r, dir_index[id(ex[1])] if ex else 0xFFFFFFFF, dir_index[id(ro[1])]))

    for f in files:
        f.append(name(f[0]))

    np_ = 2
    toc_hdr = 0x30
    pool_off = toc_hdr + np_ * 0x40 + nd * 0x18 + len(files) * 0x50 + len(slots) * 4
    toc_size = pool_off + len(pool)
    blob = 0x80 + toc_size
    blob = (blob + 15) & ~15

    # meta block for the base, placed in the blob region
    def tlv(t, v):
        return struct.pack("<HHI", t, 0, len(v)) + v
    meta = tlv(5, b"Test Title") + tlv(4, b"1.0.0") + tlv(3, b"\xff\xd8\xff\xd9")
    pos = blob
    meta_off = pos
    pos = (pos + len(meta) + 15) & ~15
    for f in files:
        if f[1]:
            f.append(pos)
            pos = (pos + len(f[2]) + 15) & ~15
        else:
            f.append(0)
    file_size = pos

    toc = bytearray()
    toc += b"TOC\x01" + struct.pack("<IQQQQQ", np_, nd, len(files), pool_off, len(pool), 0)
    kinds = [(0x0400000000001000, 0, 0), (0x0400000000001000, 0x10000, 1)]
    for (tid, ver, kind), (r, ex, ro), mo in zip(kinds, pkg_dirs, [(meta_off, len(meta)), (0, 0)]):
        toc += struct.pack("<QIIIIBBHIQI", tid, r, ex, ro, ver, kind, 0, 0, 0, mo[0], mo[1]) + bytes(20)
    for (no, nl), first, cnt in dir_recs:
        toc += struct.pack("<IIIIQ", no, nl, first, cnt, 0)
    for nm, data, stored, codec, (no, nl), off in files:
        toc += struct.pack("<IIQQQ", no, nl, len(data), off, len(stored) if data else 0)
        toc += hashlib.sha256(data).digest() + bytes([codec, 0]) + bytes(14)
    toc += struct.pack("<%dI" % len(slots), *slots)
    toc += pool
    assert len(toc) == toc_size

    uuid = hashlib.sha256(b"".join(struct.pack("<QIB", t, v, k) for t, v, k in kinds)).digest()[:16]
    sb = b"NUP\x01" + struct.pack("<HHQQ", 0x80, 1, 0x80, toc_size) + hashlib.sha256(toc).digest()
    sb += struct.pack("<QQII", blob, file_size, np_, 0) + uuid + bytes(32)
    assert len(sb) == 0x80

    img = bytearray(file_size)
    img[0:0x80] = sb
    img[0x80:0x80 + toc_size] = toc
    img[meta_off:meta_off + len(meta)] = meta
    for nm, data, stored, codec, _, off in files:
        if data:
            img[off:off + len(stored)] = stored
    open(out, "wb").write(img)


if __name__ == "__main__":
    build(sys.argv[1])
