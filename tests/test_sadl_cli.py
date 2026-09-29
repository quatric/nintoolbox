"""Synthetic SADL codec, channel, header, destination and loop regressions."""
from pathlib import Path
import io
import struct
import subprocess
import tempfile
import unittest
import wave

BIN = Path(__file__).resolve().parents[1] / 'project/bin/wszst'


def sadl(codec=0x70, channels=2, blocks=2, start=0x100, loop=False):
    data = bytearray(start + 16 * channels * blocks)
    data[:4] = b'sadl'
    data[0x31:0x34] = bytes((int(loop), channels, codec | 2))
    struct.pack_into('<I', data, 0x40, len(data))
    struct.pack_into('<I', data, 0x48, start)
    struct.pack_into('<I', data, 0x54, start + 16 * channels if loop else start)
    for ch in range(channels):
        struct.pack_into('<hh', data, 0x80 + ch*4, 1000 if ch == 0 else -1000, 0)
        for block in range(blocks):
            pos = start + (block*channels+ch)*16
            if codec == 0xb0:
                data[pos:pos+16] = bytes([0x91 if ch == 0 else 0x6e])*15 + bytes([0x86])
            else:
                data[pos:pos+16] = bytes([0x11 if ch == 0 else 0x99])*16
    return data


def audio(data):
    with wave.open(io.BytesIO(data), 'rb') as wav:
        return wav.getparams()[:4], struct.unpack('<'+'h'*(wav.getnframes()*wav.getnchannels()),
                                                  wav.readframes(wav.getnframes()))


class SADLTests(unittest.TestCase):
    def run_tool(self, source, command='DECOMPRESS', flags=(), destination=None):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        src = root/'sample.sad'
        src.write_bytes(source)
        out = root/'decoded.wav' if destination is None else root/destination
        result = subprocess.run([str(BIN), command, str(src), '-d', str(out), '--no-passthrough', *flags],
                                capture_output=True, text=True, timeout=30)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        return result, out

    def test_ima_stereo_history_and_interleave(self):
        result, out = self.run_tool(sadl())
        self.assertEqual(result.returncode, 0, result.stderr)
        params, pcm = audio(out.read_bytes())
        self.assertEqual(params, (2, 2, 16364, 64))
        self.assertEqual(pcm[::2], tuple(range(1001, 1065)))
        self.assertEqual(pcm[1::2], tuple(range(-1001, -1065, -1)))

    def test_procyon_stereo_quantization(self):
        result, out = self.run_tool(sadl(codec=0xb0))
        self.assertEqual(result.returncode, 0, result.stderr)
        params, pcm = audio(out.read_bytes())
        self.assertEqual(params, (2, 2, 16364, 60))
        self.assertEqual(pcm[::2], (64,)*60)
        self.assertEqual(pcm[1::2], (-64,)*60)

    def test_short_header_and_uppercase_compatibility(self):
        data=sadl(channels=1,start=0xc0);data[:4]=b'SADL';data[0x33]=0x74
        result,out=self.run_tool(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(audio(out.read_bytes())[0],(1,2,32728,64))

    def test_loop_chunk(self):
        result,out=self.run_tool(sadl(codec=0xb0,loop=True))
        self.assertEqual(result.returncode,0,result.stderr)
        data=out.read_bytes(); pos=44+struct.unpack_from('<I',data,40)[0]
        self.assertEqual(data[pos:pos+4],b'smpl')
        self.assertEqual(struct.unpack_from('<II',data,pos+52),(30,59))
        self.assertEqual(struct.unpack_from('<I',data,4)[0],len(data)-8)

    def test_extract_keeps_decoded_wav(self):
        result,out=self.run_tool(sadl(),command='EXTRACT')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(out.is_file(),list(out.parent.iterdir()))
        self.assertEqual(audio(out.read_bytes())[0],(2,2,16364,64))

    def test_testmode_writes_nothing(self):
        result,out=self.run_tool(sadl(),flags=('--test',))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(out.exists())

    def test_invalid_headers_and_truncated_blocks(self):
        variants=[]
        for off,value in ((0x32,0),(0x32,3),(0x33,0x76),(0x33,0x52),(0x82,89)):
            data=sadl();data[off]=value;variants.append(data)
        variants.append(sadl()[:-1])
        data=sadl();struct.pack_into('<I',data,0x40,len(data)-1);variants.append(data)
        data=sadl(loop=True);struct.pack_into('<I',data,0x54,len(data));variants.append(data)
        for data in variants:
            with self.subTest(header=data[:0x88].hex()):
                result,out=self.run_tool(data)
                self.assertNotEqual(result.returncode,0)
                self.assertFalse(out.exists())

    def test_plain_text_is_not_sadl(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'dialogue.txt'
            source.write_text('sadly, a text line is not an audio header.' * 8)
            result=subprocess.run([str(BIN),'FILETYPE',str(source)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertNotIn('SADL',result.stdout)

    def test_output_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'sample.sad';src.write_bytes(sadl())
            blocker=root/'blocker';blocker.write_text('existing file')
            result=subprocess.run([str(BIN),'DECOMPRESS',str(src),'-d',str(blocker/'out.wav'),
                                   '--no-passthrough'],capture_output=True,text=True)
            self.assertGreater(result.returncode,0,result.stderr)
            self.assertEqual(blocker.read_text(),'existing file')


if __name__ == '__main__':
    unittest.main()
