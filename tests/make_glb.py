"""Author a minimal valid GLB (binary glTF) with one or more meshes.

wmdlt/wszst CREATE only accepts GLB as its model input now (COLLADA/DAE
authoring was removed), so tests that used to hand-write a COLLADA XML
file as a synthetic fixture need to hand-write an equivalent GLB instead.

Usage: make_glb.py <out.glb> <spec.json>
spec.json: [{"name": str, "positions": [x,y,z,...], "normals": [x,y,z,...],
             "indices": [i0,i1,i2,...]}, ...]
"""
import json, struct, sys


def build(meshes):
    buf = bytearray()
    accessors = []
    buffer_views = []
    gltf_meshes = []

    def add_view(data, target=None):
        while len(buf) % 4:
            buf.append(0)
        offset = len(buf)
        buf.extend(data)
        bv = {'buffer': 0, 'byteOffset': offset, 'byteLength': len(data)}
        if target is not None:
            bv['target'] = target
        buffer_views.append(bv)
        return len(buffer_views) - 1

    for mesh in meshes:
        pos = mesh['positions']
        norm = mesh['normals']
        idx = mesh['indices']
        nv = len(pos) // 3

        pos_bytes = struct.pack('<%df' % len(pos), *pos)
        pos_bv = add_view(pos_bytes, 34962)
        mins = [min(pos[i::3]) for i in range(3)]
        maxs = [max(pos[i::3]) for i in range(3)]
        accessors.append({'bufferView': pos_bv, 'componentType': 5126,
                           'count': nv, 'type': 'VEC3', 'min': mins, 'max': maxs})
        pos_acc = len(accessors) - 1

        norm_bytes = struct.pack('<%df' % len(norm), *norm)
        norm_bv = add_view(norm_bytes, 34962)
        accessors.append({'bufferView': norm_bv, 'componentType': 5126,
                           'count': nv, 'type': 'VEC3'})
        norm_acc = len(accessors) - 1

        idx_bytes = struct.pack('<%dI' % len(idx), *idx)
        idx_bv = add_view(idx_bytes, 34963)
        accessors.append({'bufferView': idx_bv, 'componentType': 5125,
                           'count': len(idx), 'type': 'SCALAR'})
        idx_acc = len(accessors) - 1

        gltf_meshes.append({
            'name': mesh['name'],
            'primitives': [{
                'attributes': {'POSITION': pos_acc, 'NORMAL': norm_acc},
                'indices': idx_acc, 'mode': 4,
            }],
        })

    doc = {
        'asset': {'version': '2.0'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(gltf_meshes)))}],
        'nodes': [{'mesh': i, 'name': m['name']} for i, m in enumerate(gltf_meshes)],
        'meshes': gltf_meshes,
        'accessors': accessors,
        'bufferViews': buffer_views,
        'buffers': [{'byteLength': len(buf)}],
    }
    return doc, bytes(buf)


def write_glb(path, doc, bin_data):
    json_bytes = json.dumps(doc).encode('utf-8')
    while len(json_bytes) % 4:
        json_bytes += b' '
    bin_padded = bin_data
    while len(bin_padded) % 4:
        bin_padded += b'\x00'
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_padded)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, total))
        f.write(struct.pack('<II', len(json_bytes), 0x4E4F534A))
        f.write(json_bytes)
        f.write(struct.pack('<II', len(bin_padded), 0x004E4942))
        f.write(bin_padded)


def main():
    out_path, spec_path = sys.argv[1], sys.argv[2]
    meshes = json.load(open(spec_path))
    doc, bin_data = build(meshes)
    write_glb(out_path, doc, bin_data)


if __name__ == '__main__':
    main()
