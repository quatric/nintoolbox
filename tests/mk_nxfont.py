"""Write minimal Switch / Switch 2 font files.

    mk_nxfont.py png OUT.png           8x8 RGBA: rows 0-3 red, rows 4-7 blue
    mk_nxfont.py OUTDIR BASE.bntx      font files (BASE.bntx: a one-texture BNTX, e.g. OUT.png encoded by wimgt)

OUTDIR/t.bfttf  scrambled TrueType (Switch key), payload 00 01 00 00 + 12 bytes
OUTDIR/t.bfotf  scrambled OpenType (Windows key), payload "OTTO" + 12 bytes
OUTDIR/t.bfcpx  FCPX 9.3.0.0: one pair font {bitmap "pic.bffnt", scalable "f.bfttf" 24.0, range 0x20..0x7e}
OUTDIR/t.bffnt  FFNT LE 4.1, TGLP sheet data = BASE.bntx (the sheet is stored bottom-up, so the
                decoded PNG must show blue at the top and red at the bottom)
"""
import os, struct, sys, zlib

SIG, KEY_NX, KEY_WIN = 0x7f9a0218, 0x49621806, 0xa6018502


def scramble_word(w, key):
    n = (w ^ key) & 0xffffffff
    n = ((n & 0xff00ff00) >> 8) | ((n << 8) & 0xff00ff00)
    return ((n >> 16) | (n << 16)) & 0xffffffff


def bfttf(payload, key):
    payload = payload + b'\0' * (-len(payload) % 4)
    out = struct.pack('<II', scramble_word(SIG, key), scramble_word(len(payload), key))
    for i in range(0, len(payload), 4):
        out += struct.pack('<I', scramble_word(struct.unpack('>I', payload[i:i + 4])[0], key))
    return out


def fcpx():
    body = bytearray()
    # pair node at 0 (relative to 0x14): children at +0x10 and +0x20
    body += struct.pack('<III', 2, 0x10, 0x20) + b'\0' * 4
    body += struct.pack('<II', 0, 0x60) + b'\0' * 8                       # bitmap node @0x10, name @0x70
    sc = bytearray(0x48)
    struct.pack_into('<I', sc, 0, 1)
    struct.pack_into('<f', sc, 4, 24.0)
    struct.pack_into('<I', sc, 0x14, 0x3f)
    struct.pack_into('<f', sc, 0x24, 1.0)
    struct.pack_into('<f', sc, 0x34, 1.0)
    struct.pack_into('<I', sc, 0x2c, 0x60)                                # name @0x80
    struct.pack_into('<II', sc, 0x40, 1, 0x48)                            # range @0x68
    body += sc + struct.pack('<II', 0x20, 0x7e)
    body = body.ljust(0x70, b'\0')
    body += b'pic.bffnt'.ljust(0x10, b'\0') + b'f.bfttf'.ljust(0x10, b'\0')
    head = b'FCPX\xff\xfe' + struct.pack('<H', 0x14) + bytes((0, 0, 3, 9))
    total = 0x14 + 4 + len(body)
    # the u32 count is at 0x10 and the root offset at 0x14 (relative to 0x14): root node at 0x18
    return head + struct.pack('<II', total, 1) + struct.pack('<I', 4) + bytes(body)


def bffnt(bntx):
    off = 0x200
    finf = b'FINF' + struct.pack('<I', 0x20) + b'\0' * 0x18
    tglp = bytearray(0x20)
    tglp[0:4] = b'TGLP'
    struct.pack_into('<I', tglp, 4, 0x20)
    tglp[8], tglp[9] = 4, 4                                               # cell size
    struct.pack_into('<H', tglp, 0x0a, 1)                                 # sheet count
    struct.pack_into('<I', tglp, 0x0c, len(bntx))
    struct.pack_into('<HHHHHH', tglp, 0x10, 3, 14, 2, 2, 8, 8)            # baseline, RGBA8_SRGB, rows, lines, 8x8
    struct.pack_into('<I', tglp, 0x1c, off)
    total = off + len(bntx)
    head = b'FFNT\xff\xfe' + struct.pack('<HIIHH', 0x14, 0x04010000, total, 2, 0)
    data = head + finf + bytes(tglp)
    return data.ljust(off, b'\0') + bntx


def png(path):
    rows = b''
    for y in range(8):
        px = (255, 0, 0, 255) if y < 4 else (0, 0, 255, 255)
        rows += b'\0' + bytes(px) * 8

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 8, 8, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main(outdir, base):
    os.makedirs(outdir, exist_ok=True)
    files = {
        't.bfttf': bfttf(b'\0\1\0\0' + bytes(range(1, 13)), KEY_NX),
        't.bfotf': bfttf(b'OTTO' + bytes(range(1, 13)), KEY_WIN),
        't.bfcpx': fcpx(),
    }
    with open(base, 'rb') as f:
        files['t.bffnt'] = bffnt(f.read())
    for name, data in files.items():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    if sys.argv[1] == 'png':
        png(sys.argv[2])
    else:
        main(sys.argv[1], sys.argv[2])
