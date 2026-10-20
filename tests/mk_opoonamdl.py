#!/usr/bin/env python3
# Synthetic Opoona model package (.mdl: DHDM/TMDM/BODM/SMDM/TSDM chunks + empty .tex)
# with a sibling .mot clip, and a .map whose LDMF entry holds an SL01 model package.
import struct, sys, os
out = sys.argv[1]
BE = '>'

def chunk(tag, payload):
    return tag + struct.pack('>I', len(payload)) + payload

def skin_vertex(pos, nrm, uv):
    return struct.pack('>3f3fHHf', *pos, *nrm, uv[0], uv[1], 1.0)

def static_vertex(pos, nrm, uv):
    return struct.pack('>3f3f4B2f', *pos, *nrm, 255, 255, 255, 255, *uv)

def model(static=False):
    tris = [(0, 0, 0), (4, 0, 0), (0, 4, 0), (4, 4, 0)]  # strip of 4 vertices
    nrm = (0, 0, 1)
    uvs = [(0, 0), (4096, 0), (0, 4096), (4096, 4096)]
    if static:
        verts = b''.join(static_vertex(p, nrm, (u / 4096.0, v / 4096.0)) for p, (u, v) in zip(tris, uvs))
        ty = 0x9a
    else:
        verts = b''.join(skin_vertex(p, nrm, uv) for p, uv in zip(tris, uvs))
        ty = 0x9b
    group = struct.pack('>HH', ty, len(tris)) + verts
    part_size = 0x30 + len(group)
    part = struct.pack('>HHHHII', 8, 91, len(tris), 0, 2, part_size)
    part += struct.pack('>8H', 0, 0, 0, 0, 0, 0, 0, 0) + b'\0' * 8
    part += struct.pack('>HHI', 0 if static else 1, 1, 0xffffffff) + group
    mesh_size = 0x30 + len(part)
    mesh = struct.pack('>IIH', mesh_size, 0x1002, 1).ljust(0x30, b'\0') + part

    dhdm = chunk(b'DHDM', b'\0' * 0x38)
    mat = bytearray(0x110)
    for i in range(4):
        struct.pack_into('>f', mat, 0xc0 + 4 * i, 1.0)
    struct.pack_into('>HH', mat, 0x104, 0xffff, 0xffff)
    tmdm = chunk(b'TMDM', struct.pack('>IIII', 0, 1, 0, 0)[:8] + bytes(mat))
    tmdm = chunk(b'TMDM', struct.pack('>II', 0, 1) + bytes(mat))
    node = bytearray(0xc0)
    struct.pack_into('>3f', node, 0, 0, 0, 0)
    struct.pack_into('>3f', node, 0x20, 1, 1, 1)
    struct.pack_into('>I', node, 0xc, 0x8000)
    bodm = chunk(b'BODM', struct.pack('>II', 0, 1) + bytes(node))
    tsdm = chunk(b'TSDM', b'Frame_root\0\0\0\0\0\0')
    pre = len(dhdm) + len(tmdm) + len(bodm)
    smdm_hdr = 16 + 16
    mesh_off = pre + smdm_hdr
    smdm = chunk(b'SMDM', struct.pack('>IIII', 0, 1, mesh_off, 0)[:8] + struct.pack('>IIII', mesh_off, 0, 0, 0) + mesh)
    return dhdm + tmdm + bodm + smdm + tsdm + b'\0' * 8

def pkg(members):
    n = len(members)
    off = 8 + 0x20 * n
    hdr = struct.pack('<IHH', n, 0x20, 0x20)
    body = b''
    for name, data in members:
        hdr += struct.pack('<II', off + len(body), len(data)) + name.ljust(24, b'\0')
        body += data
    return hdr + body

def lit_lzo(data):
    # literal-only LZO1X stream (first byte 17 + n only reaches n = 238; use the t < 16 form beyond)
    assert len(data) >= 4
    n = len(data) - 3
    if n < 16:
        s = bytes([n]) + data
    else:
        s = b'\0'
        n -= 15
        while n > 255:
            s += b'\0'
            n -= 255
        s += bytes([n]) + data
    return s + b'\x11\x00\x00'

def sl01(data):
    s = lit_lzo(data)
    return b'SL01' + struct.pack('<II', len(data), len(s)) + s

def mot():
    nb = 1
    rec = bytearray(80)
    struct.pack_into('>HHHH', rec, 0, 0, 2, 0, 0)  # 2 rotation keys
    struct.pack_into('>3f', rec, 0x20, 0, 0, 0)
    struct.pack_into('>4f', rec, 0x30, 0, 0, 0, 1)
    struct.pack_into('>3f', rec, 0x40, 1, 1, 1)
    keys = struct.pack('>hhhh', 0, 0, 0, 4096) + struct.pack('>hhhh', 0, 2896, 0, 2896)
    base = 0x30 + 80 * nb
    struct.pack_into('>I', rec, 0x10, base)
    hdr = struct.pack('>IffI', 0, 1.0, 30.0, nb).ljust(0x30, b'\0')
    body = hdr + bytes(rec) + keys
    body = body + b'\0' * 32
    body = bytearray(body)
    struct.pack_into('>I', body, 0x2c, len(body) - 32)
    return bytes(body)

m = model()
os.makedirs(os.path.join(out, 'c'), exist_ok=True)
open(os.path.join(out, 'c', 'p001_01.mdl'), 'wb').write(pkg([(b'x/p001_01.mdl', m), (b'x/p001_01.tex', pkg([])), (b'x/p001_01.tmp', b'\0' * 16)]))
open(os.path.join(out, 'c', 'p001_01.mot'), 'wb').write(mot())

# map: RDHF + MANF + LDMF (one SL01 model package) + XETF (no textures)
mp = pkg([(b'a/stat.mdl', model(static=True)), (b'NULL', b'')])
blob = sl01(mp)
rdhf = chunk(b'RDHF', b'a_test\0'.ljust(152, b'\0'))
manf = chunk(b'MANF', b'static_obj\0\0\0\0\0\0')
xetf = chunk(b'XETF', struct.pack('>II', 0, 0))
ldmf_len = 8 + 8 + 0x20
pos = len(rdhf) + len(manf) + ldmf_len + len(xetf)
pos = (pos + 15) & ~15
manf_off = len(rdhf) + 8
ent = struct.pack('>IIIIIIII', pos, len(mp), 0, 0, 0, 0, manf_off, 0)
ldmf = chunk(b'LDMF', struct.pack('>II', 1, 0) + ent)
data = rdhf + manf + ldmf + xetf
data = data.ljust(pos, b'\0') + blob + b'\0' * 8
open(os.path.join(out, 'c', 'room.map'), 'wb').write(data)
