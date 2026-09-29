"""ALAR layouts observed in Jump Super Stars and Jump Ultimate Stars retail data."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def alar2(members, named=True):
    data = bytearray(16 + 16 * len(members))
    data[:8] = b'ALAR' + bytes([2, 1]) + struct.pack('<H', len(members))
    for i, (name, payload) in enumerate(members):
        if named:
            data += bytes(2) + name.encode().ljust(32, b'\0') + bytes(2)
        offset = len(data)
        struct.pack_into('<HHIII', data, 16 + 16*i, i, 0x4700, offset, len(payload), 0x80000001)
        data += payload
    return bytes(data)


def alar3(members):
    data = bytearray(18 + 2*len(members))
    data[:8] = b'ALAR' + bytes([3, 5]) + struct.pack('<H', len(members))
    records = []
    for i, (name, payload) in enumerate(members):
        data += bytes(-len(data) % 4)
        records.append(len(data))
        struct.pack_into('<H', data, 18+2*i, len(data))
        data += struct.pack('<HHIIIH', i, 0x4700, 0, len(payload), 0x80000001, 0)
        data += name.encode() + b'\0'
    data += bytes(-len(data) % 4)
    struct.pack_into('<H', data, 16, len(data))
    for rec, (_, payload) in zip(records, members):
        struct.pack_into('<I', data, rec+4, len(data))
        data += payload
    return bytes(data)


def dscp(payload):
    stream = b'\x10' + len(payload).to_bytes(3, 'little')
    for i in range(0, len(payload), 8):
        stream += b'\0' + payload[i:i+8]
    return b'DSCP' + stream


class AlarTests(unittest.TestCase):
    def tool(self, *args):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result

    def check_members(self, builder, wrapped=False):
        members = [('first.bin', b'first payload'), ('folder/second.bin', b'other payload'), ('empty.bin', b'')]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / 'sample.aar'
            blob = builder(members)
            src.write_bytes(dscp(blob) if wrapped else blob)
            out = root / 'out'
            result = self.tool('EXTRACT', src, '-d', out)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name, payload in members:
                self.assertEqual((out / name).read_bytes(), payload)

    def test_type2_all_members_and_names(self):
        self.check_members(alar2)

    def test_type3_names_match_their_own_records(self):
        self.check_members(alar3)

    def test_dscp_wrapped_archives(self):
        for builder in (alar2, alar3):
            with self.subTest(builder=builder.__name__):
                self.check_members(builder, True)

    def test_unnamed_type2_compatibility(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / 'sample.aar'
            src.write_bytes(alar2([('', b'first'), ('', b'next')], False))
            result = self.tool('EXTRACT', src, '-d', root / 'out')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root/'out/file0.bin').read_bytes(), b'first')
            self.assertEqual((root/'out/file1.bin').read_bytes(), b'next')

    def test_invalid_later_member_rejected_before_writing(self):
        for builder in (alar2, alar3):
            with self.subTest(builder=builder.__name__), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                data = bytearray(builder([('first.bin', b'first'), ('next.bin', b'next')]))
                record = 32 if builder is alar2 else struct.unpack_from('<H', data, 20)[0]
                struct.pack_into('<I', data, record+8, 0xffffffff)
                src = root/'sample.aar'; src.write_bytes(data)
                result = self.tool('EXTRACT', src, '-d', root/'out')
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((root/'out').exists())

    def test_testmode_does_not_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root/'sample.aar'; src.write_bytes(alar3([('a.bin', b'payload')]))
            result = self.tool('EXTRACT', src, '-d', root/'out', '--test')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((root/'out').exists())

    def test_unsafe_names_rejected_before_writing(self):
        for builder in (alar2, alar3):
            with self.subTest(builder=builder.__name__), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                src = root/'sample.aar'; src.write_bytes(builder([('../escape.bin', b'payload')]))
                self.assertNotEqual(self.tool('EXTRACT', src, '-d', root/'out').returncode, 0)
                self.assertFalse((root/'escape.bin').exists())
                self.assertFalse((root/'out').exists())

    def test_explicit_zero_recursion_preserves_nested_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            nested=alar2([('leaf.bin',b'leaf payload')])
            src=root/'sample.aar';src.write_bytes(alar3([('nested.aar',nested)]))
            result=self.tool('EXTRACT',src,'-d',root/'out','--recurse=0')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual((root/'out/nested.aar').read_bytes(),nested)
            self.assertFalse((root/'out/nested.aar.d').exists())

    def test_write_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            src=root/'sample.aar'; src.write_bytes(alar2([('blocked.bin', b'payload')]))
            (root/'out/sample/blocked.bin').mkdir(parents=True)
            self.assertNotEqual(self.tool('EXTRACT', src, '-d', root/'out').returncode, 0)

    def test_dscp_and_cx00_decompression(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for magic in (b'DSCP', b'CX00'):
                src=root/'wrapped.bin'; src.write_bytes(magic+dscp(b'opaque payload')[4:])
                out=root/'decoded.bin'
                result=self.tool('DECOMPRESS', src, '-d', out, '--overwrite')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(out.read_bytes(), b'opaque payload')


if __name__ == '__main__':
    unittest.main()
