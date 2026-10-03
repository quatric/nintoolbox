"""Archive extraction must stop and fail when buffered member writes cannot close."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes
from test_iear_cli import iear
from test_pck2_cli import pck2
from test_alar_cli import alar2, alar3
from test_pikmin_cli import pair

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def core_archives():
    payload = b'firstsecond'
    sarc = (struct.pack('<4sHHIIHH',b'SARC',20,0xfeff,72+len(payload),72,0x100,0)
            + struct.pack('<4sHHI',b'SFAT',12,2,0x65)
            + struct.pack('<8I',0,0,0,5,0,0,5,11)
            + struct.pack('<4sHH',b'SFNT',8,0) + payload)
    fat = struct.pack('<4sIHH4I',b'BTAF',28,2,0,0,5,5,11)
    image = struct.pack('<4sI',b'GMIF',8+len(payload))+payload
    narc = struct.pack('<4sHHIHH',b'NARC',0xfffe,0x100,16+len(fat)+len(image),16,2)+fat+image
    names = '\0first.bin\0second.bin\0'.encode('utf-16le')
    data_offset = 28+36+len(names)
    darc = (struct.pack('<4sHH5I',b'darc',0xfeff,28,0x1000000,data_offset+len(payload),28,36+len(names),data_offset)
            + struct.pack('<9I',0x1000000,0,3,2,data_offset,5,22,data_offset+5,6)+names+payload)
    index,arc=pair([('first.bin',b'first'),('second.bin',b'second')])
    return [('.sarc',sarc,None),('.narc',narc,None),('.darc',darc,None),('.arc',arc,index)]


@unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
class ArchiveWriteTests(unittest.TestCase):
    def test_core_archives_normal_and_failed_writes(self):
        for suffix,data,index in core_archives():
            for limited in (False,True):
                with self.subTest(format=suffix,limited=limited), tempfile.TemporaryDirectory() as tmp:
                    root=Path(tmp); source=root/('sample'+suffix);dest=root/'out'
                    source.write_bytes(data)
                    if index is not None:(root/'sample.dir').write_bytes(index)
                    result=subprocess.run([str(BIN),'EXTRACT',str(source),'-d',str(dest),
                                           '--no-passthrough','--recurse=0'],capture_output=True,
                                          text=True,timeout=30,preexec_fn=reject_file_writes if limited else None)
                    files=[p for p in dest.rglob('*') if p.is_file()]
                    if limited:
                        self.assertGreater(result.returncode,0,result.stdout+result.stderr)
                        self.assertEqual(result.stderr.count('Error while closing file:'),1,result.stderr)
                        self.assertFalse(files)
                    else:
                        self.assertEqual(result.returncode,0,result.stderr)
                        self.assertEqual(sorted(p.read_bytes() for p in files),[b'first',b'second'])

    def test_close_failure_stops_member_extraction(self):
        members = [('first.bin', b'first'), ('second.bin', b'second')]
        fixtures = [('.iear', iear([(b'bin', b'first'), (b'bin', b'second')])),
                    ('.plz', pck2(members)), ('.aar', alar2(members)), ('.aar', alar3(members))]
        for suffix, data in fixtures:
            with self.subTest(format=suffix, version=data[4]), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source, dest = root/('sample'+suffix), root/'out'
                source.write_bytes(data)
                result = subprocess.run([str(BIN),'EXTRACT',str(source),'-d',str(dest),
                                         '--no-passthrough','--recurse=0'],
                                        capture_output=True,text=True,timeout=30,
                                        preexec_fn=reject_file_writes)
                self.assertGreater(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual(result.stderr.count('Error while closing file:'),1,result.stderr)
                self.assertFalse(any(p.is_file() for p in dest.rglob('*')))
                self.assertEqual(source.read_bytes(),data)


if __name__ == '__main__':
    unittest.main()
