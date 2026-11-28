"""CREATE round trips for flat archives handled by lib-create-extra (AFS, 3DS Camera PACK)."""
from pathlib import Path
import struct
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

    def test_gbin_yaml_round_trip(self):
        import struct

        def name(text):
            return text.encode().ljust(16, b'\0')
        data = b'GBIN' + struct.pack('<I', 2)
        data += b'GUID' + struct.pack('<I', 2) + struct.pack('<6I', 1, 0, 3, 4, 5, 6) + name('Camera01')
        data += name('left') + struct.pack('<I', 7) + name('right') + struct.pack('<I', 9)
        data += b'GUID' + struct.pack('<I', 0) + struct.pack('<6I', 0, 0, 0, 0, 0, 1) + name('Empty')
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / 't.gbin'
            f.write_bytes(data)
            run('XX', str(f))
            f.unlink()
            run('CREATE', str(f) + '.yaml')
            self.assertEqual(f.read_bytes(), data)

    def test_rstb_and_restbl_text_round_trip(self):
        import struct

        def name(text, width):
            return text.encode().ljust(width, b'\0')
        restbl = b'RESTBL' + struct.pack('<IIII', 1, 32, 3, 2)
        restbl += struct.pack('<2I', 0x10203040, 111) + struct.pack('<2I', 0x20304050, 222)
        restbl += struct.pack('<2I', 0xdeadbeef, 333)
        restbl += name('Actor/Pack/Link.pack', 32) + struct.pack('<I', 4444) + name('x' * 32, 32) + struct.pack('<I', 5)
        rstb = b'RSTB' + struct.pack('<II', 2, 1) + struct.pack('<2I', 0xabcdef01, 10)
        rstb += struct.pack('<2I', 0xfedcba98, 20) + name('Some/Long/Name.bin', 128) + struct.pack('<I', 99)
        with tempfile.TemporaryDirectory() as tmp:
            for filename, data in (('t.rsizetable', restbl), ('t.rstb', rstb)):
                f = Path(tmp) / filename
                f.write_bytes(data)
                run('XX', str(f))
                f.unlink()
                run('CREATE', str(f) + '.txt')
                self.assertEqual(f.read_bytes(), data, filename)


if __name__ == '__main__':
    unittest.main()


class Gen5Text(unittest.TestCase):
    @staticmethod
    def build(strings):
        def enc(i, ws):
            key = (0x7c89 + 0x2983 * i) & 0xffff
            out = b''
            for w in ws + [0xffff]:
                out += struct.pack('<H', w ^ key)
                key = ((key << 3) | (key >> 13)) & 0xffff
            return out, len(ws) + 1
        tab, st, off = b'', b'', 4 + 8 * len(strings)
        for i, s in enumerate(strings):
            e, n = enc(i, [ord(c) for c in s])
            tab += struct.pack('<IHH', off, n, 0)
            st += e
            off += len(e)
        body = tab + st
        body += b'\0' * (-(4 + len(body)) % 4)
        blk = struct.pack('<I', 4 + len(body)) + body
        return struct.pack('<HHII', 1, len(strings), len(blk), 0) + struct.pack('<I', 16) + blk

    def test_roundtrip(self):
        raw = self.build(['Hello', 'Caf\u00e9 \\ ok', ''])
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp, 'file_0000.bin')
            f.write_bytes(raw)
            run('xx', str(f), '--decode', '-o')
            t = Path(tmp, 'file_0000.bin.gen5.txt')
            text = t.read_text()
            self.assertIn('0\tHello', text)
            self.assertIn('\\u005c', text)
            t.write_text(text.replace('Hello', 'Howdy!'))
            run('create', str(t), '--dest', str(Path(tmp, 'new.bin')), '-o')
            new = Path(tmp, 'new.bin').read_bytes()
            self.assertEqual(new, self.build(['Howdy!', 'Caf\u00e9 \\ ok', '']))
