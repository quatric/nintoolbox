"""ZDAT extraction/rebuild regressions using synthetic bundle headers."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def archive(payload):
    name = b'payload.bin'
    header = bytearray(32)
    header[:4] = b'ZDAT'
    for offset, value in ((6, 32), (10, 48), (14, 48 + len(name)), (18, 1)):
        struct.pack_into('<H', header, offset, value)
    return bytes(header) + struct.pack('<4I', len(name), len(payload), len(payload), 0) + name + payload


def bundle(total=32, signature=b'UnityFS\0'):
    return signature + struct.pack('>I', 6) + b'v\0r\0' + struct.pack('>Q', total) + bytes(8)


class ZdatCliTests(unittest.TestCase):
    def run_tool(self, *args):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unmask_only_verified_bundles_and_rebuild_exactly(self):
        cases = {
            'valid': (bundle(), True),
            'wrong_size': (bundle(total=100), False),
            'unterminated_version': (b'UnityFS\0' + struct.pack('>I', 6) + b'A' * 24, False),
            'wrong_signature_terminator': (bundle(signature=b'UnityFSX'), False),
        }
        for name, (plain, valid) in cases.items():
            with self.subTest(case=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                stored = bytes(value ^ 0x5a for value in plain)
                original = archive(stored)
                source = root / 'input.zdat'
                source.write_bytes(original)
                output = root / 'extracted'
                self.run_tool('EXTRACT', source, '-d', output)
                self.assertEqual((output / 'payload.bin').read_bytes(), plain if valid else stored)
                cache = (output / '.zdat-cache.txt').read_text()
                self.assertIn('payload.bin\t' + ('90' if valid else '0'), cache)
                rebuilt = root / 'rebuilt.zdat'
                self.run_tool('CREATE', output, '-d', rebuilt)
                self.assertEqual(rebuilt.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
