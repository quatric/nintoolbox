#!/usr/bin/env python3
"""Build synthetic fixtures for the QuickBMS-derived flat archive ports.

One fixture per format, laid out exactly as the corresponding public .bms
script describes:

  sfz.dat        Star Fox Zero DAT (Wii U)          star_fox_zero_dat.bms
  mlpj.bg4       Mario & Luigi: Paper Jam BG4 (3DS) mario_luigi_paper.bms
  hwl.idx/.bin   Hyrule Warriors Legends (3DS)      hyrule_warriors_legends.bms
  xeno.arc       Xenoblade Chronicles 3D "cram"     xenoblade_arc.bms
  mii_sa01.bin   Mii Maker SA01 (Wii U)             mii_maker.bms
  amiibo.cbarc   amiibo Settings CA01 (3DS)         amiibo.bms
  msr.pkg        Metroid: Samus Returns (3DS)       metroid_sr_3ds.bms

These exercise the scanners' structure handling; they are not retail data.
BG4's BLZ-compressed member path is not covered here (no BLZ encoder is
reachable from this script), only its uncompressed path.
"""
import os
import struct
import sys
import zlib

BE = lambda v: struct.pack(">I", v)
LE = lambda v: struct.pack("<I", v)
L2 = lambda v: struct.pack("<H", v)

PAYLOAD = [
    (b"bxm", b"model.bxm", b"HELLO-BXM-PAYLOAD-0001"),
    (b"wtb", b"tex.wtb", b"WTBDATA" * 7),
    (b"dat", b"sub.dat", bytes(33)),
]


def star_fox_zero_dat():
    n = len(PAYLOAD)
    stride = (max(len(p[1]) for p in PAYLOAD) + 1 + 3) & ~3
    off_offsets = 32
    off_exts = off_offsets + n * 4
    off_names = off_exts + n * 4
    off_sizes = off_names + 4 + n * stride
    off_hash = off_sizes + n * 4
    cur = (off_hash + 20 + 15) & ~15
    offs = []
    for _, _, d in PAYLOAD:
        offs.append(cur)
        cur = (cur + len(d) + 15) & ~15
    b = bytearray(cur)
    b[0:4] = b"DAT\0"
    b[4:8] = BE(n)
    for i, v in enumerate([off_offsets, off_exts, off_names, off_sizes, off_hash, 0]):
        b[8 + i * 4 : 12 + i * 4] = BE(v)
    b[off_names : off_names + 4] = BE(stride)
    for i, (e, nm, d) in enumerate(PAYLOAD):
        b[off_offsets + i * 4 : off_offsets + i * 4 + 4] = BE(offs[i])
        b[off_sizes + i * 4 : off_sizes + i * 4 + 4] = BE(len(d))
        b[off_exts + i * 4 : off_exts + i * 4 + len(e)] = e
        b[off_names + 4 + i * stride : off_names + 4 + i * stride + len(nm)] = nm
        b[offs[i] : offs[i] + len(d)] = d
    return bytes(b)


def paper_jam_bg4():
    n = len(PAYLOAD)
    names = b""
    noffs = []
    for _, nm, _ in PAYLOAD:
        noffs.append(len(names))
        names += nm + b"\0"
    data_off = (16 + n * 14 + len(names) + 3) & ~3
    b = bytearray(b"BG4\0" + L2(0) + L2(n) + LE(data_off) + LE(0))
    cur = data_off
    offs = []
    for _, _, d in PAYLOAD:
        offs.append(cur)
        cur += len(d)
    for i, (_, _, d) in enumerate(PAYLOAD):
        b += LE(offs[i]) + LE(len(d)) + LE(0xCAFE) + L2(noffs[i])
    b += names
    b += bytes(data_off - len(b))
    for _, _, d in PAYLOAD:
        b += d
    return bytes(b)


def xenoblade_cram():
    n = len(PAYLOAD)
    blob = b""
    noffs = []
    for _, nm, _ in PAYLOAD:
        noffs.append(len(blob))
        blob += nm + b"\0"
    names_off = 16 + n * 16 + n * 4
    data_start = (names_off + len(blob) + 3) & ~3
    b = bytearray(b"cram" + LE(n) + LE(0x80) + LE(names_off))
    cur = data_start
    offs = []
    for _, _, d in PAYLOAD:
        offs.append(cur)
        cur += len(d)
    for i, (e, _, d) in enumerate(PAYLOAD):
        b += LE(0xDEADBEEF) + e.ljust(4, b"\0") + LE(offs[i]) + LE(len(d))
    for x in noffs:
        b += LE(x)
    b += blob
    b += bytes(data_start - len(b))
    for _, _, d in PAYLOAD:
        b += d
    return bytes(b)


def _inner_a01(magic, named):
    n = len(PAYLOAD)
    rec = 0x80 if named else 0
    base = 12 + n * 4 + n * 4 + n * rec
    b = bytearray(magic + BE(n) + BE(base))
    cur = 0
    offs = []
    for _, _, d in PAYLOAD:
        offs.append(cur)
        cur += len(d)
    for o in offs:
        b += BE(o)
    for _, _, d in PAYLOAD:
        b += BE(len(d))
    if named:
        for _, nm, _ in PAYLOAD:
            b += nm.ljust(rec, b"\0")
    for _, _, d in PAYLOAD:
        b += d
    return bytes(b)


def mii_maker_sa01():
    inner = _inner_a01(b"SA01", True)
    return BE(len(inner)) + zlib.compress(inner, 9)


def amiibo_ca01():
    inner = _inner_a01(b"CA01", False)
    h = bytearray(0x80)
    h[0:4] = b"ZCMP"
    h[8:12] = BE(1)
    h[16:20] = BE(len(inner))
    return bytes(h) + zlib.compress(inner, 9)


def metroid_sr():
    n = len(PAYLOAD)
    info = 12 + n * 12
    cur = info
    recs = []
    for _, _, d in PAYLOAD:
        recs.append((cur, cur + len(d)))
        cur += len(d)
    b = bytearray(LE(info) + LE(cur - info) + LE(n))
    for lo, hi in recs:
        b += LE(0x11223344) + LE(lo) + LE(hi)
    for _, _, d in PAYLOAD:
        b += d
    return bytes(b)


def hyrule_warriors():
    binb = bytearray()
    idx = bytearray(LE(0) + LE(0))  # leading hole, skipped by the scanner
    for _, _, d in PAYLOAD:
        idx += LE(len(d)) + LE(len(binb))
        binb += d
    return bytes(idx), bytes(binb)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(os.path.join(out, "expect"), exist_ok=True)
    for _, nm, d in PAYLOAD:
        with open(os.path.join(out, "expect", nm.decode()), "wb") as f:
            f.write(d)
    idx, binb = hyrule_warriors()
    files = {
        "sfz.dat": star_fox_zero_dat(),
        "mlpj.bg4": paper_jam_bg4(),
        "xeno.arc": xenoblade_cram(),
        "mii_sa01.bin": mii_maker_sa01(),
        "amiibo.cbarc": amiibo_ca01(),
        "msr.pkg": metroid_sr(),
        "hwl.idx": idx,
        "hwl.bin": binb,
    }
    for name, data in files.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
