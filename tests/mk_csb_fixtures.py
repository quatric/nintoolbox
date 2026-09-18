"""Synthetic Paper Mario collision scene fixtures.

Builds retail-layout .csb files exercising the lib-csb parser: combined
DEADBEEF mesh buffers with slices, a split map-object model, sphere/box
trigger volumes, and both endiannesses (little-endian TTYD/Origami King
layout and big-endian Color Splash layout with u32 flags).

Usage: python3 tests/mk_csb_fixtures.py [outdir]
Defaults to tests/fixtures/.
"""
import os
import struct
import sys


def pad4(b):
    return b + b"\x00" * ((-len(b)) % 4)


def strtab(names):
    return pad4(b"".join(n.encode() + b"\x00" for n in names))


def name_offsets(names):
    offs, o = [], 0
    for n in names:
        offs.append(o)
        o += len(n.encode()) + 1
    return offs


def fix64(name):
    b = name.encode()[:63]
    return b + b"\x00" * (64 - len(b))


def build_csb(big_endian=False):
    e = ">" if big_endian else "<"
    F = lambda *vs: b"".join(struct.pack(e + "f", v) for v in vs)
    V = lambda p: F(*p)
    out = b""

    # --- sphere + box trigger volumes ---
    spheres = [((5.0, 1.0, 5.0), (5.0, 1.0, 5.0), 2.5)]
    boxes = [((0.0, 0.0, 0.0), (4.0, 2.0, 4.0),
              (4.0, 2.0, 4.0), (0.0, 0.0, 0.0))]
    out += struct.pack(e + "I", len(spheres))
    for p1, p2, r in spheres:
        out += F(0.0) + V(p1) + V(p2) + F(r)
    out += struct.pack(e + "I", len(boxes))
    for p1, p2, s, rot in boxes:
        out += F(0.0) + V(p1) + V(p2) + V(s) + V(rot)
        out += F(0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    out += struct.pack(e + "I", 0)  # reserved
    out += struct.pack(e + "I", 1)  # unknown, always 1
    out += b"\x00" * 16

    obj_names = ["trigger0", "zone0"]
    obj_flags = [0x7, 0x100]
    obj_nodes = [5, 6]
    out += b"".join(struct.pack(e + "I", o) for o in name_offsets(obj_names))
    for f in obj_flags:
        out += struct.pack(e + ("I" if big_endian else "Q"), f)
    out += b"".join(struct.pack(e + "H", n) for n in obj_nodes)
    st = strtab(obj_names)
    out += struct.pack(e + "I", len(st)) + st

    # --- combined geometry: ground quad (2 tris) + wall (1 tri) ---
    ground_pos = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0),
                  (10.0, 0.0, 10.0), (0.0, 0.0, 10.0)]
    ground_tris = [(0, 1, 2, (0.0, 1.0, 0.0)),
                   (0, 2, 3, (0.0, 1.0, 0.0))]
    wall_pos = [(0.0, 0.0, 0.0), (0.0, 5.0, 0.0), (0.0, 0.0, 10.0)]
    wall_tris = [(0, 1, 2, (-1.0, 0.0, 0.0))]
    all_pos = ground_pos + wall_pos
    all_tris = ground_tris + [(a + 4, b + 4, c + 4, n) for a, b, c, n in wall_tris]

    mesh_names = ["ground", "wall"]
    mesh_tri_off = [0, 2]
    mesh_vtx_off = [0, 4]
    mesh_flags = [0x3, 0x100]
    mesh_attrs = [1, 5]
    mesh_nodes = [1, 2]

    out += struct.pack(e + "H", 2 if not big_endian else 1)  # unknown3
    out += struct.pack(e + "H", 1)  # num_models (combined group)
    out += struct.pack(e + "I", len(mesh_names))
    out += b"".join(struct.pack(e + "I", o) for o in name_offsets(mesh_names))
    out += b"".join(struct.pack(e + "I", o) for o in mesh_tri_off)
    out += b"".join(struct.pack(e + "I", o) for o in mesh_vtx_off)
    for f in mesh_flags:
        out += struct.pack(e + ("I" if big_endian else "Q"), f)
    out += b"".join(struct.pack(e + "I", a) for a in mesh_attrs)
    out += b"".join(struct.pack(e + "I", 0) for _ in mesh_names)  # model ids
    out += b"".join(struct.pack(e + "H", n) for n in mesh_nodes)
    st = strtab(mesh_names)
    out += struct.pack(e + "I", len(st)) + st

    # node tree: root(4) -> ground, wall, pipe, mid(2) -> sphere, box
    nodes = [(0, 0, 4), (1, 0, 0), (2, 0, 0), (3, 0, 0),
             (4, 0, 2), (5, 0, 0), (6, 0, 0)]
    out += struct.pack(e + "H", len(nodes))
    for i, f, c in nodes:
        out += struct.pack(e + "HBB", i, f, c)

    # combined model body
    out += struct.pack(e + "I", 3)  # unknown0
    out += struct.pack(e + "I", 0)  # id
    if not big_endian:
        out += struct.pack(e + "Q", 0)
    out += struct.pack(e + "I", 0)
    out += struct.pack(e + "I", 0)
    out += fix64("DEADBEEF")
    out += struct.pack(e + "I", 1)
    out += struct.pack(e + "I", len(all_pos))
    out += struct.pack(e + "I", len(all_tris))
    out += V((0.0, 0.0, 0.0)) + V((0.0, 0.0, 0.0)) + V((0.0, 0.0, 0.0))
    mins = [min(p[i] for p in all_pos) for i in range(3)]
    maxs = [max(p[i] for p in all_pos) for i in range(3)]
    out += V(mins) + V(maxs)
    for p in all_pos:
        out += V(p)
    for a, b, c, n in all_tris:
        out += struct.pack(e + "III", a, b, c) + V(n)

    # tail + split model
    out += struct.pack(e + "I", 0)
    out += bytes([0xFE, 0x07]) + struct.pack(e + "H", 0)
    split_pos = [(20.0, 0.0, 20.0), (22.0, 0.0, 20.0), (21.0, 3.0, 20.0)]
    split_tris = [(0, 1, 2, (0.0, 0.0, 1.0))]
    out += struct.pack(e + "I", 1)  # num_split_models
    out += V((20.0, 0.0, 20.0)) + V((22.0, 3.0, 20.0))  # sub bbox
    out += struct.pack(e + "I", 3)  # unknown0
    out += struct.pack(e + "HH", 3, 0)  # node + pad
    out += struct.pack(e + ("I" if big_endian else "Q"), 0x200)
    out += struct.pack(e + "I", 9)  # mat_attr
    if not big_endian:
        out += struct.pack(e + "I", 0)  # unknown4
    out += fix64("pipe")
    out += struct.pack(e + "I", 1)
    out += struct.pack(e + "I", len(split_pos))
    out += struct.pack(e + "I", len(split_tris))
    out += V((0.0, 0.0, 0.0)) + V((20.0, 0.0, 20.0)) + V((0.0, 0.0, 0.0))
    out += V((20.0, 0.0, 20.0)) + V((22.0, 3.0, 20.0))
    for p in split_pos:
        out += V(p)
    for a, b, c, n in split_tris:
        out += struct.pack(e + "III", a, b, c) + V(n)
    return out


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "fixtures")
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "csb_basic.csb"), "wb") as f:
        f.write(build_csb(big_endian=False))
    with open(os.path.join(outdir, "csb_big.csb"), "wb") as f:
        f.write(build_csb(big_endian=True))
    print("wrote csb_basic.csb + csb_big.csb to %s" % outdir)


if __name__ == "__main__":
    main()
