#!/usr/bin/env python3
"""Extract Blitz Games "Babel" .rev packages (SpongeBob: Creature from the
Krusty Krab, Wii) -- Packages_Rev/*.rev and AudioRev/*.rev.

Layout (big endian):
  0x00 u32 id            0x04 u32 align (0x20 or 0x800; offsets are in units of it)
  0x0c u32 file count    0x10 u32 index offset (32-byte entries, sorted by CRC)
  0x28 u32 name-table offset  0x2c u32 name-table length (NUL separated strings)
Index entry: u32 offset, u32 name CRC, u32 size, u32 size2, u32 1, u32 4, u64 FILETIME.
Name CRC = MSB-first CRC32 (poly 04C11DB7, init 0, no xor) of the lowercased name.
Names in the name table are in file order, not CRC order; unnamed files are
written as <crc>.bin.

usage: rev_extract.py [-l] OUTDIR|- file.rev...
"""
import os, struct, sys

_T = []
for _i in range(256):
    _c = _i << 24
    for _ in range(8):
        _c = ((_c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if _c & 0x80000000 else (_c << 1) & 0xFFFFFFFF
    _T.append(_c)


def name_crc(s):
    c = 0
    for b in s.lower():
        c = ((c << 8) & 0xFFFFFFFF) ^ _T[(c >> 24) ^ b]
    return c


def parse(d):
    h = struct.unpack('>13I', d[:52])
    mul, n, io, no, nl = h[1], h[3], h[4] * h[1], h[10] * h[1], h[11]
    ents = [struct.unpack('>6IQ', d[io + i * 32:io + i * 32 + 32]) for i in range(n)]
    by = {e[1]: e for e in ents}
    order, used = [], set()
    for nm in d[no:no + nl].split(b'\0'):
        e = by.get(name_crc(nm)) if nm else None
        if e and e[1] not in used:
            used.add(e[1])
            order.append((nm.decode('latin1'), e))
    for e in ents:
        if e[1] not in used:
            order.append(('%08x.bin' % e[1], e))
    return mul, order


def main(a):
    lst = a[0] == '-l'
    if lst:
        a = a[1:]
    out, files = a[0], a[1:]
    for f in files:
        d = open(f, 'rb').read()
        mul, order = parse(d)
        base = os.path.join(out, os.path.splitext(os.path.basename(f))[0])
        for nm, e in order:
            off, size = e[0] * mul, e[2]
            if lst:
                print('%s\t%08x\t%d\t%s' % (os.path.basename(f), off, size, nm))
                continue
            p = os.path.join(base, nm.replace('\\', '/').lstrip('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(d[off:off + size])


if __name__ == '__main__':
    main(sys.argv[1:]) if len(sys.argv) > 2 else sys.exit(__doc__)
