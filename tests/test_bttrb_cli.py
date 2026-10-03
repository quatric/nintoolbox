"""Blue Tongue TRB extraction must report failed XUR and model outputs."""
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


class BlueTongueTests(unittest.TestCase):
    def fixture(self, root):
        subprocess.run([sys.executable, str(ROOT/'tests/mk_bttrb.py'), str(root)],
                       check=True, capture_output=True, timeout=30)
        return root/'T.trb'

    def extract(self, source, dest, limited=False):
        return subprocess.run([str(BIN), 'EXTRACT', str(source), '-d', str(dest),
                               '--no-passthrough', '--recurse=0'], capture_output=True,
                              text=True, timeout=30,
                              preexec_fn=reject_file_writes if limited else None)

    def test_normal_outputs(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, dest = self.fixture(root), root/'out'
            result = self.extract(source, dest)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((dest/'T.xur').read_bytes(), b'XUIBTEST')
            self.assertTrue(any(p.read_bytes().startswith(b'glTF') for p in (dest/'models').glob('*.glb')))
            self.assertTrue(any(p.read_bytes().startswith(b'\x89PNG') for p in (dest/'textures').glob('*.png')))

    def test_model_directory_failure_is_not_success(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, dest = self.fixture(root), root/'out'
            dest.mkdir()
            package = dest/'T'
            package.mkdir()
            (package/'models').write_bytes(b'keep')
            result = self.extract(source, dest)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual((package/'models').read_bytes(), b'keep')

    def test_model_export_failure_is_not_success(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, dest = self.fixture(root), root/'out'
            blocked = dest/'T'/'models'/'tri.glb'
            blocked.mkdir(parents=True)
            result = self.extract(source, dest)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertTrue(blocked.is_dir())

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_xur_close_failure_is_not_success(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, dest = self.fixture(root), root/'out'
            data = bytearray(source.read_bytes())
            # Hide the ttex symbol so the first output is the raw XUR section.
            struct.pack_into('>I', data, 0x170+12, 0)
            source.write_bytes(data)
            result = self.extract(source, dest, limited=True)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertFalse((dest/'T.xur').exists())
            self.assertFalse((dest/'models').exists())
