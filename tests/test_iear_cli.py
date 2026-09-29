"""Independent IEAR directory fixtures and extraction behavior."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN=Path(__file__).resolve().parents[1]/'project/bin/wszst'


def iear(members):
    words=(len(members)*2+3)//4*4
    data=bytearray(32+words*4)
    struct.pack_into('<4sI',data,0,b'MAIN',len(members))
    struct.pack_into('<4sI',data,16,b'JTBL',words)
    for i,(tag,payload) in enumerate(members):
        struct.pack_into('<II',data,32+i*8,len(data),16+len(payload))
        data+=struct.pack('<4sI8x',tag,len(payload))+payload
    return data+b'ENDT'+bytes(12)


class IEARTests(unittest.TestCase):
    def extract(self,data,suffix='.iear',flags=()):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        root=Path(tmp.name);src=root/('sample'+suffix);src.write_bytes(data);out=root/'out'
        result=subprocess.run([str(BIN),'EXTRACT',str(src),'-d',str(out),'--no-passthrough',
                               '--recurse=0',*flags],capture_output=True,text=True,timeout=30)
        self.assertGreaterEqual(result.returncode,0,result.stderr)
        return result,out

    def test_typed_payloads_and_empty_member(self):
        result,out=self.extract(iear([(b'NCLR',b'RLCN palette'),(b'NCBR',b'RGCN pixels'),(b'SWD',b'')]))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((out/'file_0000.nclr').read_bytes(),b'RLCN palette')
        self.assertEqual((out/'file_0001.ncbr').read_bytes(),b'RGCN pixels')
        self.assertEqual((out/'file_0002.swd').read_bytes(),b'')
        self.assertEqual(len(list(out.iterdir())),3)

    def test_magic_detection_without_extension(self):
        result,out=self.extract(iear([(b'bin',b'payload')]),suffix='.bin')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((out/'file_0000.bin').read_bytes(),b'payload')

    def test_tag_cannot_escape_destination(self):
        result,out=self.extract(iear([(b'../x',b'payload'),(bytes([255])*4,b'other')]))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((out/'file_0000.bin').read_bytes(),b'payload')
        self.assertEqual((out/'file_0001.bin').read_bytes(),b'other')

    def test_invalid_later_member_writes_nothing(self):
        data=bytearray(iear([(b'bin',b'first'),(b'bin',b'second')]))
        struct.pack_into('<I',data,44,0xffffffff)
        result,out=self.extract(data)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(out.exists())

    def test_invalid_count_size_offset_footer(self):
        variants=[]
        for offset,value in ((4,0xffffffff),(20,0xffffffff),(32,0),(36,15),(52,99)):
            data=bytearray(iear([(b'bin',b'payload')]))
            struct.pack_into('<I',data,offset,value);variants.append(data)
        variants.append(iear([(b'bin',b'payload')])[:-1])
        for data in variants:
            result,out=self.extract(data)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(out.exists())

    def test_empty_archive_and_dry_run(self):
        for data,flags in ((iear([]),()),(iear([(b'bin',b'payload')]),('--test',))):
            result,out=self.extract(data,flags=flags)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertFalse(out.exists())

    def test_duplicate_tags_have_unique_names(self):
        result,out=self.extract(iear([(b'NCER',b'one'),(b'NCER',b'two')]))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((out/'file_0000.ncer').read_bytes(),b'one')
        self.assertEqual((out/'file_0001.ncer').read_bytes(),b'two')

    def test_write_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'sample.iear';src.write_bytes(iear([(b'bin',b'payload')]))
            out=root/'out';(out/'sample/file_0000.bin').mkdir(parents=True)
            result=subprocess.run([str(BIN),'EXTRACT',str(src),'-d',str(out),'--no-passthrough',
                                   '--recurse=0'],capture_output=True,text=True,timeout=30)
            self.assertGreater(result.returncode,0,result.stderr)


if __name__ == '__main__':
    unittest.main()
