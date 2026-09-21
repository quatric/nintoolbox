"""Regressions for overlapping archive signatures and empty MDR members."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

BINARY = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def mdr(members):
    offset = 4 + 4 * len(members)
    offsets, chunks = [], []
    for payload, compressed in members:
        stored = zlib.compress(payload) if compressed else payload
        offsets.append(offset)
        chunk = struct.pack('>4I', len(payload), 0, len(payload), len(stored)) + stored
        chunk += bytes(len(chunk) % 2)
        chunks.append(chunk)
        offset += len(chunk)
    return struct.pack('>I', len(members)) + struct.pack('>' + 'I' * len(offsets), *offsets) + b''.join(chunks)


class ArchiveDetectionTests(unittest.TestCase):
    def tool(self, *args):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_mdr_compressed_raw_and_empty_members(self):
        members = [(b'alpha payload', True), (b'raw payload', False), (b'', False)]
        for suffix in ('.mdr', '.bin'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / ('input' + suffix)
                source.write_bytes(mdr(members))
                self.assertIn('MDR', self.tool('FILETYPE', source))
                output = root / 'out'
                self.tool('EXTRACT', source, '-d', output)
                files = sorted(output.glob('chunk_*.bin'))
                self.assertEqual([p.read_bytes() for p in files], [p for p, _ in members])

    def test_mdr_reports_member_write_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input.mdr'
            source.write_bytes(mdr([(b'payload', False)]))
            output = root / 'out'
            (output / 'chunk_00_flags_00000000_raw.bin').mkdir(parents=True)
            result = subprocess.run([str(BINARY), 'EXTRACT', str(source), '-d', str(output)],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mpbin_still_extracts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input.bin'
            payload = b'Mario Party stored member'
            source.write_bytes(struct.pack('>4I', 1, 8, len(payload), 0) + payload)
            self.assertIn('MPBIN', self.tool('FILETYPE', source))
            self.tool('EXTRACT', source, '-d', root / 'out')
            self.assertEqual((root / 'out' / 'file000.dat').read_bytes(), payload)

    def test_mpbin_rejects_truncated_compressed_members(self):
        for compression in (1, 2, 3, 4, 5, 7):
            with self.subTest(compression=compression), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / 'input.bin'
                source.write_bytes(struct.pack('>4I', 1, 8, 100, compression) + b'X')
                result = subprocess.run([str(BINARY), 'EXTRACT', str(source), '-d', str(root / 'out')],
                                        capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(list((root / 'out').glob('file*')))

    def test_wrapped_g1t_precedes_atb(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = b'RAW_G1T_MEMBER_PAYLOAD_BYTES....'
            body = struct.pack('<I', 4) + bytes([1, 255, 0x33, 0]) + bytes(16) + payload
            container = b'GT1G0600' + struct.pack('<4I', 48 + len(body), 48, 1, 5) + bytes(24) + body
            streams = [zlib.compress(container[:60]), zlib.compress(container[60:])]
            wrapper = struct.pack('>5I', 0x10000, 2, len(container), len(streams[0]) + 4, len(streams[1]) + 4)
            wrapper += struct.pack('>I', len(streams[0])) + streams[0] + bytes(24)
            wrapper += struct.pack('>I', len(streams[1])) + streams[1] + bytes(40)
            source = root / 'sample.g1t.gz'
            source.write_bytes(wrapper)
            self.assertIn('G1T', self.tool('FILETYPE', source))
            self.tool('EXTRACT', source, '-d', root / 'out')
            self.assertEqual((root / 'out' / 'sample.g1t.gz_0000.bin').read_bytes(), container[72:])


if __name__ == '__main__':
    unittest.main()
