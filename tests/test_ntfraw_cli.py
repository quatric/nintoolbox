"""NITRO raw 3D textures (.ntft/.ntfp/.ntfi): the 4x4 block-compressed palette offset counts 32 bit words."""
import struct, subprocess, sys, tempfile, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'wszst'
W = H = 32


def color(k):  # BGR555 of block k: a smooth gradient over the 8x8 block grid
    return (k % 8) * 4 | ((k // 8) * 4) << 5 | 16 << 10


class NtfRaw(unittest.TestCase):
    def test_4x4_palette_offset_is_in_words(self):
        n = (W // 4) * (H // 4)
        pal = b''.join(struct.pack('<HH', color(k), color(k)) for k in range(n))  # block k at word k
        idx = b''.join(struct.pack('<H', k | 3 << 14) for k in range(n))  # mode 3: two colors + blends
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 't.ntft').write_bytes(bytes(4 * n))  # every texel uses color 0
            Path(tmp, 't.ntfp').write_bytes(pal)
            Path(tmp, 't.ntfi').write_bytes(idx)
            subprocess.run([str(WSZST), 'xx', 't.ntft', '--decode', '-o'], cwd=tmp, capture_output=True)
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 't.ntft.d', 't.png'))
            self.assertEqual((w, h), (W, H))
            for k in (0, 5, 17, 63):
                c = color(k)
                want = ((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31, 255)
                self.assertEqual(rgba(k % 8 * 4 + 1, k // 8 * 4 + 1), want, k)


if __name__ == '__main__':
    unittest.main()
