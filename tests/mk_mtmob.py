#!/usr/bin/env python3
# Stdlib-only synthetic Capcom MT Framework Mobile + ModelBinary fixtures
# for regress.sh (see SPICA/Formats/MTFramework and SPICA/Formats/ModelBinary
# in gdkchan/SPICA, wrapped by KillzXGaming/3dsTools):
#   mttex.tex  MTTEX RGBA8 8x8 checkerboard (PICA morton-swizzled payload)
#   mtmod.mod  MTMOD: 1 bone, 1 mesh (stride-32 float fallback layout)
#   mtmod_mfx.mod + companion.mfx: 1 mesh, stride-20 layout via MFX
#   mtmrl.mrl  MTMRL: 1 material (CRC32 name hash -> "tex0" diffuse)
#   sample.mbn MBN: 1 mesh / 1 submesh, float positions (external buffers)
# Usage: mk_mtmob.py OUTDIR
import binascii
import struct
import sys
import os


def morton8(x, y):
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)


def pica_rgba8(w, h):
    px = bytearray(w * h * 4)
    tw = (w + 7) & ~7
    for y in range(h):
        for x in range(w):
            pos = ((y // 8) * (tw // 8) + (x // 8)) * 64 + morton8(x & 7, y & 7)
            red = ((x // 2) + (y // 2)) % 2 == 0
            px[pos * 4:pos * 4 + 4] = bytes((255, 0, 0, 255)) if red else bytes((0, 255, 0, 255))
    return bytes(px)


def mttex():
    px = pica_rgba8(8, 8)
    w0 = 0xA0  # version 0xA0, shift 0
    w1 = (8 << 6) | (8 << 19)
    w2 = 0x03 << 8  # RGBA8
    return b'TEX\0' + struct.pack('<iII', w0, w1, w2) + px


def crc32(s):
    # SPICA CRC32Hash.Hash: IEEE table, init 0xFFFFFFFF, NO final negation,
    # i.e. the bitwise complement of the standard (binascii/zlib) CRC32.
    return binascii.crc32(s.encode('ascii')) ^ 0xFFFFFFFF


def mtmod(fmt_hash=0, stride=32, verts=None, name='mtmod'):
    # 1 bone, 1 mesh, 1 material; vertices as (pos3f, nrm3f, uv2f[, pad])
    if verts is None:
        verts = [(0, 0, 0, 0, 0, 1, 0, 0), (1, 0, 0, 0, 0, 1, 1, 0), (0, 1, 0, 0, 0, 1, 0, 1)]
    vbuf = b''.join(struct.pack('<%df' % (stride // 4), *v) for v in verts)
    ibuf = struct.pack('<3H', 0, 1, 2)
    skel = struct.pack('<bbbb', 0, -1, 0, 0) + struct.pack('<ff', 0, 0) + struct.pack('<3f', 0, 0, 0)
    ident = struct.pack('<16f', *[1 if i % 5 == 0 else 0 for i in range(16)])
    skel += ident + ident + b'\0' * 0x100  # local + world + unknown
    matnames = b'mat0' + b'\0' * (0x80 - 4)
    # mesh entry: verts=3, matMesh(group0,mat0,render -1), stride, ...
    matmesh = (0xFF << 24) | (0 << 12) | 0
    mesh = struct.pack('<HH', 0, 3) + struct.pack('<I', matmesh)
    mesh += struct.pack('<BBBB', 0, 0, stride, 3)
    mesh += struct.pack('<II', 0, 0)  # vertIndex, vertOff
    mesh += struct.pack('<I', fmt_hash)
    mesh += struct.pack('<III', 0, 3, 0)  # idxIndex, idxCount, idxOff(ignored)
    mesh += struct.pack('<BBH', 0, 0, 0)  # boneCnt, boneIdx, meshIdx
    hdr = bytearray(0x74)
    hdr[0:4] = b'MOD\0'
    struct.pack_into('<HHHH', hdr, 4, 1, 1, 1, 1)  # ver, bones, meshes, mats
    struct.pack_into('<III', hdr, 12, len(vbuf), 3, 1)  # totalVerts/Idx/Tris
    struct.pack_into('<II', hdr, 24, len(vbuf), 0)  # vbufLen, pad
    struct.pack_into('<II', hdr, 32, 0, 0)  # meshGroups, boneGroups
    skeladdr = 0x74
    mataddr = skeladdr + len(skel)
    meshaddr = mataddr + 0x80
    vbufaddr = meshaddr + 0x28
    ibufaddr = vbufaddr + len(vbuf)
    struct.pack_into('<IIIIII', hdr, 40, skeladdr, 0, mataddr, meshaddr, vbufaddr, ibufaddr)
    flen = vbufaddr + 0  # placeholder
    blob = bytes(hdr) + skel + matnames + mesh + vbuf + ibuf
    struct.pack_into('<I', hdr, 0x40, len(blob))
    return bytes(hdr) + skel + matnames + mesh + vbuf + ibuf


def mtmrl():
    # version 0x20: lutAddr @0x14, matAddr @0x18
    # LUT entry: 0x4C bytes, 0x40-byte name at +0x0C (SPICA MTMaterials)
    lut = b'\0' * 0x0C + b'tex0' + b'\0' * (0x4C - 0x0C - 4)
    mat = bytearray(0x3C)
    struct.pack_into('<I', mat, 4, crc32('mat0'))
    mat[0x18] = 1  # one texture desc
    struct.pack_into('<I', mat, 0x34, 0)  # texDescAddr patched below
    hdr = b'MRL\0' + struct.pack('<IIII', 0x20, 1, 1, 0)
    lutaddr = 0x1C
    mataddr = lutaddr + 0x4C
    taddr = mataddr + 0x3C
    struct.pack_into('<I', mat, 0x34, taddr)
    tdesc = struct.pack('<IiI', 3, 1, 0)  # TextureMap, idx+1=1, mapHash
    hdr += struct.pack('<II', lutaddr, mataddr)
    return hdr + lut + bytes(mat) + tdesc


def mtmfx():
    # one __InputLayout descriptor ("test", idx 0)
    strs = b'__InputLayout\0test\0position\0normal\0texcoord\0'
    s_off = {}
    o = 0
    for s in (b'__InputLayout', b'test', b'position', b'normal', b'texcoord'):
        s_off[s] = o
        o += len(s) + 1
    # stride-20 layout (pos 3f @0, uv 2f @12): only the MFX path can
    # decode it, since the float fallback only knows stride 32.
    attrs = [
        (s_off[b'position'], 0 | (1 << 6) | (2 << 11) | (0 << 24)),
        (s_off[b'texcoord'], 0 | (1 << 6) | (1 << 11) | (3 << 24)),
    ]
    group = struct.pack('<BBH', 0, 2 << 4, 0)[:3] + bytes([5]) + b'\0' * 4
    for no, fm in attrs:
        group += struct.pack('<II', no, fm)
    hdr = bytearray(40)
    hdr[0:4] = b'MFX\0'
    struct.pack_into('<III', hdr, 12, 1, 0, 0)  # ndesc, frag, vtx
    struct.pack_into('<II', hdr, 24, 0, 0)  # fragAddr, vtxAddr
    strtab = 44
    fmttab = 40
    struct.pack_into('<II', hdr, 32, strtab, 0)  # strings, vtxProg
    descaddr = strtab + len(strs)
    groupaddr = descaddr + 20
    blob = bytes(hdr) + struct.pack('<I', descaddr) + strs
    blob += struct.pack('<II', s_off[b'test'], s_off[b'__InputLayout'])
    blob += struct.pack('<HHHH', 0, 0, 0, 0)  # type, mapLen, mapIdx, descIdx
    blob += struct.pack('<I', groupaddr) + group
    return blob, (crc32('test') << 12) & 0xFFFFFFFF


def mbn():
    # type 0 (external), 1 mesh, 1 sub, pos-float attr
    out = struct.pack('<IIiI', 0, 0, 0, 1)  # type, meshFlags, vflags, nmesh
    out += struct.pack('<i', 1)  # nsub
    out += struct.pack('<I', 0)  # nbones
    out += struct.pack('<I', 3)  # nidx
    out += struct.pack('<I', 1)  # nattrs
    out += struct.pack('<IIf', 0, 0, 1.0)  # pos, float, scale
    out += struct.pack('<i', 36)  # buflen
    tail = (len(out) + 0x1F) & ~0x1F
    out += b'\0' * (tail - len(out))
    out += struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)  # verts
    out += b'\0' * ((4 - (len(out) % 4)) % 4)
    idxoff = (len(out) + 0x1F) & ~0x1F
    out += b'\0' * (idxoff - len(out))
    out += struct.pack('<3H', 0, 1, 2)
    return out


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    mfx, key = mtmfx()
    files = {
        'mttex.tex': mttex(),
        'mtmod.mod': mtmod(),
        'mtmod_mfx.mod': mtmod(fmt_hash=key, stride=20,
                               verts=[(0, 0, 0, 0, 0), (1, 0, 0, 1, 0), (0, 1, 0, 0, 1)]),
        'companion.mfx': mfx,
        'mtmrl.mrl': mtmrl(),
        'sample.mbn': mbn(),
    }
    # stride-20 verts: pos(3f) + uv(2f)
    for name, blob in files.items():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(blob)
        print('wrote %s (%d bytes)' % (name, len(blob)))
    print('mfx layout key: 0x%08x' % key)


main()
