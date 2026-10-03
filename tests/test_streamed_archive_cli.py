"""COD PAK0 and Radical RCF must preserve bytes and fail on buffered output errors."""
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT/'project/bin/wszst'


class StreamedArchiveTests(unittest.TestCase):
    def fixture(self, root, kind):
        if kind == 'cod':
            source = root/'sound.pak'
            source.write_bytes(struct.pack('<4s4I', b'PAK0', 0, 2, 1, 44)
                               + struct.pack('<6I', 1, 0, 5, 2, 5, 6) + b'firstsecond')
            return source, [b'first', b'second']
        subprocess.run([sys.executable, str(ROOT/'tests/mk_rcf.py'), str(root)],
                       check=True, capture_output=True, timeout=30)
        return root/'T.rcf', [b'\x1bLua'+b'x'*100, b'P3D\xff'+bytes(range(200)), b'first', b'second']

    def test_members_preserve_contents(self):
        for kind in ('cod', 'rcf'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                source, expected = self.fixture(root, kind)
                dest = root/'out'
                result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                         '--no-passthrough', '--recurse=0'], capture_output=True,
                                        text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(sorted(p.read_bytes() for p in dest.rglob('*') if p.is_file()),
                                 sorted(expected))

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_close_failure_stops_member_extraction(self):
        for kind in ('cod', 'rcf'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                source, _ = self.fixture(root, kind)
                original = source.read_bytes()
                dest = root/'out'
                result = subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                                         '--no-passthrough', '--recurse=0'], capture_output=True,
                                        text=True, timeout=30, preexec_fn=reject_file_writes)
                self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
                self.assertEqual(result.stderr.count('Error while closing file:'), 1, result.stderr)
                self.assertFalse(any(p.is_file() for p in dest.rglob('*')))
                self.assertEqual(source.read_bytes(), original)
