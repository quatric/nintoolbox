"""NITRO 3D texture format 5 (4x4 block compressed): palette offset in 32 bit words, the four
interpolation modes, and palettes with more than 256 colors."""
import struct, subprocess, sys, tempfile, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WIMGT = Path(__file__).resolve().parents[1] / 'project' / 'wimgt'
W = H = 64
BLOCKS = (W // 4) * (H // 4)


def pal(i):  # distinct BGR555 colors, 2 per block so the palette has 2 * BLOCKS + 2 entries
    return (i * 5 & 31) | (i * 3 + 7 & 31) << 5 | (i * 7 + 1 & 31) << 10


def rgb(v):
    return ((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31)


def blend(a, b, f):
    return tuple((x * (8 - f) + y * f + 4) // 8 for x, y in zip(a, b))


def expected(k, idx):
    mode = k % 4
    c0, c1, p2, p3 = (rgb(pal(2 * k + j)) for j in range(4))
    if idx < 2:
        return (c0, c1)[idx] + (255,)
    if mode == 0:
        return p2 + (255,) if idx == 2 else (0, 0, 0, 0)
    if mode == 1:
        return blend(c0, c1, 4) + (255,) if idx == 2 else (0, 0, 0, 0)
    if mode == 2:
        return (p2 if idx == 2 else p3) + (255,)
    return blend(c0, c1, 3 if idx == 2 else 5) + (255,)


def build():
    texels = b''.join(bytes([0xAA, 0xAA, 0xFF, 0xFF]) for _ in range(BLOCKS))  # rows: idx 2, 2, 3, 3
    info = b''.join(struct.pack('<H', k | (k % 4) << 14) for k in range(BLOCKS))  # offset k = color 2k
    colors = b''.join(struct.pack('<H', pal(i)) for i in range(2 * BLOCKS + 4))
    tex0 = bytearray(0x700 + len(colors))
    tex0[0:4] = b'TEX0'
    struct.pack_into('<I', tex0, 0x14, 0x40)  # texture dictionary
    struct.pack_into('<I', tex0, 0x20, 0x100)  # compressed texels
    struct.pack_into('<I', tex0, 0x24, 0x500)  # compressed palette indices
    struct.pack_into('<I', tex0, 0x28, 0x70)  # palette dictionary
    struct.pack_into('<I', tex0, 0x2c, 0x700)  # palette data
    param = 3 << 4 | 3 << 7 | 5 << 10
    d = 0x40
    tex0[d + 1] = 1
    struct.pack_into('<H', tex0, d + 6, 8)
    struct.pack_into('<HH', tex0, d + 8, 8, 12)  # entry size, name offset
    struct.pack_into('<HH', tex0, d + 12, 0, param)
    tex0[d + 20:d + 24] = b'test'
    d = 0x70
    tex0[d + 1] = 1
    struct.pack_into('<H', tex0, d + 6, 8)
    struct.pack_into('<HH', tex0, d + 8, 4, 8)
    tex0[d + 16:d + 20] = b'test'
    tex0[0x100:0x100 + len(texels)] = texels
    tex0[0x500:0x500 + len(info)] = info
    tex0[0x700:] = colors
    struct.pack_into('<I', tex0, 4, len(tex0))
    return b'BTX0' + struct.pack('<HHIHHI', 0xFEFF, 1, 0x14 + len(tex0), 0x10, 1, 0x14) + bytes(tex0)


class Nitro4x4(unittest.TestCase):
    def test_modes_offsets_and_large_palette(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 't.nsbtx').write_bytes(build())
            r = subprocess.run([str(WIMGT), 'DECODE', 't.nsbtx', '-d', 'out.png'], cwd=tmp, capture_output=True, text=True)
            self.assertTrue(Path(tmp, 'out.png').exists(), r.stdout[-500:])
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.png'))
            self.assertEqual((w, h), (W, H))
            for k in (0, 1, 2, 3, 5, 6, 7, 100, 255):  # blocks past 128 need color indices > 256
                bx, by = k % (W // 4) * 4, k // (W // 4) * 4
                for row, idx in ((0, 2), (2, 3)):
                    got = rgba(bx, by + row)
                    want = expected(k, idx)
                    if want[3] == 0:
                        self.assertEqual(got[3], 0, (k, idx))
                    else:
                        self.assertEqual(got, want, (k, idx))


if __name__ == '__main__':
    unittest.main()
