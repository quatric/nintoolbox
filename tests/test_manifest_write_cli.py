"""Text conversion must report buffered output failures."""
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from test_decompression_write_cli import resource, reject_file_writes

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT/'project/bin/wszst'


class ManifestWriteTests(unittest.TestCase):
    def test_normal_conversion_and_failed_closes(self):
        for kind in ('xmb', 'numatb'):
            modes = (False, True) if resource is not None and hasattr(signal, 'SIGXFSZ') else (False,)
            for limited in modes:
                with self.subTest(kind=kind, limited=limited), tempfile.TemporaryDirectory() as td:
                    source = Path(td)/('sample.'+kind)
                    subprocess.run([sys.executable, str(ROOT/'tests'/('mk_'+kind+'.py')), str(source)],
                                   check=True, capture_output=True, timeout=30)
                    original = source.read_bytes()
                    result = subprocess.run([str(BIN), 'EXTRACT', str(source), '--no-passthrough',
                                             '--recurse=0'], capture_output=True, text=True, timeout=30,
                                            preexec_fn=reject_file_writes if limited else None)
                    outputs = [p for p in Path(td).iterdir() if p != source]
                    if limited:
                        self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
                        self.assertEqual(result.stderr.count('Error while closing file:'), 1, result.stderr)
                        self.assertFalse(outputs)
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertTrue(outputs)
                        self.assertTrue(all(p.stat().st_size > 0 for p in outputs))
                    self.assertEqual(source.read_bytes(), original)

    @unittest.skipUnless(resource is not None and hasattr(signal, 'SIGXFSZ'), 'requires POSIX file limits')
    def test_second_xml_output_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as td:
            source = Path(td)/'sample.numatb'
            subprocess.run([sys.executable, str(ROOT/'tests/mk_numatb.py'), str(source)],
                           check=True, capture_output=True, timeout=30)
            command = [str(BIN), 'EXTRACT', str(source), '--no-passthrough', '--recurse=0']
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            text, xml = Path(str(source)+'.txt'), Path(str(source)+'.xml')
            expected = text.read_bytes()
            self.assertGreater(xml.stat().st_size, len(expected))
            text.unlink()
            xml.unlink()

            def limit_to_text_size():
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE, (len(expected), len(expected)))

            result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                    preexec_fn=limit_to_text_size)
            self.assertGreater(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(text.read_bytes(), expected)
            self.assertFalse(xml.exists())
