"""Tests for Treasure DS Multi-Resource Archive (TREASURE-MRG) format support."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def build_synthetic_treasure_mrg(members: list[bytes]) -> bytes:
    """Build a synthetic Treasure MRG archive given a list of member payloads."""
    count = len(members)
    table_len = 4 + count * 8
    # Pad first offset to 4-byte alignment
    first_offset = (table_len + 3) & ~3
    
    header = bytearray(struct.pack("<I", count))
    cur_offset = first_offset
    for m in members:
        header.extend(struct.pack("<II", cur_offset, len(m)))
        cur_offset += len(m)
        # 4-byte align between members if needed
        cur_offset = (cur_offset + 3) & ~3
        
    pad_bytes = first_offset - len(header)
    data = header + b"\x00" * pad_bytes
    
    cur_pos = first_offset
    for m in members:
        data += m
        cur_pos += len(m)
        aligned = (cur_pos + 3) & ~3
        data += b"\x00" * (aligned - cur_pos)
        cur_pos = aligned
        
    return bytes(data)


class TreasureMrgTests(unittest.TestCase):
    def test_empty_member_does_not_borrow_next_member_magic(self):
        members = [b'', bytes([4, 0, 1, 1]) + b'payload']
        with tempfile.TemporaryDirectory() as td:
            source, dest = Path(td) / 'sample.mrg', Path(td) / 'out'
            source.write_bytes(build_synthetic_treasure_mrg(members))
            result = subprocess.run([str(WSZST), 'EXTRACT', str(source), '-d', str(dest),
                                     '--no-passthrough', '--recurse=0'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest / 'file_000.bin').read_bytes(), b'')
            self.assertFalse((dest / 'file_000.bg4').exists())
            self.assertEqual((dest / 'file_001.bg4').read_bytes(), members[1])

    def test_detection_and_extraction(self):
        with tempfile.TemporaryDirectory() as td:
            mrg_path = Path(td) / "sample.mrg"
            dest_dir = Path(td) / "extracted"
            
            # Synthetic member payloads: BG4, BG8, and raw binary
            bg4_payload = bytes([0x04, 0x00, 0x01, 0x01]) + b"\x12\x34\x56\x78" * 16
            bg8_payload = bytes([0x08, 0x00, 0x01, 0x01]) + b"\x9a\xbc\xde\xf0" * 32
            raw_payload = b"TREASURE_DATA_TEST_123456789"
            
            members = [bg4_payload, bg8_payload, raw_payload]
            archive_data = build_synthetic_treasure_mrg(members)
            mrg_path.write_bytes(archive_data)
            
            # Verify filetype detection
            res = subprocess.run([str(WSZST), "filetype", str(mrg_path)], capture_output=True, text=True, check=True)
            self.assertIn("TREASURE-MRG", res.stdout)
            
            # Verify extraction
            subprocess.run([str(WSZST), "extract", str(mrg_path), "--dest", str(dest_dir)], check=True)
            
            # Verify members match byte-for-byte with appropriate typed extensions
            f0 = dest_dir / "file_000.bg4"
            f1 = dest_dir / "file_001.bg8"
            f2 = dest_dir / "file_002.bin"
            
            self.assertTrue(f0.exists(), "Missing file_000.bg4")
            self.assertTrue(f1.exists(), "Missing file_001.bg8")
            self.assertTrue(f2.exists(), "Missing file_002.bin")
            
            self.assertEqual(f0.read_bytes(), bg4_payload)
            self.assertEqual(f1.read_bytes(), bg8_payload)
            self.assertEqual(f2.read_bytes(), raw_payload)


if __name__ == "__main__":
    unittest.main()
