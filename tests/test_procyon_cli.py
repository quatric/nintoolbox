"""Tests for Procyon Studio SWD (Sound Wave Data) and SMD (Standard MIDI Data) detection."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def make_swd(size=0x40):
    data = bytearray(size)
    data[:4] = b'swdl'
    struct.pack_into('<I', data, 8, size)
    return bytes(data)


def make_smd(size=0x40):
    data = bytearray(size)
    data[:4] = b'smdl'
    struct.pack_into('<I', data, 8, size)
    return bytes(data)


class ProcyonDetectionTests(unittest.TestCase):
    def tool(self, *args):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_swd_detection(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'test.swd'
            path.write_bytes(make_swd(0x60))
            out = self.tool('FILETYPE', path)
            self.assertIn('PROCYON-SWD', out)

    def test_smd_detection(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'test.smd'
            path.write_bytes(make_smd(0x50))
            out = self.tool('FILETYPE', path)
            self.assertIn('PROCYON-SMD', out)

    def test_generic_extension_detection(self):
        with tempfile.TemporaryDirectory() as td:
            swd_bin = Path(td) / 'sound.bin'
            swd_bin.write_bytes(make_swd(0x40))
            out_swd = self.tool('FILETYPE', swd_bin)
            self.assertIn('PROCYON-SWD', out_swd)

            smd_bin = Path(td) / 'music.bin'
            smd_bin.write_bytes(make_smd(0x40))
            out_smd = self.tool('FILETYPE', smd_bin)
            self.assertIn('PROCYON-SMD', out_smd)


if __name__ == '__main__':
    unittest.main()
