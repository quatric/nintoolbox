"""PVOL filename fields, payload bounds, and extraction regressions."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def archive(name, payload, declared_size=None):
    name = name.encode('ascii')
    assert len(name) <= 40
    return (struct.pack('<3I', 2, 32, len(payload) if declared_size is None else declared_size)
            + bytes(20) + name.ljust(40, b'\0') + payload)


class PvolTests(unittest.TestCase):
    def tool(self, *args, success=True):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_full_width_name_is_not_extended_with_payload(self):
        name = 'a' * 32 + 'b' * 8
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.pvol'
            source.write_bytes(archive(name, b'PAYLOAD_WITH_NO_NUL'))
            self.tool('EXTRACT', source, '-d', root / 'out')
            self.assertEqual((root / 'out' / name).read_bytes(), b'PAYLOAD_WITH_NO_NUL')
            self.assertEqual(len(list((root / 'out').iterdir())), 1)

    def test_empty_files_roundtrip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            (source / 'empty.bin').touch()
            packed = root / 'sample.pvol'
            self.tool('CREATE', source, '-d', packed)
            self.tool('EXTRACT', packed, '-d', root / 'out')
            self.assertEqual((root / 'out' / 'empty.bin').read_bytes(), b'')

    def test_truncated_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.pvol'
            source.write_bytes(archive('payload.bin', b'abc', declared_size=20))
            self.tool('EXTRACT', source, '-d', root / 'out', success=False)
            self.assertFalse((root / 'out' / 'payload.bin').exists())

    def test_unsafe_names_do_not_escape_output_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.pvol'
            source.write_bytes(archive('../escaped.bin', b'payload'))
            self.tool('EXTRACT', source, '-d', root / 'out')
            self.assertFalse((root / 'escaped.bin').exists())
            self.assertEqual((root / 'out' / 'file_0000.bin').read_bytes(), b'payload')

    def test_write_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.pvol'
            source.write_bytes(archive('payload.bin', b'payload'))
            (root / 'out' / 'payload.bin').mkdir(parents=True)
            self.tool('EXTRACT', source, '-d', root / 'out', success=False)

    def test_dry_run_does_not_create_output_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.pvol'
            source.write_bytes(archive('payload.bin', b'payload'))
            self.tool('EXTRACT', source, '-d', root / 'out', '--test')
            self.assertFalse((root / 'out').exists())

    def test_long_names_are_not_silently_truncated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            (source / ('x' * 41)).write_bytes(b'payload')
            packed = root / 'sample.pvol'
            self.tool('CREATE', source, '-d', packed, success=False)
            self.assertFalse(packed.exists())


if __name__ == '__main__':
    unittest.main()
