"""Layton backgrounds: palette colors, map order, transparency and wrappers."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from pngtool import _read_png

BIN = Path(__file__).resolve().parents[1]/'project/bin'


def background():
    return (struct.pack('<I3HI',3,0x03e0,0x001f,0x7c00,2)
            + bytes([0]+[1]*63) + bytes([2]*64) + struct.pack('<4H',2,1,1,0))


def literal(data,kind):
    stream=bytearray(bytes([0x10 if kind==2 else 0x30])+len(data).to_bytes(3,'little'))
    stride=8 if kind==2 else 128
    for pos in range(0,len(data),stride):
        part=data[pos:pos+stride]
        stream+=bytes([0 if kind==2 else len(part)-1])+part
    return struct.pack('<I',kind)+stream


class LaytonBackgroundTests(unittest.TestCase):
    def decode(self,data,suffix='.arc',flags=()):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        root=Path(tmp.name);src=root/('image'+suffix);src.write_bytes(data);out=root/'image.png'
        result=subprocess.run([str(BIN/'wimgt'),'DECODE',str(src),'-d',str(out),*flags],
                              capture_output=True,text=True,timeout=30)
        self.assertGreaterEqual(result.returncode,0,result.stderr)
        return result,out

    def check_image(self,data,suffix='.arc'):
        result,out=self.decode(data,suffix)
        self.assertEqual(result.returncode,0,result.stderr)
        w,h,channels,pixel=_read_png(out)
        self.assertEqual((w,h),(16,8))
        for y in range(h):
            for x in range(w):
                expected=(0,0,255,255) if x<8 else (0,255,0,0) if (x,y)==(8,0) else (255,0,0,255)
                self.assertEqual(pixel(x,y),expected,(x,y))

    def test_raw_arc_and_arb(self):
        for suffix in ('.arc','.arb','.ARC'):
            self.check_image(background(),suffix)

    def test_lz10_and_rle_wrappers(self):
        for kind in (1,2):
            self.check_image(literal(background(),kind))

    def test_huffman_wrappers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);raw=root/'bg.raw';raw.write_bytes(background())
            for kind,ext in ((3,'huff4'),(4,'huff8')):
                packed=root/('packed.'+ext)
                result=subprocess.run([str(BIN/'wszst'),'COMPRESS',str(raw),'-d',str(packed)],
                                      capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stderr)
                self.check_image(struct.pack('<I',kind)+packed.read_bytes())

    def test_palette_zero_alpha_bit(self):
        data=bytearray(background());struct.pack_into('<H',data,4,0x83e0)
        result,out=self.decode(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(_read_png(out)[3](8,0),(0,255,0,255))

    def test_raw_palette_is_not_a_compression_prefix(self):
        # Four colors and a first color starting with 0x28 resemble typed Huffman.
        data=struct.pack('<I4HI',4,0x28,31,0x3e0,0x7c00,1)+bytes([1]*64)+struct.pack('<3H',1,1,0)
        result,out=self.decode(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(_read_png(out)[3](0,0),(255,0,0,255))

    def test_malformed_images_create_no_output(self):
        variants=[background()[:-1]]
        for offset,value,fmt in ((0,257,'I'),(10,0xffffffff,'I'),(142,0,'H'),(148,2,'H'),(14,3,'B')):
            data=bytearray(background());struct.pack_into('<'+fmt,data,offset,value);variants.append(data)
        variants.append(struct.pack('<I4sI',2,b'\x10\0\0\0',0xffffffff))
        for data in variants:
            result,out=self.decode(data)
            self.assertNotEqual(result.returncode,0,result.stderr)
            self.assertFalse(out.exists())

    def test_probe_is_extension_gated(self):
        result,out=self.decode(background(),'.bin')
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(out.exists())

    def test_dry_run(self):
        result,out=self.decode(literal(background(),2),flags=('--test',))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(out.exists())


if __name__ == '__main__':
    unittest.main()
