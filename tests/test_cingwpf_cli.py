"""Tests for CiNG Wish Pack File (CING-WPF) archive format support."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def build_synthetic_wpf(files: list[tuple[str, bytes]]) -> bytes:
    """Build a synthetic CiNG WPF archive from a list of (filename, payload) pairs."""
    data = bytearray()
    offsets = []
    
    # Calculate offsets for each file record
    cur = 0
    for name, payload in files:
        offsets.append(cur)
        fsize = len(payload)
        padded = (fsize + 15) & ~15
        cur += 32 + padded
    
    for i, (name, payload) in enumerate(files):
        rec_off = offsets[i]
        next_off = offsets[i + 1] if i + 1 < len(files) else (rec_off + 32 + ((len(payload) + 15) & ~15))
        
        # Name field: 24 bytes null terminated, typically with leading backslash
        raw_name = ("\\" + name).encode("ascii")
        name_buf = raw_name.ljust(24, b"\x00")[:24]
        
        fsize = len(payload)
        hdr = name_buf + struct.pack("<II", fsize, next_off)
        data.extend(hdr)
        data.extend(payload)
        
        # 16-byte alignment padding
        pad_len = ((fsize + 15) & ~15) - fsize
        data.extend(b"\x00" * pad_len)
        
    return bytes(data)


class CingWpfTests(unittest.TestCase):
    def test_detection_and_extraction(self):
        with tempfile.TemporaryDirectory() as td:
            wpf_path = Path(td) / "archive.wpf"
            dest_dir = Path(td) / "extracted"
            
            members = [
                ("hello.bin", b"Hello CiNG world!"),
                ("test_data.txt", b"Arbitrary test payload that has some length 1234567890"),
                ("item.dat", b"\x01\x02\x03\x04\x05\x06\x07\x08"),
            ]
            
            archive_data = build_synthetic_wpf(members)
            wpf_path.write_bytes(archive_data)
            
            # Test filetype detection
            res = subprocess.run([str(WSZST), "filetype", str(wpf_path)], capture_output=True, text=True, check=True)
            self.assertIn("CING-WPF", res.stdout)
            
            # Test extraction
            subprocess.run([str(WSZST), "extract", str(wpf_path), "--dest", str(dest_dir)], check=True)
            
            # Verify extracted files match byte-for-byte
            for name, payload in members:
                out_file = dest_dir / name
                self.assertTrue(out_file.exists(), f"Missing extracted file {name}")
                self.assertEqual(out_file.read_bytes(), payload)


if __name__ == "__main__":
    unittest.main()
