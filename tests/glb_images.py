"""Assert a GLB carries N images and that each one has real embedded data.

A glTF image entry can exist with only a name, which is what the exporter
emits when it cannot find the texture file: counting entries alone would
call that a success. Check the bufferView too.
"""
import json, struct, sys

path, want = sys.argv[1], int(sys.argv[2])
buf = open(path, "rb").read()
off, doc = 12, None
while off < len(buf):
    clen, ctype = struct.unpack_from("<II", buf, off)
    if ctype == 0x4E4F534A:
        doc = json.loads(buf[off + 8: off + 8 + clen])
        break
    off += 8 + clen
if doc is None:
    sys.exit("no JSON chunk in %s" % path)

images = doc.get("images", [])
if len(images) != want:
    sys.exit("expected %d images, got %d" % (want, len(images)))
for i, img in enumerate(images):
    if "bufferView" not in img:
        sys.exit("image %d (%s) declared but carries no data" % (i, img.get("name")))
    if not img.get("name"):
        sys.exit("image %d has no texture name" % i)
