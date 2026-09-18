"""Synthetic TT Games NTT engine .model fixture.

Builds a minimal but structurally faithful .model (LEGO Star Wars: The
Skywalker Saga layout) exercising the lib-nttmodel reader: a hierarchy
chunk plus one scene chunk with a single mesh, two DXTV vertex buffers
(float positions/normals/UVs, byte colours, float tangents) and a u32
index list. Mirrors KillzXGaming/NTT-Model-Dumper Model.cs field order.

    mk-nttmodel-fixtures.py [outdir]   (defaults to tests/fixtures/)
"""
import os
import struct
import sys

HIER = b".CC4HSERHSER"
CSG = b".CC4HSER2CSG"


def chunk(ctype, payload, version=1):
    return struct.pack(">I", 16 + len(payload)) + ctype + struct.pack(">I", version) + payload


def hier_chunk():
    payload = b"root\x00" + b"TestModel\x00" + b"extra\x00"
    return chunk(HIER, payload)


def csg_chunk():
    out = bytearray()
    out += struct.pack(">IHH", 1, 0, 0)  # numMaterials, unk, padding
    # Material: FilePath double-string, Name double-string, 3 pad bytes.
    out += b"models/test.mat\x00" + b"x\x00"
    out += b"TestMat\x00" + b"y\x00"
    out += b"\x00\x00\x00"
    out += struct.pack(">III", 0, 1, 1)  # padding, numMeshes, 1
    # SubMesh: magic, version, numBuffers, unk1, unk2, numVerts.
    out += struct.pack(">IIIIII", 1, 1, 2, 0, 0, 3)
    # Buffer 0: position + normal + uv (floats).
    out += b"DXTV" + struct.pack(">II", 1, 3)
    out += bytes((0, 3, 0, 1, 3, 12, 5, 2, 24))
    out += b"\x00" * 6
    verts0 = [
        ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0)),
        ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0)),
        ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0)),
    ]
    for p, n, uv in verts0:
        out += struct.pack("<fff", *p) + struct.pack("<fff", *n) + struct.pack("<ff", *uv)
    out += b"\x00" * 16
    # Buffer 1: colour (bytes) + tangent (float4).
    out += b"DXTV" + struct.pack(">II", 1, 2)
    out += bytes((2, 9, 0, 3, 4, 4))
    out += b"\x00" * 6
    cols = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)]
    for c in cols:
        out += bytes(c) + struct.pack("<ffff", 1.0, 0.0, 0.0, 1.0)
    out += b"\x00" * 16
    # Indices: u32 triangle list + per-mesh footer.
    out += struct.pack(">II", 3, 4)
    out += struct.pack("<III", 0, 1, 2)
    out += b"\x00" * (70 + 4 * 2)
    return chunk(CSG, bytes(out))


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "fixtures")
    os.makedirs(outdir, exist_ok=True)
    blob = hier_chunk() + csg_chunk()
    path = os.path.join(outdir, "nttmodel_tri.model")
    open(path, "wb").write(blob)
    print("wrote", path, len(blob), "bytes")


main()
