"""CLI tests for Konami Castlevania DS sprite object format (0xBEEFF00D)."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def make_cv_spr():
    part_off = 0x30
    hit_off = part_off + 16
    frame_off = hit_off + 8
    fdelay_off = frame_off + 12
    anim_off = fdelay_off + 8
    footer_off = anim_off + 8
    file_sz = footer_off + 16

    buf = bytearray(file_sz)
    buf[0:4] = b'\x0d\xf0\xef\xbe'
    struct.pack_into('<11I', buf, 4,
                     part_off, hit_off, frame_off, fdelay_off, anim_off,
                     0, 0, footer_off, 1, 1, file_sz)

    # Part 0
    struct.pack_into('<hh4H4B', buf, part_off, -16, -16, 0, 0, 32, 32, 0, 0, 0, 0)

    # Hitbox 0
    struct.pack_into('<hh2H', buf, hit_off, -8, -8, 16, 16)

    # Frame 0
    struct.pack_into('<H2B2I', buf, frame_off, 0, 1, 1, 0, 0)

    # FrameDelay 0
    struct.pack_into('<HHI', buf, fdelay_off, 0, 5, 0)

    # Animation 0
    struct.pack_into('<2I', buf, anim_off, 1, 0)

    return bytes(buf)


class CastlevaniaSpriteTests(unittest.TestCase):
    def test_text_conversion(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        spr_path = Path(tmp.name) / 'sprite.dat'
        txt_path = Path(tmp.name) / 'sprite.txt'
        spr_path.write_bytes(make_cv_spr())

        subprocess.run([str(BIN), 'text', str(spr_path), '-d', str(txt_path), '--overwrite'], check=True)
        self.assertTrue(txt_path.exists())
        content = txt_path.read_text()
        self.assertIn('magic = 0xBEEFF00D', content)
        self.assertIn('part_count = 1', content)
        self.assertIn('[[part]]', content)
        self.assertIn('[[hitbox]]', content)
        self.assertIn('[[frame]]', content)
        self.assertIn('[[frame_delay]]', content)
        self.assertIn('[[animation]]', content)

    def test_dump_structure(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        spr_path = Path(tmp.name) / 'sprite.dat'
        spr_path.write_bytes(make_cv_spr())

        res = subprocess.run([str(BIN), 'dump', str(spr_path)], stdout=subprocess.PIPE, text=True, check=True)
        self.assertIn('Dump structure of CV-SPR', res.stdout)
        self.assertIn('magic = 0xBEEFF00D', res.stdout)
        self.assertIn('part_count = 1', res.stdout)

    def test_retail_sample_text(self):
        retail_path = Path('/tmp/dos/extracted/files/so/p_light.dat')
        if not retail_path.exists():
            return
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        txt_path = Path(tmp.name) / 'p_light.txt'

        subprocess.run([str(BIN), 'text', str(retail_path), '-d', str(txt_path), '--overwrite'], check=True)
        self.assertTrue(txt_path.exists())
        content = txt_path.read_text()
        self.assertIn('magic = 0xBEEFF00D', content)
        self.assertIn('file_size = 576', content)
        self.assertIn('part_count = 22', content)
        self.assertIn('frame_count = 5', content)
        self.assertIn('animation_count = 5', content)


if __name__ == '__main__':
    unittest.main()
