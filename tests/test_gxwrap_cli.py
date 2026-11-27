"""PNG re-injection into raw GX texture wrappers (GCT0, WIT, CTX, GAFTX, S3T): extract an archive,
edit the decoded PNG, repack, extract again and compare the decoded pixels."""
import os, shutil, struct, subprocess, sys, tempfile, time, unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngtool

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'wszst'
W = H = 16
LOSSY = ('pv.pvr', 'bg0', 'bg1', 'at.tex', 'pz_texl', 'pz_atxl', 'vt.dat', 'tg.bin', 'z.dbt0', 'z.dbt1', 'z.dbt2', 'z.Wiitex', 'n.wii', 'e.bct', 'f.txw', 'j.iwi', 'k.5a3')
SIZE = {'k.5a3': (192, 64), 'z.Wiitex': (64, 64)}  # everything else is W x H


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
    dbt = struct.pack('>II', 3, 8) + bytes(0xf8) + struct.pack('>IHHI', 0, W, H, 14) + bytes(0x180 - 0x10c) + bytes(3 * W * H // 2)
    wiitex = struct.pack('>IIII', 2, 0x50 + 64 * 64 // 2 - 0x10, 0x10, 8) + bytes(0x40) + bytes(64 * 64 // 2)
    ptxh = bytearray(0x50); ptxh[8:16] = b'PATX WII'; struct.pack_into('>HH', ptxh, 0x18, W, H); ptxh[0x1c:0x20] = b'ABGR'; struct.pack_into('>I', ptxh, 0x20, 32)
    def btf_chunk(): return b'DXTG' + struct.pack('<I', 0x80 + len(base)) + bytes(0x20) + struct.pack('>II', W, H) + bytes(8) + b'8B8G8R8A' + bytes(0x80 - 0x38) + base
    btf = b'FTBG' + struct.pack('<II', 0, 2) + btf_chunk() + btf_chunk()
    lmp = struct.pack('>HH', 1, 1) + struct.pack('>IIIIII', 0, 0x20, 0, 0x40, 0x80 + len(base), 0) + bytes(0x20 - 28) + b'T.tex\0' + bytes(0x40 - 0x26) + struct.pack('>HHHH', W, H, 0, 0x8080) + bytes(0x78) + base
    tpc = bytes([0, 1, 0, 0]) + struct.pack('>IIHH', 0x40, 0, H, W) + bytes(0x17 - 12) + bytes([6]) + bytes(0x40 - 4 - 0x18) + base
    wiibang = b'WII!' + struct.pack('>II', 2, 1) + bytes(0x14) + struct.pack('>I', 0x40) + bytes(0x34 - 0x18) + b'abcd' + struct.pack('>HHIIII', W, H, 0, 0, W * H * 4, 0x58) + base
    kwtp = b'kwtp02' + bytes(0x12) + struct.pack('>I', 0x180) + bytes(0x180 - 0x1c) + tpl
    pte = struct.pack('>II', 13, 1) + bytes(0x38) + tpl
    rec = b'abc'.ljust(16, b'\0') + struct.pack('>IHHI', 0, 0, 0xffff, 0) + bytes(0x40 - 28) + struct.pack('>HH', W, H) + bytes(4) + bytes([14]) + bytes(88 - 0x49)
    txg = b'TXG ' + bytes(4) + b'HEAD' + struct.pack('>I', 4) + bytes(4) + b'TXHE' + struct.pack('>I', 88) + rec + b'TXDA' + struct.pack('>I', W * H // 2) + cmpr
    ptex = b'PTEX' + struct.pack('>IIIII', 0, 0, 0x44332211, 0, 0) + struct.pack('>I', 1) + struct.pack('>IIHHHHIffI', 0x1234abcd, 2, W, H, W, H, 0x30, 1.0, 1.0, 0) + bytes(0x10) + cmpr + bytes(W * H // 2)
    ttpl = b'TTPL' + struct.pack('>I', len(tpl)) + tpl
    texl = b'TEXL' + bytes(12) + struct.pack('>HH', W, H) + bytes(5) + cmpr + bytes(8)
    texla = b'TEXL' + bytes(12) + struct.pack('>HH', W, H) + bytes(5) + cmpr + bytes(W * H // 2) + bytes(8)
    athtex = struct.pack('>IIIIII', 0x04020000, 13, len(cmpr), W, H, 1) + cmpr
    bgst = b'BGST' + struct.pack('>IIIIIII', 0x10, W, H, 0, 3, 2, 0) + bytes(8) + struct.pack('>II', 0x40, 0x40) + bytes(0x40 - 0x30) + cmpr + cmpr
    pvr = b'XIBG' + struct.pack('>IQ', 8, 0) + b'TRVP' + struct.pack('>IBBBBHH', 0, 0, 0, 0x4e, 0, W, H) + bytes(0x20 - 0x20) + cmpr
    return {'pv.pvr': pvr, 'bg.bgst3': bgst, 'at.tex': athtex, 'pz_ttpl': ttpl, 'pz_texl': texl, 'pz_atxl': texla, 'vt.dat': ptex, 'tg.bin': txg, 'kw.kwtp': kwtp, 'pp.pte': pte, 'k2.WII': wiibang, 't.tpc': tpc, 'l.lmp': lmp, 'b2.btf': btf, 'a2.PTX': base, 'a2.PTXH': bytes(ptxh), 'z.dbt': dbt, 'z.Wiitex': wiitex, 'x.apx': apx, 'w.aig': aig, 'v.gvr': gvr, 'q.hvt': hvt, 'r.etx': etx, 's.txe': txe, 'u.tex': krome, 'm.d4': d4, 'n.wii': petvet, 'o.ngcbm': ngcbm, 'p.MWT': mwt, 'i.a2': a2, 'j.iwi': iwi, 'k.5a3': bytes(192 * 64 * 2), 'e.bct': bytes(bct) + cmpr + bytes(32), 'f.txw': bytes(txw) + bytes(0x40) + cmpr,
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
             'i.a2': 'i.a2.png', 'm.d4': 'm.png', 'q.hvt': 'q.hvt.png', 'v.gvr': 'v.gvr.png', 'w.aig': 'w.aig.d/w.png', 'x.apx': 'x.apx.d/x.png', 'z.dbt0': 'z.dbt.0.png', 'z.dbt1': 'z.dbt.1.png', 'z.dbt2': 'z.dbt.2.png', 'z.Wiitex': 'z.Wiitex.png', 'a2.PTX': 'a2.PTX.d/a2.PTX.tpl.png', 'l.lmp': 'l.lmp.d/T.tex.png', 't.tpc': 't.tpc.d/00.png', 'k2.WII': 'k2.WII.d/00_abcd.png', 'kw.kwtp': 'kw.kwtp.d/kw.tpl.png', 'tg.bin': 'tg.bin.000_abc.png', 'vt.dat': 'vt.dat.d/00_1234abcd.png', 'pz_ttpl': 'pz_ttpl.png', 'at.tex': 'at.png', 'pv.pvr': 'pv.png', 'bg0': 'bg.bgst3.000.png', 'bg1': 'bg.bgst3.001.png', 'pz_texl': 'pz_texl.png', 'pz_atxl': 'pz_atxl.png', 'pp.pte': 'pp.pte.tpl.png', 'btf0': 'b2.btf.00.png', 'btf1': 'b2.btf.01.png', 'r.etx': 'r.etx.d/r.png', 's.txe': 's.bti.png', 'u.tex': 'u.png', 'n.wii': 'n.png', 'o.ngcbm': 'o.ngcbm.png', 'p.MWT': 'p.MWT.png', 'j.iwi': 'j.iwi.png', 'k.5a3': 'k.5a3.png'}
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

    def test_multi_image(self):
        one = tim2(3, 3, 0, bytes(W * H * 4), b'')
        two = one[:6] + struct.pack('<H', 2) + one[8:] + one[0x10:]
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'm.tm2').write_bytes(two)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            edit = {0: lambda x, y: (x * 16, y * 16, 128, 255), 1: lambda x, y: (255 - x * 16, y * 8, 7, 255)}
            for i, f in edit.items():
                self.assertTrue(Path(tmp, 'in.d', 'm.tm2.%02d.png' % i).exists(), i)
                write_png(Path(tmp, 'in.d', 'm.tm2.%02d.png' % i), W, H, b''.join(bytes(f(x, y)) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for i, f in edit.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'm.tm2.%02d.png' % i))
                for y in range(H):
                    for x in range(W):
                        self.assertEqual(tuple(rgba(x, y)), f(x, y), (i, x, y))


class ImyWrap(unittest.TestCase):
    def test_roundtrip(self):
        pal = [(0, 0, 0), (31, 0, 0), (0, 31, 0), (0, 0, 31)]
        pb = b''.join(struct.pack('>H', 0x8000 | c[0] << 10 | c[1] << 5 | c[2]) for c in pal)
        rgb = lambda c: tuple(v * 255 // 31 for v in c) + (255,)
        # 8 bit, 16 x 16 (8 x 4 tiles), compressed with literal-only control bytes: 8 x 0x0f copy 16 two-byte units
        hdr = b'IMY\0' + struct.pack('<IHBBHH', 256, 16, 0x20, 8, 16, 4) + bytes(0x10)
        stream = struct.pack('<H', 8) + bytes([0x0f] * 8) + bytes(256)
        src_files = {'a.imy': hdr + pb + stream}
        want = lambda x, y: rgb(pal[(x // 3 + y) % 4])
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, d in src_files.items():
                (src / n).write_bytes(d)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            self.assertTrue(Path(tmp, 'in.d', 'a.imy.png').exists())
            write_png(Path(tmp, 'in.d', 'a.imy.png'), W, H, b''.join(bytes(want(x, y)) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'a.imy.png'))
            for y in range(H):
                for x in range(W):
                    self.assertEqual(tuple(rgba(x, y)), want(x, y), (x, y))
            self.assertEqual(Path(tmp, 'out.d', 'a.imy').read_bytes()[:0x28], (hdr + pb)[:10] + b'\x21' + (hdr + pb)[11:0x28])  # flags gain bit 0


class LtexWrap(unittest.TestCase):
    def test_roundtrip(self):
        def lz11(d):  # literal-only LZ11 stream
            out = bytearray(b'\x11' + len(d).to_bytes(3, 'little'))
            for i in range(0, len(d), 8):
                out += b'\0' + d[i:i + 8]
            return bytes(out)
        def ltex(fmt, flag, body):
            return b'LTEX' + bytes([0, 0, flag, 0]) + struct.pack('<HHH', fmt, W, H)[0:6] + bytes(0x18 - 0x0e) + body
        z = lz11(bytes(W * H * 2))
        files = {'a.ltex': (17, ltex(17, 0, bytes(W * H * 4))), 'b.ltex': (14, ltex(14, 0x10, struct.pack('<I', len(z)) + z))}
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, (f, d) in files.items():
                (src / n).write_bytes(d)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            f = lambda x, y: (x * 16, y * 16, 128, 255)
            for n in files:
                png = Path(tmp, 'in.d', n + '.d', n[0] + '.png')
                self.assertTrue(png.exists(), n)
                write_png(png, W, H, b''.join(bytes(f(x, y)) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n, (fmt, d) in files.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', n + '.d', n[0] + '.png'))
                for y in range(H):
                    for x in range(W):
                        self.assertTrue(all(abs(a - b) < 9 for a, b in zip(rgba(x, y), f(x, y))), (n, x, y))
            self.assertEqual(Path(tmp, 'out.d', 'b.ltex').read_bytes()[6], 0x10)  # still LZ11 packed


class NtftWrap(unittest.TestCase):
    def test_roundtrip(self):
        # (palette colours, texel bytes): direct, 2 bit, a5i3, 4 bit, a3i5 and 8 bit indexed
        cases = {'d': (0, 2048), 'e': (4, 256), 'f': (8, 1024), 'g': (16, 512), 'h': (32, 1024), 'i': (256, 1024)}
        col = lambda i: (i % 32, i // 32 * 3 + 1, (i * 7 + 9) % 32 if i < 32 else 4)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            for n, (nc, tsz) in cases.items():
                (src / (n + '.ntft')).write_bytes(bytes(tsz))
                if nc:
                    (src / (n + '.ntfp')).write_bytes(b''.join(struct.pack('<H', c[0] | c[1] << 5 | c[2] << 10) for c in map(col, range(nc))))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            want = {}
            for n, (nc, tsz) in cases.items():
                png = Path(tmp, 'in.d', n + '.ntft.d', n + '.png')
                self.assertTrue(png.exists(), n)
                w, h, ch, rgba = pngtool._read_png(png)
                pix = []
                for i in range(w * h):
                    k = (i * 7 + i // w * 3) % max(nc, 1)
                    c = col(k) if nc else ((i * 3) % 32, (i * 5) % 32, (i * 9) % 32)
                    a = 255 if n not in 'fh' else (255 if i % 3 else 0)
                    if n in 'fh':
                        a = (i % 8) * 255 // 7 if n == 'h' else (i % 32) * 255 // 31
                    pix.append((k, tuple(v * 255 // 31 for v in c), a))
                want[n] = pix
                write_png(png, w, h, b''.join(bytes(c) + bytes([a]) for k, c, a in pix))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n, (nc, tsz) in cases.items():
                data = Path(tmp, 'out.d', n + '.ntft').read_bytes()
                pix = want[n]
                self.assertEqual(len(data), tsz)
                for i, (k, c, a) in enumerate(pix):
                    if n == 'd':
                        v = data[2 * i] | data[2 * i + 1] << 8
                        self.assertEqual((v & 31, v >> 5 & 31, v >> 10 & 31), tuple((x * 31 + 127) // 255 for x in c), (n, i))
                    elif n == 'e':
                        self.assertEqual(data[i // 4] >> (2 * (i & 3)) & 3, k, (n, i))
                    elif n == 'g':
                        self.assertEqual(data[i // 2] >> (4 * (i & 1)) & 15, k, (n, i))
                    elif n == 'f':
                        self.assertEqual((data[i] & 7, data[i] >> 3), (k, (a * 31 + 127) // 255), (n, i))
                    elif n == 'h':
                        self.assertEqual((data[i] & 31, data[i] >> 5), (k, (a * 7 + 127) // 255), (n, i))
                    else:
                        self.assertEqual(data[i], k, (n, i))


class Dst0Wrap(unittest.TestCase):
    def test_roundtrip(self):
        def entry(name, bpp, pal):
            ps, xs = 2 * len(pal), W * H * bpp // 8
            hdr = b'DST\0' + b'1.7\0' + struct.pack('<IHHIIIIII', 0x40 + ps + xs, W, H, ps, 0, xs, 0x40, 0x40 + ps, 0x40 + ps) + b''
            hdr = hdr[:0x28 - 4 + 0] if False else hdr
            e = bytearray(0x40); e[0:8] = b'DST\0' + b'1.7\0'
            struct.pack_into('<IHHII', e, 8, 0x40 + ps + xs, W, H, ps, 0)
            struct.pack_into('<III', e, 24, xs, 0x40, 0x40 + ps)
            struct.pack_into('<I', e, 36, 0x40 + ps)
            e[0x28:0x28 + len(name)] = name
            return bytes(e) + b''.join(struct.pack('<H', c) for c in pal) + bytes(xs)
        pal4 = [0x7c1f] + [(i * 3) | (i * 5) << 5 | (i * 2) << 10 for i in range(1, 16)]
        pal8 = [(i % 32) | (i // 32 * 3) << 5 | 4 << 10 for i in range(256)]
        bank = entry(b'one', 4, pal4) + entry(b'two', 8, pal8)
        want = {0: lambda x, y: (None if (x + y) % 7 == 0 else pal4[1 + (x + y * 3) % 15]), 1: lambda x, y: pal8[(x * 9 + y * 11) % 256]}
        rgb = lambda v: ((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31, 255)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'b.dat').write_bytes(bank)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            names = {0: '000_one.png', 1: '001_two.png'}
            for i, n in names.items():
                png = Path(tmp, 'in.d', 'b.dat.d', n)
                self.assertTrue(png.exists(), n)
                write_png(png, W, H, b''.join(bytes((0, 0, 0, 0) if want[i](x, y) is None else rgb(want[i](x, y))) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for i, n in names.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'b.dat.d', n))
                for y in range(H):
                    for x in range(W):
                        v = want[i](x, y)
                        got = tuple(rgba(x, y))
                        self.assertEqual(got[3] if ch == 4 else 0, 0 if v is None else 255, (i, x, y)) if (ch == 4 and v is None) else None
                        if v is not None:
                            self.assertEqual(got[:3], rgb(v)[:3], (i, x, y))


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
