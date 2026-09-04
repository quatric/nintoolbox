"""Read/modify vertex attribute arrays in a model file (GLB or COLLADA DAE).

wmdlt/wszst model export is GLB (binary glTF); some regress.sh tests still
inspect/modify a "positions-array"/"normals-array" the way they would in a
COLLADA DAE. This sniffs the actual format and does the equivalent thing:

  dae_field.py <file> positions          -> prints all position floats
  dae_field.py <file> normals            -> prints all normal floats
  dae_field.py <file> set-first-position <value>
                                          -> overwrites vertex 0's X position
                                             in place (all meshes/primitives)
"""
import json, re, struct, sys

COMPONENT_FMT = {5126: ('f', 4)}


def _glb_chunks(buf):
    magic, version, length = struct.unpack_from('<4sII', buf, 0)
    assert magic == b'glTF'
    off = 12
    json_off = json_len = bin_off = None
    while off < length:
        clen, ctype = struct.unpack_from('<II', buf, off)
        if ctype == 0x4E4F534A:
            json_off, json_len = off + 8, clen
        elif ctype == 0x004E4942:
            bin_off = off + 8
        off += 8 + clen
    return json_off, json_len, bin_off


def _accessor_byte_offsets(doc, acc_idx):
    acc = doc['accessors'][acc_idx]
    bv = doc['bufferViews'][acc['bufferView']]
    ncomp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[acc['type']]
    start = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', ncomp * 4)
    return start, stride, ncomp, acc['count']


def _read_array(path, attr_name):
    buf = bytearray(open(path, 'rb').read())
    json_off, json_len, bin_off = _glb_chunks(buf)
    doc = json.loads(bytes(buf[json_off:json_off + json_len]))
    out = []
    for mesh in doc.get('meshes', []):
        for prim in mesh.get('primitives', []):
            attrs = prim.get('attributes', {})
            if attr_name not in attrs:
                continue
            start, stride, ncomp, count = _accessor_byte_offsets(doc, attrs[attr_name])
            for i in range(count):
                base = bin_off + start + i * stride
                out.extend(struct.unpack_from('<%df' % ncomp, buf, base))
    return out


def _set_first_position(path, value, mesh_name=None):
    buf = bytearray(open(path, 'rb').read())
    json_off, json_len, bin_off = _glb_chunks(buf)
    doc = json.loads(bytes(buf[json_off:json_off + json_len]))
    for mesh in doc.get('meshes', []):
        name = mesh.get('name', '')
        if mesh_name is not None and name != mesh_name and not name.startswith(mesh_name + '_'):
            continue
        for prim in mesh.get('primitives', []):
            attrs = prim.get('attributes', {})
            if 'POSITION' not in attrs:
                continue
            start, stride, ncomp, count = _accessor_byte_offsets(doc, attrs['POSITION'])
            base = bin_off + start
            struct.pack_into('<f', buf, base, float(value))
    open(path, 'wb').write(buf)


def _dae_positions(path, attr):
    txt = open(path).read()
    tag = 'positions-array' if attr == 'POSITION' else 'normals-array'
    m = re.search(r'id="[^"]*%s"[^>]*>([^<]*)<' % re.escape(tag), txt)
    return [float(x) for x in m.group(1).split()] if m else []


def _mesh_vertex_count(path, name):
    buf = open(path, 'rb').read()
    json_off, json_len, bin_off = _glb_chunks(buf)
    doc = json.loads(buf[json_off:json_off + json_len])
    for mesh in doc.get('meshes', []):
        mesh_name = mesh.get('name', '')
        if mesh_name == name or mesh_name.startswith(name + '_'):
            attrs = mesh['primitives'][0]['attributes']
            return doc['accessors'][attrs['POSITION']]['count']
    return None


def _dae_mesh_vertex_count(path, name):
    txt = open(path).read()
    m = re.search(r'<geometry[^>]*name="%s"[\s\S]*?</geometry>' % re.escape(name), txt)
    if not m:
        return None
    fa = re.search(r'positions-array"[^>]*count="(\d+)"', m.group(0))
    return int(fa.group(1)) // 3 if fa else None


def is_glb(path):
    with open(path, 'rb') as f:
        return f.read(4) == b'glTF'


def main():
    path, cmd = sys.argv[1], sys.argv[2]
    if cmd in ('positions', 'normals'):
        attr = 'POSITION' if cmd == 'positions' else 'NORMAL'
        vals = _read_array(path, attr) if is_glb(path) else _dae_positions(path, attr)
        print(' '.join(repr(v) for v in vals))
    elif cmd == 'set-first-position':
        if is_glb(path):
            mesh_name = sys.argv[4] if len(sys.argv) > 4 else None
            _set_first_position(path, sys.argv[3], mesh_name)
        else:
            raise SystemExit("set-first-position only supported on GLB input")
    elif cmd == 'mesh-verts':
        name = sys.argv[3]
        n = _mesh_vertex_count(path, name) if is_glb(path) else _dae_mesh_vertex_count(path, name)
        print(n if n is not None else 0)
    else:
        raise SystemExit(f"unknown command: {cmd}")


if __name__ == '__main__':
    main()
