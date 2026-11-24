#!/usr/bin/env python3
"""Synthetic SEGA GHSL tree (Sakura Wars .bin; see project/src/wszst_cmd/extract/sega_ghsl.inc): ROOT > ITEM {nVal=300, sTxt="hi"} with the plain value children 5 and 7.
usage: mk_sega_ghsl.py OUT.bin"""
import struct, sys
names = [b'ROOT', b'ITEM', b'nVal', b'sTxt', b'hi']
# item node: header (1 + 3 name/A/B bytes), attributes, children (values), end token
attrs = bytes([0x44, 2, 0x2c, 0x01]) + bytes([0x42, 3, 4])       # nVal = 300 (2 bytes), sTxt = string 4
kids = bytes([0x80, 5, 0x80, 7]) + bytes([0xc1])
item_hdr_len = 4
a = item_hdr_len + len(attrs)
item_len = a + len(kids)
item = bytes([0x00, 1, a, item_len]) + attrs + kids
root_a = 4
root_len = root_a + len(item) + 1
root = bytes([0x00, 0, root_a, root_len]) + item + bytes([0xc1])
stream = root
tabpos = 0x14 + len(stream)
tab = struct.pack('<I', len(names))
off = 4 + 4 * len(names)
offs = []
strs = b''
for n in names:
    offs.append(off + len(strs)); strs += n + b'\0'
tab += b''.join(struct.pack('<I', o) for o in offs) + strs
body = struct.pack('<I', tabpos - 0x10) + stream + tab
hdr = b'GHSL' + struct.pack('<III', 0x10 + len(body), 0x10, 0)
open(sys.argv[1], 'wb').write(hdr + body)

# optional second file: the same tree encrypted as IF/ECATCH/gameinfo.bin is (header flag 0x200, key 0x7f),
# the inverse of sega_ghsl_decrypt, with the substitution table taken from the extractor source
if len(sys.argv) > 2:
    import os, re
    src = open(os.path.join(os.path.dirname(__file__), '../project/src/wszst_cmd/extract/sega_ghsl.inc')).read()
    sbox = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', src.split('sega_ghsl_sbox[3 * 256] = {')[1].split('};')[0])]
    inv = [[0] * 256 for _ in range(3)]
    for k in range(3):
        for x in range(256):
            inv[k][sbox[k * 256 + x]] = x
    y = bytearray(body)
    for i in range(1, len(y)):
        y[i] = (y[i] + y[i - 1] + 0x7f + i) & 0xff
    enc = bytes(inv[i % 3][v] for i, v in enumerate(y))
    open(sys.argv[2], 'wb').write(b'GHSL' + struct.pack('<III', len(body), 0x10, 0x200) + enc)
