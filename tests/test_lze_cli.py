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


class InioLZOTests(unittest.TestCase):
    def test_compress_round_trip(self):
        import random
        rng = random.Random(5)
        noise = bytes(rng.randrange(256) for _ in range(1000))
        for data in (b'abcd', b'x' * 5000, noise, noise * 5 + b'zz', noise[:300] + b'Q' * 700 + noise[:50],
                     bytes(range(256)) * 70, noise[:240] + noise[:240]):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                raw, packed_file, back = root / 'in.bin', root / 'out.1ozl', root / 'back.bin'
                raw.write_bytes(data)
                for verb, src, dest in (('COMPRESS', raw, packed_file), ('DECOMPRESS', packed_file, back)):
                    r = subprocess.run([str(BIN), verb, str(src), '-d', str(dest)],
                                       capture_output=True, text=True, timeout=60)
                    self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(packed_file.read_bytes()[:4], b'1OZL')
                self.assertEqual(back.read_bytes(), data)


class MtxzRleBangTests(unittest.TestCase):
    def test_round_trip(self):
        import random
        rng = random.Random(1)
        data = bytes(rng.randrange(256) for _ in range(300)) + b'A' * 500 + b'xyz' * 40 + bytes(range(130))
        for ext, expect in (('mz', lambda root: root / 'o.mz.dec'), ('rle', lambda root: root / 'x' / 'o.rle.bin')):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / 'in.bin').write_bytes(data)
                packed = root / ('o.' + ext)
                r = subprocess.run([str(BIN), 'COMPRESS', str(root / 'in.bin'), '-d', str(packed)],
                                   capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
                args = [str(BIN), 'EXTRACT', str(packed)] + (['-d', str(root / 'x')] if ext == 'rle' else [])
                r = subprocess.run(args, capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(expect(root).read_bytes(), data, ext)


class Lz4BfttfTests(unittest.TestCase):
    def test_round_trip(self):
        data = b'\x00\x01\x00\x00' + b'hello world ' * 200 + bytes(range(200))
        for ext, verb, out in (('cmpbin', 'DECOMPRESS', 'back.bin'), ('lz4', 'DECOMPRESS', 'back.bin'),
                               ('bfttf', 'EXTRACT', 'x/o.ttf')):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / 'in.bin').write_bytes(data)
                packed = root / ('o.' + ext)
                dest = root / out.split('/')[0]
                for args in ([str(BIN), 'COMPRESS', str(root / 'in.bin'), '-d', str(packed)],
                             [str(BIN), verb, str(packed), '-d', str(dest)]):
                    r = subprocess.run(args, capture_output=True, text=True, timeout=60)
                    self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual((root / out).read_bytes(), data, ext)


class LzmaBrtexTests(unittest.TestCase):
    def test_round_trip(self):
        data = b'bres' * 500 + bytes(range(256)) * 4
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'in.bin').write_bytes(data)
            for args in ([str(BIN), 'COMPRESS', str(root / 'in.bin'), '-d', str(root / 'o.brtex')],
                         [str(BIN), 'EXTRACT', str(root / 'o.brtex')]):
                r = subprocess.run(args, capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual((root / 'o.brtex').read_bytes()[:6], b'\xffLZMA\x00')
            self.assertEqual((root / 'o.brtex.brres').read_bytes(), data)


class WrapperGroup2Tests(unittest.TestCase):
    def test_ngr_packlz_crfnt_lpf2(self):
        import random
        rng = random.Random(2)
        data = b'RFNT' + bytes(rng.randrange(256) for _ in range(200)) + b'ab' * 600 + bytes(range(100))
        found = {'ngr': 'o.ngr.d/o.bin', 'packlz': 'o.packlz.d/o.packlz.bin',
                 'crfnt': 'o.brfnt', 'lpf2': 'o.lpf2.d/o.bin'}
        for ext, rel in found.items():
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / 'in.bin').write_bytes(data)
                for args in ([str(BIN), 'COMPRESS', str(root / 'in.bin'), '-d', str(root / ('o.' + ext))],
                             [str(BIN), 'EXTRACT', str(root / ('o.' + ext))]):
                    r = subprocess.run(args, capture_output=True, text=True, timeout=60)
                    self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual((root / rel).read_bytes(), data, ext)


class DspEncodeTests(unittest.TestCase):
    def test_wav_to_dsp_round_trip(self):
        import math, struct, wave
        n = 3000
        src = [int(12000 * math.sin(i * 0.07) + 4000 * math.sin(i * 0.31)) for i in range(n)]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with wave.open(str(root / 'in.wav'), 'wb') as w:
                w.setnchannels(1), w.setsampwidth(2), w.setframerate(32000)
                w.writeframes(struct.pack('<%dh' % n, *src))
            for args in ([str(BIN), 'COMPRESS', str(root / 'in.wav'), '-d', str(root / 'o.dsp')],
                         [str(BIN), 'EXTRACT', str(root / 'o.dsp'), '-d', str(root / 'x.wav')]):
                r = subprocess.run(args, capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(len((root / 'o.dsp').read_bytes()), 0x60 + (n + 13) // 14 * 8)
            with wave.open(str(root / 'x.wav')) as w:
                self.assertEqual(w.getframerate(), 32000)
                got = struct.unpack('<%dh' % w.getnframes(), w.readframes(w.getnframes()))
            self.assertEqual(len(got), n)
            self.assertLess(max(abs(a - b) for a, b in zip(src, got)), 600)


class Bzip2Tests(unittest.TestCase):
    def test_plain_and_block_wrapper(self):
        import bz2
        data = b'BLKDATA' + b'hello ' * 400 + bytes(range(256))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'in.blk').write_bytes(data)
            for ext in ('bz2', 'blkbz2'):
                r = subprocess.run([str(BIN), 'COMPRESS', str(root / 'in.blk'), '-d', str(root / ('o.' + ext))],
                                   capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(bz2.decompress((root / 'o.bz2').read_bytes()), data)
            (root / 'o.blkbz2').rename(root / 't.blk.bz2')
            r = subprocess.run([str(BIN), 'EXTRACT', str(root / 't.blk.bz2')], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(next((root / 't.blk.bz2.d').iterdir()).read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
