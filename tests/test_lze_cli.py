"""LZE token forms, output naming, and malformed stream regressions."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def packed(length, body):
    return b'Le' + struct.pack('<I', length) + body


class LZETests(unittest.TestCase):
    def run_decode(self, data, suffix='.LZE', options=(), explicit=True):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        source = root / ('sample' + suffix)
        source.write_bytes(data)
        dest = root / 'decoded.bin'
        args = [str(BIN), 'DECOMPRESS', str(source)]
        if explicit:
            args += ['-d', str(dest)]
        result = subprocess.run(args + list(options), capture_output=True, text=True, timeout=30)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result, dest if explicit else root / 'sample.bin'

    def test_all_four_modes(self):
        # literal3 ABC, literal3 DEF, long match ABC, short overlap CCCCCC.
        result, dest = self.run_decode(packed(15, b'\x4fABCDEF\x01\x00\x10'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dest.read_bytes(), b'ABCDEFABC' + b'C' * 6)

    def test_literal1_and_flag_rollover(self):
        result, dest = self.run_decode(packed(5, b'\xaaABCD\x02E'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dest.read_bytes(), b'ABCDE')

    def test_short_final_literal3(self):
        for payload in (b'A', b'AB'):
            result, dest = self.run_decode(packed(len(payload), b'\x03' + payload))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(dest.read_bytes(), payload)

    def test_magic_detection_other_extensions(self):
        for suffix in ('.imb', '.scb', '.bin'):
            result, dest = self.run_decode(packed(1, b'\x02A'), suffix)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(dest.read_bytes(), b'A')

    def test_default_name_strips_lze(self):
        result, dest = self.run_decode(packed(1, b'\x02A'), explicit=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dest.read_bytes(), b'A')

    def test_empty_and_dry_run(self):
        result, dest = self.run_decode(packed(0, b''))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dest.read_bytes(), b'')
        result, dest = self.run_decode(packed(1, b'\x02A'), options=('--test',))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(dest.exists())

    def test_maximum_distance_and_short_run(self):
        literals = bytes(i % 251 for i in range(4100))
        body = b''.join(b'\xaa' + literals[i:i+4] for i in range(0, 4100, 4))
        # Long distance 4100, length 18; then short distance 1, length 65.
        body += b'\x04\xff\xff\xfc'
        expected = literals + literals[:18] + literals[17:18] * 65
        result, dest = self.run_decode(packed(len(expected), body))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dest.read_bytes(), expected)

    def test_extract_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, dest = root / 'sample.LZE', root / 'out.bin'
            source.write_bytes(packed(3, bytes([3]) + b'ABC'))
            result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                     '--recurse=0', '--no-passthrough'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(dest.read_bytes(), b'ABC')

    def test_bad_streams_write_nothing(self):
        variants = [packed(0xffffffff, b''), packed(3, b'\x00\x00\x00'),
                    packed(3, b'\x01\x00'), packed(3, b'\x03AB'),
                    packed(2, b'\x06A\x00'), packed(8, b'\x00\x00')]
        for data in variants:
            result, dest = self.run_decode(data)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(dest.exists())

    def test_compress_round_trip(self):
        import random
        rng = random.Random(7)
        noisy = bytes(rng.randrange(256) for _ in range(300))
        samples = [b'A', b'AB', b'ABC', b'C' * 100, b'ABCDEFABC' * 50, noisy,
                   noisy * 20 + b'tail', bytes(range(256)) * 40]
        for data in samples:
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                raw, packed_file, back = root / 'in.bin', root / 'out.lze', root / 'back.bin'
                raw.write_bytes(data)
                result = subprocess.run([str(BIN), 'COMPRESS', str(raw), '-d', str(packed_file)],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(packed_file.read_bytes()[:2], b'Le')
                result = subprocess.run([str(BIN), 'DECOMPRESS', str(packed_file), '-d', str(back)],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(back.read_bytes(), data)



class DADTests(unittest.TestCase):
    def test_compress_round_trip(self):
        import random
        rng = random.Random(3)
        noisy = bytes(rng.randrange(256) for _ in range(500))
        for data in (b'ABABAB', b'Z' * 1000, noisy, noisy * 10 + b'x', bytes(range(256)) * 30):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                raw, packed_file, back = root / 'in.bin', root / 'out.dad', root / 'back.bin'
                raw.write_bytes(data)
                for args, dest in (('COMPRESS', packed_file), ('DECOMPRESS', back)):
                    src = raw if args == 'COMPRESS' else packed_file
                    result = subprocess.run([str(BIN), args, str(src), '-d', str(dest)],
                                            capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(packed_file.read_bytes()[:4], b'DAD\x01')
                self.assertEqual(back.read_bytes(), data)


class WrapperCompressTests(unittest.TestCase):
    def test_ibz_bgw_rz_round_trip(self):
        data = b'hello world ' * 300 + bytes(range(200))
        for ext, verb, out_name in (('ibz', 'EXTRACT', 'sample.ibz.bin'), ('bgw', 'EXTRACT', 'sample.bin'),
                                    ('rz', 'DECOMPRESS', 'back.bin')):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                raw, packed_file = root / 'in.bin', root / ('sample.' + ext)
                raw.write_bytes(data)
                r = subprocess.run([str(BIN), 'COMPRESS', str(raw), '-d', str(packed_file)],
                                   capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
                dest = root / ('x' if verb == 'EXTRACT' else out_name)
                r = subprocess.run([str(BIN), verb, str(packed_file), '-d', str(dest)],
                                   capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
                got = next(dest.rglob('*.bin')) if verb == 'EXTRACT' else dest
                self.assertEqual(got.read_bytes(), data, ext)


if __name__ == '__main__':
    unittest.main()
