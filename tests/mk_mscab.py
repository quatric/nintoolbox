#!/usr/bin/env python3
# Synthetic Microsoft Cabinets: stored.cab (type 0) and mszip.cab (type 1,
# two CFDATA blocks so the 32 KiB deflate dictionary carry-over is exercised).
import struct, sys, os, zlib

def cab(files, blocks, ctype):
    # files: [(name, size, offset)]; blocks: [(comp_bytes, uncomp_size)]
    names = b''.join(struct.pack('<IIHHHH', sz, off, 0, 0, 0, 0x20) + n.encode() + b'\0'
                     for n, sz, off in files)
    coff_files = 36 + 8
    coff_data = coff_files + len(names)
    data = b''.join(struct.pack('<IHH', 0, len(c), u) + c for c, u in blocks)
    total = coff_data + len(data)
    hdr = struct.pack('<4sIIIIIBBHHHHH', b'MSCF', 0, total, 0, coff_files, 0, 3, 1, 1, len(files), 0, 1, 0)
    folder = struct.pack('<IHH', coff_data, len(blocks), ctype)
    return hdr + folder + names + data

out = sys.argv[1]
p1 = b'hello world, stored cabinet\n' * 3
open(os.path.join(out, 'stored.cab'), 'wb').write(
    cab([('a/one.txt', len(p1), 0)], [(p1, len(p1))], 0))

blk1 = bytes((i * 7) & 0xff for i in range(32768))
blk2 = blk1[:20000] + b'tail-of-file' * 50          # matches the first block
comp = []
dict_ = b''
for b in (blk1, blk2):
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9, zlib.Z_DEFAULT_STRATEGY, zdict=dict_[-32768:]) if dict_ else \
        zlib.compressobj(9, zlib.DEFLATED, -15)
    comp.append((b'CK' + c.compress(b) + c.flush(), len(b)))
    dict_ += b
allb = blk1 + blk2
open(os.path.join(out, 'mszip.cab'), 'wb').write(
    cab([('dir\\big.bin', len(allb), 0)], comp, 1))
open(os.path.join(out, 'expect.bin'), 'wb').write(allb)
