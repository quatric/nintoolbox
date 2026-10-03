"""NARC table integrity and empty-member extraction."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def narc(payloads, order='<'):
    entries = bytearray(); data = bytearray()
    for payload in payloads:
        entries += struct.pack(order+'II',len(data),len(data)+len(payload))
        data += payload
    fat = struct.pack(order+'4sIHH',b'BTAF',12+len(entries),len(payloads),0)+entries
    image = struct.pack(order+'4sI',b'GMIF',8+len(data))+data
    return bytearray(struct.pack(order+'4sHHIHH',b'NARC',0xfffe,0x100,16+len(fat)+len(image),16,2)+fat+image)


class NarcValidationTests(unittest.TestCase):
    def extract(self,data):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        root=Path(tmp.name);source=root/'sample.narc';dest=root/'out'
        source.write_bytes(data)
        result=subprocess.run([str(BIN),'EXTRACT',str(source),'-d',str(dest),
                               '--recurse=0','--no-passthrough'],capture_output=True,text=True,timeout=30)
        self.assertGreaterEqual(result.returncode,0,result.stderr)
        return result,dest

    def test_empty_members_in_both_byte_orders(self):
        for order in ('<','>'):
            result,dest=self.extract(narc([b'',b'payload',b''],order))
            self.assertEqual(result.returncode,0,result.stderr)
            for i,payload in enumerate((b'',b'payload',b'')):
                self.assertEqual((dest/f'file_{i:04d}.bin').read_bytes(),payload)

    def test_bad_second_member_writes_nothing(self):
        for order in ('<','>'):
            for start,end in ((7,6),(0,999),(0xffffffff,0xffffffff)):
                data=narc([b'first',b'second'],order)
                struct.pack_into(order+'II',data,36,start,end)
                result,dest=self.extract(data)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertFalse(dest.exists())

    def test_short_count_and_chunk_lengths(self):
        for offset,value in ((20,8),(20,0xffffffff),(24,2),(40,0xffffffff)):
            data=narc([b'hello'])
            struct.pack_into('<I',data,offset,value)
            result,dest=self.extract(data)
            self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertFalse(dest.exists())


if __name__ == '__main__':
    unittest.main()
