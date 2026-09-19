#!/usr/bin/env python3
# Stdlib-only synthetic Game Freak 3DS fixtures for regress.sh.
#
# Builds minimal but structurally complete SPICA GFL2 blobs (see
# SPICA/Formats/GFL2 in gdkchan/SPICA, wrapped by KillzXGaming/3dsTools):
#   gftex.gftex    GFTexture RGBA8 8x8 checkerboard (PICA morton-swizzled)
#   gfmodel.gfmodel GFModel: 1 bone, 1 material, 1 mesh (1 triangle)
#   gfmot.gfmot    GFMotion: subheader + 1 skeletal bone, 1 const track
#   gfpack.gfpack  GFModelPack: 1 model + 1 texture entry
#   sample.gfpkg   GFPackage "PC": gftex + gfmot entries
#   sample.gflxpack GFLXPack: 1 literals-only LZ4 member
# Usage: mk_gf3ds.py OUTDIR
import struct
import sys
import os


def gfnv1(name):
    h = 16777619
    for b in name.encode('ascii'):
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= b
    return h


def blen(s):
    b = s.encode('ascii')
    return struct.pack('B', len(b)) + b


def section(magic, payload):
    m = magic.encode('ascii')
    m += b'\0' * (8 - len(m))
    return m + struct.pack('<II', len(payload), 0xFFFFFFFF) + payload


def morton8(x, y):
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)


def gftex(name='tex0', w=8, h=8, fmt=0x04):
    px = bytearray(w * h * 4)
    tw = (w + 7) & ~7
    for y in range(h):
        for x in range(w):
            pos = ((y // 8) * (tw // 8) + (x // 8)) * 64 + morton8(x & 7, y & 7)
            red = ((x // 2) + (y // 2)) % 2 == 0
            px[pos * 4:pos * 4 + 4] = bytes((255, 0, 0, 255)) if red else bytes((0, 255, 0, 255))
    body = struct.pack('<I', len(px)) + b'\0' * 12
    body += name.encode('ascii') + b'\0' * (0x40 - len(name))
    body += struct.pack('<HHHH', w, h, fmt, 1) + b'\xff' * 16 + bytes(px)
    blob = struct.pack('<II', 0x15041213, 1) + section('texture', body)
    return blob


def gfmaterial(matname='mat0', texname='tex0'):
    p = b''
    for nm in (matname, 'sh0', 'vsh0', 'fsh0'):
        p += struct.pack('<I', gfnv1(nm)) + blen(nm)
    p += struct.pack('<III', 0, 0, 0)  # LUT hashes
    p += struct.pack('<I', 0)  # padding
    p += bytes([0] * 8)  # bump + assigns + pad
    p += bytes([0xCC] * 48)  # 12 RGBA colours
    p += struct.pack('<iii', 0, 0, 0)  # edge
    p += struct.pack('<i', 0)  # projection
    p += struct.pack('<ffff', 0, 0, 0, 0)  # rim/phong
    p += struct.pack('<i', 0)  # idedge offset
    p += struct.pack('<i', 0)  # edge mask
    p += struct.pack('<9i', *([0] * 9))  # bake
    p += struct.pack('<i', 0)  # vtx shader type
    p += struct.pack('<ffff', 0, 0, 0, 0)  # params
    p += struct.pack('<I', 1)  # one texture unit
    p += struct.pack('<I', gfnv1(texname)) + blen(texname)
    p += struct.pack('<BB', 0, 0)  # unit, mapping
    p += struct.pack('<ff', 1.0, 1.0) + struct.pack('<f', 0.0) + struct.pack('<ff', 0.0, 0.0)
    p += struct.pack('<IIIII', 1, 1, 0, 0, 0)  # wrap/filter/lod
    while len(p) % 16:
        p += b'\0'
    p += struct.pack('<I', 0)  # commands length
    p += struct.pack('<iIiIIII', 0, 0, 0, 0, 0, 0, 0)  # prio,?,layer,4 hashes
    return section('material', p)


def pica_cmds(pairs):
    # non-consecutive single-register writes: (param, reg) words
    w = bytearray()
    for reg, param in pairs:
        w += struct.pack('<II', param, reg)
    return bytes(w)


def gfmesh(meshname='mesh0', matname='mat0'):
    enable = pica_cmds([
        (0x201, 0xB),  # FORMAT_LOW: slot0 = float x3
        (0x204, 0x0),  # CONFIG1
        (0x205, (12 << 16) | (1 << 28)),  # CONFIG2: stride 12
        (0x2BB, 0x0),  # PERM_LOW: slot0 = position
        (0x242, 0x0),  # NUM_ATTR: 1 total
    ])
    index = pica_cmds([
        (0x227, 0x80000000),  # INDEXBUFFER_CONFIG: 16-bit
        (0x228, 0x3),  # NUMVERTICES
        (0x25E, 0x0000),  # PRIMITIVE_CONFIG: triangles
    ])
    cmds = b''
    for i, c in enumerate((enable, b'', index)):
        cmds += struct.pack('<IIII', len(c), i, 3, 0) + c
    verts = struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)
    idx = struct.pack('<3H', 0, 1, 2) + b'\0' * 10
    desc = struct.pack('<I', gfnv1(matname)) + struct.pack('<i', 4) + b'mat0'
    desc += struct.pack('<B', 0) + bytes(31)  # boneCount + map
    desc += struct.pack('<iiii', 3, 3, len(verts), len(idx))
    body = struct.pack('<I', gfnv1(meshname)) + meshname.encode() + b'\0' * (0x40 - len(meshname))
    body += struct.pack('<I', 0)
    body += struct.pack('<4f', 0, 0, 0, 1) * 2  # bbox
    body += struct.pack('<Ii', 1, 0)  # nSub, boneIndicesPerVertex
    body += b'\xff' * 16
    body += cmds + desc + verts + idx
    while len(body) % 16:
        body += b'\0'
    body += b'\0' * 16
    return section('mesh', body)


def gfmodel():
    tables = b''
    for names in (['sh0'], ['tex0'], ['mat0'], ['mesh0']):
        tables += struct.pack('<I', len(names))
        for nm in names:
            tables += struct.pack('<I', gfnv1(nm)) + nm.encode() + b'\0' * (0x40 - len(nm))
    body = tables
    body += struct.pack('<4f', -1, -1, -1, 1) + struct.pack('<4f', 1, 1, 1, 1)
    body += struct.pack('<16f', *[1 if i % 5 == 0 else 0 for i in range(16)])
    body += struct.pack('<II', 0, 0x10) + b'\0' * 24  # unk len/off + pad + data
    body += struct.pack('<i', 1) + b'\0' * 12  # 1 bone
    body += blen('root') + blen('') + bytes([1])
    body += struct.pack('<9f', 1, 1, 1, 0, 0, 0, 0, 0, 0)
    while len(body) % 16:
        body += b'\0'
    body += struct.pack('<ii', 0, 0x420)  # no LUTs
    while len(body) % 16:
        body += b'\0'
    blob = struct.pack('<II', 0x15122117, 3)
    blob += b'\0' * (16 - 8)
    blob += section('gfmodel', body)
    blob += gfmaterial()
    blob += gfmesh()
    return blob


def gfmotion():
    sub = struct.pack('<I', 30) + struct.pack('<HH', 1, 0)
    sub += struct.pack('<3f', -1, -1, -1) + struct.pack('<3f', 1, 1, 1)
    sub += struct.pack('<I', 0x1234)
    skel = struct.pack('<iI', 1, 5) + blen('root')
    skel += struct.pack('<II', 3, 4) + struct.pack('<f', 1.0)  # SX const
    blob = struct.pack('<II', 0x00060000, 2)
    blob += struct.pack('<III', 0, len(sub), 8 + 2 * 12)
    blob += struct.pack('<III', 1, len(skel), 8 + 2 * 12 + len(sub))
    blob += sub + skel
    return blob


def gfpack(model, tex):
    m_entry = blen('model0') + struct.pack('<I', 0)  # addr patched below
    t_entry = blen('tex0') + struct.pack('<I', 0)
    tab = struct.pack('<IIIII', 1, 1, 0, 0, 0)
    ptrs = [0x18, 0x1C]
    base = 0x18 + 8
    m_off = base + len(m_entry) + len(t_entry)
    t_off = m_off + len(model)
    m_entry = blen('model0') + struct.pack('<I', m_off)
    t_entry = blen('tex0') + struct.pack('<I', t_off)
    return struct.pack('<II', 0x00010000, 0)[:4] + tab + struct.pack('<II', base, base + len(m_entry)) \
        + m_entry + t_entry + model + tex


def gfpackage(tex, mot):
    offs = [4 + 3 * 4, 4 + 3 * 4 + len(tex), 4 + 3 * 4 + len(tex) + len(mot)]
    blob = b'PC' + struct.pack('<H', 2)
    blob += struct.pack('<III', *offs)
    return blob + tex + mot


def lz4_literals(data):
    # literals-only raw LZ4 block
    out = bytearray()
    n = len(data)
    pos = 0
    first = True
    while pos < n:
        take = min(n - pos, 15) if first else min(n - pos, 15)
        if n - pos > 15 and first:
            take = 15
        out.append((take << 4) | 0)
        if take == 15:
            rem = n - pos - 15
            while rem >= 255:
                out.append(255)
                rem -= 255
            out.append(rem)
            out += data[pos:]
            break
        out += data[pos:pos + take]
        pos += take
        first = False
    return bytes(out)


def gflxpack():
    raw = b'GFLXHello'
    comp = lz4_literals(raw)
    info = 0x38
    hdr = b'GFLXPACK' + struct.pack('<Q', 0) + struct.pack('<II', 1, 0)
    hdr += struct.pack('<Q', info) + struct.pack('<QQQ', 0, 0, 0)
    entry = struct.pack('<IIII', 0, len(raw), len(comp), 0) + struct.pack('<Q', info + 24)
    return hdr + entry + comp


def gf1motion():
    # GF1MotionPack: count + offset table (pack-relative); entry 0 is the
    # skeleton, entries 1.. are animations (0 = absent).
    skel = struct.pack('<BB', 2, 0)  # 2 bones, first index 0
    skel += struct.pack('<BBB', 0, 0, 0)  # bone1: parent, flags, children
    skel += b'bone1\0'
    while len(skel) % 4:
        skel += b'\0'
    skel += struct.pack('<3f4f', 0, 0, 0, 0, 0, 0, 1)  # bone0 bind
    skel += struct.pack('<3f4f', 1, 0, 0, 0, 0, 0, 1)  # bone1 bind
    anim = struct.pack('<HH', 2, 30)  # 2 octets, 30 frames
    anim += struct.pack('<I', 1 | (1 << 3))[:3]  # octet pair [1,1]: skip
    while len(anim) % 4:
        anim += b'\0'
    skel_off = 4 + 2 * 4
    anim_off = skel_off + len(skel)
    blob = struct.pack('<III', 2, skel_off, anim_off) + skel + anim
    return blob


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    tex = gftex()
    model = gfmodel()
    mot = gfmotion()
    files = {
        'gftex.gftex': tex,
        'gfmodel.gfmodel': model,
        'gfmot.gfmot': mot,
        'gf1mot.gf1mot': gf1motion(),
        'gfpack.gfpack': gfpack(model, tex),
        'sample.gfpkg': gfpackage(tex, mot),
        'sample.gflxpack': gflxpack(),
    }
    for name, blob in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(blob)
        print('wrote %s (%d bytes)' % (name, len(blob)))


main()
