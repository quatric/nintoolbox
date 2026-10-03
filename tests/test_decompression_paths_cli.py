"""Output extensions must be derived from the filename, not parent directories."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


class DecompressionPathTests(unittest.TestCase):
    def check_path(self, filename, data, expected_name, payload):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'archive.d' / 'nested'
            root.mkdir(parents=True)
            source = root / filename
            source.write_bytes(data)
            result = subprocess.run([str(BIN), 'EXTRACT', str(source), '--recurse=1', '--no-passthrough'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root/expected_name).is_file(), result.stdout + result.stderr)
            self.assertEqual((root/expected_name).read_bytes(), payload)
            self.assertEqual(source.read_bytes(), data)

    def test_zlib_dotted_parent(self):
        self.check_path('sample.zlib', zlib.compress(b'hello'), 'sample.bin', b'hello')

    def test_deflate_dotted_parent(self):
        encoder = zlib.compressobj(wbits=-15)
        self.check_path('sample.deflate', encoder.compress(b'hello')+encoder.flush(),
                        'sample.bin', b'hello')

    def test_existing_payload_extension_preserved(self):
        self.check_path('sample.txt.zlib', zlib.compress(b'hello'), 'sample.txt', b'hello')

    def test_mtxz_extensionless_source_stays_beside_source(self):
        payload = b'hello'
        data = b'MTXZ' + struct.pack('>III',0,len(payload),0) + zlib.compress(payload)
        self.check_path('sample', data, 'sample.hgpk', payload)

    def test_lz4_dotted_parent(self):
        # Independent 64 KiB frame, one uncompressed block, end marker.
        data = bytes.fromhex('04224d1860408205000080') + b'hello' + bytes(4)
        self.check_path('sample.lz4', data, 'sample.bin', b'hello')

    def test_zstd_dotted_parent(self):
        # Single-segment frame, five-byte content, one last raw block.
        data = bytes.fromhex('28b52ffd2005290000') + b'hello'
        self.check_path('sample.zst', data, 'sample.bin', b'hello')


if __name__ == '__main__':
    unittest.main()
