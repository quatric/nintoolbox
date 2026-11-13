#!/usr/bin/env python3
"""Synthetic fixtures for the Switch 2 Zelda (Tears-of-the-Kingdom engine) formats.

usage: mk_zelda_switch2.py DIR
writes into DIR:
  ZsDic.pack.zs / Model/a.bin.zs  zstd dictionary pack + a file compressed with dict #1
  flow.bfevfl                     BFEVFL 0x0400 (action, switch, return, sub flow switch)
  hght.hght / mate.mate           TARB tiles: R32F heights, two RGBA8 slices (GPU block)
  hght.expect                     expected 16 bit height samples (u16, little endian)
"""
import os, struct, subprocess, sys

out = sys.argv[1]
os.makedirs(out + "/Pack", exist_ok=True)
os.makedirs(out + "/Model", exist_ok=True)


# ---- zstd dictionary pack ---------------------------------------------------
def zstd(*a, inp=None):
    return subprocess.run(["zstd", "-q", "-c", *a], input=inp, check=True, capture_output=True).stdout


samples = os.path.join(out, "samples")
os.makedirs(samples, exist_ok=True)
for i in range(200):
    open(f"{samples}/s{i}.txt", "w").write(
        "".join(f"Actor_{i}_{k}: {{pos: [{i}.0, {k}.5, 0.0], hash: {i * k * 977}}}\n" for k in range(30)))
subprocess.run(["zstd", "-q", "--train", *[f"{samples}/s{i}.txt" for i in range(200)], "-o", out + "/d.zsdic",
                "--maxdict=4096"], check=True, capture_output=True)
dic = bytearray(open(out + "/d.zsdic", "rb").read())
assert dic[:4] == b"7\xa40\xec"
dic[4:8] = struct.pack("<I", 1)  # the game's dictionary IDs are small numbers
open(out + "/d.zsdic", "wb").write(dic)


def sarc(files):
    names = b""
    offs = []
    for n, _ in files:
        offs.append(len(names) // 4)
        names += n.encode() + b"\0"
        names += b"\0" * (-len(names) % 4)
    data = b""
    ents = []
    for (n, d), o in zip(files, offs):
        data += b"\0" * (-len(data) % 8)
        ents.append((len(data), len(data) + len(d)))
        data += d
    hdr_end = 0x14 + 0x0c + 16 * len(files) + 8 + len(names)
    data_off = hdr_end + (-hdr_end % 0x100)
    b = b"SARC" + struct.pack("<HHIIHH", 0x14, 0xFEFF, data_off + len(data), data_off, 0x100, 0)
    b += b"SFAT" + struct.pack("<HHI", 0xc, len(files), 0x65)
    for (n, d), o, (s, e) in zip(files, offs, ents):
        b += struct.pack("<IIII", 0, 0x01000000 | o, s, e)
    b += b"SFNT" + struct.pack("<HH", 8, 0) + names
    b += b"\0" * (data_off - len(b))
    return b + data


payload = b"".join(f"Actor_7_{k}: {{pos: [7.0, {k}.5, 0.0], hash: {7 * k * 977}}}\n".encode() for k in range(40))
dpack = sarc([("zs.zsdic", bytes(dic))])
open(out + "/Pack/ZsDic.pack.zs", "wb").write(zstd(inp=dpack))
open(out + "/Model/a.txt.zs", "wb").write(zstd("-D", out + "/d.zsdic", inp=payload))
open(out + "/a.expect", "wb").write(payload)
fid = struct.unpack("<I", open(out + "/Model/a.txt.zs", "rb").read()[6:7] + b"\0\0\0")[0]
assert fid in (1, 0), fid


# ---- BFEVFL ------------------------------------------------------------------
class W:
    def __init__(self):
        self.b = bytearray()

    def align(self, n):
        self.b += b"\0" * (-len(self.b) % n)

    def alloc(self, n, al=8):
        self.align(al)
        o = len(self.b)
        self.b += b"\0" * n
        return o

    def p64(self, o, v):
        self.b[o:o + 8] = struct.pack("<Q", v)

    def p32(self, o, v):
        self.b[o:o + 4] = struct.pack("<I", v)

    def p16(self, o, v):
        self.b[o:o + 2] = struct.pack("<H", v)

    def pascal(self, s):
        self.align(2)
        o = len(self.b)
        self.b += struct.pack("<H", len(s)) + s.encode() + b"\0"
        return o


w = W()
w.alloc(0x48)
w.b[0:8] = b"BFEVFL\0\0"
w.b[8:10] = struct.pack("<H", 0x0400)
w.b[0xc:0xe] = struct.pack("<H", 0xFEFF)
w.b[0xe] = 3
S = {}


def st(s):
    if s not in S:
        S[s] = w.pascal(s)
    return S[s]


def dic(names):
    o = w.alloc(8 + 16 * (len(names) + 1))
    w.b[o:o + 4] = b"DIC "
    w.p32(o + 4, len(names))
    w.p32(o + 8, 0xFFFFFFFF)
    w.p64(o + 16, st(""))
    for i, n in enumerate(names):
        w.p64(o + 8 + 16 * (i + 1) + 8, st(n))
    return o


def container(items):
    # items: name -> (tag, value)
    d = dic(list(items))
    c = w.alloc(0x10 + 8 * len(items))
    w.b[c] = 1
    w.p16(c + 2, len(items))
    w.p64(c + 8, d)
    for i, (n, (tag, v)) in enumerate(items.items()):
        it = w.alloc(0x18)
        w.b[it] = tag
        w.p16(it + 2, 1)
        if tag == 2:
            w.b[it + 0x10:it + 0x14] = struct.pack("<i", v)
        elif tag == 3:
            w.b[it + 0x10:it + 0x14] = struct.pack("<I", 0x80000001 if v else 0)
        elif tag == 4:
            w.b[it + 0x10:it + 0x14] = struct.pack("<f", v)
        elif tag == 5:
            w.p64(it + 0x10, st(v))
        w.p64(c + 0x10 + 8 * i, it)
    return c


hdr_name = w.pascal("")  # placeholder so offsets are non-zero
name_off = len(w.b)
w.b += b"flow_test\0"
w.align(8)
fcp = w.alloc(8)  # pointer array for the flowchart
w.p64(0x28, fcp)
fc = w.alloc(0x48)
w.p64(fcp, fc)
w.b[fc:fc + 4] = b"EVFL"
w.p32(0x10, name_off)
w.p16(0x20, 1)

actors = w.alloc(0x38 * 2)
actions0 = w.alloc(8)
queries1 = w.alloc(8)
w.p64(actions0, st("DoThing"))
w.p64(queries1, st("CheckThing"))
for i, nm in enumerate(["ActorA", "ActorB"]):
    a = actors + 0x38 * i
    w.p64(a, st(nm))
    w.p64(a + 8, st(""))
    w.p64(a + 0x10, st(""))
    if i == 0:
        w.p64(a + 0x18, actions0)
        w.p16(a + 0x30, 1)
    else:
        w.p64(a + 0x20, queries1)
        w.p16(a + 0x32, 1)
    w.p16(a + 0x34, 0xFFFF)
    w.p16(a + 0x36, 1)
    w.p64(a + 0x28, container({"Mode": (2, 7), "Flag": (3, True)}))

NEV = 5
events = w.alloc(0x30 * NEV)
cases = w.alloc(16)
w.b[cases:cases + 8] = struct.pack("<IHH", 5, 2, 0)
w.b[cases + 8:cases + 16] = struct.pack("<IHH", 0xFFFFFFFF, 3, 0)  # -1


def ev(i, typ):
    o = events + 0x30 * i
    w.p64(o, st(f"Event{i}"))
    w.b[o + 8] = typ
    return o + 10


d = ev(0, 0)  # action
w.p16(d, 1); w.p16(d + 2, 0); w.p16(d + 4, 0)
w.p64(d + 6, container({"Frames": (2, 30), "Ratio": (4, 0.5), "Label": (5, "hi")}))
d = ev(1, 1)  # switch
w.p16(d, 2); w.p16(d + 2, 1); w.p16(d + 4, 0); w.p64(d + 14, cases)
d = ev(2, 3)  # join
w.p16(d, 0xFFFF)
d = ev(3, 6)  # return
w.b[d + 2:d + 6] = struct.pack("<i", 9)
d = ev(4, 5)  # sub flow switch
w.p16(d, 1); w.p64(d + 14, cases + 8 - 8)
w.p64(d + 22, st("OtherFlow")); w.p64(d + 30, st("Entry"))
w.p16(d, 1)

epdic = dic(["Start"])
epa = w.alloc(0x20)
w.p16(epa + 0x1c, 0)
w.p64(epa + 8, 0)
w.p64(fc + 0x20, st("flow_test"))
w.p64(fc + 0x28, actors)
w.p64(fc + 0x30, events)
w.p64(fc + 0x38, epdic)
w.p64(fc + 0x40, epa)
w.p16(fc + 0x10, 2)
w.p16(fc + 0x16, NEV)
w.p16(fc + 0x18, 1)
w.p32(0x10, name_off)
w.p32(0x1c, len(w.b))
open(out + "/flow.bfevfl", "wb").write(bytes(w.b))


# ---- TARB tiles ---------------------------------------------------------------
def tarb_file(pl):
    h = b"TARB" + struct.pack("<I", 1) + struct.pack("<H", 1) + struct.pack("<H", 0x14) + struct.pack("<I", len(pl)) + struct.pack("<I", 0)
    return h + pl


grid = 8
heights = [((x * 7 + y * 3) % 17) / 40.0 for y in range(grid) for x in range(grid)]
data = b"".join(struct.pack("<f", v) for v in heights)
pl = bytearray(0x34)
pl[0:2] = struct.pack(">H", grid)
pl[2] = 1
pl[3] = 1
pl[4:8] = bytes([0x0b, 0, 0, 0])
pl[8] = 0x01
pl[0x14:0x18] = bytes([0x0b, 0, 0, 0])
pl[0x19] = 1
pl[0x1c:0x24] = struct.pack("<iI", 0x34 - 0x1c, len(data))
open(out + "/hght.hght", "wb").write(tarb_file(bytes(pl) + data))
open(out + "/hght.expect", "wb").write(b"".join(struct.pack("<H", round(min(max(v, 0), 1) * 65535)) for v in heights))


# ---- small data tables: BKLT, culling boxes, foliage impostor info, MCPK container ------------
def f32s(*v):
    return b"".join(struct.pack("<f", x) for x in v)


bk = b"BKLT" + struct.pack("<IIII", 1, 1, 1, 1)
bk += f32s(1, 2, 3, 0.5, 0.25, 0.125, 2)  # point
bk += f32s(1, 2, 3, 0, 0, 1, 1, 1, 1, 0.5, 0.75, 4, 8)  # spot
bk += f32s(0, 0, 0, 0, 0, 1, 0, 1, 0, 3, 3, 3, 2, 4, 5)  # area
open(out + "/test.bklight", "wb").write(bk)

bb = struct.pack("<II", 2, 2)
for i in range(2):
    bb += struct.pack("<II", 0x100 + i, 1) + f32s(i * 0.5, 1.5, -2) + f32s(0, 0, 0) + f32s(4, 8, 16)
open(out + "/Boxes.bbox", "wb").write(bb)

nm = b"treeA"
fl = struct.pack("<IIII", 2, 0, 1, 1) + bytes([len(nm)]) + nm + bytes(255 - len(nm))
fl += struct.pack("<IIII", 0, 1, 7, 0) + f32s(*range(20))
open(out + "/imp.flnt", "wb").write(fl)

secA = b"FMSH" + struct.pack("<IIIII", 0x10000, 0x1000, 0x400, 0x800, 0x129) + bytes(16)
secB = bytes(40)
mc = b"MCPK" + struct.pack("<BBHI", 0, 0, 1, 0x1234) + struct.pack("<IIII", len(secA), 0x2000, len(secB), 0x803) + secA + secB
open(out + "/m.bfres.mc", "wb").write(mc)
