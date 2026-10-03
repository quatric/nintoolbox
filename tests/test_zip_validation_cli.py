"""ZIP payload validation, bounded decoding and archive-wide preflight."""
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile
from test_decompression_write_cli import resource, reject_file_writes

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def archive(members, method=zipfile.ZIP_STORED, descriptor=False):
    class Stream(io.BytesIO):
        def seek(self, *args):
            raise OSError('stream is not seekable')
    buffer = Stream() if descriptor else io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=method) as output:
        for name, data in members:
            output.writestr(name, data)
    return buffer.getvalue()


def field(data, local_offset, central_offset, value, fmt='<I'):
    data = bytearray(data)
    central = data.index(b'PK\x01\x02')
    struct.pack_into(fmt,data,local_offset,value)
    struct.pack_into(fmt,data,central+central_offset,value)
    return data


class ZipValidationTests(unittest.TestCase):
    def extract(self, data, flags=(), limit=False):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        root = Path(tmp.name); source = root/'sample.zip'; dest = root/'out'
        source.write_bytes(data)
        result = subprocess.run([str(BIN),'EXTRACT',str(source),'-d',str(dest),
                                 '--recurse=0','--no-passthrough',*flags],capture_output=True,
                                text=True,timeout=30,preexec_fn=reject_file_writes if limit else None)
        self.assertGreaterEqual(result.returncode,0,result.stderr)
        self.assertEqual(source.read_bytes(),data)
        return result,dest

    def reject(self,data,flags=()):
        result,dest=self.extract(data,flags)
        self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertFalse(dest.exists())

    def test_stored_and_deflated_files(self):
        members=[('nested/first.bin',b'ABC'*30000),('empty.bin',b''),('café.txt',b'hello')]
        for method in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED):
            result,dest=self.extract(archive(members,method))
            self.assertEqual(result.returncode,0,result.stderr)
            for name,data in members:self.assertEqual((dest/name).read_bytes(),data)

    def test_data_descriptors(self):
        data=archive([('first.bin',b'first'*100),('empty.bin',b'')],zipfile.ZIP_DEFLATED,True)
        self.assertTrue(struct.unpack_from('<H',data,6)[0]&8)
        result,dest=self.extract(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((dest/'first.bin').read_bytes(),b'first'*100)
        self.assertEqual((dest/'empty.bin').read_bytes(),b'')

    def test_empty_archive(self):
        result,dest=self.extract(archive([]))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(dest.exists())

    def test_directory_and_comment(self):
        data=io.BytesIO()
        with zipfile.ZipFile(data,'w') as z:
            z.comment=b'archive comment'
            z.writestr('folder/',b'')
            z.writestr('folder/test.txt',b'test')
        result,dest=self.extract(data.getvalue())
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((dest/'folder/test.txt').read_bytes(),b'test')

    def test_crc_mismatch(self):
        for method in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED):
            self.reject(field(archive([('file',b'payload')],method),14,16,0))

    def test_corrupt_later_member_writes_nothing(self):
        data=bytearray(archive([('first',b'first'),('second',b'broken')]))
        pos=data.index(b'broken');data[pos]^=1
        self.reject(data)

    def test_unsupported_and_encrypted(self):
        data=archive([('file',b'payload')])
        self.reject(field(data,8,10,99,'<H'))
        self.reject(field(data,6,8,1,'<H'))

    def test_unsafe_and_embedded_nul_names(self):
        for name in ('../escape','/absolute','safe/../../escape'):
            self.reject(archive([('first',b'first'),(name,b'bad')]))
        data=bytearray(archive([('file',b'data')]))
        central=data.index(b'PK\x01\x02');data[30]=0;data[central+46]=0
        self.reject(data)

    def test_local_header_disagrees(self):
        data=bytearray(archive([('file',b'payload')]))
        data[30]=ord('X');self.reject(data)
        data=bytearray(archive([('file',b'payload')]))
        struct.pack_into('<I',data,22,999);self.reject(data)

    def test_declared_output_and_input_lengths(self):
        data=archive([('file',b'payload')],zipfile.ZIP_DEFLATED)
        for size in (0,6,8,0xffffffff):self.reject(field(data,22,24,size))
        compressed=struct.unpack_from('<I',data,18)[0]
        self.reject(field(data,18,20,compressed-1))

    def test_truncated_directory(self):
        data=archive([('file',b'payload')])
        for removed in (1,22,30):self.reject(data[:-removed])

    def test_dry_run_validates_without_writes(self):
        result,dest=self.extract(archive([('file',b'data')]),('--test',))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(dest.exists())
        self.reject(field(archive([('file',b'data')]),14,16,0),('--test',))

    @unittest.skipUnless(resource is not None,'requires POSIX file limits')
    def test_close_failure_stops_extraction(self):
        result,dest=self.extract(archive([('first',b'first'),('second',b'second')]),limit=True)
        self.assertGreater(result.returncode,0,result.stderr)
        self.assertEqual(result.stderr.count('Error while closing file:'),1,result.stderr)
        self.assertFalse(any(p.is_file() for p in dest.rglob('*')))


if __name__ == '__main__':
    unittest.main()
