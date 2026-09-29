"""Pikmin retail-style interleaved ARC/DIR records and standalone TXE loading."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / 'project/bin'


def pair(members, terminated=True):
    index = bytearray(8)
    arc = bytearray()
    for name, payload in members:
        encoded = (name if isinstance(name, bytes) else name.encode()) + (b'\0' if terminated else b'')
        encoded += bytes(-len(encoded) % 4)
        arc += bytes(-len(arc) % 32)
        index += struct.pack('>III', len(arc), len(payload), len(encoded)) + encoded
        arc += payload
    struct.pack_into('>II', index, 0, len(index), len(members))
    return index, arc


class PikminTests(unittest.TestCase):
    def tool(self, binary, *args):
        result = subprocess.run([str(BIN/binary), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result

    def test_arc_dir_named_members_and_empty_file(self):
        members = [('objects/one.bin', b'first'), ('other.bin', b'second'), ('empty.bin', b'')]
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            index, arc = pair(members)
            (root/'sample.dir').write_bytes(index);(root/'sample.arc').write_bytes(arc)
            result=self.tool('wszst','EXTRACT',root/'sample.arc','-d',root/'out')
            self.assertEqual(result.returncode,0,result.stderr)
            for name, payload in members:
                self.assertEqual((root/'out'/name).read_bytes(),payload)

    def test_full_width_and_shift_jis_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            name='folder/ｐ.bin'
            index,arc=pair([('four',b'full width'),(name.encode('shift_jis'),b'japanese')],False)
            (root/'sample.dir').write_bytes(index);(root/'sample.arc').write_bytes(arc)
            result=self.tool('wszst','EXTRACT',root/'sample.arc','-d',root/'out')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual((root/'out/four').read_bytes(),b'full width')
            self.assertEqual((root/'out'/name).read_bytes(),b'japanese')

    def test_bad_later_member_never_writes_partial_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            index,arc=pair([('first.bin',b'first'),('second.bin',b'second')])
            second=20+struct.unpack_from('>I',index,16)[0]
            struct.pack_into('>I',index,second+4,0xffffffff)
            (root/'sample.dir').write_bytes(index);(root/'sample.arc').write_bytes(arc)
            self.assertNotEqual(self.tool('wszst','EXTRACT',root/'sample.arc','-d',root/'out').returncode,0)
            self.assertFalse((root/'out').exists())

    def test_traversal_name_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            index,arc=pair([('../escape.bin',b'payload')])
            (root/'sample.dir').write_bytes(index);(root/'sample.arc').write_bytes(arc)
            self.assertNotEqual(self.tool('wszst','EXTRACT',root/'sample.arc','-d',root/'out').returncode,0)
            self.assertFalse((root/'escape.bin').exists())

    def test_testmode_is_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            index,arc=pair([('one.bin',b'payload')])
            (root/'sample.dir').write_bytes(index);(root/'sample.arc').write_bytes(arc)
            result=self.tool('wszst','EXTRACT',root/'sample.arc','-d',root/'out','--test')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertFalse((root/'out').exists())

    def test_txe_rgb565_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            # One GX 4x4 RGB565 block, all pixels red, in a 32-byte TXE header.
            header=struct.pack('>4HI',4,4,2,0,32)+bytes(20)
            src=root/'red.txe';src.write_bytes(header+b'\xf8\x00'*16)
            out=root/'red.png'
            result=self.tool('wimgt','DECODE',src,'-d',out)
            self.assertEqual(result.returncode,0,result.stderr)
            from pngtool import _read_png
            width,height,channels,pixel=_read_png(str(out))
            self.assertEqual((width,height),(4,4))
            self.assertTrue(all(pixel(x,y)==(255,0,0,255) for y in range(height) for x in range(width)))

    def test_txe_omitted_size_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            src=root/'effect.txe';src.write_bytes(struct.pack('>4HI',4,4,2,0,0)+bytes(20)+b'\xf8\x00'*16)
            result=self.tool('wimgt','DECODE',src,'-d',root/'effect.png')
            self.assertEqual(result.returncode,0,result.stderr)
            from pngtool import _read_png
            width,height,_,pixel=_read_png(root/'effect.png')
            self.assertEqual((width,height,pixel(0,0)),(4,4,(255,0,0,255)))

    def test_txe_truncated_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            src=root/'short.txe';src.write_bytes(struct.pack('>4HI',4,4,2,0,32)+bytes(20)+bytes(31))
            result=self.tool('wimgt','DECODE',src,'-d',root/'out.png')
            self.assertNotEqual(result.returncode,0)
            self.assertFalse((root/'out.png').exists())


if __name__ == '__main__':
    unittest.main()
