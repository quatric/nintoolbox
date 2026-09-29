"""Independent PCK2 record fixtures, including compressed and malformed archives."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1]/'project/bin/wszst'


def pck2(members):
    data=bytearray(16)
    for name,payload in members:
        name=name.encode()+b'\0'
        head=(16+len(name)+3)&~3
        size=(head+len(payload)+3)&~3
        record=bytearray(size)
        struct.pack_into('<4I',record,0,head,size,0,len(payload))
        record[16:16+len(name)]=name
        record[head:head+len(payload)]=payload
        data+=record
    struct.pack_into('<II4sI',data,0,16,len(data),b'PCK2',0)
    return data


def lz10(data):
    result=bytearray(b'\x10'+len(data).to_bytes(3,'little'))
    for pos in range(0,len(data),8):
        result+=b'\0'+data[pos:pos+8]
    return result


class PCK2Tests(unittest.TestCase):
    def run_archive(self,data,flags=()):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        root=Path(tmp.name);source=root/'sample.plz';source.write_bytes(data);dest=root/'out'
        result=subprocess.run([str(BIN),'EXTRACT',str(source),'-d',str(dest),'--no-passthrough',
                               '--recurse=0',*flags],capture_output=True,text=True,timeout=30)
        self.assertGreaterEqual(result.returncode,0,result.stderr)
        return result,dest

    def test_named_nested_and_empty_members(self):
        members=[('folder/one.txt',b'one'),('other.bin',b'two'),('empty',b'')]
        for data in (pck2(members),lz10(pck2(members))):
            result,dest=self.run_archive(data)
            self.assertEqual(result.returncode,0,result.stderr)
            for name,payload in members:
                self.assertEqual((dest/name).read_bytes(),payload)

    def test_bad_later_record_leaves_no_partial_tree(self):
        data=pck2([('one',b'one'),('two',b'two')])
        second=16+struct.unpack_from('<I',data,20)[0]
        struct.pack_into('<I',data,second+12,0xffffffff)
        result,dest=self.run_archive(data)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(dest.exists())

    def test_bad_names(self):
        for name in ('../escape','/absolute','a/../../escape'):
            result,dest=self.run_archive(pck2([('first',b'first'),(name,b'bad')]))
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(dest.exists())

    def test_unterminated_name(self):
        data=pck2([('four',b'payload')]);head=struct.unpack_from('<I',data,16)[0]
        data[32:16+head]=b'x'*(head-16)
        result,dest=self.run_archive(data)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(dest.exists())

    def test_zero_record_cannot_loop(self):
        data=pck2([('one',b'one')]);struct.pack_into('<I',data,20,0)
        result,dest=self.run_archive(data)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(dest.exists())

    def test_dry_run(self):
        result,dest=self.run_archive(lz10(pck2([('one',b'one')])),('--test',))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(dest.exists())

    def test_recurse_zero_preserves_nested_archive(self):
        nested=pck2([('inner',b'payload')])
        result,dest=self.run_archive(pck2([('nested.pck2',nested)]))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((dest/'nested.pck2').read_bytes(),nested)
        self.assertEqual(len(list(dest.iterdir())),1)

    def test_empty_archive(self):
        result,dest=self.run_archive(pck2([]))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(dest.exists())


if __name__ == '__main__':
    unittest.main()
