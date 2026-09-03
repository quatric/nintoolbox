"""Count geometry/material/image/triangle elements in a model file.

wmdlt/wszst model export is GLB (binary glTF) only now -- COLLADA/DAE output
was removed. Callers still pass ".dae" destination names in a few places;
this counts the right thing either way by sniffing the actual bytes.
"""
import json, re, struct, sys

KIND_ALIASES = {
    'geometry': 'geometry', '<geometry': 'geometry', '<geometry ': 'geometry',
    'material': 'material', '<material': 'material', '<material ': 'material',
    'image': 'image', '<image': 'image', '<image ': 'image',
    'triangles': 'triangles', '<triangles': 'triangles', '<triangles ': 'triangles',
}

def count_glb(path, kind):
    buf = open(path, 'rb').read()
    magic, version, length = struct.unpack_from('<4sII', buf, 0)
    off = 12
    doc = None
    while off < length:
        clen, ctype = struct.unpack_from('<II', buf, off)
        if ctype == 0x4E4F534A:  # 'JSON'
            doc = json.loads(buf[off + 8: off + 8 + clen])
            break
        off += 8 + clen
    if doc is None:
        return 0
    if kind == 'geometry':
        return len(doc.get('meshes', []))
    if kind == 'material':
        return len(doc.get('materials', []))
    if kind == 'image':
        return len(doc.get('images', []))
    if kind == 'triangles':
        return sum(len(m.get('primitives', [])) for m in doc.get('meshes', []))
    return 0

def count_dae(path, kind):
    tag = {'geometry': '<geometry', 'material': '<material',
           'image': '<image', 'triangles': '<triangles'}[kind]
    txt = open(path).read()
    return len(re.findall(re.escape(tag), txt))

def main():
    path, kind = sys.argv[1], KIND_ALIASES.get(sys.argv[2], sys.argv[2])
    with open(path, 'rb') as f:
        head = f.read(4)
    n = count_glb(path, kind) if head == b'glTF' else count_dae(path, kind)
    print(n)

if __name__ == '__main__':
    main()
