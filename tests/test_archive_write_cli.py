"""Archive extraction must stop and fail when buffered member writes cannot close."""
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes
from test_iear_cli import iear
from test_pck2_cli import pck2
from test_alar_cli import alar2, alar3

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


@unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
class ArchiveWriteTests(unittest.TestCase):
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
