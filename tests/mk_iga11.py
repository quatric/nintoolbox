#!/usr/bin/env python3
# Synthetic Skylanders Trap Team IGA (version 11): one stored FSB5-like member and one little-endian-chunk LZMA member, hash named. -> V11.arc, V11.expect
import lzma, os, struct, sys
out = sys.argv[1]
stored = b'FSB5' + bytes(range(60))
payload = (b'IGZ\x01 trap team lzma member ' * 2000)[:0x8000 + 1500]

def lzma_chunk(data):
    filt = [{'id': lzma.FILTER_LZMA1, 'dict_size': 0x8000, 'lc': 3, 'lp': 0, 'pb': 2}]
    enc = lzma.compress(data, format=lzma.FORMAT_ALONE, filters=filt)
    return struct.pack('<H', len(enc) - 13) + enc[:5] + enc[13:]

def pad(b): return b + b'\0' * (-len(b) % 0x800)
d0 = pad(stored)
d1 = pad(lzma_chunk(payload[:0x8000])) + pad(lzma_chunk(payload[0x8000:]))
buf = bytearray(0x800)
buf[0:4] = b'IGA\x1a'
struct.pack_into('<5I', buf, 4, 11, 0x40, 2, 0x800, 0)
hashes = (0x11111111, 0x22222222)
struct.pack_into('<2I', buf, 0x38, *hashes)
struct.pack_into('<4I', buf, 0x40, 0x800, 0, len(stored), 0xffffffff)
struct.pack_into('<4I', buf, 0x50, 0x800 + len(d0), 0, len(payload), 0x20000000)
open(os.path.join(out, 'V11.arc'), 'wb').write(bytes(buf) + d0 + d1)
open(os.path.join(out, 'V11.expect'), 'wb').write(payload)
