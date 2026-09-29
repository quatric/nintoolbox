"""SCB map validation, palette modes, tile flips and companion loading."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from pngtool import _read_png

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wimgt'


def literal(data):
    return b'Le' + struct.pack('<I', len(data)) + b''.join(
        bytes([0xaa]) + data[i:i+4] for i in range(0, len(data), 4))


def fixture(colors=16):
    palette = struct.pack('<' + 'H'*colors, *[i for i in range(colors)])
    pixels = bytes((x+y*3) % colors for y in range(8) for x in range(8))
    tiles = pixels if colors == 256 else bytes(pixels[i] | pixels[i+1] << 4 for i in range(0,64,2))
    return struct.pack('<4I4H', 4, 1, 16, 8, 0, 0x400, 0x800, 0xc00), tiles, palette


class LuminousBackgroundTests(unittest.TestCase):
    def decode(self, screen, tiles, palette, flags=()):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for ext, data in (('scb', screen), ('imb', tiles), ('plb', palette)):
            if data is not None:
                (root / ('sample.' + ext)).write_bytes(data)
        dest = root / 'out.png'
        result = subprocess.run([str(BIN), 'DECODE', str(root/'sample.scb'), '-d', str(dest), *flags],
                                capture_output=True, text=True, timeout=30)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result, dest

    def test_palette_modes_and_four_flip_states(self):
        for colors in (16, 256):
            result, dest = self.decode(*fixture(colors))
            self.assertEqual(result.returncode, 0, result.stderr)
            w, h, channels, pixel = _read_png(dest)
            self.assertEqual((w,h), (32,8))
            for y in range(h):
                for x in range(w):
                    flip = x//8
                    tx, ty = (7-x%8 if flip&1 else x%8), (7-y if flip&2 else y)
                    index = (tx+ty*3) % colors
                    red, green = index&31, (index>>5)&31
                    self.assertEqual(pixel(x,y), ((red<<3)|(red>>2), (green<<3)|(green>>2), 0, 255 if index else 0))

    def test_lze_compressed_companions(self):
        screen, tiles, palette = fixture()
        result, dest = self.decode(literal(screen), literal(tiles), palette)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(dest.exists())

    def test_missing_companions(self):
        screen, tiles, palette = fixture()
        for t,p in ((None,palette),(tiles,None)):
            result, dest = self.decode(screen,t,p)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(dest.exists())

    def test_invalid_map_bounds(self):
        screen, tiles, palette = fixture()
        for offset,value in ((0,0),(4,0xffffffff),(8,8),(12,7),(16,1),(16,0x1000)):
            broken = bytearray(screen);struct.pack_into('<I',broken,offset,value)
            result,dest=self.decode(broken,tiles,palette)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(dest.exists())

    def test_truncated_and_unsupported_resources(self):
        screen, tiles, palette = fixture()
        for resources in ((screen[:-1],tiles,palette),(screen,tiles[:-1],palette),
                          (screen,tiles,palette[:-2]),(literal(screen)[:-1],tiles,palette)):
            result,dest=self.decode(*resources)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(dest.exists())

    def test_dry_run(self):
        result,dest=self.decode(*fixture(),flags=('--test',))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(dest.exists())


if __name__ == '__main__':
    unittest.main()
