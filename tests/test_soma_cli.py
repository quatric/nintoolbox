"""CLI tests for Monolith Soft Soma Bringer formats (OBP, BGP, DAD, PCS)."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def make_soma_obp(width=32, height=32, pal_colors=16):
    pal_bytes = pal_colors * 2
    hdr = bytearray(16 + pal_bytes)
    struct.pack_into('<4sHHHHHH', hdr, 0, b'OBP1', 1, 2, width, height, 16, pal_bytes)
    for i in range(pal_colors):
        struct.pack_into('<H', hdr, 16 + i * 2, (i & 0x1f))
    payload = b'\x12' * ((width * height) // 2)
    return bytes(hdr) + payload


def make_soma_bgp(width=256, height=192):
    sz = 24 + 32
    hdr = bytearray(sz)
    struct.pack_into('<4sIHHI II', hdr, 0, b'BGP1', sz, width, height, 0x1234, 24, 32)
    return bytes(hdr)


def make_soma_dad(uncomp_data):
    hdr = bytearray(8)
    struct.pack_into('<4sI', hdr, 0, b'DAD\x01', len(uncomp_data))
    stream = bytearray()
    pos = 0
    while pos < len(uncomp_data):
        chunk = uncomp_data[pos:pos+8]
        pos += len(chunk)
        stream.append((1 << len(chunk)) - 1)
        stream.extend(chunk)
    return bytes(hdr) + bytes(stream)


def make_soma_pcs():
    pcs = bytearray(128)
    struct.pack_into('<4sIII', pcs, 0, b'pcs\0', len(pcs), 1, 0)
    struct.pack_into('<I', pcs, 16, 32)
    pcn = pcs[32:128]
    struct.pack_into('<4s12xI10xH', pcs, 32, b'pcn\0', 96, 1)
    struct.pack_into('<I', pcs, 64, 48)  # 32 + 48 = 80
    struct.pack_into('<4sIII', pcs, 80, b'pos\0', 48, 0, 2)
    struct.pack_into('<iiii', pcs, 96, 0, 40960, 0, 0)
    struct.pack_into('<iiii', pcs, 112, 20480, 81920, 0, 0)
    return bytes(pcs)


class SomaBringTests(unittest.TestCase):
    def test_dad_decompression(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        orig = make_soma_obp()
        dad_data = make_soma_dad(orig)
        src = root / 'sample.dad'
        src.write_bytes(dad_data)
        out = root / 'sample.obp'

        res = subprocess.run([str(BIN), 'DECOMPRESS', str(src), '-d', str(out), '--overwrite'],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(out.read_bytes(), orig)

    def test_pcs_text_dump(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        pcs_data = make_soma_pcs()
        src = root / 'anim.pcs'
        src.write_bytes(pcs_data)
        out = root / 'anim.txt'

        res = subprocess.run([str(BIN), 'TEXT', str(src), '-d', str(out), '--overwrite'],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(res.returncode, 0, res.stderr)
        txt = out.read_text()
        self.assertIn('magic = "pcs\\0"', txt)
        self.assertIn('tag = "pos"', txt)
        self.assertIn('frame = 0.000', txt)

    def test_obp_dump(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        obp_data = make_soma_obp()
        src = root / 'sprite.obp'
        src.write_bytes(obp_data)

        res = subprocess.run([str(BIN), 'DUMP', str(src)],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn('Dump structure of SOMA-OBP', res.stdout)
        self.assertIn('width = 32', res.stdout)

    def test_bgp_text(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        bgp_data = make_soma_bgp()
        src = root / 'bg.bgp'
        src.write_bytes(bgp_data)
        out = root / 'bg.txt'

        res = subprocess.run([str(BIN), 'TEXT', str(src), '-d', str(out), '--overwrite'],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(res.returncode, 0, res.stderr)
        txt = out.read_text()
        self.assertIn('magic = "BGP1"', txt)
        self.assertIn('width = 256', txt)


if __name__ == '__main__':
    unittest.main()
