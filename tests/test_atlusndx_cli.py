"""Tests for Atlus Nintendo DS/3DS Directory Index (ATLUS-NDX) format support."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'


def atlus_hash(path: str, bucket_count: int = 256) -> int:
    """Calculate Atlus 37-multiplier hash for a given path."""
    p = path.replace('\\', '/')
    c0 = ord(p[0].lower()) if p else 0
    h = 0 if c0 == ord('/') else c0
    for c in p[1:]:
        h = (h * 37 + ord(c.lower())) & 0xffffffff
    return h & (bucket_count - 1)


def build_synthetic_atlus_archive(files: dict[str, bytes], bucket_count: int = 256):
    """
    Build a synthetic Atlus NDX, IDX, and BIN trio.
    files: dict of { "Folder/Sub/file.bin": b"payload" }
    """
    # 1. Build .bin payload and track file offsets & sizes
    bin_data = bytearray()
    file_info = {}  # path -> (offset, size)

    for path, content in files.items():
        pad = (4 - (len(bin_data) % 4)) % 4
        bin_data.extend(b"\x00" * pad)
        offset = len(bin_data)
        bin_data.extend(content)
        file_info[path] = (offset, len(content))

    # 2. Build .ndx hierarchical tree
    tree = {}
    for path in files.keys():
        parts = path.split("/")
        curr = tree
        for part in parts[:-1]:
            if part not in curr:
                curr[part] = {}
            curr = curr[part]
        curr[parts[-1]] = None  # None indicates leaf file

    queue = [tree]
    node_offsets = {}
    cur_offset = 0
    for n in queue:
        node_offsets[id(n)] = cur_offset
        size = 2
        for name, child in n.items():
            size += 2 + len(name.encode('ascii')) + 4
            if child is not None:
                queue.append(child)
        cur_offset += size

    ndx_data = bytearray()
    for n in queue:
        entries = []
        for name, child in n.items():
            coff = node_offsets[id(child)] if child is not None else 0
            entries.append((name, coff))

        ndx_data.extend(struct.pack("<H", len(entries)))
        for name, coff in entries:
            nb = name.encode('ascii')
            ndx_data.extend(struct.pack("<H", len(nb)))
            ndx_data.extend(nb)
            ndx_data.extend(struct.pack("<I", coff))

    # 3. Build .idx hash index
    buckets = [[] for _ in range(bucket_count)]
    for path, (offset, size) in file_info.items():
        h = atlus_hash(path, bucket_count)
        buckets[h].append((path, offset, size))

    idx_header = bytearray(struct.pack("<H", bucket_count) + b"\x00" * 6)
    idx_table = bytearray(bucket_count * 6)
    idx_extra = bytearray()

    for h, items in enumerate(buckets):
        if not items:
            struct.pack_into("<IH", idx_table, h * 6, 0, 0)
        else:
            list_off = 8 + len(idx_table) + len(idx_extra)
            w0 = (list_off << 1) | 1
            default_sz = items[0][2]
            w0 |= (default_sz & 0x3f) << 26
            w4 = default_sz >> 6
            struct.pack_into("<IH", idx_table, h * 6, w0, w4)

            list_block = bytearray([len(items)])
            for idx_i, (path, off, sz) in enumerate(items):
                list_block.extend(struct.pack("<I", off >> 2))
                if idx_i > 0:
                    list_block.extend(struct.pack("<I", sz))
                # Match condition: verify unique character index
                last_slash = path.rfind("/")
                check_char_idx = last_slash + 1 if last_slash >= 0 else 0
                check_char = ord(path[check_char_idx].lower())
                list_block.extend(bytes([check_char, check_char_idx, 0x00]))
            idx_extra.extend(list_block)

    idx_data = bytes(idx_header + idx_table + idx_extra)
    return bytes(ndx_data), bytes(idx_data), bytes(bin_data)


class AtlusNdxTests(unittest.TestCase):
    def test_detection_and_extraction(self):
        with tempfile.TemporaryDirectory() as td:
            ndx_path = Path(td) / "Data.ndx"
            idx_path = Path(td) / "Data.idx"
            bin_path = Path(td) / "Data.bin"
            dest_dir = Path(td) / "extracted"

            sample_files = {
                "Maps/Town/map01.bin": b"TOWN_MAP_DATA_01_TESTING_ATLUS",
                "Maps/Field/field01.dat": b"FIELD_DATA_9876543210",
                "Scripts/intro.ev": b"EVENT_SCRIPT_INTRO_SCENE_001",
                "Msg/dialogue.txt": b"Hello from Radiant Historia Atlus extractor!",
            }

            ndx_data, idx_data, bin_data = build_synthetic_atlus_archive(sample_files)
            ndx_path.write_bytes(ndx_data)
            idx_path.write_bytes(idx_data)
            bin_path.write_bytes(bin_data)

            # Test filetype detection on .ndx
            res = subprocess.run([str(WSZST), "filetype", str(ndx_path)], capture_output=True, text=True, check=True)
            self.assertIn("ATLUS-NDX", res.stdout)

            # Test extraction from .ndx
            subprocess.run([str(WSZST), "extract", str(ndx_path), "--dest", str(dest_dir)], check=True)

            # Verify extracted files match original content byte-for-byte
            for rel_path, expected_bytes in sample_files.items():
                out_file = dest_dir / rel_path
                self.assertTrue(out_file.exists(), f"Missing extracted file: {rel_path}")
                self.assertEqual(out_file.read_bytes(), expected_bytes, f"Content mismatch in {rel_path}")

    def test_extract_via_bin_companion(self):
        with tempfile.TemporaryDirectory() as td:
            ndx_path = Path(td) / "Archive.ndx"
            idx_path = Path(td) / "Archive.idx"
            bin_path = Path(td) / "Archive.bin"
            dest_dir = Path(td) / "extracted_bin"

            sample_files = {
                "Chr2D/hero.nanr": b"HERO_SPRITE_ANIMATION_DATA",
                "Chr2D/hero.ncer": b"HERO_SPRITE_CELL_DATA",
            }

            ndx_data, idx_data, bin_data = build_synthetic_atlus_archive(sample_files)
            ndx_path.write_bytes(ndx_data)
            idx_path.write_bytes(idx_data)
            bin_path.write_bytes(bin_data)

            # Test extraction by pointing wszst directly at .bin with companion .ndx
            subprocess.run([str(WSZST), "extract", str(bin_path), "--dest", str(dest_dir)], check=True)

            for rel_path, expected_bytes in sample_files.items():
                out_file = dest_dir / rel_path
                self.assertTrue(out_file.exists(), f"Missing extracted file: {rel_path}")
                self.assertEqual(out_file.read_bytes(), expected_bytes)


if __name__ == "__main__":
    unittest.main()
