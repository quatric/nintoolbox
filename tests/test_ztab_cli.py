"""ZTAB bounds, empty entries, and error handling."""

from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / "project" / "bin" / "wszst"


def archive(payload=b"payload", *, offset=32, size=None):
    return (
        b"ZTAB"
        + struct.pack(
            ">5I", 1, 0x1234, offset, len(payload) if size is None else size, 0
        )
        + bytes(8)
        + payload
    )


class ZtabTests(unittest.TestCase):
    def tool(self, *args, success=True):
        result = subprocess.run(
            [str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_empty_entry_roundtrip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.ztab"
            original = archive(b"")
            source.write_bytes(original)
            self.tool("EXTRACT", source, "-d", root / "out")
            self.assertEqual(
                (root / "out" / "entry_0000_flags_00001234.bin").read_bytes(), b""
            )
            self.tool("CREATE", root / "out", "-d", root / "rebuilt.ztab")
            self.assertEqual((root / "rebuilt.ztab").read_bytes(), original)

    def test_mixed_empty_and_nonempty_entries_roundtrip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            entries = {
                "entry_0000_flags_00000001.bin": b"",
                "entry_0001_flags_00000002.bin": b"preserved payload",
            }
            for name, payload in entries.items():
                (source / name).write_bytes(payload)
            packed = root / "packed.ztab"
            self.tool("CREATE", source, "-d", packed)
            self.tool("EXTRACT", packed, "-d", root / "out")
            for name, payload in entries.items():
                self.assertEqual((root / "out" / name).read_bytes(), payload)

    def test_truncated_and_wrapping_ranges_are_rejected(self):
        cases = [(32, 100), (0xFFFFFFF0, 64), (0xFFFFFFFF, 0)]
        for offset, size in cases:
            with self.subTest(
                offset=offset, size=size
            ), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / "input.ztab"
                source.write_bytes(archive(offset=offset, size=size))
                self.tool("EXTRACT", source, "-d", root / "out", success=False)
                self.assertFalse((root / "out").exists())

    def test_later_invalid_entry_is_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.ztab"
            data = b"ZTAB" + struct.pack(">I", 2)
            data += struct.pack(">4I", 1, 48, 3, 0) + struct.pack(">4I", 2, 64, 100, 0)
            data += bytes(8) + b"abc" + bytes(13) + b"bad!"
            source.write_bytes(data)
            self.tool("EXTRACT", source, "-d", root / "out", success=False)
            self.assertFalse((root / "out").exists())

    def test_write_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.ztab"
            source.write_bytes(archive())
            (root / "out" / "entry_0000_flags_00001234.bin").mkdir(parents=True)
            self.tool("EXTRACT", source, "-d", root / "out", success=False)

    def test_testmode_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.ztab"
            source.write_bytes(archive())
            self.tool("EXTRACT", source, "-d", root / "out", "--test")
            self.assertFalse((root / "out").exists())


if __name__ == "__main__":
    unittest.main()
