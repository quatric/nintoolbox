"""Write a Camelot GX texture bank, LZ-wrapped the way the discs store it.

Two 8x8 textures: one RGB5A3 (GX format 5), one RGB565 (format 4). The
compressor emits literals only, which is a valid Camelot stream -- the codec
is a flag byte followed by eight literal-or-backreference slots.
"""
import struct, sys

def camelot_compress(raw):
    out = bytearray([1, (len(raw) >> 16) & 0xFF, (len(raw) >> 8) & 0xFF, len(raw) & 0xFF])
    for i in range(0, len(raw), 8):
        out.append(0)                      # all eight slots are literals
        out += raw[i:i + 8]
    return bytes(out)

count = 2
tbl = 0x0C
entries_at = tbl + count * 8
e0, e1 = entries_at, entries_at + 16
data0 = e1 + 16
data1 = data0 + 128                        # 8x8 * 2 bytes

body = bytearray(struct.pack(">III", 0x0020AF30, count, tbl))
body += struct.pack(">II", e0, 0) + struct.pack(">II", e1, 0)
# Each entry is 16 bytes: geometry, format, data offset, then sampler state.
body += struct.pack(">HHII", 8, 8, 5, data0) + bytes(4)
body += struct.pack(">HHII", 8, 8, 4, data1) + bytes(4)
body += bytes(range(128))                  # RGB5A3 pixels
body += bytes(range(128, 256))             # RGB565 pixels
open(sys.argv[1], "wb").write(camelot_compress(bytes(body)))
