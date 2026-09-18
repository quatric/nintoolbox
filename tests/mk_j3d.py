"""Write minimal valid J3D BMD (GameCube/Wii) models for regression tests.

Layout mirrors SuperBMD's writer. Two variants:

  mk_j3d.py OUT.bmd            -- 1 joint, 1 triangle, 1 RGBA32 texture
  mk_j3d.py --rigged OUT.bmd   -- 2 joints, strip + trilist primitives,
                                  vertex colors, UVs, CMPR + C8 textures,
                                  mipmaps, single + multi weights

All integers big-endian; section sizes include trailing 32-byte padding.
"""
import struct
import sys


def pad32(b):
    return b + b'\x00' * ((-len(b)) % 32)


def sect(magic, body):
    raw = magic + b'\x00\x00\x00\x00' + body
    raw += b'\x00' * ((-len(raw)) % 32)
    return raw[:4] + struct.pack('>I', len(raw)) + raw[8:]


def nametable(names):
    out = bytearray(struct.pack('>h', len(names)) + struct.pack('>h', -1))
    offs = []
    for _ in names:
        out += struct.pack('>H', 0)
        offs.append(len(out))
        out += struct.pack('>h', 0)
    for i, nm in enumerate(names):
        struct.pack_into('>h', out, offs[i], len(out))
        out += nm.encode() + b'\x00'
    return bytes(out)


def joint_blob(mtxtype, scale, rot, trans, bounds):
    j = struct.pack('>h', mtxtype) + b'\x00\xff'
    j += struct.pack('>3f', *scale) + struct.pack('>3h', *rot)
    j += struct.pack('>h', -1) + struct.pack('>3f', *trans)
    j += struct.pack('>7f', *bounds)
    assert len(j) == 64
    return j


TEXMTX_IDENT = (b'\x00\x00\xff\xff' + struct.pack('>3f', 0, 0, 0)
                + struct.pack('>2f', 1, 1) + struct.pack('>h', 0) + b'\xff\xff'
                + struct.pack('>2f', 0, 0)
                + struct.pack('>16f', *([1 if i % 5 == 0 else 0 for i in range(16)])))


def mat3_section(mats):
    """mats: list of (name, [(texslot, texidx)...]) with diffuse white."""
    n = len(mats)
    recs = b''
    for _, slots in mats:
        rec = b'\x01\x00\x00\x01\x01\x00\x00\x00'
        rec += struct.pack('>h', 0) + struct.pack('>h', -1)
        rec += struct.pack('>4h', 0, 1, -1, -1)
        rec += struct.pack('>2h', 0, -1)
        rec += struct.pack('>8h', *([-1] * 8))
        tg = [-1] * 8
        for s, _ in slots:
            tg[s] = s
        rec += struct.pack('>8h', *tg)
        rec += struct.pack('>8h', *([-1] * 8))
        tm = [-1] * 10
        if slots:
            tm[0] = 0
        rec += struct.pack('>10h', *tm)
        rec += struct.pack('>20h', *([-1] * 20))
        tx = [-1] * 8
        for s, ti in slots:
            tx[s] = ti
        rec += struct.pack('>8h', *tx)
        rec += struct.pack('>4h', 0, -1, -1, -1)
        rec += bytes(16) + bytes(16)
        ntex = len(slots)
        rec += struct.pack('>16h', *[0 if i < ntex else -1 for i in range(16)])
        rec += struct.pack('>4h', 0, -1, -1, -1)
        rec += struct.pack('>16h', 0, *([-1] * 15))
        rec += struct.pack('>16h', 0, *([-1] * 15))
        rec += struct.pack('>16h', 0, *([-1] * 15))
        rec += struct.pack('>4h', 0, 0, 0, 0)
        assert len(rec) == 332, len(rec)
        recs += rec
    trem = sorted({ti for _, slots in mats for _, ti in slots})
    blobs = [recs, struct.pack('>%dh' % n, *range(n)),
             nametable([nm for nm, _ in mats]), b'\x00' * 312,
             struct.pack('>i', 2), b'\xff\xff\xff\xff', b'\x02\x00\x00\x00',
             bytes([0, 0, 0, 2, 1, 0, 0xff, 0xff]) * 2,
             bytes([50, 50, 50, 50]), b'', b'\x01\x00\x00\x00',
             bytes([1, 4, 60, 0xff]), b'', TEXMTX_IDENT, b'',
             struct.pack('>%dh' % len(trem), *trem),
             bytes([0, 0, 4, 0xff]),
             struct.pack('>4h', 0x00ff, 0x00ff, 0x00ff, 0x00ff),
             b'\xff\xff\xff\xff', b'\x01\x00\x00\x00',
             bytes([0xff, 8, 15, 15, 15, 0, 0, 0, 1, 0,
                    4, 7, 7, 7, 0, 0, 0, 1, 0, 0xff]),
             bytes([0, 0, 0xff, 0xff]), bytes([0, 1, 2, 3]), b'\x00' * 44,
             bytes([4, 0x7f, 0, 7, 0, 0xff, 0xff, 0xff]), bytes([1, 4, 5, 5]),
             bytes([1, 3, 1, 0xff]), b'\x00\x00\x00\x00', b'\x00\x00\x00\x00',
             b'\x00\xff\xff\xff' + struct.pack('>3f', 0, 0, 0)]
    assert len(blobs) == 30
    head = struct.pack('>h', n) + struct.pack('>h', -1)
    cur = 132
    for b in blobs:
        head += struct.pack('>I', cur)
        cur += len(b)
    raw = b'MAT3' + struct.pack('>I', 0) + head + b''.join(blobs)
    raw = raw[:4] + struct.pack('>I', len(raw) + (-len(raw)) % 32) + raw[8:]
    return raw + b'\x00' * ((-len(raw)) % 32)


def build_simple():
    nodes = [(16, 0), (1, 0), (17, 0), (1, 0), (18, 0), (2, 0), (2, 0), (0, 0)]
    inf1_body = struct.pack('>h', 2) + struct.pack('>h', -1)
    inf1_body += struct.pack('>III', 1, 3, 0x18)
    for t, x in nodes:
        inf1_body += struct.pack('>hh', t, x)
    inf1 = sect(b'INF1', inf1_body)

    vtx1_body = struct.pack('>I', 0x40) + struct.pack('>I', 96) + struct.pack('>12I', *([0] * 12))
    vtx1_body += struct.pack('>IIIB', 9, 1, 4, 0) + b'\xff\xff\xff'
    vtx1_body += struct.pack('>IIIB', 255, 1, 0, 0) + b'\xff\xff\xff'
    assert len(vtx1_body) == 96 - 8
    vtx1_body += struct.pack('>9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)
    vtx1 = sect(b'VTX1', vtx1_body)

    evp1 = sect(b'EVP1', struct.pack('>h', 0) + struct.pack('>h', -1)
                + struct.pack('>IIII', 0, 0, 0, 0))
    drw1_body = struct.pack('>h', 1) + struct.pack('>h', -1)
    drw1_body += struct.pack('>II', 20, 22) + b'\x00\x00' + struct.pack('>h', 0)
    drw1 = sect(b'DRW1', drw1_body)

    jnt1_body = struct.pack('>h', 1) + struct.pack('>h', -1)
    jnt1_body += struct.pack('>III', 24, 24 + 64, 24 + 64 + 4)
    jnt1_body += joint_blob(0, (1, 1, 1), (0, 0, 0), (0, 0, 0),
                            (10, -10, -10, -10, 10, 10, 10))
    jnt1_body += struct.pack('>h', 0) + b'\x00\x00' + nametable(['root'])
    jnt1 = sect(b'JNT1', jnt1_body)

    shape = struct.pack('B', 3) + b'\xff' + struct.pack('>5h', 1, 0, 0, 0, -1)
    shape += struct.pack('>7f', 2.0, 0, 0, 0, 1, 0, 0)
    prim = b'\x90' + struct.pack('>H', 3) + struct.pack('>HHH', 0, 1, 2)
    mdat = struct.pack('>h', 0) + struct.pack('>h', 1) + struct.pack('>I', 0)
    shp1_body = struct.pack('>h', 1) + struct.pack('>h', -1)
    off = 44
    parts = {'shape': (off, shape)}
    off += 40
    parts['remap'] = (off, struct.pack('>h', 0))
    off = (off + 2 + 31) // 32 * 32
    attrd = struct.pack('>II', 9, 3) + struct.pack('>II', 255, 0)
    parts['attr'] = (off, attrd)
    off += len(attrd)
    parts['midx'] = (off, struct.pack('>h', 0))
    off = (off + 2 + 31) // 32 * 32
    parts['prim'] = (off, pad32(prim))
    p0len = len(pad32(prim))
    off += p0len
    parts['mdat'] = (off, mdat)
    off += 8
    parts['pinf'] = (off, struct.pack('>II', p0len, 0))
    off += 8
    off = (off + 31) // 32 * 32
    for k in ['shape', 'remap', 'unused', 'attr', 'midx', 'prim', 'mdat', 'pinf']:
        shp1_body += struct.pack('>I', parts[k][0] if k in parts else 0)
    blob = bytearray(off)
    for k, (o, dd) in parts.items():
        blob[o:o + len(dd)] = dd
    shp1_raw = b'SHP1' + struct.pack('>I', 0) + bytes(shp1_body) + bytes(blob[44:])
    shp1_raw = shp1_raw[:4] + struct.pack('>I', len(shp1_raw) + (-len(shp1_raw)) % 32) + shp1_raw[8:]
    shp1 = shp1_raw + b'\x00' * ((-len(shp1_raw)) % 32)

    mat3 = mat3_section([('mat0', [(0, 0)])])

    px = [(255, 0, 0, 255)] * 16
    imgdata = b''.join(bytes([a, r]) for r, g, b, a in px)
    imgdata += b''.join(bytes([g, b]) for r, g, b, a in px)
    hdr = struct.pack('B', 6) + struct.pack('B', 0) + struct.pack('>HH', 4, 4)
    hdr += bytes([1, 1, 0, 0]) + struct.pack('>H', 0) + struct.pack('>i', 0)
    hdr += bytes([0, 0, 0, 0, 1, 1, 0, 0, 1, 0xff]) + struct.pack('>h', 0)
    hdr += struct.pack('>i', 32)
    assert len(hdr) == 32
    tbody = struct.pack('>h', 1) + struct.pack('>h', -1) + struct.pack('>II', 32, 128)
    tbody += b'\x00' * 12 + hdr + imgdata + nametable(['tex0'])
    tex1_raw = b'TEX1' + struct.pack('>I', 0) + tbody
    tex1_raw = tex1_raw[:4] + struct.pack('>I', len(tex1_raw) + (-len(tex1_raw)) % 32) + tex1_raw[8:]
    tex1 = tex1_raw + b'\x00' * ((-len(tex1_raw)) % 32)

    f = b'J3D2bmd3' + struct.pack('>II', 0, 8) + b'TestModelV1.0\x00\x00\x00'
    return f + inf1 + vtx1 + evp1 + drw1 + jnt1 + shp1 + mat3 + tex1


def build_rigged():
    nodes = [(16, 0), (1, 0), (17, 0), (1, 0), (18, 0), (2, 0), (2, 0),
             (1, 0), (16, 1), (1, 0), (17, 0), (1, 0), (18, 1), (2, 0), (2, 0),
             (2, 0), (0, 0)]
    inf1_body = struct.pack('>h', 2) + struct.pack('>h', -1)
    inf1_body += struct.pack('>III', 2, 6, 0x18)
    for t, x in nodes:
        inf1_body += struct.pack('>hh', t, x)
    inf1 = sect(b'INF1', inf1_body)

    pos = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (2, 0, 0), (2, 1, 0)]
    nrm = [(0, 0, 1)] * 6
    col = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
           (255, 255, 255, 255), (255, 255, 0, 255), (0, 255, 255, 255)]
    uv = [(0, 0), (1, 0), (1, 1), (0, 1), (2, 0), (2, 1)]

    def hdr(attr, comp, typ, frac):
        return struct.pack('>IIIB', attr, comp, typ, frac) + b'\xff\xff\xff'

    def q(v, frac):
        return max(-32768, min(32767, int(round(v * (1 << frac)))))

    headers = hdr(9, 1, 4, 0) + hdr(10, 0, 3, 14) + hdr(11, 1, 5, 0) + hdr(13, 1, 3, 8)
    headers += struct.pack('>IIIB', 255, 1, 0, 0) + b'\xff\xff\xff'
    posd = b''.join(struct.pack('>3f', *p) for p in pos)
    nrmd = b''.join(struct.pack('>3h', *[q(v, 14) for v in n]) for n in nrm)
    cold = b''.join(bytes(c) for c in col)
    uvd = b''.join(struct.pack('>2h', *[q(v, 8) for v in t]) for t in uv)
    blobs = [posd, nrmd, cold, uvd]
    offs, cur = [], 0x40 + len(headers)
    for b in blobs:
        offs.append(cur)
        cur += len(b)
    vtx1_body = struct.pack('>I', 0x40) + struct.pack('>13I', *(offs + [0] * 9)) + headers
    for b in blobs:
        vtx1_body += b
    vtx1 = sect(b'VTX1', vtx1_body)

    evp1_body = struct.pack('>h', 1) + struct.pack('>h', -1)
    evp1_body += struct.pack('>IIII', 28, 30, 34, 42)
    evp1_body += b'\x02\x00' + struct.pack('>2h', 0, 1) + struct.pack('>2f', 0.5, 0.5)
    evp1_body += struct.pack('>12f', *[1 if i % 5 == 0 else 0 for i in range(12)])
    evp1_body += struct.pack('>12f', *[1 if i % 5 == 0 else 0 for i in range(12)])
    evp1 = sect(b'EVP1', evp1_body)

    drw1_body = struct.pack('>h', 4) + struct.pack('>h', -1)
    drw1_body += struct.pack('>II', 20, 24) + bytes([0, 0, 1, 1])
    drw1_body += struct.pack('>4h', 0, 1, 0, 0)
    drw1 = sect(b'DRW1', drw1_body)

    jnt1_body = struct.pack('>h', 2) + struct.pack('>h', -1)
    jnt1_body += struct.pack('>III', 24, 24 + 128, 24 + 128 + 4)
    jnt1_body += joint_blob(0, (1, 1, 1), (0, 0, 0), (0, 0, 0),
                            (10, -10, -10, -10, 10, 10, 10))
    jnt1_body += joint_blob(1, (1, 1, 1), (0, 0, 0), (1, 0, 0),
                            (10, -10, -10, -10, 10, 10, 10))
    jnt1_body += struct.pack('>2h', 0, 1) + nametable(['root', 'child'])
    jnt1 = sect(b'JNT1', jnt1_body)

    def corner(pmtx, p, uvv=None, nrmm=0, coll=None):
        uvv = p if uvv is None else uvv
        coll = p if coll is None else coll
        return struct.pack('B', pmtx) + struct.pack('>HHHH', p, nrmm, coll, uvv)

    # packet-local matrix indices! packet0 slots [drw0, drw1, drw2], packet1 [drw1]
    p0 = b'\x98' + struct.pack('>H', 4)
    p0 += corner(0, 0) + corner(0, 1) + corner(6, 2) + corner(6, 3)
    p1 = b'\x90' + struct.pack('>H', 3)
    p1 += corner(0, 4) + corner(0, 5) + corner(0, 2)
    sh0 = struct.pack('B', 3) + b'\xff' + struct.pack('>5h', 1, 0, 0, 0, -1)
    sh0 += struct.pack('>7f', 2.0, 0, 0, 0, 2, 1, 0)
    sh1 = struct.pack('B', 3) + b'\xff' + struct.pack('>5h', 1, 0, 1, 1, -1)
    sh1 += struct.pack('>7f', 2.0, 0, 0, 0, 2, 1, 0)
    attrd = (struct.pack('>II', 0, 1) + struct.pack('>II', 9, 3) + struct.pack('>II', 10, 3)
             + struct.pack('>II', 11, 3) + struct.pack('>II', 13, 3) + struct.pack('>II', 255, 0))
    midx = struct.pack('>4h', 0, 1, 2, 1)
    mdat = struct.pack('>h', 0) + struct.pack('>h', 3) + struct.pack('>I', 0)
    mdat += struct.pack('>h', 0) + struct.pack('>h', 1) + struct.pack('>I', 3)
    shp1_body = struct.pack('>h', 2) + struct.pack('>h', -1)
    off = 44
    parts = {'shape': (off, sh0 + sh1)}
    off += 80
    parts['remap'] = (off, struct.pack('>2h', 0, 1))
    off = (off + 4 + 31) // 32 * 32
    parts['attr'] = (off, attrd)
    off += len(attrd)
    parts['midx'] = (off, midx)
    off = (off + len(midx) + 31) // 32 * 32
    parts['prim'] = (off, pad32(p0) + pad32(p1))
    p0len = len(pad32(p0))
    off += p0len + len(pad32(p1))
    parts['mdat'] = (off, mdat)
    off += 16
    parts['pinf'] = (off, struct.pack('>IIII', p0len, 0, len(pad32(p1)), p0len))
    off += 16
    off = (off + 31) // 32 * 32
    for k in ['shape', 'remap', 'unused', 'attr', 'midx', 'prim', 'mdat', 'pinf']:
        shp1_body += struct.pack('>I', parts[k][0] if k in parts else 0)
    blob = bytearray(off)
    for k, (o, dd) in parts.items():
        blob[o:o + len(dd)] = dd
    srec = parts['shape'][0]
    struct.pack_into('>h', blob, srec + 6, 0)
    struct.pack_into('>h', blob, srec + 8, 0)
    struct.pack_into('>h', blob, srec + 40 + 6, 1)
    struct.pack_into('>h', blob, srec + 40 + 8, 1)
    shp1_raw = b'SHP1' + struct.pack('>I', 0) + bytes(shp1_body) + bytes(blob[44:])
    shp1_raw = shp1_raw[:4] + struct.pack('>I', len(shp1_raw) + (-len(shp1_raw)) % 32) + shp1_raw[8:]
    shp1 = shp1_raw + b'\x00' * ((-len(shp1_raw)) % 32)

    mat3 = mat3_section([('mat0', [(0, 0), (1, 1)])])

    sub = struct.pack('>HHI', 0xF800, 0x001F, 0)
    cmpr8, mip4 = sub * 4, sub * 4
    c8idx = bytes([(x + y) % 2 for y in range(4) for x in range(8)])
    pal = struct.pack('>2H', 0xFFFF, 0xFF00)
    h0 = struct.pack('B', 0x0e) + struct.pack('B', 0) + struct.pack('>HH', 8, 8)
    h0 += bytes([1, 1, 0, 0]) + struct.pack('>H', 0) + struct.pack('>i', 0)
    h0 += bytes([0, 0, 0, 0, 1, 1, 0, 0, 2, 0xff]) + struct.pack('>h', 0)
    h0 += struct.pack('>i', 100 - 32)
    h1 = struct.pack('B', 9) + struct.pack('B', 0) + struct.pack('>HH', 8, 4)
    h1 += bytes([1, 1, 1, 0]) + struct.pack('>H', 2) + struct.pack('>i', 96 - 64)
    h1 += bytes([0, 0, 0, 0, 1, 1, 0, 0, 1, 0xff]) + struct.pack('>h', 0)
    h1 += struct.pack('>i', 164 - 64)
    assert len(h0) == 32 and len(h1) == 32
    tbody = struct.pack('>h', 2) + struct.pack('>h', -1) + struct.pack('>II', 32, 196)
    tbody += b'\x00' * 12 + h0 + h1 + pal + cmpr8 + mip4 + c8idx
    tbody += nametable(['cmprtex', 'c8tex'])
    tex1_raw = b'TEX1' + struct.pack('>I', 0) + tbody
    tex1_raw = tex1_raw[:4] + struct.pack('>I', len(tex1_raw) + (-len(tex1_raw)) % 32) + tex1_raw[8:]
    tex1 = tex1_raw + b'\x00' * ((-len(tex1_raw)) % 32)

    f = b'J3D2bmd3' + struct.pack('>II', 0, 8) + b'TestRigV1.0\x00\x00\x00\x00\x00'
    return f + inf1 + vtx1 + evp1 + drw1 + jnt1 + shp1 + mat3 + tex1


def main():
    rigged = '--rigged' in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    out = args[0] if args else '-'
    data = build_rigged() if rigged else build_simple()
    data = data[:8] + struct.pack('>I', len(data)) + data[12:]
    if out == '-':
        sys.stdout.buffer.write(data)
    else:
        open(out, 'wb').write(data)
    print('wrote %d bytes (%s)' % (len(data), 'rigged' if rigged else 'simple'), file=sys.stderr)


main()
