"""Tests for Nintendo Little-Endian BMG (CBMG / GSEM1gmb) format support."""
from pathlib import Path
import subprocess
import tempfile
import unittest

WSZST = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wszst'
WBMGT = Path(__file__).resolve().parents[1] / 'project' / 'bin' / 'wbmgt'

SAMPLE_BMG_TXT = """#BMG
@ENDIAN = 1
@ENCODING = 3
@INF-MAGIC = "1FNI"

     0 = Hello
     1 = World
"""


class BmgLittleEndianTests(unittest.TestCase):
    def test_encode_decode_roundtrip(self):
        with tempfile.TemporaryDirectory() as td:
            src_txt = Path(td) / "source.txt"
            bmg_path = Path(td) / "test.cbmg"
            dec_txt = Path(td) / "decoded.txt"
            reenc_bmg = Path(td) / "reenc.cbmg"
            
            src_txt.write_text(SAMPLE_BMG_TXT, encoding="utf-8")
            
            # Encode text to LE BMG
            subprocess.run([str(WBMGT), "encode", str(src_txt), "--dest", str(bmg_path)], check=True)
            
            # Verify file magic is GSEM1gmb
            data = bmg_path.read_bytes()
            self.assertTrue(data.startswith(b"GSEM1gmb"))
            
            # Verify wszst filetype detects BMG
            res = subprocess.run([str(WSZST), "filetype", str(bmg_path)], capture_output=True, text=True, check=True)
            self.assertIn("BMG", res.stdout)
            
            # Decode back to text
            subprocess.run([str(WBMGT), "decode", str(bmg_path), "--dest", str(dec_txt)], check=True)
            txt_content = dec_txt.read_text(encoding="utf-8")
            self.assertIn("@ENDIAN = 1", txt_content)
            self.assertIn("Hello", txt_content)
            self.assertIn("World", txt_content)
            
            # Re-encode and verify byte-for-byte exact match
            subprocess.run([str(WBMGT), "encode", str(dec_txt), "--dest", str(reenc_bmg)], check=True)
            self.assertEqual(data, reenc_bmg.read_bytes())


if __name__ == "__main__":
    unittest.main()
