"""Tests for Jupiter Corp Nintendo DS Model/Motion Package (JUPITER-PCK) format support."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def build_synthetic_jupiter_pck(members: list[bytes]) -> bytes:
    """Build a synthetic Jupiter PCK archive given a list of member payloads."""
    count = len(members)
    hdr_table_len = 8 + count * 4
    hdr_len = (hdr_table_len + 15) & ~15
    pad_bytes = hdr_len - hdr_table_len
    
    header = struct.pack("<II", hdr_len, count)
    for m in members:
        header += struct.pack("<I", len(m))
    header += b"\x00" * pad_bytes
    
    body = bytearray()
    for m in members:
        body.extend(m)
        
    return header + bytes(body)


class JupiterPckTests(unittest.TestCase):
    def test_short_member_does_not_borrow_next_member_magic(self):
        members = [b'BMD0first', b'B', b'MD0last']
        with tempfile.TemporaryDirectory() as td:
            source, dest = Path(td) / 'sample.pck', Path(td) / 'out'
            source.write_bytes(build_synthetic_jupiter_pck(members))
            result = subprocess.run([str(WSZST), 'EXTRACT', str(source), '-d', str(dest),
                                     '--no-passthrough', '--recurse=0'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest / 'file_001.bin').read_bytes(), b'B')
            self.assertFalse((dest / 'file_001.nsbmd').exists())

    def test_detection_and_extraction(self):
        with tempfile.TemporaryDirectory() as td:
            pck_path = Path(td) / "sample.pck"
            dest_dir = Path(td) / "extracted"
            
            # Synthetic BMD0 and BCA0 payloads
            bmd_payload = b"BMD0" + b"\x00" * 60
            bca_payload = b"BCA0" + b"\x11\x22\x33\x44" * 10
            coli_payload = b"COLI" + b"\xaa\xbb" * 20
            
            members = [bmd_payload, bca_payload, coli_payload]
            archive_data = build_synthetic_jupiter_pck(members)
            pck_path.write_bytes(archive_data)
            
            # Verify filetype detection
            res = subprocess.run([str(WSZST), "filetype", str(pck_path)], capture_output=True, text=True, check=True)
            self.assertIn("JUPITER-PCK", res.stdout)
            
            # Verify extraction
            subprocess.run([str(WSZST), "extract", str(pck_path), "--dest", str(dest_dir)], check=True)
            
            # Verify members match byte-for-byte with appropriate typed extensions
            f0 = dest_dir / "file_000.nsbmd"
            f1 = dest_dir / "file_001.nsbca"
            f2 = dest_dir / "file_002.coli"
            
            self.assertTrue(f0.exists(), "Missing file_000.nsbmd")
            self.assertTrue(f1.exists(), "Missing file_001.nsbca")
            self.assertTrue(f2.exists(), "Missing file_002.coli")
            
            self.assertEqual(f0.read_bytes(), bmd_payload)
            self.assertEqual(f1.read_bytes(), bca_payload)
            self.assertEqual(f2.read_bytes(), coli_payload)


if __name__ == "__main__":
    unittest.main()
