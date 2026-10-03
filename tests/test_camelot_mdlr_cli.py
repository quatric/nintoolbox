"""Tests for Camelot Software Planning MDLR (Model/Map Object Definition) detection."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def make_mdlr(name="test_object"):
    name_bytes = name.encode('ascii')
    namelen = len(name_bytes)
    data = bytearray()
    data += b'MDLR'
    data += b'\x00'
    data += struct.pack('<I', namelen)
    data += name_bytes
    data += b'\x00'
    data += b'REND\x00'
    return bytes(data)


class CamelotMdlrDetectionTests(unittest.TestCase):
    def tool(self, *args):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_mdlr_detection(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'test.mdlr'
            path.write_bytes(make_mdlr('ctrlrod_ylw_a'))
            out = self.tool('FILETYPE', path)
            self.assertIn('CAMELOT-MDLR', out)

    def test_generic_extension_detection(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'sample.bin'
            path.write_bytes(make_mdlr('ayty_test09'))
            out = self.tool('FILETYPE', path)
            self.assertIn('CAMELOT-MDLR', out)


if __name__ == '__main__':
    unittest.main()
