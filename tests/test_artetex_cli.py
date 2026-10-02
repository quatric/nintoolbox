"""Tests for ArtePiazza Nintendo DS Texture Container (ARTE-TEX) format support."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def build_synthetic_texture_object(width: int, height: int, fmt: int, pixels: list[int], palette: list[tuple[int, int, int]]) -> bytes:
    """
    Build a synthetic ArtePiazza TextureObject .tex file.
    fmt: 3 (4-bpp), 4 (8-bpp), 1 (A3I5), 6 (A5I3)
    """
    # Calculate sw shift (width = 8 << sw)
    sw = 0
    while (8 << sw) < width:
        sw += 1
    sh = 0
    while (8 << sh) < height:
        sh += 1

    header = bytearray(112)
    header[0:14] = b'TextureObject\x00'
    struct.pack_into('<I', header, 0x10, 0x00010000)
    struct.pack_into('<I', header, 0x20, fmt)
    struct.pack_into('<I', header, 0x24, sw)
    struct.pack_into('<I', header, 0x28, sh)

    if fmt == 3:  # 4-bpp
        pix_len = (width * height + 1) // 2
        pix_data = bytearray(pix_len)
        for i in range(0, len(pixels), 2):
            p0 = pixels[i] & 0x0f
            p1 = (pixels[i + 1] & 0x0f) if i + 1 < len(pixels) else 0
            pix_data[i // 2] = p0 | (p1 << 4)
    else:  # 8-bpp, A3I5, A5I3 (1 byte per pixel)
        pix_len = width * height
        pix_data = bytes(pixels[:pix_len])

    pal_len = len(palette) * 2
    pal_data = bytearray(pal_len)
    for i, (r, g, b) in enumerate(palette):
        # RGB888 -> RGB555
        r5 = (r * 31 + 127) // 255
        g5 = (g * 31 + 127) // 255
        b5 = (b * 31 + 127) // 255
        rgb555 = (r5 & 0x1f) | ((g5 & 0x1f) << 5) | ((b5 & 0x1f) << 10)
        struct.pack_into('<H', pal_data, i * 2, rgb555)

    hlen = 112
    poff = hlen + len(pix_data)
    struct.pack_into('<I', header, 0x30, len(pix_data))
    struct.pack_into('<I', header, 0x34, hlen)
    struct.pack_into('<I', header, 0x38, pal_len)
    struct.pack_into('<I', header, 0x3c, poff)

    return bytes(header) + bytes(pix_data) + bytes(pal_data)


def build_synthetic_texture_data(width: int, height: int, fmt: int, pixels: list[int], palette: list[tuple[int, int, int]]) -> bytes:
    """Build a synthetic ArtePiazza TextureData .tex file (56-byte header)."""
    header = bytearray(56)
    header[0:12] = b'TextureData\x00'
    struct.pack_into('<I', header, 0x10, fmt)
    struct.pack_into('<I', header, 0x14, 56)  # header_len
    struct.pack_into('<I', header, 0x24, width // 4)
    struct.pack_into('<I', header, 0x28, height // 4)

    if fmt == 3:
        pix_len = (width * height + 1) // 2
        pix_data = bytearray(pix_len)
        for i in range(0, len(pixels), 2):
            p0 = pixels[i] & 0x0f
            p1 = (pixels[i + 1] & 0x0f) if i + 1 < len(pixels) else 0
            pix_data[i // 2] = p0 | (p1 << 4)
    else:
        pix_len = width * height
        pix_data = bytes(pixels[:pix_len])

    pal_len = len(palette) * 2
    pal_data = bytearray(pal_len)
    for i, (r, g, b) in enumerate(palette):
        r5 = (r * 31 + 127) // 255
        g5 = (g * 31 + 127) // 255
        b5 = (b * 31 + 127) // 255
        rgb555 = (r5 & 0x1f) | ((g5 & 0x1f) << 5) | ((b5 & 0x1f) << 10)
        struct.pack_into('<H', pal_data, i * 2, rgb555)

    struct.pack_into('<I', header, 0x2c, len(pix_data))
    struct.pack_into('<I', header, 0x30, len(pix_data))  # poff relative to header
    struct.pack_into('<I', header, 0x34, pal_len)

    return bytes(header) + bytes(pix_data) + bytes(pal_data)


class TestArteTextureCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not WSZST.is_file():
            raise unittest.SkipTest(f'{WSZST} binary not found; run make first')

    def test_filetype_identification_texture_object(self):
        pal = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)] + [(0, 0, 0)] * 12
        pix = [1, 2, 3, 0] * (32 * 32 // 4)
        raw = build_synthetic_texture_object(32, 32, 3, pix, pal)

        with tempfile.TemporaryDirectory() as tmpdir:
            tex_file = Path(tmpdir) / 'test_sample.tex'
            tex_file.write_bytes(raw)

            res = subprocess.run(
                [str(WSZST), 'filetype', str(tex_file)],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn('ARTE-TEX', res.stdout)

    def test_filetype_identification_texture_data(self):
        pal = [(0, 0, 0), (255, 255, 255)] + [(0, 0, 0)] * 14
        pix = [1, 0, 1, 0] * (64 * 32 // 4)
        raw = build_synthetic_texture_data(64, 32, 3, pix, pal)

        with tempfile.TemporaryDirectory() as tmpdir:
            tex_file = Path(tmpdir) / 'chricon.tex'
            tex_file.write_bytes(raw)

            res = subprocess.run(
                [str(WSZST), 'filetype', str(tex_file)],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn('ARTE-TEX', res.stdout)

    def test_extract_4bpp_texture(self):
        width, height = 16, 16
        pal = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)] + [(0, 0, 0)] * 12
        pix = [1, 2, 3, 1] * (width * height // 4)
        raw = build_synthetic_texture_object(width, height, 3, pix, pal)

        with tempfile.TemporaryDirectory() as tmpdir:
            tex_file = Path(tmpdir) / 'sprite.tex'
            tex_file.write_bytes(raw)

            res = subprocess.run(
                [str(WSZST), 'extract', str(tex_file)],
                capture_output=True,
                text=True,
                check=True,
            )
            png_file = Path(tmpdir) / 'sprite.png'
            self.assertTrue(png_file.is_file(), f'Expected {png_file} to exist after extraction')
            self.assertGreater(png_file.stat().st_size, 0)

    def test_extract_8bpp_texture(self):
        width, height = 32, 32
        pal = [(i, (i * 2) % 256, (i * 3) % 256) for i in range(256)]
        pix = [i % 256 for i in range(width * height)]
        raw = build_synthetic_texture_object(width, height, 4, pix, pal)

        with tempfile.TemporaryDirectory() as tmpdir:
            tex_file = Path(tmpdir) / 'background.tex'
            tex_file.write_bytes(raw)

            res = subprocess.run(
                [str(WSZST), 'extract', str(tex_file)],
                capture_output=True,
                text=True,
                check=True,
            )
            png_file = Path(tmpdir) / 'background.png'
            self.assertTrue(png_file.is_file(), f'Expected {png_file} to exist after extraction')
            self.assertGreater(png_file.stat().st_size, 0)

    def test_extract_a5i3_texture(self):
        width, height = 16, 16
        pal = [(255, 255, 255)] * 8
        # fmt 6: 3-bit color index, 5-bit alpha (val = (alpha5 << 3) | color_idx)
        pix = [((i % 32) << 3) | 0 for i in range(width * height)]
        raw = build_synthetic_texture_object(width, height, 6, pix, pal)

        with tempfile.TemporaryDirectory() as tmpdir:
            tex_file = Path(tmpdir) / 'mask.tex'
            tex_file.write_bytes(raw)

            res = subprocess.run(
                [str(WSZST), 'extract', str(tex_file)],
                capture_output=True,
                text=True,
                check=True,
            )
            png_file = Path(tmpdir) / 'mask.png'
            self.assertTrue(png_file.is_file(), f'Expected {png_file} to exist after extraction')
            self.assertGreater(png_file.stat().st_size, 0)

    def test_extract_retail_dq4_if_present(self):
        dq4_sample = Path('/tmp/game_dq4/extracted/data/2D/arte_logo.tex')
        if not dq4_sample.is_file():
            self.skipTest('Retail DQ4 arte_logo.tex fixture not present')

        with tempfile.TemporaryDirectory() as tmpdir:
            dst = Path(tmpdir) / 'arte_logo.tex'
            dst.write_bytes(dq4_sample.read_bytes())

            res = subprocess.run(
                [str(WSZST), 'extract', str(dst)],
                capture_output=True,
                text=True,
                check=True,
            )
            out_png = Path(tmpdir) / 'arte_logo.png'
            self.assertTrue(out_png.is_file())
            self.assertGreater(out_png.stat().st_size, 1000)


if __name__ == '__main__':
    unittest.main()
