"""CREATE round trips for flat archives handled by lib-create-extra (AFS, 3DS Camera PACK)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def run(*args):
    r = subprocess.run([str(BIN), *args], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    return r


class CreateExtraTests(unittest.TestCase):
    def roundtrip(self, ext, members, expected):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / ('in.' + ext + '.d')
            src.mkdir()
            for name, data in members.items():
                (src / name).write_bytes(data)
            run('CREATE', str(src), '-d', str(root / ('new.' + ext)))
            run('EXTRACT', str(root / ('new.' + ext)), '-d', str(root / 'out'))
            for name, data in expected.items():
                self.assertEqual((root / 'out' / name).read_bytes(), data, name)
            self.assertEqual(len(list((root / 'out').iterdir())), len(expected))

    def test_afs(self):
        big = bytes(range(256)) * 20
        self.roundtrip('afs', {'0000_one.bin': b'hello', '0001.bin': big},
                       {'0000_one.bin': b'hello', '0001.bin': big})

    def test_ctr_pack(self):
        self.roundtrip('pack', {'a.bin': b'abc', 'b.cam': b'x' * 100},
                       {'a.bin': b'abc', 'b.cam': b'x' * 100})


if __name__ == '__main__':
    unittest.main()
