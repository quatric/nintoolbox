"""NULSTB list bounds and output failure regressions."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes

BIN = Path(__file__).resolve().parents[1]/'project/bin/wszst'


def nulstb():
    data = bytearray(56)
    data[:4] = b'HBSS'
    data[16:20] = b'TSLN'
    struct.pack_into('<HHQQ', data, 20, 1, 0, 16, 2)
    for offset, name in ((40, b'first'), (48, b'second')):
        struct.pack_into('<Q', data, offset, len(data)-offset)
        data += name + b'\0'
    return data


class NULSTBTests(unittest.TestCase):
    def run_tool(self, root, data, limited=False):
        source = root/'sample.nulstb'
        source.write_bytes(data)
        result = subprocess.run([str(BIN), 'EXTRACT', str(source), '--no-passthrough',
                                 '--recurse=0'], capture_output=True, text=True, timeout=30,
                                preexec_fn=reject_file_writes if limited else None)
        return result, Path(str(source)+'.txt')

    def test_valid_names(self):
        with tempfile.TemporaryDirectory() as td:
            result, output = self.run_tool(Path(td), nulstb())
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('[0] first', output.read_text())
            self.assertIn('[1] second', output.read_text())

    def test_empty_list_is_valid(self):
        data = nulstb()
        struct.pack_into('<QQ', data, 24, 0, 0)
        with tempfile.TemporaryDirectory() as td:
            result, output = self.run_tool(Path(td), data)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('<none>', output.read_text())

    def test_invalid_array_offsets_and_counts_fail(self):
        for offset, count in ((0xffffffffffffffe0, 1), (0xffffffffffffffff, 1),
                              (16, 100), (16, 65537), (0, 2)):
            with self.subTest(offset=offset, count=count), tempfile.TemporaryDirectory() as td:
                data = nulstb()
                struct.pack_into('<QQ', data, 24, offset, count)
                result, _ = self.run_tool(Path(td), data)
                self.assertGreater(result.returncode, 0, result.stdout+result.stderr)

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_close_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as td:
            result, output = self.run_tool(Path(td), nulstb(), limited=True)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertFalse(output.exists())
