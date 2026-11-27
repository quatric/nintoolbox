"""NSCR rendering: extended palette banks (256 colors x 16) and 8-bit affine screens."""
import struct, subprocess, sys, tempfile, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'wszst'


def nclr(colors):
    data = b''.join(struct.pack('<H', c) for c in colors)
    ttlp = b'TTLP' + struct.pack('<IIIII', 0x18 + len(data), 4, 0, len(data), 0x10) + data
    return b'RLCN' + struct.pack('<HHIHH', 0xFEFF, 1, 0x10 + len(ttlp), 0x10, 1) + ttlp


def ncgr(tiles):  # 8 bit tiles, 1 tile per entry of the list: 64 bytes each
    data = b''.join(tiles)
    rahc = b'RAHC' + struct.pack('<IHHIIIII', 0x20 + len(data), 1, len(tiles), 4, 0, 0, len(data), 0x18) + data
    return b'RGCN' + struct.pack('<HHIHH', 0xFEFF, 1, 0x10 + len(rahc), 0x10, 1) + rahc


def nscr(w, h, color_mode, bg_type, data):
    nrcs = b'NRCS' + struct.pack('<IHHHHI', 0x14 + len(data), w, h, color_mode, bg_type, len(data)) + data
    return b'RCSN' + struct.pack('<HHIHH', 0xFEFF, 1, 0x10 + len(nrcs), 0x10, 1) + nrcs


def bgr(r, g, b):
    return r | g << 5 | b << 10


class NscrRender(unittest.TestCase):
    def render(self, files):
        with tempfile.TemporaryDirectory() as tmp:
            for name, data in files.items():
                Path(tmp, name).write_bytes(data)
            subprocess.run([str(WSZST), 'xx', 't.nscr', '--decode', '-o'], cwd=tmp, capture_output=True)
            pngs = list(Path(tmp).glob('t*.png')) + list(Path(tmp, 't.nscr.d').glob('*.png'))
            self.assertTrue(pngs, 'no PNG written')
            return pngtool._read_png(pngs[0])

    def test_extended_palette_bank(self):
        colors = [0] * (16 * 256)
        colors[1] = bgr(31, 0, 0)  # bank 0, index 1: red
        colors[3 * 256 + 1] = bgr(0, 31, 0)  # bank 3, index 1: green
        w, h, _, rgba = self.render({
            't.ncgr': ncgr([bytes([1]) * 64]),
            't.nclr': nclr(colors),
            't.nscr': nscr(16, 8, 2, 2, struct.pack('<HH', 0, 0 | 3 << 12))})
        self.assertEqual(rgba(0, 0)[:3], (255, 0, 0))
        self.assertEqual(rgba(8, 0)[:3], (0, 255, 0))

    def test_affine_bytes(self):
        colors = [0, bgr(31, 0, 0), bgr(0, 0, 31)]
        w, h, _, rgba = self.render({
            't.ncgr': ncgr([bytes([1]) * 64, bytes([2]) * 64]),
            't.nclr': nclr(colors),
            't.nscr': nscr(16, 8, 1, 1, bytes([0, 1]))})
        self.assertEqual((w, h), (16, 8))
        self.assertEqual(rgba(0, 0)[:3], (255, 0, 0))
        self.assertEqual(rgba(8, 0)[:3], (0, 0, 255))


if __name__ == '__main__':
    unittest.main()
