"""SARC metadata must stay within its declared sections before extraction."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def sarc(members, order='<', extra_header=b''):
    names, payload, nodes = bytearray(), bytearray(), bytearray()
    for name, content in members:
        attr = 0
        if name is not None:
            attr = 0x1000000 | len(names) // 4
            names += name.encode('utf-8') + b'\0'
            names += b'\0' * (-len(names) % 4)
        nodes += struct.pack(order + '4I', 0, attr, len(payload), len(payload) + len(content))
        payload += content
    filename_table = struct.pack(order + '4sHH', b'SFNT', 8 + len(extra_header), 0) + extra_header + names
    fat = struct.pack(order + '4sHHI', b'SFAT', 12, len(members), 0x65) + nodes
    data_offset = 20 + len(fat) + len(filename_table)
    header = struct.pack(order + '4sHHIIHH', b'SARC', 20, 0xfeff,
                         data_offset + len(payload), data_offset, 0x100, 0)
    return bytearray(header + fat + filename_table + payload)


class SarcValidationTests(unittest.TestCase):
    def extract(self, data, *, success=True):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        source, dest = root / 'sample.sarc', root / 'out'
        source.write_bytes(data)
        result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                 '--no-passthrough', '--recurse=0'], capture_output=True,
                                text=True, timeout=30)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(dest.exists(), result.stdout + result.stderr)
        return dest

    def test_both_byte_orders_and_extended_filename_header(self):
        for order in ('<', '>'):
            for extension in (b'', b'xxxx'):
                with self.subTest(order=order, extension=extension):
                    dest = self.extract(sarc([('雪.bin', b'first'), ('empty.bin', b''),
                                              (None, b'last')], order, extension))
                    self.assertEqual((dest / '雪.bin').read_bytes(), b'first')
                    self.assertEqual((dest / 'empty.bin').read_bytes(), b'')
                    self.assertEqual((dest / 'file_0002.bin').read_bytes(), b'last')

    def test_bad_late_member_writes_nothing(self):
        for order in ('<', '>'):
            for start, end in ((9, 8), (0, 1000), (0xffffffff, 0xffffffff)):
                data = sarc([(None, b'first'), (None, b'second')], order)
                struct.pack_into(order + 'II', data, 56, start, end)
                self.extract(data, success=False)

    def test_members_cannot_read_appended_data(self):
        for order in ('<', '>'):
            data = sarc([(None, b'first'), (None, b'second')], order)
            data += b'appended'
            struct.pack_into(order + 'I', data, 60, 19)
            self.extract(data, success=False)

    def test_names_cannot_read_payload(self):
        for order in ('<', '>'):
            data = sarc([(None, b'first'), (None, b'second\0')], order)
            struct.pack_into(order + 'I', data, 52, 0x1000000)
            self.extract(data, success=False)
            # A non-terminated name must not consume a zero from the payload.
            data = sarc([(None, b'first'), ('name.bin', b'second\0')], order)
            data_offset = struct.unpack_from(order + 'I', data, 12)[0]
            data[72:data_offset] = b'x' * (data_offset - 72)
            self.extract(data, success=False)

    def test_filename_table_cannot_overlap_payload(self):
        for order in ('<', '>'):
            data = sarc([(None, b'first'), (None, b'second')], order)
            for offset, value, kind in ((12, 64, 'I'), (68, 0xffff, 'H'), (8, 20, 'I')):
                bad = data.copy()
                struct.pack_into(order + kind, bad, offset, value)
                self.extract(bad, success=False)

    def test_short_recognized_archive_is_rejected(self):
        self.extract(b'SARC' + b'\0' * 12, success=False)


if __name__ == '__main__':
    unittest.main()
