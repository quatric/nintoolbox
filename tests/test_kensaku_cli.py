"""And-Kensaku Pres archive name bounds and output error regressions."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes

BIN = Path(__file__).resolve().parents[1]/'project/bin/wszst'


def pres():
    data = bytearray(0xc0)
    data[:4] = b'Pres'
    struct.pack_into('<I', data, 0x60, 0xc0)
    descriptors = len(data)
    data += bytes(24)
    name1 = len(data); data += b'first\0'
    name2 = len(data); data += b'second\0'
    extension = len(data); data += b'bin\0'
    for i, (name, payload) in enumerate(((name1, b'first'), (name2, b'second'))):
        struct.pack_into('<4I', data, 0x80+i*32, len(data), len(payload), descriptors+i*12, 2)
        struct.pack_into('<3I', data, descriptors+i*12, name, extension, 0)
        data += payload
    return bytes(data)


class KensakuTests(unittest.TestCase):
    def extract(self, root, data, limited=False, compressed=False):
        if compressed:
            data = struct.pack('<I', (len(data) << 8) | 0x10) + b''.join(
                b'\0' + data[i:i+8] for i in range(0, len(data), 8))
        source, dest = root/('sample.rz' if compressed else 'sample.res'), root/'out'
        source.write_bytes(data)
        result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                 '--no-passthrough', '--recurse=0'], capture_output=True,
                                text=True, timeout=30,
                                preexec_fn=reject_file_writes if limited else None)
        return result, dest

    def test_members_preserve_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            result, dest = self.extract(Path(td), pres())
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest/'first.bin').read_bytes(), b'first')
            self.assertEqual((dest/'second.bin').read_bytes(), b'second')

    def test_invalid_name_descriptors_are_skipped(self):
        original = pres()
        for offset in (0xfffffff8, 0xffffffff, len(original)-11, len(original)):
            data = bytearray(original)
            struct.pack_into('<I', data, 0x88, offset)
            with self.subTest(offset=offset), tempfile.TemporaryDirectory() as td:
                result, dest = self.extract(Path(td), data)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse((dest/'first.bin').exists())
                self.assertEqual((dest/'second.bin').read_bytes(), b'second')

    def test_compressed_archive_uses_the_same_name_bounds(self):
        data = bytearray(pres())
        struct.pack_into('<I', data, 0x88, 0xfffffff8)
        with tempfile.TemporaryDirectory() as td:
            result, dest = self.extract(Path(td), data, compressed=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((dest/'first.bin').exists())
            self.assertEqual((dest/'second.bin').read_bytes(), b'second')

    def test_name_descriptor_at_buffer_end_is_valid(self):
        data = bytearray(pres())
        descriptor = bytes(data[0xc0:0xcc])
        struct.pack_into('<I', data, 0x88, len(data))
        data += descriptor
        with tempfile.TemporaryDirectory() as td:
            result, dest = self.extract(Path(td), data)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest/'first.bin').read_bytes(), b'first')
            self.assertEqual((dest/'second.bin').read_bytes(), b'second')

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_close_failure_stops_extraction(self):
        with tempfile.TemporaryDirectory() as td:
            result, dest = self.extract(Path(td), pres(), limited=True)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(result.stderr.count('Error while closing file:'), 1, result.stderr)
            self.assertFalse(any(p.is_file() for p in dest.rglob('*')))
