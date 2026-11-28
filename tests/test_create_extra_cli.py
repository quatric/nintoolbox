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

    def test_jupiter_pck(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / 'in.pck.d'
            src.mkdir()
            members = [b'MODL' + b'1' * 30, b'MOTN' + b'2' * 77]
            for i, m in enumerate(members):
                (src / ('file_%03u.bin' % i)).write_bytes(m)
            run('CREATE', str(src), '-d', str(root / 'new.pck'))
            run('EXTRACT', str(root / 'new.pck'), '-d', str(root / 'out'))
            got = [p.read_bytes() for p in sorted((root / 'out').iterdir())]
            self.assertEqual(got, members)

    def test_aal_yaml_round_trip(self):
        import struct

        def hdr(magic, version):
            return magic + b'\xff\xfe' + struct.pack('<H', version)
        fixtures = {
            'a.baadr': hdr(b'AADR', 1) + struct.pack('<4f', 30.5, 90.0, 0.123456, 0.2),
            'b.baroc': hdr(b'AROC', 1) + struct.pack('<I4fI', 2, 1.5, 100.0, 0.75, -3.25, 1),
            'c.baroc': hdr(b'AROC', 2) + struct.pack('<I4fIf', 1, 1.0, 50.0, 2.0, 0.0, 0, 12.5),
            'd.baacl': hdr(b'AACL', 1) + struct.pack('<2f', 200.0, 0.1),
            'e.baacl': hdr(b'AACL', 2) + struct.pack('<2fB', 200.0, 0.1, 1),
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, data in fixtures.items():
                f = root / name
                f.write_bytes(data)
                run('XX', str(f))
                f.unlink()
                run('CREATE', str(f) + '.yml')
                self.assertEqual(f.read_bytes(), data, name)

    def test_dapg_csv_round_trip(self):
        import random
        import struct
        rng = random.Random(4)
        n = 4
        cells = [0, 1, 0, 2, 3, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3]
        recs = b''
        for k in range(3):
            rec = struct.pack('<2f2H4f', rng.random() * 10, -rng.random(), k, 7, *[rng.random() for _ in range(4)])
            for _ in range(12):
                rec += struct.pack('<fI', rng.random() * 100, rng.randrange(1 << 32))
            recs += rec
        data = struct.pack('<I', n) + struct.pack('<%dH' % (n * n), *cells) + recs
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / 'g.dapg'
            f.write_bytes(data)
            run('XX', str(f))
            f.unlink()
            run('CREATE', str(f) + '.csv')
            self.assertEqual(f.read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
