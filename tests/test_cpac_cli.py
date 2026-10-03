"""Capcom CPAC multi-section archive container detection and extraction tests."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'
if not BIN.exists():
    BIN = Path(__file__).resolve().parents[1] / 'project/wszst'


def make_synthetic_cpac():
    # Outer header: 32 bytes (0x20) -> 4 section records (sec0 at 0x20, sec1 at 0x20+s0_sz, etc.)
    # We will build 2 sections:
    # Sec 0: BKEY section with 1 record (2 files)
    # Sec 1: PKEY section with 2 palettes
    
    # Payload 1: raw 16 bytes
    file1 = b'TEST_DATA_PART_1'
    # Payload 2: raw 16 bytes
    file2 = b'TEST_DATA_PART_2'
    
    # Sec 0 BKEY table: 32 bytes header + 16 bytes record
    # table_len = 48 (0x30)
    sec0_tbl = bytearray(48)
    # 24-byte tag: len=24, ver=2, key='BKEY' (0x424B4559), next=24, dat='BDAT' (0x42444154), tbl_len=48
    struct.pack_into('<IIIIII', sec0_tbl, 0, 24, 2, 0x424b4559, 24, 0x42444154, 48)
    # Record 0 at offset 32:
    # off1 = 0, size1 = len(file1)
    # off2 = len(file1), size2 = len(file2)
    struct.pack_into('<IIII', sec0_tbl, 32, 0, len(file1), len(file1), len(file2))
    sec0_data = sec0_tbl + file1 + file2
    sec0_sz = len(sec0_data)

    # Sec 1 PKEY table: 32 bytes header + 2 palette descriptors (8 bytes) = 40 bytes (0x28)
    sec1_tbl = bytearray(40)
    # 24-byte tag: len=24, ver=2, key='PKEY' (0x504B4559), next=24, dat='PDAT' (0x50444154), tbl_len=40
    struct.pack_into('<IIIIII', sec1_tbl, 0, 24, 2, 0x504b4559, 24, 0x50444154, 40)
    # Descriptor 0: 16 colors (w0=0x1000), bank 0 (w1=0) -> offset 0 in payload, size 32
    struct.pack_into('<HH', sec1_tbl, 32, 0x1000, 0)
    # Descriptor 1: 256 colors (w0=0x0100), bank 1 (w1=1) -> offset 32 in payload, size 512
    struct.pack_into('<HH', sec1_tbl, 36, 0x0100, 1)
    
    pal_payload = bytearray(32 + 512)
    # fill some colors
    pal_payload[0:32] = b'\x1f\x00' * 16
    pal_payload[32:32+512] = b'\x00\x7c' * 256
    sec1_data = sec1_tbl + pal_payload
    sec1_sz = len(sec1_data)

    # Outer header:
    # header_len = 0x20
    # Sec 0: offset is header_len (0x20), size is sec0_sz
    # Sec 1: offset is 0x20 + sec0_sz, size is sec1_sz
    # Entries 2 and 3 can be empty (offset = 0, size = 0)
    outer_hdr = bytearray(32)
    struct.pack_into('<I', outer_hdr, 0, 32)
    struct.pack_into('<I', outer_hdr, 4, sec0_sz)
    struct.pack_into('<II', outer_hdr, 8, 32 + sec0_sz, sec1_sz)
    struct.pack_into('<II', outer_hdr, 16, 0, 0)
    struct.pack_into('<II', outer_hdr, 24, 0, 0)

    return bytes(outer_hdr) + sec0_data + sec1_data


class CpacTests(unittest.TestCase):
    def test_member_cannot_read_the_following_section(self):
        data = bytearray(make_synthetic_cpac())
        # The first payload is 30 bytes, so a 100-byte member crosses into PKEY.
        struct.pack_into('<I', data, 32 + 36, 100)
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            source, dest = root / 'archive.bin', root / 'out'
            source.write_bytes(data)
            result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                     '--no-passthrough', '--recurse=0'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest / 'sec0_0000_0000.bin').read_bytes(), b'TEST_DATA_PART_2')
            files = [p for p in dest.rglob('*') if p.is_file()]
            self.assertEqual(len(files), 3)
            self.assertIn(b'TEST_DATA_PART_2', [p.read_bytes() for p in files])

    def test_identify_synthetic(self):
        data = make_synthetic_cpac()
        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
            f.write(data)
            f.flush()
            temp_path = f.name

        try:
            res = subprocess.run([str(BIN), 'filetype', temp_path], capture_output=True, text=True, check=True)
            self.assertIn('CPAC', res.stdout)
        finally:
            Path(temp_path).unlink(missing_ok=True)

    def test_extract_synthetic(self):
        data = make_synthetic_cpac()
        with tempfile.TemporaryDirectory() as tmpdir:
            archive_path = Path(tmpdir) / 'archive.bin'
            archive_path.write_bytes(data)
            dest_dir = Path(tmpdir) / 'extracted'
            
            res = subprocess.run([str(BIN), 'extract', str(archive_path), '--dest', str(dest_dir)],
                                 capture_output=True, text=True, check=True)
            self.assertTrue(dest_dir.is_dir())
            files = sorted([f.name for f in dest_dir.iterdir()])
            self.assertEqual(len(files), 4)
            self.assertIn('sec0_0000_0000.bin', files)
            self.assertIn('sec0_0000_0001.bin', files)
            self.assertIn('sec1_pal_0000.nclr', files)
            self.assertIn('sec1_pal_0001.nclr', files)

            # Check content
            p0 = dest_dir / 'sec0_0000_0000.bin'
            p1 = dest_dir / 'sec0_0000_0001.bin'
            self.assertEqual(p0.read_bytes(), b'TEST_DATA_PART_1')
            self.assertEqual(p1.read_bytes(), b'TEST_DATA_PART_2')

            pal0 = dest_dir / 'sec1_pal_0000.nclr'
            pal1 = dest_dir / 'sec1_pal_0001.nclr'
            self.assertEqual(len(pal0.read_bytes()), 32)
            self.assertEqual(len(pal1.read_bytes()), 512)


if __name__ == '__main__':
    unittest.main()
