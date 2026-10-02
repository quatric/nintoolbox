"""Tests for Capcom Ghost Trick proprietary format support:
1. CAPCOM-MODS: Full-screen character animation stream (.mods / MODSN3)
2. CAPCOM-GML1: Game Message / Script Binary (.xml.bin / 1LMG)
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def build_synthetic_capcom_mods(frames: int = 60, keyframe_indices: list[int] = None) -> bytes:
    """Build a synthetic Capcom MODS animation stream container."""
    if keyframe_indices is None:
        keyframe_indices = [0, 15, 30, 45]

    hdr_size = 192
    blk_size = 256
    id_crc = 0x0efc2ed9

    # Body length arbitrary
    body_data = b"\x11\x22\x33\x44" * 1000
    trailer_offset = hdr_size + len(body_data)
    trailer_count = len(keyframe_indices)

    # Header 48 bytes of defined fields + 144 bytes of data/padding = 192 bytes
    header = bytearray(192)
    struct.pack_into("<4s4sIIII", header, 0, b"MODS", b"N3\n\0", frames, blk_size, hdr_sz := 192, id_crc)
    # trailer_offset at 0x28, trailer_count at 0x2C
    struct.pack_into("<II", header, 0x28, trailer_offset, trailer_count)

    trailer = bytearray()
    for idx in keyframe_indices:
        # keyframe offset inside body
        kf_off = (idx * 60) + 16
        trailer.extend(struct.pack("<II", idx, kf_off))

    return bytes(header) + body_data + bytes(trailer)


def build_synthetic_capcom_gml1(keys: list[str]) -> bytes:
    """Build a synthetic Capcom GML1 (1LMG) message/script binary."""
    # Bytecode section (sample instructions)
    bytecode = b"\x2d\xff\x00\x00\x00\x00\x0e\x00" + b"\x31\x00\x2a\x00\x2f\x00\x2c\x00\x36\x00\x2b\x00" + b"\xfe\xff"
    data_len = len(bytecode)

    header = bytearray(48)
    struct.pack_into("<4sIIII", header, 0, b"1LMG", 0, data_len, len(keys), key_off := 48)

    # Key table footer: terminator 0xfffe, padding 0, string table length, key count
    str_table_raw = b"\x00".join(k.encode('latin1') for k in keys) + b"\x00"
    footer_hdr = struct.pack("<HHII", 0xfffe, 0, 42, len(keys))

    key_records = bytearray()
    for i, _ in enumerate(keys):
        key_records.extend(struct.pack("<II", (i + 1) * 2, 52 + i * 22))

    footer_str_hdr = struct.pack("<H", 42)

    return bytes(header) + bytecode + footer_hdr + bytes(key_records) + footer_str_hdr + str_table_raw


class CapcomGhostTrickTests(unittest.TestCase):
    def test_mods_detection(self):
        with tempfile.TemporaryDirectory() as td:
            mods_path = Path(td) / "anim.mods"
            mods_data = build_synthetic_capcom_mods(frames=75, keyframe_indices=[0, 10, 25, 50, 70])
            mods_path.write_bytes(mods_data)

            res = subprocess.run([str(WSZST), "filetype", str(mods_path)], capture_output=True, text=True, check=True)
            self.assertIn("CAPCOM-MODS", res.stdout)

    def test_gml1_detection(self):
        with tempfile.TemporaryDirectory() as td:
            gml1_path = Path(td) / "message.xml.bin"
            gml1_data = build_synthetic_capcom_gml1(["languageEnglish", "languageFrancais", "languageDeutsch"])
            gml1_path.write_bytes(gml1_data)

            res = subprocess.run([str(WSZST), "filetype", str(gml1_path)], capture_output=True, text=True, check=True)
            self.assertIn("CAPCOM-GML1", res.stdout)


if __name__ == "__main__":
    unittest.main()
