"""Level-5 / Armor Project PAC archive container detection and extraction tests."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'
if not BIN.exists():
    BIN = Path(__file__).resolve().parents[1] / 'project/wszst'


def make_synthetic_l5pac():
    buf = bytearray()

    # Entry 1: hello.txt
    file1 = b'Hello world!\n'
    e1_hdr = bytearray(0x50)
    e1_hdr[0:10] = b'hello.txt\0'
    struct.pack_into('<III', e1_hdr, 0x40, 0x50, len(file1), 0x60)
    buf += e1_hdr + file1 + b'\x00' * (0x60 - 0x50 - len(file1))

    # Entry 2: number.bin
    file2 = b'\x01\x02\x03\x04'
    e2_hdr = bytearray(0x50)
    e2_hdr[0:11] = b'number.bin\0'
    struct.pack_into('<III', e2_hdr, 0x40, 0x50, len(file2), 0x60)
    buf += e2_hdr + file2 + b'\x00' * (0x60 - 0x50 - len(file2))

    # Terminal sentinel record
    sentinel = bytearray(0x50)
    struct.pack_into('<III', sentinel, 0x40, 0x50, 0xffffffff, 0xffffffff)
    buf += sentinel

    return bytes(buf)


class L5PacTests(unittest.TestCase):
    def test_identify_synthetic(self):
        data = make_synthetic_l5pac()
        with tempfile.NamedTemporaryFile(suffix='.pac', delete=False) as f:
            f.write(data)
            f.flush()
            temp_path = f.name

        try:
            res = subprocess.run([str(BIN), 'filetype', temp_path], capture_output=True, text=True, check=True)
            self.assertIn('L5-PAC', res.stdout)
        finally:
            Path(temp_path).unlink(missing_ok=True)

    def test_extract_synthetic(self):
        data = make_synthetic_l5pac()
        with tempfile.TemporaryDirectory() as tmpdir:
            archive_path = Path(tmpdir) / 'archive.pac'
            archive_path.write_bytes(data)
            dest_dir = Path(tmpdir) / 'extracted'

            res = subprocess.run([str(BIN), 'extract', str(archive_path), '--dest', str(dest_dir)],
                                 capture_output=True, text=True, check=True)
            self.assertTrue(dest_dir.is_dir())
            files = sorted([f.name for f in dest_dir.iterdir()])
            self.assertEqual(len(files), 2)
            self.assertIn('hello.txt', files)
            self.assertIn('number.bin', files)

            self.assertEqual((dest_dir / 'hello.txt').read_bytes(), b'Hello world!\n')
            self.assertEqual((dest_dir / 'number.bin').read_bytes(), b'\x01\x02\x03\x04')


if __name__ == '__main__':
    unittest.main()
