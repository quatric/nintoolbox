"""G1T member boundaries, table ordering, and extraction errors."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def archive(members, *, extended=True, order=None, endian='<'):
    if order is None:
        order = list(range(len(members)))
    offset = len(order) * 4
    offsets, bodies = [], []
    for format_id, payload in members:
        body = bytes([1, format_id, 0x33, 0]) + bytes(4)
        if extended:
            body += bytes(12)
        body += payload
        offsets.append(offset)
        bodies.append(body)
        offset += len(body)
    header = (b'GT1G' if endian == '<' else b'G1TG') + b'0600'
    header += struct.pack(endian + '4I', 48 + offset, 48, len(order), 5) + bytes(24)
    return header + b''.join(struct.pack(endian + 'I', offsets[i]) for i in order) + b''.join(bodies)


def png_pixels(path):
    data = path.read_bytes()
    offset, packed = 8, bytearray()
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        if data[offset + 4:offset + 8] == b'IDAT':
            packed += data[offset + 8:offset + 8 + size]
        offset += size + 12
    return zlib.decompress(packed)


class G1tTests(unittest.TestCase):
    def extract(self, root, data, *options, success=True):
        source = root / 'sample.g1t'
        source.write_bytes(data)
        result = subprocess.run([str(BINARY), 'EXTRACT', str(source), '-d', str(root / 'out'), *options],
                                capture_output=True, text=True, timeout=30)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
        return root / 'out'

    def test_raw_members_have_individual_bounds(self):
        payloads = [b'first texture payload', b'second texture payload']
        for endian in ('<', '>'):
            for order in ([0, 1], [1, 0]):
                with self.subTest(endian=endian, order=order), tempfile.TemporaryDirectory() as directory:
                    out = self.extract(Path(directory), archive([(255, p) for p in payloads], order=order, endian=endian))
                    for i, index in enumerate(order):
                        self.assertEqual((out / f'sample.g1t_{i:04d}.bin').read_bytes(), payloads[index])

    def test_alias_offsets_and_empty_raw_members(self):
        for payload in (b'aliased texture payload', b''):
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as directory:
                out = self.extract(Path(directory), archive([(255, payload)], order=[0, 0]))
                for i in range(2):
                    self.assertEqual((out / f'sample.g1t_{i:04d}.bin').read_bytes(), payload)

    def test_unextended_textures_use_their_own_pixel_offset(self):
        payload = bytes([255, 0, 0, 255]) * 64
        with tempfile.TemporaryDirectory() as directory:
            out = self.extract(Path(directory), archive([(9, payload), (9, payload)], extended=False))
            self.assertEqual(png_pixels(out / 'sample.g1t_0000.png'), png_pixels(out / 'sample.g1t_0001.png'))

    def test_short_texture_cannot_borrow_next_members_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            out = self.extract(Path(directory), archive([(9, bytes(16)), (255, bytes(512))]), success=False)
            self.assertFalse((out / 'sample.g1t_0000.png').exists())

    def test_write_failure_is_not_hidden_by_successful_member(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'out' / 'sample.g1t_0000.bin').mkdir(parents=True)
            self.extract(root, archive([(255, b'first payload'), (255, b'second payload')]), success=False)

    def test_testmode_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            out = self.extract(Path(directory), archive([(255, b'raw payload')]), '--test')
            self.assertFalse(out.exists())

    def test_texture_offsets_cannot_point_into_table(self):
        with tempfile.TemporaryDirectory() as directory:
            data = bytearray(archive([(255, b'payload')]))
            struct.pack_into('<I', data, 48, 0)
            self.extract(Path(directory), data, success=False)


if __name__ == '__main__':
    unittest.main()
