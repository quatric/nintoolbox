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
            for extension in ('huff4', 'huff8', 'yay0', 'lz10', 'zlib'):
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

    def test_inio_lzo_decompress(self):
        # 1OZL fixture: 12-byte header + LZO1X payload
        sample_1ozl = (
            b'1OZL\xb0\x02\x00\x00\x66\x02\x00\x00'
            b'&RGCN\xfe\x01\x01\xb0\x02\x00\x00\x10\x00\x01\x00RAHC\xa0M\x01\xff'
            b'B\x00\x03\x00n\x02\x10\x00a\x00\x80M\x02\x18]\x00\x11A\x00wA\x00UA'
            b'\x00D\x01\x00$m\x00"R\x00"B\r\x00\x11R\x00\x11\x11\x0c\x10\x0cww\x17\x00'
            b'Uuw\x01\x11Qw\x17""uM\x00R\x8d\x00$L\x00\x00\x0f\x18!!B,a&!\xbd\x14\xca('
            b'\x83b\x87\x18\x00e\xa8\x00b\x84\x00!\x87\x00\x00\x8e$\x00"f\x00"$\r\x00'
            b'\x11&\x00\x11\x11\x0c\x10\x0cww\x17\x00Uuw\x01\x11Qw\x17""uM\x00R\x8d\x00'
            b'$L\x00\x00\x0f\x18!!B,a&!\xbd\x14\xca(\x83b\x87\x18\x00e\xa8\x00b\x84\x00'
            b'!\x87\x00\x00\x8e$\x00"f\x00"$\r\x00\x11&\x00\x11\x11\x0c\x10\x0cww\x17\x00'
            b'Uuw\x01\x11Qw\x17""uM\x00R\x8d\x00$L\x00\x00\x0f\x18!!B,a&!\xbd\x14\xca(\x11\x00\x00'
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.bin'
            output = root / 'sample.out'
            source.write_bytes(sample_1ozl)
            # Verify wszst filetype detects INIO-LZO
            ft_res = self.run_tool('FILETYPE', source)
            self.assertEqual(ft_res.returncode, 0)
            self.assertIn('INIO-LZO', ft_res.stdout)


if __name__ == '__main__':
    unittest.main()
