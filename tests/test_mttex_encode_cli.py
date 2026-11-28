"""PNG -> Capcom MT Framework Mobile TEX (PICA200 RGBA8) via wimgt, checked by decoding it back."""
from pathlib import Path
import subprocess
import tempfile
import unittest

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None

WIMGT = Path(__file__).resolve().parents[1] / 'project/bin/wimgt'


@unittest.skipIf(Image is None, 'Pillow is not installed')
class MttexEncodeTests(unittest.TestCase):
    def test_rgba_round_trip(self):
        w, h = 20, 12
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for alpha in (lambda x, y: 255, lambda x, y: 128 if (x + y) % 5 == 0 else 255):
                src = Image.new('RGBA', (w, h))
                src.putdata([(x * 12 % 256, y * 20 % 256, x * y % 256, alpha(x, y))
                             for y in range(h) for x in range(w)])
                src.save(root / 'in.png')
                for args in (['COPY', str(root / 'in.png'), '-d', str(root / 'o.mttex'), '--overwrite'],
                             ['COPY', str(root / 'o.mttex'), '-d', str(root / 'b.png'), '--overwrite']):
                    r = subprocess.run([str(WIMGT), *args], capture_output=True, text=True, timeout=60)
                    self.assertEqual(r.returncode, 0, r.stderr)
                head = (root / 'o.mttex').read_bytes()[:16]
                self.assertEqual(head[:4], b'TEX\x00')
                back = Image.open(root / 'b.png').convert('RGBA')
                self.assertEqual(back.size, (w, h))
                self.assertEqual(back.tobytes(), src.tobytes())


if __name__ == '__main__':
    unittest.main()
