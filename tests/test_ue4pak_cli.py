"""UE4 PAK malformed indexes, payload bounds, paths, and output failures."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import signal
import zlib
from test_decompression_write_cli import resource, reject_file_writes

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def fstring(value):
    raw = value.encode() + b'\0'
    return struct.pack('<i', len(raw)) + raw


def ue4pak(names=('first.bin', 'second.bin'), offset=None, truncate=False, bad_string=False):
    body = bytearray()
    entries = []
    for name in names:
        payload = b'hello'
        start = len(body)
        body += bytes(53) + payload
        entries.append(fstring(name) + struct.pack('<QQQI', start if offset is None else offset,
                                                   len(payload), len(payload), 0)
                       + bytes(20) + struct.pack('<BI', 0, 0))
    index = fstring('../../../') + struct.pack('<I', len(entries)) + b''.join(entries)
    if truncate:
        index = index[:-3]
    if bad_string:
        index = struct.pack('<I', 0x80000000) + index[4:]
    footer = struct.pack('<IIQQ', 0x5a6f12e1, 3, len(body), len(index)) + bytes(20)
    return bytes(body) + index + footer


def compressed_pak(stream, expected=5):
    body = bytes(53) + stream
    entry = (fstring('first.bin') + struct.pack('<QQQI', 0, len(stream), expected, 1)
             + bytes(20) + struct.pack('<IQQBI', 1, 53, len(body), 0, expected))
    index = fstring('../../../') + struct.pack('<I', 1) + entry
    footer = struct.pack('<IIQQ', 0x5a6f12e1, 3, len(body), len(index)) + bytes(20)
    return body + index + footer


class UE4PakTests(unittest.TestCase):
    def extract(self, root, data, limited=False):
        source, dest = root/'sample.pak', root/'out'
        source.write_bytes(data)
        result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                 '--no-passthrough', '--recurse=0'], capture_output=True,
                                text=True, timeout=30,
                                preexec_fn=reject_file_writes if limited else None)
        return result, dest

    def test_compressed_payload_must_finish_and_fill_declared_size(self):
        for stream, expected, success in ((zlib.compress(b'hello'), 5, True),
                                           (zlib.compress(b'hello'), 10, False),
                                           (b'garbage', 5, False),
                                           (zlib.compress(b'hello')[2:-5], 5, False)):
            with self.subTest(stream=stream, expected=expected), tempfile.TemporaryDirectory() as td:
                result, dest = self.extract(Path(td), compressed_pak(stream, expected))
                if success:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual((dest/'first.bin').read_bytes(), b'hello')
                else:
                    self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
                    self.assertFalse((dest/'first.bin').exists())

    def test_valid_members(self):
        with tempfile.TemporaryDirectory() as td:
            result, dest = self.extract(Path(td), ue4pak())
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest/'first.bin').read_bytes(), b'hello')
            self.assertEqual((dest/'second.bin').read_bytes(), b'hello')

    def test_malformed_indexes_and_overflowing_offsets_fail(self):
        for data in (ue4pak(truncate=True), ue4pak(bad_string=True),
                     ue4pak(offset=0xffffffffffffffff)):
            with self.subTest(data=data[-44:]), tempfile.TemporaryDirectory() as td:
                result, dest = self.extract(Path(td), data)
                self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
                self.assertFalse(any(p.is_file() for p in dest.rglob('*')))

    def test_internal_parent_path_is_replaced(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            result, dest = self.extract(root, ue4pak(names=('folder/../../escape.bin',)))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((root/'escape.bin').exists())
            self.assertEqual((dest/'file_0000.bin').read_bytes(), b'hello')

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_close_failure_stops_extraction(self):
        with tempfile.TemporaryDirectory() as td:
            result, dest = self.extract(Path(td), ue4pak(), limited=True)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(result.stderr.count('Error while closing file:'), 1, result.stderr)
            self.assertFalse(any(p.is_file() for p in dest.rglob('*')))
