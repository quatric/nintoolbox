#!/usr/bin/env python3
# Synthetic Skylanders IGA with one chunked-LZMA member: an LZMA chunk, a
# stored (incompressible) 0x8000 chunk, then a short LZMA chunk. -> L.bld
import lzma, os, struct, sys, random
out = sys.argv[1]
random.seed(7)
a = b'IGZ\x01 chunked lzma member ' * 1500
a = a[:0x8000]
raw = bytes(random.randrange(256) for _ in range(0x8000))
c = b'tail of the member ' * 200
c = c[:3000]
payload = a + raw + c

def lzma_chunk(data):
    filt = [{'id': lzma.FILTER_LZMA1, 'dict_size': 0x8000, 'lc': 3, 'lp': 0, 'pb': 2}]
    enc = lzma.compress(data, format=lzma.FORMAT_ALONE, filters=filt)
    props, stream = enc[:5], enc[13:]
    return struct.pack('>H', len(stream)) + props + stream

def pad(b): return b + b'\0' * (-len(b) % 0x800)
data = pad(lzma_chunk(a)) + raw + pad(lzma_chunk(c))
name = b'level.dat\0'
names = struct.pack('<I', 4) + name
names_off = 0x800 + len(data)
n = 1
buf = bytearray(0x800)
buf[0:4] = b'IGA\x1a'
struct.pack_into('<7I', buf, 4, 4, 0x38, n, 0, 0, names_off, len(names))
struct.pack_into('<I', buf, 0x30, 0x1234)
struct.pack_into('<3I', buf, 0x34, 0x800, len(payload), 0x10000000)
open(os.path.join(out, 'L.bld'), 'wb').write(bytes(buf) + data + names)
open(os.path.join(out, 'L.expect'), 'wb').write(payload)
