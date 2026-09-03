#!/usr/bin/env python3
"""Validate the COLLADA invariants that XML parsing/import smoke tests miss."""

import argparse
import json
import math
import os
import struct
import sys
import xml.etree.ElementTree as ET


NS = "{http://www.collada.org/2005/11/COLLADASchema}"

COMPONENT_FMT = {
    5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2),
    5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4),
}
TYPE_COUNT = {
    'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4,
    'MAT2': 4, 'MAT3': 9, 'MAT4': 16,
}


def validate_glb(path, require_images=False):
    # wmdlt/wszst model export is GLB (binary glTF), not COLLADA, but callers
    # still pass ".dae" destination names in a few places -- validate the
    # equivalent invariants (finite floats, in-range indices, >=1 triangle
    # primitive, referenced images present) against the real format instead.
    errors = []
    buf = open(path, 'rb').read()
    magic, version, length = struct.unpack_from('<4sII', buf, 0)
    off = 12
    json_chunk = bin_chunk = None
    while off < length:
        clen, ctype = struct.unpack_from('<II', buf, off)
        cdata = buf[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            json_chunk = cdata
        elif ctype == 0x004E4942:
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
            out.extend(struct.unpack_from('<%d%s' % (ncomp, fmt_c), bin_chunk, base))
        return out

    triangles_seen = 0
    for mi, mesh in enumerate(doc.get('meshes', [])):
        for pi, prim in enumerate(mesh.get('primitives', [])):
            attrs = prim.get('attributes', {})
            if 'POSITION' not in attrs:
                errors.append(f"mesh {mi} primitive {pi}: no POSITION attribute")
                continue
            pos = accessor_values(attrs['POSITION'])
            if any(not math.isfinite(v) for v in pos):
                errors.append(f"mesh {mi} primitive {pi}: non-finite position")
            nverts = len(pos) // 3
            if 'indices' in prim:
                idxs = [int(v) for v in accessor_values(prim['indices'])]
                bad = next((i for i in idxs if i < 0 or i >= nverts), None)
                if bad is not None:
                    errors.append(f"mesh {mi} primitive {pi}: index {bad} outside 0..{nverts-1}")
            else:
                idxs = list(range(nverts))
            triangles_seen += len(idxs) // 3

    if triangles_seen == 0:
        errors.append("no triangles")

    if require_images:
        images = doc.get('images', [])
        if not images:
            errors.append("no images")
        for i, img in enumerate(images):
            if 'bufferView' in img:
                continue
            uri = img.get('uri', '')
            if not uri:
                errors.append(f"image {i}: no bufferView or uri")
                continue
            target = os.path.normpath(os.path.join(os.path.dirname(path), uri))
            if not os.path.isfile(target):
                errors.append(f"image {i}: missing {uri}")
    return errors


def validate(path, require_images=False):
    with open(path, 'rb') as f:
        head = f.read(4)
    if head == b'glTF':
        return validate_glb(path, require_images)

    errors = []
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as exc:
        return [f"XML: {exc}"]

    source_counts = {}
    for source in root.iter(NS + "source"):
        accessor = source.find(".//" + NS + "accessor")
        if accessor is not None:
            source_counts[source.get("id")] = int(accessor.get("count", "0"))
        array = source.find(NS + "float_array")
        if array is not None:
            try:
                values = [float(value) for value in (array.text or "").split()]
            except ValueError as exc:
                errors.append(f"{array.get('id')}: invalid float: {exc}")
                continue
            if any(not math.isfinite(value) for value in values):
                errors.append(f"{array.get('id')}: non-finite value")
            declared = int(array.get("count", "0"))
            if declared != len(values):
                errors.append(f"{array.get('id')}: {len(values)} values, declares {declared}")

    vertices = {}
    for node in root.iter(NS + "vertices"):
        position = next((item for item in node.findall(NS + "input")
                         if item.get("semantic") == "POSITION"), None)
        if position is not None:
            vertices[node.get("id")] = position.get("source", "").lstrip("#")

    triangles_seen = 0
    for triangles in root.iter(NS + "triangles"):
        inputs = triangles.findall(NS + "input")
        stride = max((int(item.get("offset", "0")) for item in inputs), default=-1) + 1
        try:
            values = [int(value) for p in triangles.findall(NS + "p")
                      for value in (p.text or "").split()]
        except ValueError as exc:
            errors.append(f"triangles: invalid index: {exc}")
            continue
        count = int(triangles.get("count", "0"))
        triangles_seen += count
        expected = count * 3 * stride
        if len(values) != expected:
            errors.append(f"triangles: {len(values)} tuple values, expected {expected}")
            continue
        for item in inputs:
            offset = int(item.get("offset", "0"))
            source = item.get("source", "").lstrip("#")
            if item.get("semantic") == "VERTEX":
                source = vertices.get(source, source)
            limit = source_counts.get(source)
            if limit is None:
                errors.append(f"triangles: missing source {source}")
                continue
            bad = next((value for value in values[offset::stride]
                        if value < 0 or value >= limit), None)
            if bad is not None:
                errors.append(f"triangles: {item.get('semantic')} index {bad} outside 0..{limit-1}")

    if triangles_seen == 0:
        errors.append("no triangles")

    if require_images:
        for image in root.iter(NS + "image"):
            init = image.find(NS + "init_from")
            if init is None or not (init.text or "").strip():
                errors.append(f"image {image.get('id')}: no init_from")
                continue
            target = os.path.normpath(os.path.join(os.path.dirname(path), init.text.strip()))
            if not os.path.isfile(target):
                errors.append(f"image {image.get('id')}: missing {init.text.strip()}")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--require-images", action="store_true")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()
    failed = False
    for path in args.files:
        for error in validate(path, args.require_images):
            failed = True
            print(f"{path}: {error}", file=sys.stderr)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
