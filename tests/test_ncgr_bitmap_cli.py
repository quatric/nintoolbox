"""NCGR character format 1 holds a scanline bitmap, not 8x8 tiles."""
import struct, subprocess, sys, tempfile, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WIMGT = Path(__file__).resolve().parents[1] / 'project' / 'wimgt'


def ncgr(tiles_y, tiles_x, depth, char_fmt, data):
    rahc = b'RAHC' + struct.pack('<IHHIIIII', 0x20 + len(data), tiles_y, tiles_x, depth, 0, char_fmt, len(data), 0x18) + data
    return b'RGCN' + struct.pack('<HHIHH', 0xFEFF, 1, 0x10 + len(rahc), 0x10, 1) + rahc


class NcgrBitmap(unittest.TestCase):
    def decode(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 't.ncgr').write_bytes(data)
            subprocess.run([str(WIMGT), 'DECODE', 't.ncgr', '-d', 'out.png'], cwd=tmp, capture_output=True)
            return pngtool._read_png(Path(tmp, 'out.png'))

    def test_8bpp_scanline_bitmap(self):
        w, h = 16, 16  # 2x2 tiles
        data = bytes((y * 16 + x) % 251 + 1 for y in range(h) for x in range(w))
        pw, ph, _, rgba = self.decode(ncgr(2, 2, 4, 1, data))
        self.assertEqual((pw, ph), (w, h))
        for x, y in ((0, 0), (15, 0), (3, 9), (15, 15)):
            self.assertEqual(rgba(x, y)[0], data[y * w + x], (x, y))

    def test_4bpp_scanline_bitmap(self):
        w, h = 16, 8  # 2x1 tiles
        px = [(x + y) % 15 + 1 for y in range(h) for x in range(w)]
        data = bytes(px[i] | px[i + 1] << 4 for i in range(0, len(px), 2))
        pw, ph, _, rgba = self.decode(ncgr(1, 2, 3, 1, data))
        self.assertEqual((pw, ph), (w, h))
        for x, y in ((0, 0), (9, 0), (3, 5), (15, 7)):
            self.assertEqual(rgba(x, y)[0], px[y * w + x] * 17, (x, y))

    def test_tiled_is_unchanged(self):
        tile0 = bytes([1] * 64)
        tile1 = bytes([2] * 64)
        pw, ph, _, rgba = self.decode(ncgr(1, 2, 4, 0, tile0 + tile1))
        self.assertEqual((pw, ph), (16, 8))
        self.assertEqual((rgba(0, 0)[0], rgba(8, 0)[0]), (1, 2))


if __name__ == '__main__':
    unittest.main()
