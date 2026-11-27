"""PNG re-injection into raw GX texture wrappers (GCT0, WIT, CTX, GAFTX, S3T): extract an archive,
edit the decoded PNG, repack, extract again and compare the decoded pixels."""
import os, shutil, struct, subprocess, sys, tempfile, time, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'wszst'
W = H = 16


def write_png(path, w, h, rgba):
    import zlib
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def ch(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def image():
    px = bytearray()
    for y in range(H):
        for x in range(W):
            px += bytes([x * 16, y * 16, 128, 255])
    return px


def wrappers():
    # RGBA8 level 0 (16x16x4 = 1024 bytes) plus an 8x8 mip for the formats that record levels
    base = bytes(W * H * 4)
    mip = bytes(8 * 8 * 4)
    gct = bytearray(0x40); gct[0:4] = b'GCT0'; struct.pack_into('>IHH', gct, 4, 6, W, H)
    wit = struct.pack('<IIII', W, H, 6, 2) + base + mip
    ctx = struct.pack('>IHHHHI', 0, W, H, 6, 2, 0x34) + bytes(0x40 - 16) + base + mip
    s3t = struct.pack('>IIIIIIII', 6, W, H, 0, 0, 0, 0x201, 0) + base
    gaf = bytearray(0x50)
    struct.pack_into('>HHHHII', gaf, 0, W, H, W, H, 0x10001, 8)
    struct.pack_into('>I', gaf, 0x24, 0x50)
    return {'d.gaftx': bytes(gaf) + base, 't.bin': bytes(gct) + base, 'a.wit': wit, 'b.CTX': ctx, 'c.s3t': s3t}


class GxWrap(unittest.TestCase):
    def run_w(self, *args, cwd):
        return subprocess.run([str(WSZST), *args], cwd=cwd, capture_output=True, text=True)

    def test_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d')
            src.mkdir()
            for name, data in wrappers().items():
                (src / name).write_bytes(data)
            self.assertEqual(self.run_w('create', 'src.d', '--u8', '--dest', 'in.arc', '-o', cwd=tmp).returncode, 0)
            self.run_w('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o', cwd=tmp)
            d = Path(tmp, 'in.d')
            names = {'t.bin': 't.bin.png', 'a.wit': 'a.wit.png', 'b.CTX': 'b.CTX.png', 'c.s3t': 'c.s3t.tpl.png', 'd.gaftx': 'd.png'}
            time.sleep(1.1)
            for png in names.values():
                self.assertTrue((d / png).exists(), png)
                write_png(d / png, W, H, image())
            r = self.run_w('create', 'in.d', '--u8', '--dest', 'out.arc', '-o', cwd=tmp)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.run_w('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o', cwd=tmp)
            for name, png in names.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', png))
                pix = bytes(c for y in range(h) for x in range(w) for c in rgba(x, y))
                self.assertEqual((w, h), (W, H), name)
                self.assertEqual(bytes(pix), bytes(image()), name)
            # the mip behind the base level is rewritten, not left zero
            wit = Path(tmp, 'out.d', 'a.wit').read_bytes()
            self.assertEqual(len(wit), 16 + W * H * 4 + 8 * 8 * 4)
            self.assertNotEqual(wit[16 + W * H * 4:], bytes(8 * 8 * 4))


if __name__ == '__main__':
    unittest.main()
