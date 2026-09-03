import json, re, struct, sys

# Model files produced by wmdlt/wszst are GLB (binary glTF) now, regardless of
# the ".dae" extension callers still use for these paths -- DAE/COLLADA output
# was removed in favour of GLB. Detect by magic and parse accordingly; the
# .dae name is kept only so existing regress.sh call sites don't need to churn.

COMPONENT_FMT = {
    5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2),
    5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4),
}
TYPE_COUNT = {
    'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4,
    'MAT2': 4, 'MAT3': 9, 'MAT4': 16,
}

def glb_data(path):
    buf = open(path, 'rb').read()
    magic, version, length = struct.unpack_from('<4sII', buf, 0)
    assert magic == b'glTF', "not a GLB file"
    off = 12
    json_chunk = bin_chunk = None
    while off < length:
        clen, ctype = struct.unpack_from('<II', buf, off)
        cdata = buf[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:  # 'JSON'
            json_chunk = cdata
        elif ctype == 0x004E4942:  # 'BIN\0'
            bin_chunk = cdata
        off += 8 + clen
    doc = json.loads(json_chunk)

    def accessor_values(idx):
        acc = doc['accessors'][idx]
        bv = doc['bufferViews'][acc['bufferView']]
        fmt_c, csize = COMPONENT_FMT[acc['componentType']]
        ncomp = TYPE_COUNT[acc['type']]
        start = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
        stride = bv.get('byteStride', ncomp * csize)
        count = acc['count']
        out = []
        for i in range(count):
            base = start + i * stride
            vals = struct.unpack_from('<%d%s' % (ncomp, fmt_c), bin_chunk, base)
            out.extend(vals)
        return out, ncomp

    tris = []
    for mesh in doc.get('meshes', []):
        for prim in mesh.get('primitives', []):
            attrs = prim['attributes']
            if 'POSITION' not in attrs:
                continue
            P, _ = accessor_values(attrs['POSITION'])
            U = None
            if 'TEXCOORD_0' in attrs:
                U, _ = accessor_values(attrs['TEXCOORD_0'])
            if 'indices' in prim:
                idxs, _ = accessor_values(prim['indices'])
                idxs = [int(x) for x in idxs]
            else:
                idxs = list(range(len(P) // 3))
            for base in range(0, len(idxs) - 2, 3):
                tri = []
                for k in range(3):
                    vi = idxs[base + k]
                    j = vi * 3
                    pos = (P[j], P[j + 1], P[j + 2])
                    uv = (0.0, 0.0)
                    if U:
                        j2 = vi * 2
                        uv = (U[j2], 1.0 - U[j2 + 1])
                    tri.append(pos + uv)
                tris.append(tuple(sorted(tri)))
    assert tris, "no primitives"
    return tris


def dae_data(path):
    with open(path, 'rb') as f:
        head = f.read(4)
    if head == b'glTF':
        return glb_data(path)

    txt = open(path).read()
    P = U = None
    for mm in re.finditer(r'<source[\s\S]*?</source>', txt):
        s = mm.group(0)
        fa = re.search(r'<float_array[^>]*>([^<]+)</float_array>', s)
        ta = re.search(r'stride="(\d+)"', s)
        if not fa or not ta:
            continue
        vals = [float(x) for x in fa.group(1).split()]
        stride = int(ta.group(1))
        if stride == 3 and P is None:
            P = vals
        elif stride == 2 and U is None:
            U = vals
    assert P is not None, "no positions"
    prim = re.search(r'<triangles[\s\S]*?</triangles>|<polylist[\s\S]*?</polylist>', txt)
    assert prim, "no primitives"
    body = prim.group(0)
    pin = re.search(r'semantic="VERTEX"[^>]*offset="(\d+)"', body)
    tin = re.search(r'semantic="TEXCOORD"[^>]*offset="(\d+)"', body)
    poff = int(pin.group(1)) if pin else 0
    toff = int(tin.group(1)) if tin else -1
    ncol = max(poff, toff) + 1
    idxs = [int(x) for x in re.search(r'<p>([\s\d]+)</p>', body).group(1).split()]
    tris = []
    for base in range(0, len(idxs), ncol * 3):
        tri = []
        for k in range(3):
            i = idxs[base + k * ncol : base + k * ncol + ncol]
            j = i[poff] * 3
            pos = (P[j], P[j+1], P[j+2])
            uv = (0.0, 0.0)
            if toff >= 0 and U:
                j = i[toff] * 2
                uv = (U[j], 1.0 - U[j+1])
            tri.append(pos + uv)
        tris.append(tuple(sorted(tri)))
    return tris

TOL = 2e-4  # covers s16/shift-13 requantization (6.1e-5) + %f text rounding

def close(a, b):
    return all(abs(x - y) <= TOL for ca, cb in zip(a, b) for x, y in zip(ca, cb))

A = dae_data(sys.argv[1])
B = dae_data(sys.argv[2])
if len(A) != len(B):
    print(f"MISMATCH: triangle count {len(A)} != {len(B)}")
    sys.exit(1)
unused = list(B)
unmatched = []
for t in A:
    for k, u in enumerate(unused):
        if close(t, u):
            unused.pop(k)
            break
    else:
        unmatched.append(t)
if unmatched or unused:
    print(f"MISMATCH: {len(unmatched)} unmatched, {len(unused)} extra")
    for t in unmatched[:2]: print("  A", t)
    for t in unused[:2]: print("  B", t)
    sys.exit(1)
print(f"MATCH ({len(A)} triangles)")
