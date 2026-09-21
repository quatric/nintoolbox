"""C CLI regressions: python3 -m unittest discover -s tests -p test_compression_cli.py."""
from pathlib import Path
import subprocess
import tempfile
import unittest


BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


class CompressionCliTests(unittest.TestCase):
    def run_tool(self, *args):
        result = subprocess.run(
            [str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30
        )
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result

    def test_roundtrips(self):
        payload = b'Huffman and Yay0 regression: 0123456789 abcdef!\x12\x34\x56\x78' * 20
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input.bin'
            source.write_bytes(payload)
            for extension in ('huff4', 'huff8', 'yay0', 'lz10'):
                with self.subTest(format=extension):
                    packed = root / ('packed.' + extension)
                    output = root / ('output.' + extension)
                    result = self.run_tool('COMPRESS', source, '-d', packed)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = self.run_tool('DECOMPRESS', packed, '-d', output)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(output.read_bytes(), payload)

    def test_corrupt_input_reports_failure_without_writing_output(self):
        fixtures = {
            'bad.huff8': bytes([0x28, 33, 0, 0, 1, 0xc0, 0, 65, 66, 0, 0, 0, 0]),
            'bad.yay0': (
                b'Yay0' + bytes([0, 0, 0, 6]) + b'\xff' * 4
                + bytes([0, 0, 0, 22, 0xe0, 0, 0, 0, 0x10, 2, 65, 66, 67])
            ),
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, data in fixtures.items():
                with self.subTest(format=name):
                    source = root / name
                    source.write_bytes(data)
                    output = root / (name + '.out')
                    result = self.run_tool('DECOMPRESS', source, '-d', output)
                    self.assertGreater(result.returncode, 0)
                    self.assertIn("Can't decompress", result.stderr)
                    self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
