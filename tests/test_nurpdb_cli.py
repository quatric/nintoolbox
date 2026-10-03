"""NURPDB conversion must report buffered close failures."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes

BIN = Path(__file__).resolve().parents[1]/'project/bin/wszst'


class NURPDBTests(unittest.TestCase):
    def test_valid_empty_database_and_close_failure(self):
        modes = (False, True) if resource is not None and hasattr(signal, 'SIGXFSZ') else (False,)
        for limited in modes:
            with self.subTest(limited=limited), tempfile.TemporaryDirectory() as td:
                source = Path(td)/'sample.nurpdb'
                data = bytearray(0xb0)
                data[:4] = b'HBSS'
                data[16:20] = b'DPRN'
                struct.pack_into('<HH', data, 20, 1, 0)
                source.write_bytes(data)
                result = subprocess.run([str(BIN), 'EXTRACT', str(source), '--no-passthrough',
                                         '--recurse=0'], capture_output=True, text=True, timeout=30,
                                        preexec_fn=reject_file_writes if limited else None)
                output = Path(str(source)+'.txt')
                if limited:
                    self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
                    self.assertFalse(output.exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('[frame_buffers]', output.read_text())
                    self.assertIn('version = 1.0', output.read_text())
                self.assertEqual(source.read_bytes(), data)
