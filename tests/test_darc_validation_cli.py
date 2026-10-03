"""Lossless DARC creation and archive-wide validation before extraction."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest

from test_decompression_write_cli import resource, reject_file_writes

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'project/bin/wszst'


def darc(rows):
    """Rows are (name, directory parent or None, subtree end or file bytes)."""
    rows = [('', 0, len(rows) + 1)] + rows
    names = bytearray()
    name_offsets = []
    for name, _, _ in rows:
        name_offsets.append(len(names))
        names += name.encode('utf-16le') + b'\0\0'
    table_size = 12 * len(rows) + len(names)
    data_offset = 28 + table_size
    payload = bytearray()
    table = bytearray()
    for (name, parent, value), offset in zip(rows, name_offsets):
        if parent is not None:
            table += struct.pack('<III', 0x1000000 | offset, parent, value)
        else:
            table += struct.pack('<III', offset, data_offset + len(payload), len(value))
            payload += value
    header = struct.pack('<4sHH5I', b'darc', 0xfeff, 28, 0x1000000,
                         data_offset + len(payload), 28, table_size, data_offset)
    return bytearray(header + table + names + payload)


class DarcValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def tool(self, *args, limited=False):
        result = subprocess.run([str(BIN), *map(str, args)], capture_output=True,
                                text=True, timeout=60,
                                preexec_fn=reject_file_writes if limited else None)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result

    def extract(self, data, *, success=True):
        source = self.root / 'sample.darc'
        source.write_bytes(data)
        dest = self.root / 'out'
        result = self.tool('EXTRACT', source, '-d', dest, '--no-passthrough', '--recurse=0')
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(dest.exists(), result.stdout + result.stderr)
        return dest

    def roundtrip(self, source, dest):
        archive = self.root / 'created.darc'
        result = self.tool('CREATE', source, '-d', archive)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.tool('EXTRACT', archive, '-d', dest, '--no-passthrough', '--recurse=0')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unicode_and_empty_members_roundtrip(self):
        names = {'café/雪😀.bin': b'hello', 'café/empty.bin': b'',
                 'plain.bin': b'plain', '日本語/été.bin': b'unicode'}
        source = self.root / 'source'
        for name, payload in names.items():
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        dest = self.root / 'out'
        self.roundtrip(source, dest)
        self.assertEqual({p.relative_to(dest).as_posix(): p.read_bytes()
                          for p in dest.rglob('*') if p.is_file()}, names)

    def test_large_tree_preserves_all_members(self):
        source = self.root / 'source'
        for i in range(600):
            path = source / f'dir_{i:04d}' / 'file.bin'
            path.parent.mkdir(parents=True)
            path.write_bytes(struct.pack('<I', i))
        dest = self.root / 'out'
        self.roundtrip(source, dest)
        files = [p for p in dest.rglob('*') if p.is_file()]
        self.assertEqual(len(files), 600)
        for path in files:
            i = int(path.parent.name[4:])
            self.assertEqual(path.read_bytes(), struct.pack('<I', i))

    def test_deep_tree_preserves_paths(self):
        depth = 100
        rows = [('d', i - 1, depth + 2) for i in range(1, depth + 1)]
        rows.append(('file.bin', None, b'deep'))
        dest = self.extract(darc(rows))
        member = dest.joinpath(*(['d'] * depth), 'file.bin')
        self.assertEqual(member.read_bytes(), b'deep')
        self.roundtrip(dest, self.root / 'rebuilt')
        self.assertEqual((self.root / 'rebuilt').joinpath(*(['d'] * depth), 'file.bin').read_bytes(), b'deep')

    def test_bad_late_name_writes_nothing(self):
        for name in ('../bad.bin', 'bad/name.bin', '', '.', 'a:bad', 'bad\\name'):
            with self.subTest(name=name):
                self.extract(darc([('first.bin', None, b'first'),
                                   (name, None, b'second')]), success=False)

    def test_bad_utf16_and_name_offsets(self):
        original = darc([('first.bin', None, b'first'), ('second.bin', None, b'second')])
        table_size = struct.unpack_from('<I', original, 20)[0]
        name_start = 28 + 36
        second_name = struct.unpack_from('<I', original, 52)[0]
        cases = []
        for offset in (1, 0xffffff, table_size - 36):
            data = original.copy()
            struct.pack_into('<I', data, 52, offset)
            cases.append(data)
        for unit in (0xd800, 0xdc00):
            data = original.copy()
            struct.pack_into('<H', data, name_start + second_name, unit)
            cases.append(data)
        data = original.copy()
        struct.pack_into('<H', data, 28 + table_size - 2, ord('x'))
        cases.append(data)
        for data in cases:
            self.extract(data, success=False)

    def test_corrupt_directory_hierarchy(self):
        original = darc([('first.bin', None, b'first'), ('dir', 0, 4),
                         ('last.bin', None, b'last')])
        for offset, value in ((56, 1), (60, 2), (60, 99), (52, 0x2000000)):
            with self.subTest(offset=offset, value=value):
                data = original.copy()
                struct.pack_into('<I', data, offset, value)
                self.extract(data, success=False)
        # A child subtree cannot extend past its parent or claim another parent.
        original = darc([('a', 0, 3), ('b', 1, 3), ('last.bin', None, b'last')])
        for offset, value in ((56, 0), (60, 4)):
            data = original.copy()
            struct.pack_into('<I', data, offset, value)
            self.extract(data, success=False)

    def test_header_and_payload_bounds(self):
        original = darc([('first.bin', None, b'first'), ('second.bin', None, b'second')])
        for offset, value in ((12, 28), (16, 0xffffffff), (20, 0xffffffff),
                              (24, 0), (56, 0), (60, 0xffffffff)):
            data = original.copy()
            struct.pack_into('<I', data, offset, value)
            self.extract(data, success=False)

    def test_retail_archives_extract_and_rebuild(self):
        fixtures = {'darc/retail_retro.arc': {'blyt/P_Retro.bclyt', 'timg/P_Fnd_Retro.bclim'},
                    'bcma/retail_bcmainfo.arc': {'blyt/BcmaInfo.bclyt'}}
        for index, (name, expected) in enumerate(fixtures.items()):
            with self.subTest(fixture=name):
                fixture = ROOT / 'tests/fixtures/3ds_samples' / name
                self.assertTrue(fixture.exists())
                dest = self.root / f'retail_{index}'
                result = self.tool('EXTRACT', fixture, '-d', dest, '--no-passthrough', '--recurse=0')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                files = {p.relative_to(dest).as_posix(): p.read_bytes()
                         for p in dest.rglob('*') if p.is_file()}
                self.assertEqual(set(files), expected)
                archive = self.root / 'created.darc'
                if archive.exists():
                    archive.unlink()
                rebuilt = self.root / f'rebuilt_{index}'
                self.roundtrip(dest, rebuilt)
                self.assertEqual({p.relative_to(rebuilt).as_posix(): p.read_bytes()
                                  for p in rebuilt.rglob('*') if p.is_file()}, files)

    def test_failed_nested_rebuild_preserves_editable_sources(self):
        for depth in (1, 2):
            with self.subTest(depth=depth):
                source = self.root / f'source_{depth}'
                parent = source if depth == 1 else source / 'outer.darc.d'
                nested = parent / 'broken.darc.d'
                nested.mkdir(parents=True)
                member = nested / 'bad:name.bin'
                member.write_bytes(b'edited payload')
                archive = self.root / 'created.darc'
                result = self.tool('CREATE', source, '-d', archive)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(member.read_bytes(), b'edited payload')
                self.assertFalse(archive.exists())
                self.assertFalse(list(source.rglob('*.darc')))

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_create_reports_buffered_close_failure(self):
        source = self.root / 'source'
        source.mkdir()
        (source / 'first.bin').write_bytes(b'hello')
        archive = self.root / 'created.darc'
        result = self.tool('CREATE', source, '-d', archive, limited=True)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Error while closing file:', result.stderr)
        self.assertFalse(archive.exists())


if __name__ == '__main__':
    unittest.main()
