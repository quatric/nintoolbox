"""PNG re-injection into raw GX texture wrappers (GCT0, WIT, CTX, GAFTX, S3T): extract an archive,
edit the decoded PNG, repack, extract again and compare the decoded pixels."""
import os, shutil, struct, subprocess, sys, tempfile, time, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'wszst'
W = H = 16
LOSSY = ('e.bct', 'f.txw', 'j.iwi', 'k.5a3')
SIZE = {'k.5a3': (192, 64)}  # everything else is W x H


def write_png(path, w, h, rgba):
    import zlib
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def ch(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def image(w=W, h=H):
    px = bytearray()
    for y in range(h):
        for x in range(w):
            px += bytes([x * 16 % 256, y * 16 % 256, 128, 255])
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
    a2 = struct.pack('>IIIIIII', 6, W, H, 2, 0, 0, W * H * 4) + base + mip
    iwi = b'IWi\x0b\x0b\x00' + struct.pack('<HHH', W, H, 1) + bytes(0x20 - 12) + bytes(W * H // 2)
    cmpr = bytes(W * H // 2)
    bct = bytearray(0x30); bct[0] = 6; struct.pack_into('>HH', bct, 4, W, H)
    struct.pack_into('>I', bct, 0x1c, len(cmpr) + 32)
    txw = bytearray(0x40); txw[0:4] = b'\xaf\xaf\x00\x02'; struct.pack_into('>I', txw, 0x0c, 0xbeefface)
    struct.pack_into('>HHH', txw, 0x12, 2, W, H)
    tpl = bytearray(0x60)
    struct.pack_into('>IIIIHHII', tpl, 0, 0x0020af30, 1, 0x0c, 0x14, 0, 0, 0, 0)
    struct.pack_into('>HHII', tpl, 0x14, H, W, 6, 0x60)
    tpl = bytes(tpl) + base
    tgs = bytearray(0x40); tgs[0:4] = b'tgs\0'
    struct.pack_into('>IIII', tgs, 0x10, 0x40 + len(tpl), 0, 0, 0)
    struct.pack_into('>II', tgs, 0x20, 0x40, 0x40 + len(tpl))
    return {'i.a2': a2, 'j.iwi': iwi, 'k.5a3': bytes(192 * 64 * 2), 'e.bct': bytes(bct) + cmpr + bytes(32), 'f.txw': bytes(txw) + bytes(0x40) + cmpr,
            'g.tgs': bytes(tgs) + tpl, 'h.pix': b'FPIX' + bytes(0x1c) + tpl,
            'd.gaftx': bytes(gaf) + base, 't.bin': bytes(gct) + base, 'a.wit': wit, 'b.CTX': ctx, 'c.s3t': s3t}


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
            names = {'t.bin': 't.bin.png', 'a.wit': 'a.wit.png', 'b.CTX': 'b.CTX.png', 'c.s3t': 'c.s3t.tpl.png', 'd.gaftx': 'd.png',
             'e.bct': 'e.bct.tpl.png', 'f.txw': 'f.txw.tpl.png', 'g.tgs': 'g.tgs.tpl.png', 'h.pix': 'h.pix.tpl.png',
             'i.a2': 'i.a2.png', 'j.iwi': 'j.iwi.png', 'k.5a3': 'k.5a3.png'}
            time.sleep(1.1)
            for name, png in names.items():
                self.assertTrue((d / png).exists(), png)
                sw, sh = SIZE.get(name, (W, H))
                write_png(d / png, sw, sh, image(sw, sh))
            r = self.run_w('create', 'in.d', '--u8', '--dest', 'out.arc', '-o', cwd=tmp)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.run_w('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o', cwd=tmp)
            for name, png in names.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', png))
                pix = bytes(c for y in range(h) for x in range(w) for c in rgba(x, y))
                sw, sh = SIZE.get(name, (W, H))
                self.assertEqual((w, h), (sw, sh), name)
                if name in LOSSY:  # CMPR: four colours per 4x4 block
                    err = max(abs(a - b) for a, b in zip(pix, bytes(image(sw, sh))))
                    self.assertLess(err, 40, name)
                else:
                    self.assertEqual(bytes(pix), bytes(image(sw, sh)), name)
            # the mip behind the base level is rewritten, not left zero
            wit = Path(tmp, 'out.d', 'a.wit').read_bytes()
            self.assertEqual(len(wit), 16 + W * H * 4 + 8 * 8 * 4)
            self.assertNotEqual(wit[16 + W * H * 4:], bytes(8 * 8 * 4))


def tga(bpp, rle, top_left, px):  # px: function (x, y) -> (r, g, b, a)
    typ = (11 if bpp == 8 else 10) if rle else (3 if bpp == 8 else 2)
    hdr = bytearray(18)
    hdr[0], hdr[2] = 4, typ  # a 4 byte image id
    struct.pack_into('<HHBB', hdr, 12, W, H, bpp, 0x20 if top_left else 0)
    rows = []
    for y in range(H):
        sy = y if top_left else H - 1 - y
        row = bytearray()
        for x in range(W):
            r, g, b, a = px(x, sy)
            row += bytes([g]) if bpp == 8 else bytes([b, g, r]) + (bytes([a]) if bpp == 32 else b'')
        rows.append(bytes(row))
    body = b''.join(rows)
    if rle:  # one raw packet per row
        n = bpp // 8
        body = b''.join(bytes([W - 1]) + r for r in rows)
    return bytes(hdr) + b'ID01' + body


class TgaWrap(unittest.TestCase):
    def test_roundtrip(self):
        flat = lambda x, y: (200, 100, 50, 255)  # replaced by the edited PNG below
        files = {'a.bkg': tga(24, False, False, flat), 'b.dat': tga(32, True, True, flat),
                 'c.gry': tga(8, False, True, flat)}
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, d in files.items():
                (src / n).write_bytes(d)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            for n in files:
                self.assertTrue(Path(tmp, 'in.d', n + '.png').exists(), n)
                if n == 'c.gry':
                    write_png(Path(tmp, 'in.d', n + '.png'), W, H, b''.join(bytes([x * 16] * 3 + [255]) for y in range(H) for x in range(W)))
                else:
                    write_png(Path(tmp, 'in.d', n + '.png'), W, H, image())
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n in files:
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', n + '.png'))
                want = (lambda x, y: (x * 16,) * 3 + (255,)) if n == 'c.gry' else (lambda x, y: (x * 16, y * 16, 128, 255))
                for y in range(H):
                    for x in range(W):
                        self.assertEqual(tuple(rgba(x, y)), want(x, y), (n, x, y))
            self.assertEqual(Path(tmp, 'out.d', 'b.dat').read_bytes()[18:22], b'ID01')


if __name__ == '__main__':
    unittest.main()
