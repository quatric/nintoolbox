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
    d3dtx = b'ERTM' + bytes(0x40) + tpl
    txm = struct.pack('>IIII', 0x20000, 0x40, 10, 1) + bytes(4) + struct.pack('>IIIIIII', 0, W, H, 1, 6, 8, 0) + b'tex1\0' + bytes(0x40 - 0x35)
    nlt = struct.pack('>IIIIHHHHH', 1, 3, 0x08080808, 0, 0, W, 0, H, 0)[0:0] + struct.pack('>II', 1, 3) + bytes([8, 8, 8, 8]) + bytes(2) + struct.pack('>HHH', W, H, 0) + bytes(0x20 - 0x14) + base
    nlbank = b'PTLG' + struct.pack('>I', 1) + bytes(8) + struct.pack('>IIII', 0xdeadbeef, 0, len(nlt), 0) + nlt
    idd_body = struct.pack('>III', 0, 1, 0xc) + struct.pack('>II', 0x14, 0) + struct.pack('>HHII', W, H, 6, 0x38) + bytes(0x38 - 0x14 - 12) + base
    idd = b'IDD\0' + struct.pack('>I', 1) + struct.pack('>II', 0, 0x10) + b'TEX\0' + struct.pack('>III', 1, 0, 0x10) + idd_body
    return {'id.idd': idd, 'nl.rlt': nlbank, 'tx.txm': txm, 'tx.txv': base, 'td.d3dtx': d3dtx, 'pv.pvr': pvr, 'bg.bgst3': bgst, 'at.tex': athtex, 'pz_ttpl': ttpl, 'pz_texl': texl, 'pz_atxl': texla, 'vt.dat': ptex, 'tg.bin': txg, 'kw.kwtp': kwtp, 'pp.pte': pte, 'k2.WII': wiibang, 't.tpc': tpc, 'l.lmp': lmp, 'b2.btf': btf, 'a2.PTX': base, 'a2.PTXH': bytes(ptxh), 'z.dbt': dbt, 'z.Wiitex': wiitex, 'x.apx': apx, 'w.aig': aig, 'v.gvr': gvr, 'q.hvt': hvt, 'r.etx': etx, 's.txe': txe, 'u.tex': krome, 'm.d4': d4, 'n.wii': petvet, 'o.ngcbm': ngcbm, 'p.MWT': mwt, 'i.a2': a2, 'j.iwi': iwi, 'k.5a3': bytes(192 * 64 * 2), 'e.bct': bytes(bct) + cmpr + bytes(32), 'f.txw': bytes(txw) + bytes(0x40) + cmpr,
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
             'i.a2': 'i.a2.png', 'm.d4': 'm.png', 'q.hvt': 'q.hvt.png', 'v.gvr': 'v.gvr.png', 'w.aig': 'w.aig.d/w.png', 'x.apx': 'x.apx.d/x.png', 'z.dbt0': 'z.dbt.0.png', 'z.dbt1': 'z.dbt.1.png', 'z.dbt2': 'z.dbt.2.png', 'z.Wiitex': 'z.Wiitex.png', 'a2.PTX': 'a2.PTX.d/a2.PTX.tpl.png', 'l.lmp': 'l.lmp.d/T.tex.png', 't.tpc': 't.tpc.d/00.png', 'k2.WII': 'k2.WII.d/00_abcd.png', 'kw.kwtp': 'kw.kwtp.d/kw.tpl.png', 'tg.bin': 'tg.bin.000_abc.png', 'vt.dat': 'vt.dat.d/00_1234abcd.png', 'pz_ttpl': 'pz_ttpl.png', 'at.tex': 'at.png', 'pv.pvr': 'pv.png', 'td.d3dtx': 'td.d3dtx.tpl.png', 'tx.txm': 'tx.txm.d/tex1.tpl.png', 'nl.rlt': 'nl.rlt.deadbeef.png', 'id.idd': 'id.idd.00.png', 'bg0': 'bg.bgst3.000.png', 'bg1': 'bg.bgst3.001.png', 'pz_texl': 'pz_texl.png', 'pz_atxl': 'pz_atxl.png', 'pp.pte': 'pp.pte.tpl.png', 'btf0': 'b2.btf.00.png', 'btf1': 'b2.btf.01.png', 'r.etx': 'r.etx.d/r.png', 's.txe': 's.bti.png', 'u.tex': 'u.png', 'n.wii': 'n.png', 'o.ngcbm': 'o.ngcbm.png', 'p.MWT': 'p.MWT.png', 'j.iwi': 'j.iwi.png', 'k.5a3': 'k.5a3.png'}
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


class ImagineWrap(unittest.TestCase):
    def test_roundtrip(self):
        w0, h0 = 61, 37  # stored as 64 x 40 tiles
        inner = struct.pack('<III', 4, w0, h0) + b'\xcc' * 0x14 + bytes(64 * 40 * 2)
        out = bytearray()
        for i in range(0, len(inner), 8):
            ch = inner[i:i + 8]
            out += bytes([(1 << len(ch)) - 1]) + ch
        wrapped = b'\x21\x43\x65\x87' + struct.pack('<II', len(out), len(inner)) + out
        f = lambda x, y: (x * 4 % 256, y * 6 % 256, (x + y) * 2 % 256, 255)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'x.wsp').write_bytes(wrapped)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            png = Path(tmp, 'in.d', 'x.wsp.d', 'x.png')
            self.assertTrue(png.exists())
            write_png(png, w0, h0, b''.join(bytes(f(x, y)) for y in range(h0) for x in range(w0)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'x.wsp.d', 'x.png'))
            self.assertEqual((w, h), (w0, h0))
            for y in range(h0):
                for x in range(w0):
                    self.assertTrue(all(abs(a - b) < 9 for a, b in zip(rgba(x, y), f(x, y))), (x, y))
            self.assertEqual(Path(tmp, 'out.d', 'x.wsp').read_bytes()[:4], b'\x21\x43\x65\x87')  # still the wrapper

    def test_msk_tan(self):
        def wrap(inner):
            out = bytearray()
            for i in range(0, len(inner), 8):
                ch = inner[i:i + 8]
                out += bytes([(1 << len(ch)) - 1]) + ch
            return b'\x21\x43\x65\x87' + struct.pack('<II', len(out), len(inner)) + bytes(out)
        pal = [((i * 5) % 32, (i * 3) % 64, (i * 7) % 32) for i in range(256)]
        pal565 = b''.join(struct.pack('>H', c[0] << 11 | c[1] << 5 | c[2]) for c in pal)
        tan = struct.pack('>IIII', 2, 16, 8, 0) + pal565 + bytes(0x10) + bytes(2 * 16 * 8)
        msk = struct.pack('<II', 8, 4) + b'\xcc' * 16 + bytes(32)
        rgb = lambda c: (c[0] * 255 // 31, c[1] * 255 // 63, c[2] * 255 // 31)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'a.tan').write_bytes(wrap(tan))
            (src / 'b.msk').write_bytes(wrap(msk))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            ftan = lambda fr: (lambda x, y: tuple(rgb(pal[(x * 3 + y * 17 + fr * 40) % 256])))
            fmsk = lambda x, y: ((x * 30 + y * 7) % 256,) * 3
            for fr in range(2):
                q = Path(tmp, 'in.d', 'a.tan.d', 'a.tan.d', 'frame_%02d.png' % fr)
                self.assertTrue(q.exists(), fr)
                write_png(q, 16, 8, b''.join(bytes(ftan(fr)(x, y)) + b'\xff' for y in range(8) for x in range(16)))
            q = Path(tmp, 'in.d', 'b.msk.d', 'b.png')
            self.assertTrue(q.exists())
            write_png(q, 8, 4, b''.join(bytes(fmsk(x, y)) + b'\xff' for y in range(4) for x in range(8)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for fr in range(2):
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'a.tan.d', 'a.tan.d', 'frame_%02d.png' % fr))
                for y in range(8):
                    for x in range(16):
                        self.assertEqual(tuple(rgba(x, y))[:3], ftan(fr)(x, y), (fr, x, y))
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', 'b.msk.d', 'b.png'))
            for y in range(4):
                for x in range(8):
                    self.assertEqual(tuple(rgba(x, y))[:3], fmsk(x, y), (x, y))


class SetWrap(unittest.TestCase):
    def test_gts_gvrs(self):
        gvr = b'GCIX' + struct.pack('<I', 8) + bytes(8) + b'GVRT' + struct.pack('<I', 8 + 1024) + b'\0\0\0\x06' + struct.pack('>HH', W, H) + bytes(1024)
        gts = struct.pack('>I', 0) + b'gts1' + struct.pack('>HH', 1, 0) + struct.pack('>I', 0x10) + struct.pack('>I', len(gvr)) + b'one'.ljust(0x3c, b'\0') + gvr
        names = b'p\0q\0'
        o0 = 4 + 8 + len(names)
        gvrs = struct.pack('>HH', 2, 0) + struct.pack('>II', o0, o0 + len(gvr)) + names + gvr + gvr
        want = lambda k: (lambda x, y: ((x * 16 + k * 40) % 256, y * 16, 90 + k, 255))
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'a.gts').write_bytes(gts)
            (src / 'b.GVRs').write_bytes(gvrs)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'in.d', '-o')
            time.sleep(1.1)
            pngs = {'a.gts.d/one.gvr.png': 0, 'b.GVRs.d/p.gvr.png': 1, 'b.GVRs.d/q.gvr.png': 2}
            for n, k in pngs.items():
                self.assertTrue(Path(tmp, 'in.d', n).exists(), n)
                write_png(Path(tmp, 'in.d', n), W, H, b''.join(bytes(want(k)(x, y)) for y in range(H) for x in range(W)))
            self.assertEqual(run('create', 'in.d', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'out.arc', '--decode', '--dest', 'out.d', '-o')
            for n, k in pngs.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'out.d', n))
                for y in range(H):
                    for x in range(W):
                        self.assertEqual(tuple(rgba(x, y)), want(k)(x, y), (n, x, y))
            self.assertEqual(len(Path(tmp, 'out.d', 'a.gts').read_bytes()), len(gts))


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


class OpoonaTpl(unittest.TestCase):
    def test_blanked_magic_tpl(self):
        tpl = struct.pack('>IIIIIHHII', 0, 1, 12, 20, 0, 16, 16, 6, 0x60).ljust(0x60, b'\0') + bytes(1024)
        px = bytes([200, 100, 50, 255]) * 256
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'a.tpl').write_bytes(tpl)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            time.sleep(1.1)
            write_png(Path(tmp, 'ex', 'a.tpl.d', 'a.tpl.png'), 16, 16, px)
            self.assertEqual(run('create', 'ex', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            out = Path(tmp, 'ex', 'a.tpl').read_bytes()
            self.assertEqual(len(out), len(tpl))
            self.assertEqual(out[:4], tpl[:4])
            self.assertEqual(out[0x60:0x64], bytes([0xff, 0xc8, 0xff, 0xc8]))

    def test_tple_bundle(self):
        one = struct.pack('>IIIIIHHII', 0, 1, 12, 20, 0, 16, 16, 6, 0x60).ljust(0x60, b'\0') + bytes(1024)
        hdr = (b'TPLE' + bytes(12) + struct.pack('>III', 3, len(one), 0x40) + b'a.tpl').ljust(0x40, b'\0')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'a.tpl').write_bytes(hdr + one * 3)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            time.sleep(1.1)
            write_png(Path(tmp, 'ex', 'a.tpl.d', 'a_1.tpl.png'), 16, 16, bytes([200, 100, 50, 255]) * 256)
            self.assertEqual(run('create', 'ex', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            out = Path(tmp, 'ex', 'a.tpl').read_bytes()
            self.assertEqual(len(out), len(hdr + one * 3))
            self.assertEqual(out[:4], b'TPLE')
            base = 0x40 + len(one)
            self.assertEqual(out[base + 0x60:base + 0x64], bytes([0xff, 0xc8, 0xff, 0xc8]))
            self.assertEqual(out[0x40 + 0x60:0x40 + 0x64], bytes(4))


class XtcWrap(unittest.TestCase):
    def test_top_mip(self):
        def ent(w, h):
            return struct.pack('<10I', 0x200, 0x5000, 0, w, h, 0x20, 0, w * h // 2, 0, 0x10) + bytes(w * h // 2)
        e0, e1 = ent(16, 16), ent(8, 8)
        xtc = (b'MOS DATAFILE2.0'.ljust(0x30, b'\0') + struct.pack('<II', 0x38, 0x38 + len(e0)) + e0 + e1).ljust(0x200, b'\0')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'compiledtextures.xtc').write_bytes(xtc)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            png = Path(tmp, 'ex', 'compiledtextures.xtc.d', 'tex_0000_16x16.tpl.png')
            self.assertTrue(png.exists())
            time.sleep(1.1)
            write_png(png, 16, 16, bytes([248, 8, 8, 255]) * 256)
            self.assertEqual(run('create', 'ex', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            out = Path(tmp, 'ex', 'compiledtextures.xtc').read_bytes()
            self.assertEqual(len(out), len(xtc))
            self.assertNotEqual(out[0x60:0xe0], bytes(128))
            self.assertEqual(out[0xe0:], xtc[0xe0:])  # smaller mip untouched
            self.assertEqual(out[:0x60], xtc[:0x60])


class Ntft4x4Wrap(unittest.TestCase):
    def test_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'q.ntft').write_bytes(bytes(256))
            (src / 'q.ntfi').write_bytes(bytes(128))
            (src / 'q.ntfp').write_bytes(bytes(16))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            png = Path(tmp, 'ex', 'q.ntft.d', 'q.png')
            self.assertTrue(png.exists())
            w, h, ch, rgba = pngtool._read_png(png)
            pals = [[(31, 0, 0), (0, 31, 0)], [(1, 2, 3), (4, 5, 6), (7, 8, 9), (10, 11, 12)], [(20, 20, 20)], [(5, 25, 15), (30, 2, 9)]]
            pix = []
            for y in range(h):
                for x in range(w):
                    b = (x // 4) + (y // 4) * (w // 4)
                    pl = pals[b % 4]
                    if b % 7 == 3:  # transparent texels with two colours
                        pix.append((0, 0, 0, 0) if (x + y) % 3 == 0 else tuple(v * 255 // 31 for v in pals[0][(x + y) % 2]) + (255,))
                    else:
                        pix.append(tuple(v * 255 // 31 for v in pl[(x * 3 + y) % len(pl)]) + (255,))
            time.sleep(1.1)
            write_png(png, w, h, b''.join(bytes(p) for p in pix))
            self.assertEqual(run('create', 'ex', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            tx, ix, pl = (Path(tmp, 'ex', 'q.' + e).read_bytes() for e in ('ntft', 'ntfi', 'ntfp'))
            col = lambda v: tuple((v >> (5 * k) & 31) * 255 // 31 for k in range(3))
            for i, p in enumerate(pix):
                x, y = i % w, i // w
                b = x // 4 + y // 4 * (w // 4)
                iv = ix[2 * b] | ix[2 * b + 1] << 8
                po, mode = (iv & 0x3fff) * 2, iv >> 14
                c = [pl[2 * (po + k)] | pl[2 * (po + k) + 1] << 8 for k in range(4) if 2 * (po + k) + 1 < len(pl)] + [0] * 4
                c = c[:4]
                r = [(c[0] & 31, c[0] >> 5 & 31, c[0] >> 10 & 31), (c[1] & 31, c[1] >> 5 & 31, c[1] >> 10 & 31)]
                avg = lambda m, n: tuple((r[0][k] * m + r[1][k] * n) // (m + n) for k in range(3))
                cols = [r[0], r[1], tuple(((c[2] >> (5 * k)) & 31) for k in range(3)), tuple(((c[3] >> (5 * k)) & 31) for k in range(3))]
                if mode == 0: cols[3] = None
                elif mode == 1: cols[2], cols[3] = avg(1, 1), None
                elif mode == 3: cols[2], cols[3] = avg(5, 3), avg(3, 5)
                t = tx[4 * b + y % 4] >> (2 * (x % 4)) & 3
                got = cols[t]
                self.assertEqual(got is not None, bool(p[3]), (x, y))
                if got:
                    self.assertEqual(tuple(v * 255 // 31 for v in got), p[:3], (x, y))

class OibbWrap(unittest.TestCase):
    def test_roundtrip(self):
        pc = lambda i: ((i % 32), ((i // 32) * 4 + 1), (i * 7) % 32)
        def obj(fmt, npal, n):
            pal = b''.join(struct.pack('>H', 0x8000 | c[0] << 10 | c[1] << 5 | c[2]) for c in map(pc, range(npal)))
            pix = bytes(n)
            hdr = (b'TXTR' + struct.pack('>II', 0x40 + len(pal) + len(pix), len(pix)) + struct.pack('<I', 0x80) + struct.pack('>6I', 8, 8, 1, fmt, 1, len(pal))).ljust(0x40, b'\0')
            return hdr + pal + pix
        objs = [obj(6, 0, 256), obj(9, 256, 64), obj(8, 16, 32)]
        pak = (b'OIBB' + struct.pack('<I', 4)).ljust(0x40, b'\0') + b''.join(objs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 't.pak').write_bytes(pak)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            d = Path(tmp, 'ex', 't.pak.d')
            names = sorted(x.name for x in d.glob('tex_*.png'))
            self.assertEqual(len(names), 3, names)
            time.sleep(1.1)
            want = []
            for i, (nm, npal) in enumerate(zip(names, (0, 256, 16))):
                px = [tuple(v * 255 // 31 for v in pc((x * 5 + y * 3 + i) % npal)) + (255,) if npal else ((x * 30) % 256, (y * 30) % 256, (x * y * 4) % 256, 255 - x * 9) for y in range(8) for x in range(8)]
                want.append(px)
                write_png(d / nm, 8, 8, b''.join(bytes(p) for p in px))
            self.assertEqual(run('create', 'ex', '--u8', '--dest', 'out.arc', '-o').returncode, 0)
            run('xx', 'ex/t.pak', '--decode', '-o')
            for nm, px in zip(names, want):
                w, h, ch, rgba = pngtool._read_png(d / nm)
                for y in range(8):
                    for x in range(8):
                        self.assertEqual((tuple(rgba(x, y)) + (255,))[:4], px[y * 8 + x], (nm, x, y))


class DblWrap(unittest.TestCase):
    def test_roundtrip(self):
        pals = [(i * 2 % 32, i * 5 % 32, i * 3 % 32) for i in range(16)]
        pal = b''.join(struct.pack('>H', c[0] << 11 | c[1] << 5 | c[2]) for c in pals)
        D0 = 0x18 + 3 * 0x48
        offs = [D0, D0 + 128, D0 + 128 + 64]
        pal_off = D0 + 128 + 64 + 128
        def rec(name, fmt, po, w, h, pw=0, ph=0):
            r = bytearray(0x48)
            r[:len(name)] = name
            r[0x20:0x24] = struct.pack('>I', fmt | 0x8000)
            r[0x2c:0x30] = struct.pack('>I', po)
            r[0x30:0x34] = struct.pack('>HH', w, h)
            r[0x3c:0x44] = struct.pack('>IHH', pal_off, pw, ph)[:8]
            return bytes(r)
        payload = struct.pack('>I', 3).ljust(0x18, b'\0') + rec(b'a.tga', 4, offs[0], 8, 8) + rec(b'b.tga', 9, offs[1], 8, 8, 4, 4) + rec(b'c.tga', 6, offs[2], 8, 4) + bytes(128 + 64 + 128) + pal
        dbl = (b'\0' * 8 + b'GCN\0' + bytes(4) + struct.pack('>I', 0x100)).ljust(0x20, b'\0') + struct.pack('>HHIH', 0x82, 0, len(payload), 1) + payload
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'x.dbl').write_bytes(dbl)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            names = sorted(x.name for x in Path(tmp, 'ex').glob('x.dbl.*.png'))
            self.assertEqual(len(names), 3, names)
            time.sleep(1.1)
            want = []
            a = [((x * 4 % 32) * 255 // 31, (y * 8 % 64) * 255 // 63, (x + y) % 32 * 255 // 31, 255) for y in range(8) for x in range(8)]
            b = [tuple(v * 255 // (63 if k == 1 else 31) for k, v in enumerate(pals[(x + y * 3) % 16])) + (255,) for y in range(8) for x in range(8)]
            c = [((x * 30) % 256, (y * 60) % 256, (x * y * 4) % 256, 255 - x * 9) for y in range(4) for x in range(8)]
            for nm, px, w, h in zip(names, (a, b, c), (8, 8, 8), (8, 8, 4)):
                write_png(Path(tmp, 'ex', nm), w, h, b''.join(bytes(p) for p in px))
                want.append((nm, px, w, h))
            r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            run('xx', 'ex/x.dbl', '--decode', '-o')
            for nm, px, w, h in want:
                w2, h2, ch, rgba = pngtool._read_png(Path(tmp, 'ex', nm))
                for y in range(h):
                    for x in range(w):
                        got = (tuple(rgba(x, y)) + (255,))[:4]
                        exp = px[y * w + x]
                        # RGB565 holds 5 / 6 / 5 bits
                        tol = 8 if nm.endswith('a.png') else 0
                        self.assertTrue(all(abs(g - e) <= tol for g, e in zip(got, exp)), (nm, x, y, got, exp))


class ShpgWrap(unittest.TestCase):
    def test_roundtrip(self):
        pals = [(i * 2 % 32, i * 5 % 32, i * 3 % 32) for i in range(16)]
        pal = b''.join(struct.pack('>H', c[0] << 11 | c[1] << 5 | c[2]) for c in pals)
        def rec(code, w, h, body):
            return (bytes([code]) + (16 + len(body)).to_bytes(3, 'big') + struct.pack('>HH', w, h)).ljust(16, b'\0') + body
        r0, r1, r2 = rec(0x14, 8, 8, bytes(128)), rec(0x19, 8, 8, bytes(64)), rec(0x31, 0, 0, pal)
        base = 16 + 16
        shpg = b'SHPG' + struct.pack('<I', 0) + struct.pack('>I', 2) + b'tags' + b'imgA' + struct.pack('>I', base) + b'imgB' + struct.pack('>I', base + len(r0))
        shpg += r0 + r1 + r2
        def lit(d):
            o = bytearray(b'\x10\xfb' + len(d).to_bytes(3, 'big'))
            q = 0
            while len(d) - q >= 4:
                run = min(112, (len(d) - q) // 4 * 4)
                o.append(0xe0 + run // 4 - 1); o += d[q:q + run]; q += run
            o.append(0xfc + len(d) - q); o += d[q:]
            return bytes(o)
        a = [((x * 4 % 32) * 255 // 31, (y * 8 % 64) * 255 // 63, (x + y) % 32 * 255 // 31, 255) for y in range(8) for x in range(8)]
        b = [tuple(v * 255 // (63 if k == 1 else 31) for k, v in enumerate(pals[(x + y * 3) % 16])) + (255,) for y in range(8) for x in range(8)]
        for packed in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                src = Path(tmp, 'src.d'); src.mkdir()
                (src / 'x.gsh').write_bytes(lit(shpg) if packed else shpg)
                run = lambda *args: subprocess.run([str(WSZST), *args], cwd=tmp, capture_output=True, text=True)
                run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
                run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
                names = sorted(x.name for x in Path(tmp, 'ex').glob('x.gsh.*.png'))
                self.assertEqual(len(names), 2, names)
                time.sleep(1.1)
                for nm, px in zip(names, (a, b)):
                    write_png(Path(tmp, 'ex', nm), 8, 8, b''.join(bytes(p) for p in px))
                r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertEqual(Path(tmp, 'ex', 'x.gsh').read_bytes()[:2], b'\x10\xfb' if packed else b'SH')
                run('xx', 'ex/x.gsh', '--decode', '-o')
                for nm, px in zip(names, (a, b)):
                    w, h, ch, rgba = pngtool._read_png(Path(tmp, 'ex', nm))
                    for y in range(8):
                        for x in range(8):
                            got = (tuple(rgba(x, y)) + (255,))[:4]
                            tol = 8 if nm.endswith('imgA.png') else 0
                            self.assertTrue(all(abs(g - e) <= tol for g, e in zip(got, px[y * 8 + x])), (packed, nm, x, y, got, px[y * 8 + x]))


class CarsGct(unittest.TestCase):
    def test_ci8(self):
        pals = [(i % 32, i * 5 % 32, i * 3 % 32) for i in range(256)]
        pal = b''.join(struct.pack('>H', 0x8000 | c[0] << 10 | c[1] << 5 | c[2]) for c in pals)
        gct = struct.pack('>III', 2, 58, 256) + pal + struct.pack('>IIIIII', 1, 8, 8, 8, 8, 64) + bytes(64)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 't.gct').write_bytes(gct)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            png = Path(tmp, 'ex', 't.png')
            self.assertTrue(png.exists())
            time.sleep(1.1)
            px = [tuple(v * 255 // 31 for v in pals[(x * 7 + y * 11) % 256]) + (255,) for y in range(8) for x in range(8)]
            write_png(png, 8, 8, b''.join(bytes(p) for p in px))
            r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            run('xx', 'ex/t.gct', '--decode', '-o')
            w, h, ch, rgba = pngtool._read_png(Path(tmp, 'ex', 't.png'))
            for i, p in enumerate(px):
                self.assertEqual((tuple(rgba(i % 8, i // 8)) + (255,))[:4], p, i)
            self.assertEqual(Path(tmp, 'ex', 't.gct').read_bytes()[:0x20c], gct[:0x20c])


class RtexWrap(unittest.TestCase):
    def test_roundtrip(self):
        for fmt, unit, nbytes in (('i8', 'u8', 64), ('rgba8', 'u32', 256), ('rgb565', 'u16', 128)):
            body = ' '.join(['00' * (4 if unit == 'u32' else 2 if unit == 'u16' else 1)] * (nbytes // (4 if unit == 'u32' else 2 if unit == 'u16' else 1)))
            x = ('<?xml version="1.0" encoding="utf-8"?>\r\n<nw4r_3dif version="1.4.0">\r\n<body>\r\n<rtex>\r\n'
                 '<image format="%s" palette_name=""\r\n\twidth="8" height="8"\r\n\tmipmap_level="1"\r\n\tdata_unit="%s" data_size="%d"\r\n\tpath="x.bmp"\r\n>\r\n\t'
                 % (fmt, unit, nbytes) + body + '\r\n</image>\r\n</rtex>\r\n</body>\r\n</nw4r_3dif>\r\n')
            with tempfile.TemporaryDirectory() as tmp:
                src = Path(tmp, 'src.d'); src.mkdir()
                (src / 't.rtex').write_bytes(x.encode())
                run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
                run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
                run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
                png = Path(tmp, 'ex', 't.png')
                self.assertTrue(png.exists(), fmt)
                time.sleep(1.1)
                if fmt == 'i8':
                    px = [((x * 30 + y * 3) % 256,) * 3 + (255,) for y in range(8) for x in range(8)]
                elif fmt == 'rgba8':
                    px = [((x * 30) % 256, (y * 30) % 256, (x * y * 4) % 256, 255 - x * 9) for y in range(8) for x in range(8)]
                else:
                    px = [((x * 8) * 255 // 31 // 8 * 8 * 31 // 31 if False else (x % 32) * 255 // 31, (y * 4 % 64) * 255 // 63, ((x + y) % 32) * 255 // 31, 255) for y in range(8) for x in range(8)]
                write_png(png, 8, 8, b''.join(bytes(p) for p in px))
                r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                out = Path(tmp, 'ex', 't.rtex').read_bytes().decode()
                self.assertTrue(out.startswith(x[:x.index('>\r\n\t') + 1]) and out.endswith('</image>\r\n</rtex>\r\n</body>\r\n</nw4r_3dif>\r\n'), fmt)
                run('xx', 'ex/t.rtex', '--decode', '-o')
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'ex', 't.png'))
                for i, p in enumerate(px):
                    self.assertEqual((tuple(rgba(i % 8, i // 8)) + (255,))[:4], p if fmt != 'i8' else p, (fmt, i))


class AvalancheTbb(unittest.TestCase):
    def test_atlas_and_single(self):
        def thb(n):
            hd = struct.pack('>I', n)
            recs = b''
            base = 4 + 12 * n
            for i in range(n):
                hd += struct.pack('>III', base + 32 * i, 256 * i, 256)
                recs += bytes(12) + struct.pack('>HHH', 8, 8, 6).ljust(20, b'\0')
            return hd + recs
        px = lambda i: [((x * 30 + i * 50) % 256, (y * 30) % 256, (x * y * 4) % 256, 255 - x * 9) for y in range(8) for x in range(8)]
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'a.thb').write_bytes(thb(2)); (src / 'a.tbb').write_bytes(bytes(512))
            (src / 's.thb').write_bytes(thb(1)); (src / 's.tbb').write_bytes(bytes(256))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            names = sorted(x.name for x in Path(tmp, 'ex').glob('*.png'))
            self.assertEqual(names, ['a_0.png', 'a_1.png', 's.png'])
            time.sleep(1.1)
            want = {'a_0.png': px(0), 'a_1.png': px(1), 's.png': px(2)}
            for n, p in want.items():
                write_png(Path(tmp, 'ex', n), 8, 8, b''.join(bytes(q) for q in p))
            r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            run('xx', 'ex/a.thb', '--decode', '-o'); run('xx', 'ex/s.thb', '--decode', '-o')
            for n, p in want.items():
                w, h, ch, rgba = pngtool._read_png(Path(tmp, 'ex', n))
                for i, q in enumerate(p):
                    self.assertEqual((tuple(rgba(i % 8, i // 8)) + (255,))[:4], q, (n, i))


class GrphWrap(unittest.TestCase):
    def test_roundtrip(self):
        pals = [(0, 0, 0)] + [(i * 2 % 32, i * 5 % 32, i * 3 % 32) for i in range(1, 16)]
        pal = b''.join(struct.pack('<H', 0x8000 | c[0] << 10 | c[1] << 5 | c[2]) for c in pals)
        gpal = b'GPAL' + struct.pack('<I', 16 + len(pal)) + struct.pack('<HHHH', 16, 0, 0, 0) + pal
        def gimg(w, h, bpp, tiled, body):
            return b'GIMG' + struct.pack('<IHH', 16 + len(body), w, h) + bytes([0, 1, 6 if tiled else 2, bpp]) + body
        blocks = gpal + gimg(8, 8, 8, False, bytes(64)) + gimg(8, 8, 4, True, bytes(32))
        raw = b'GRPH' + struct.pack('<HHIHH', 0xfffe, 0x100, 0x10 + len(blocks), 0x10, 3) + blocks
        raw = raw[:8] + struct.pack('<I', len(raw)) + raw[12:]
        px = lambda i: [tuple(v * 255 // 31 for v in pals[1 + (x * 3 + y * 5 + i) % 15]) + (255,) if (x + y) % 5 else (0, 0, 0, 0) for y in range(8) for x in range(8)]
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 'g.dat').write_bytes(raw)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            d = Path(tmp, 'ex', 'g.dat.d')
            names = sorted(x.name for x in d.glob('*.png'))
            self.assertEqual(names, ['g.png', 'g_1.png'])
            time.sleep(1.1)
            for i, n in enumerate(names):
                write_png(d / n, 8, 8, b''.join(bytes(q) for q in px(i)))
            r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            run('xx', 'ex/g.dat', '--decode', '-o')
            for i, n in enumerate(names):
                w, h, ch, rgba = pngtool._read_png(d / n)
                for j, q in enumerate(px(i)):
                    got = (tuple(rgba(j % 8, j // 8)) + (255,))[:4]
                    self.assertEqual(got[:3] if q[3] else got[3:], q[:3] if q[3] else q[3:], (n, j))
            self.assertEqual(Path(tmp, 'ex', 'g.dat').read_bytes()[:0x10 + len(gpal)], raw[:0x10 + len(gpal)])

    def test_sprite(self):
        pals = [(0, 0, 0)] + [(i * 2 % 32, i * 5 % 32, i * 3 % 32) for i in range(1, 16)]
        pal = b''.join(struct.pack('<H', 0x8000 | c[0] << 10 | c[1] << 5 | c[2]) for c in pals)
        gpal = b'GPAL' + struct.pack('<I', 16 + len(pal)) + struct.pack('<HHHH', 16, 0, 0, 0) + pal
        piece = lambda: b'GIMG' + struct.pack('<IHH', 16 + 64, 8, 8) + bytes([0, 1, 2, 8]) + bytes(64)
        frame = b'GIMG' + struct.pack('<IHH', 48 + 2 * 80, 0, 0) + bytes([0, 1, 0x14, 8]) + struct.pack('<IIII', 32, 16, 8, 2) \
            + struct.pack('<HHHH', 0, 0, 0, 0) + struct.pack('<HHHH', 0, 8, 0, 0) + piece() + piece()
        blocks = gpal + frame
        raw = b'GRPH' + struct.pack('<HHIHH', 0xfffe, 0x100, 0, 0x10, 2) + blocks
        raw = raw[:8] + struct.pack('<I', len(raw)) + raw[12:]
        px = [tuple(v * 255 // 31 for v in pals[1 + (x * 3 + y * 5) % 15]) + (255,) for y in range(8) for x in range(16)]
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 's.dat').write_bytes(raw)
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            png = Path(tmp, 'ex', 's.dat.d', 's.png')
            self.assertTrue(png.exists())
            time.sleep(1.1)
            write_png(png, 16, 8, b''.join(bytes(q) for q in px))
            r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            run('xx', 'ex/s.dat', '--decode', '-o')
            w, h, ch, rgba = pngtool._read_png(png)
            for j, q in enumerate(px):
                self.assertEqual((tuple(rgba(j % 16, j // 16)) + (255,))[:3], q[:3], j)


class MidwayTim(unittest.TestCase):
    def test_roundtrip(self):
        pal = [(i * 7 % 256, i * 3 % 256, i * 11 % 256, 255) for i in range(256)]
        p1 = b''.join(bytes([c[1], c[0]]) for c in pal)
        p2 = b''.join(bytes([c[3], c[2]]) for c in pal)
        for bpp in (8, 32):
            hdr = bytearray(0x40)
            n = 64 * bpp // 8
            doff = 0x440 if bpp == 8 else 0xc0
            struct.pack_into('>IIIIIII', hdr, 0x20, 8, 8, bpp, n, doff, 256 if bpp == 8 else 0, 0x40)
            raw = bytes(hdr) + (p1 + p2 if bpp == 8 else b'') 
            raw = raw.ljust(doff, b'\0') + bytes(n)
            if bpp == 8:
                px = [pal[(x * 5 + y * 9) % 256] for y in range(8) for x in range(8)]
            else:
                px = [((x * 30) % 256, (y * 30) % 256, (x * y * 4) % 256, 255 - x * 9) for y in range(8) for x in range(8)]
            with tempfile.TemporaryDirectory() as tmp:
                src = Path(tmp, 'src.d'); src.mkdir()
                (src / 't.tim').write_bytes(raw)
                run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
                run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
                run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
                png = Path(tmp, 'ex', 't.png')
                self.assertTrue(png.exists(), bpp)
                time.sleep(1.1)
                write_png(png, 8, 8, b''.join(bytes(q) for q in px))
                r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                run('xx', 'ex/t.tim', '--decode', '-o')
                w, h, ch, rgba = pngtool._read_png(png)
                for j, q in enumerate(px):
                    self.assertEqual((tuple(rgba(j % 8, j // 8)) + (255,))[:4], q, (bpp, j))
                self.assertEqual(Path(tmp, 'ex', 't.tim').read_bytes()[:doff], raw[:doff])


class TplcWrap(unittest.TestCase):
    def test_roundtrip(self):
        tpl = struct.pack('>IIIIIHHIII', 0x0020af30, 1, 12, 0x14, 0, 8, 8, 6, 0x40, 0).ljust(0x40, b'\0') + bytes(256)
        dec = struct.pack('>I', len(tpl) + 4) + tpl
        def pack(d):
            o = bytearray()
            for i in range(0, len(d), 8):
                c = d[i:i + 8]
                o.append((1 << len(c)) - 1); o += c
            return bytes(o)
        px = [((x * 30) % 256, (y * 30) % 256, (x * y * 4) % 256, 255 - x * 9) for y in range(8) for x in range(8)]
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'src.d'); src.mkdir()
            (src / 't.tplc').write_bytes(pack(dec))
            run = lambda *a: subprocess.run([str(WSZST), *a], cwd=tmp, capture_output=True, text=True)
            run('create', 'src.d', '--u8', '--dest', 'in.arc', '-o')
            run('xx', 'in.arc', '--decode', '--dest', 'ex', '-o')
            png = Path(tmp, 'ex', 't.tplc.png')
            self.assertTrue(png.exists())
            time.sleep(1.1)
            write_png(png, 8, 8, b''.join(bytes(q) for q in px))
            r = run('create', 'ex', '--u8', '--dest', 'out.arc', '-o')
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            run('xx', 'ex/t.tplc', '--decode', '-o')
            w, h, ch, rgba = pngtool._read_png(png)
            for j, q in enumerate(px):
                self.assertEqual((tuple(rgba(j % 8, j // 8)) + (255,))[:4], q, j)
            self.assertEqual(Path(tmp, 'ex', 't.tplc').read_bytes()[0], 0xff)


if __name__ == '__main__':
    unittest.main()
