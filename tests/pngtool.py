#!/usr/bin/env python3
# Stdlib-only PNG helper for regress.sh so the suite has no Pillow
# dependency (plain python3 + zlib is enough for everything we need).
# Only 8-bit non-interlaced greyscale/RGB/greyscale+alpha/RGBA PNGs are
# handled -- exactly what our tools emit and consume.
import struct, sys, zlib


def _read_png(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos = 8
    idat = b''
    ihdr = None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        chunk = d[pos+8:pos+8+ln]
        if typ == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', chunk)
        elif typ == b'IDAT':
            idat += chunk
        elif typ == b'IEND':
            break
        pos += 12 + ln
    w, h, depth, ct, comp, filt, interlace = ihdr
    if depth != 8 or interlace != 0 or comp != 0 or filt != 0:
        raise ValueError('unsupported PNG: depth=%d interlace=%d' % (depth, interlace))
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _y in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if len(line) < stride:
            raise ValueError('truncated PNG')
        if f == 1:
            for x in range(ch, stride):
                line[x] = (line[x] + line[x-ch]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x-ch] if x >= ch else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x-ch] if x >= ch else 0
                b = prev[x]
                c = prev[x-ch] if x >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        elif f != 0:
            raise ValueError('bad filter %d' % f)
        out += line
        prev = line

    def rgba(x, y):
        o = y*stride + x*ch
        px = out[o:o+ch]
        if ct == 0:
            return (px[0], px[0], px[0], 255)
        if ct == 2:
            return (px[0], px[1], px[2], 255)
        if ct == 4:
            return (px[0], px[0], px[0], px[1])
        return (px[0], px[1], px[2], px[3])
    return w, h, ch, rgba


def cmd_write(args):
    path = args[0]
    w, h, r, g, b = (int(v) for v in args[1:6])
    a = int(args[6]) if len(args) > 6 else 255
    row = b'\x00' + bytes((r, g, b, a)) * w
    raw = row * h
    def chunk(typ, data):
        c = struct.pack('>I', len(data)) + typ + data
        return c + struct.pack('>I', zlib.crc32(typ + data) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def cmd_colors(args):
    path = args[0]
    maxcolors = int(args[1]) if len(args) > 1 else 1000000
    w, h, ch, rgba = _read_png(path)
    cols = set()
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            cols.add(rgba(x, y)[:3])
            if len(cols) > maxcolors:
                break
        if len(cols) > maxcolors:
            break
    print(len(cols))


def cmd_alphacheck(args):
    # real cutout: meaningful amounts of both opaque and transparent
    w, h, ch, rgba = _read_png(args[0])
    n = w*h
    opaque = transparent = 0
    for y in range(h):
        for x in range(w):
            a = rgba(x, y)[3]
            if a > 200:
                opaque += 1
            elif a < 55:
                transparent += 1
    sys.exit(0 if opaque > n*0.05 and transparent > n*0.05 else 1)


def cmd_cmp(args):
    wa, ha, _, ra = _read_png(args[0])
    wb, hb, _, rb = _read_png(args[1])
    if (wa, ha) != (wb, hb):
        sys.exit(1)
    for y in range(ha):
        for x in range(wa):
            if ra(x, y) != rb(x, y):
                sys.exit(1)
    sys.exit(0)


def cmd_pixel(args):
    path, x, y, r, g, b, a = args
    w, h, _, rgba = _read_png(path)
    x, y = int(x), int(y)
    expected = tuple(int(v) for v in (r, g, b, a))
    if x < 0 or y < 0 or x >= w or y >= h or rgba(x, y) != expected:
        sys.exit(1)


if __name__ == '__main__':
    cmds = {'write': cmd_write, 'colors': cmd_colors,
            'alphacheck': cmd_alphacheck, 'cmp': cmd_cmp, 'pixel': cmd_pixel}
    cmds[sys.argv[1]](sys.argv[2:])
