"""PNG re-injection into raw GX texture wrappers (GCT0, WIT, CTX, GAFTX, S3T): extract an archive,
edit the decoded PNG, repack, extract again and compare the decoded pixels."""
import os, shutil, struct, subprocess, sys, tempfile, time, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'wszst'
W = H = 16
LOSSY = ('n.wii', 'e.bct', 'f.txw', 'j.iwi', 'k.5a3')
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
    d4 = struct.pack('>HHHHI', 6, W, H, 0, 0x20) + bytes(0x14) + base
    petvet = b'\x12\xb9\xb0\xa1\x65\x01' + struct.pack('>HHBB', W, H, 2, 16) + bytes(8) + bytes(W * H * 2)
    ngcbm = b'_NGCBM01' + struct.pack('>HHHBBIIII', W, H, 6, 1, 0, 0, 0x20, 0, 0) + base
    mwt = b'GDATAVERSION' + struct.pack('<IIIII', 2, len(tpl), W, H, 0) + bytes(0x20) + tpl
    hvt = b' IVH' + struct.pack('>I', 2) + b'ARGB' + struct.pack('>III', W, H, 32) + base
    etx = struct.pack('>HHI', W, H, 0x100000) + base + bytes(24)
    txe = struct.pack('>HHH', W, H, 7) + bytes(0x20 - 6) + base
    krome = struct.pack('>IIIIII', 0, W, H, len(base), 0, 0) + bytes(8) + base
    gvr = b'GCIX' + struct.pack('<I', 8) + bytes(8) + b'GVRT' + struct.pack('<I', 8 + len(base)) + b'\0\0\0\x06' + struct.pack('>HH', W, H) + base
    aig = b'ALIG' + struct.pack('<I', 0x0102) + b'GC32' + bytes(4) + struct.pack('<HH', W, H) + bytes(0x0c) + struct.pack('>I', len(base)) + bytes(0x1c) + base
    apx = struct.pack('<IIIHHHH', 0x20 + W * H * 3, W * H * 3, 0, 24, W, H, 1) + bytes(12) + bytes(W * H * 3)
    return {'x.apx': apx, 'w.aig': aig, 'v.gvr': gvr, 'q.hvt': hvt, 'r.etx': etx, 's.txe': txe, 'u.tex': krome, 'm.d4': d4, 'n.wii': petvet, 'o.ngcbm': ngcbm, 'p.MWT': mwt, 'i.a2': a2, 'j.iwi': iwi, 'k.5a3': bytes(192 * 64 * 2), 'e.bct': bytes(bct) + cmpr + bytes(32), 'f.txw': bytes(txw) + bytes(0x40) + cmpr,
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
             'i.a2': 'i.a2.png', 'm.d4': 'm.png', 'q.hvt': 'q.hvt.png', 'v.gvr': 'v.gvr.png', 'w.aig': 'w.aig.d/w.png', 'x.apx': 'x.apx.d/x.png', 'r.etx': 'r.etx.d/r.png', 's.txe': 's.bti.png', 'u.tex': 'u.png', 'n.wii': 'n.png', 'o.ngcbm': 'o.ngcbm.png', 'p.MWT': 'p.MWT.png', 'j.iwi': 'j.iwi.png', 'k.5a3': 'k.5a3.png'}
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


def tim2(itype, ctype, ncol, pixels, clut):
    hdr = bytearray(0x30)
    struct.pack_into('<IIIHHBBBBHH', hdr, 0, 0x30 + len(pixels) + len(clut), len(clut), len(pixels), 0x30, ncol, 0, 1, ctype, itype, W, H)
    return b'TIM2' + bytes([4, 0]) + struct.pack('<H', 1) + bytes(8) + bytes(hdr) + pixels + clut


def swz(i):  # CSM1 block swizzle: swap bits 3 and 4
    return (i & ~0x18) | (i >> 1 & 8) | (i << 1 & 0x10)


class Tim2Wrap(unittest.TestCase):
    def test_roundtrip(self):
        pal = [((i * 7) % 256, 255 - i, (i * 3) % 256, 255) for i in range(256)]
        clut = b''.join(bytes(c[:3]) + b'\x80' for c in pal)  # 32 bit entries, PS2 alpha 0x80 = opaque
        logical = lambda x, y: (x * 7 + y * 13) % 256
        idx8 = bytes(swz(logical(x, y)) for y in range(H) for x in range(W))
        files = {'a.tm2': tim2(3, 3, 0, bytes(W * H * 4), b''),
                 'b.tm2': tim2(2, 0, 0, bytes(W * H * 3), b''),
                 'c.tm2': tim2(5, 3, 256, bytes(W * H), clut)}
        edit = {'a.tm2': lambda x, y: (x * 16, y * 16, 128, 255), 'b.tm2': lambda x, y: (x * 16, y * 16, 128, 255),
                'c.tm2': lambda x, y: pal[logical(x, y)]}
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, d in files.items():
                (src / n).write_bytes(d)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            for n, f in edit.items():
                self.assertTrue(Path(tmp, 'in.d', n + '.png').exists(), n)
                write_png(Path(tmp, 'in.d', n + '.png'), W, H, b''.join(bytes(f(x, y)) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n, f in edit.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', n + '.png'))
                for y in range(H):
                    for x in range(W):
                        self.assertEqual(tuple(rgba(x, y)), f(x, y), (n, x, y))
            out = Path(tmp, 'out.d', 'c.tm2').read_bytes()
            self.assertEqual(out[0x40:0x40 + W * H], idx8)  # swizzled stored indices
            self.assertTrue(out.endswith(clut))  # the palette is untouched


class Dst1Wrap(unittest.TestCase):
    def test_roundtrip(self):
        pal = [(0xff, 0, 0xff), (0xff, 0, 0), (0, 0xff, 0), (0, 0, 0xff)] + [(i * 16, 0, 255 - i * 16) for i in range(4, 16)]
        raw = lambda nc, bpp: b'DST1' + bytes([2, 1, 1, 1]) + bytes(4) + struct.pack('<HHH', W, H, nc) + b''.join(
            struct.pack('<H', 0x8000 | (c[2] >> 3) << 10 | (c[1] >> 3) << 5 | c[0] >> 3) for c in pal[:nc]) + bytes(W * H * bpp // 8)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'a.dst').write_bytes(raw(16, 4))
            (src / 'b.dst').write_bytes(raw(4, 2))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            want = {}
            for n, nc in (('a', 16), ('b', 4)):
                png = Path(tmp, 'in.d', n + '.dst.d', n + '.png')
                want[n] = [pal[1 + (x + y) % (nc - 1)] for y in range(H) for x in range(W)]
                write_png(png, W, H, b''.join(bytes(c) + b'\xff' for c in want[n]))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n in want:
                q = Path(tmp, 'out.d', n + '.dst.d', n + '.png')
                w, h, ch, rgba = pngtool._read_png(q)
                for i, c in enumerate(want[n]):
                    got = tuple(rgba(i % W, i // W))[:3]
                    self.assertTrue(all(abs(a - b) < 9 for a, b in zip(got, c)), (n, i, got, c))


class NbfWrap(unittest.TestCase):
    def test_roundtrip(self):
        NT = 64  # 32 x 2 tiles
        pal = [((i * 8) % 32, (i * 5) % 32, (i * 11) % 32) for i in range(32)]  # RGB555 components, 2 banks of 16
        rgb = lambda c: tuple(v * 255 // 31 for v in c)
        # tile i: flipped when i % 3 == 1, bank = i % 2
        cells = [(i | (i % 3 == 1) << 10 | (i % 3 == 2) << 11 | (i % 2) << 12) for i in range(NT)]
        files = {'a.nbfc': bytes(NT * 32), 'a.nbfp': b''.join(struct.pack('<H', c[0] | c[1] << 5 | c[2] << 10) for c in pal),
                 'a.nbfs': b''.join(struct.pack('<H', c) for c in cells)}
        want = lambda x, y: rgb(pal[(x // 8) % 2 * 16 + (x * 3 + y * 5) % 16])
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, d in files.items():
                (src / n).write_bytes(d)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            self.assertTrue(Path(tmp, 'in.d', 'a.nbfc.d', 'a.png').exists())
            write_png(Path(tmp, 'in.d', 'a.nbfc.d', 'a.png'), 256, 16, b''.join(bytes(want(x, y)) + b'\xff' for y in range(16) for x in range(256)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'a.nbfc.d', 'a.png'))
            self.assertEqual((w, h), (256, 16))
            for y in range(16):
                for x in range(256):
                    self.assertEqual(tuple(rgba(x, y))[:3], want(x, y), (x, y))
            self.assertEqual(Path(tmp, 'out.d', 'a.nbfp').read_bytes(), files['a.nbfp'])  # palette and map are untouched


class TkltWrap(unittest.TestCase):
    def test_tklt(self):
        self.roundtrip(b'TKLT')

    def test_vnkt(self):
        self.roundtrip(b'VNKT')

    def roundtrip(self, magic):
        pal = [((i * 8) % 32, (i * 5 + 3) % 32, (i * 11 + 7) % 32) for i in range(256)]
        ttlp = b'TTLP' + struct.pack('<H', 512) + bytes(2) + b''.join(struct.pack('<H', c[0] | c[1] << 5 | c[2] << 10) for c in pal)
        rel = b'2d/palette/p.pal'
        def lz10(d):  # literal-only Nintendo LZ10 stream
            out = bytearray(b'\x10' + len(d).to_bytes(3, 'little'))
            for i in range(0, len(d), 8):
                out += b'\0' + d[i:i + 8]
            return bytes(out)
        def atk(magic, tiles):
            hdr = magic + struct.pack('<HHB', 16, 16, 0) + bytes(4) + struct.pack('<I', len(rel)) + rel
            return hdr + (tiles if magic == b'TKLT' else struct.pack('<I', len(lz10(tiles))) + lz10(tiles))
        rgb = lambda c: tuple(v * 255 // 31 for v in c)
        want = lambda x, y: (0, 0, 0, 0) if (x + y) % 5 == 0 else rgb(pal[1 + (x * 3 + y * 7) % (15 if y < 8 else 255)]) + (255,)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d', '2d'); (src / 'palette').mkdir(parents=True); (src / 'tilekit').mkdir()
            (src / 'palette' / 'p.pal').write_bytes(ttlp)
            (src / 'tilekit' / 'a.atk').write_bytes(atk(magic, bytes(16 * 32 + 16 * 64)))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            png = Path(tmp, 'in.d', '2d', 'tilekit', 'a.atk.d', 'a.png')
            self.assertTrue(png.exists())
            # tiles 0-15 (4 bit) sit in row 0, tiles 16-31 (8 bit) in row 1
            write_png(png, 128, 16, b''.join(bytes(want(x, y)) for y in range(16) for x in range(128)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', '2d', 'tilekit', 'a.atk.d', 'a.png'))
            self.assertEqual((w, h), (128, 16))
            for y in range(16):
                for x in range(128):
                    got = tuple(rgba(x, y)) + (255,) * (4 - ch)
                    exp = want(x, y)
                    self.assertEqual(got[:3] if exp[3] else got[3], exp[:3] if exp[3] else 0, (x, y))
            self.assertTrue(Path(tmp, 'out.d', '2d', 'palette', 'p.pal').read_bytes() == ttlp)


class IvtWrap(unittest.TestCase):
    def test_roundtrip(self):
        hdr = [bytes([0x2f, 0xcd, 0x1a, 0x36, 0xd8, 0x2f]), bytes([0x2f, 0x33, 0x11, 0x04, 0x02, 0x31]), bytes([0x39, 0x37, 0x0e, 0x35, 0x11, 0x1a])]
        files = {'a.ivt': (4, 0), 'b.ivt': (3, 1), 'c.ivt': (2, 2), 'd.ivt': (1, 0)}
        base = {4: 165, 3: 101, 2: 37, 1: 229}
        # gray / transparent test pixels survive every layout; colour only the 3 and 4 byte ones
        def px(bpp, x, y):
            g = (x * 16 + y) % 256
            return (g, g, g, 255 if bpp != 2 else (y * 16 + 7) % 256) if bpp < 3 else (x * 16 % 256, y * 16 % 256, 128, 255 if bpp == 3 else (x + y * 3) % 256)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, (bpp, var) in files.items():
                (src / n).write_bytes(bytes([(base[bpp] + 4 + 32) & 255, var]) + hdr[var] + bytes(W * H * bpp))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            for n, (bpp, var) in files.items():
                self.assertTrue(Path(tmp, 'in.d', n + '.png').exists(), n)
                write_png(Path(tmp, 'in.d', n + '.png'), W, H, b''.join(bytes(px(bpp, x, y)) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n, (bpp, var) in files.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', n + '.png'))
                for y in range(H):
                    for x in range(W):
                        self.assertEqual(tuple(rgba(x, y)), px(bpp, x, y)[:len(tuple(rgba(x, y)))], (n, x, y))


if __name__ == '__main__':
    unittest.main()
