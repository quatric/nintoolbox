"""AAMP header bounds and conversion output failures."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes

BIN = Path(__file__).resolve().parents[1]/'project/bin/wszst'


def aamp(version, endian="<"):
    if version == 1:
        return struct.pack(endian+'4s5I', b'AAMP', 1, 0, 44, 1, 4) + b'test' + struct.pack(endian+'4I', 16, 0, 0, 0)
    header = bytearray(48)
    struct.pack_into(endian+'4s5I', header, 0, b'AAMP', 2, 0, 65, 1, 5)
    return bytes(header) + b'test\0' + bytes(12)


class AAMPTests(unittest.TestCase):
    def test_normal_conversion_and_close_failures(self):
        modes = (False, True) if resource is not None and hasattr(signal, 'SIGXFSZ') else (False,)
        for version, endian in ((1, "<"), (1, ">"), (2, "<"), (2, ">")):
            for limited in modes:
                with self.subTest(version=version, endian=endian, limited=limited), tempfile.TemporaryDirectory() as td:
                    source = Path(td)/'sample.aamp'
                    original = aamp(version, endian)
                    source.write_bytes(original)
                    result = subprocess.run([str(BIN), 'EXTRACT', str(source), '--no-passthrough',
                                             '--recurse=0'], capture_output=True, text=True, timeout=30,
                                            preexec_fn=reject_file_writes if limited else None)
                    output = Path(str(source)+'.yml')
                    if limited:
                        self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
                        self.assertFalse(output.exists())
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertIn("test", output.read_text())
                    self.assertEqual(source.read_bytes(), original)
