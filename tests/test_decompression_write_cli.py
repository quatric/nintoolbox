"""Buffered output failures must reach the command exit status."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
import zlib
try:
    import resource
except ImportError:
    resource = None

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def reject_file_writes():
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))


@unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
class DecompressionWriteTests(unittest.TestCase):
    def test_compression_close_failure_is_not_success(self):
        for suffix in ('lz10', 'lz11', 'rl', 'huff8', 'yay0', 'zlib', 'deflate', 'szs'):
            with self.subTest(codec=suffix), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source, dest = root/'input.bin', root/('output.'+suffix)
                source.write_bytes(b'ABCD' * 16)
                result = subprocess.run([str(BIN),'COMPRESS',str(source),'-d',str(dest)],
                                        capture_output=True,text=True,timeout=30,
                                        preexec_fn=reject_file_writes)
                self.assertGreater(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('closing file',result.stderr)
                self.assertFalse(dest.exists())
                self.assertEqual(source.read_bytes(),b'ABCD' * 16)

    def test_buffered_close_failure_is_not_success(self):
        payload = b'hello'
        streams = {
            'szs': b'Yaz0' + struct.pack('>I',5) + bytes(8) + b'\xf8' + payload,
            'lz10': b'\x10\x05\x00\x00\x00' + payload,
            'zlib': zlib.compress(payload),
            'zst': bytes.fromhex('28b52ffd2005290000') + payload,
            'lz4': bytes.fromhex('04224d1860408205000080') + payload + bytes(4),
            'mz': b'MTXZ' + struct.pack('>III',0,len(payload),0) + zlib.compress(payload),
        }
        for suffix, data in streams.items():
            with self.subTest(codec=suffix), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source, dest = root/('sample.'+suffix), root/'out.bin'
                source.write_bytes(data)
                result = subprocess.run([str(BIN),'DECOMPRESS',str(source),'-d',str(dest)],
                                        capture_output=True,text=True,timeout=30,
                                        preexec_fn=reject_file_writes)
                self.assertGreater(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('closing file',result.stderr)
                self.assertFalse(dest.exists())
                self.assertEqual(source.read_bytes(),data)


if __name__ == '__main__':
    unittest.main()
