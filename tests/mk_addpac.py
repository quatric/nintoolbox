#!/usr/bin/env python3
# Synthetic Natsume "add" PAC (Gintama, big-endian): one stored member and one LZS member named "*z".
import struct, sys, os
out = sys.argv[1]

def lzs(data, unit_bits, nb=5, thr=1):
    u = unit_bits // 8
    units = [data[i:i + u] for i in range(0, len(data), u)]
    toks = []; i = 0
    while i < len(units):
        # a run of the previous unit becomes one match (distance 1)
        run = 0
        while i + run < len(units) and i > 0 and units[i + run] == units[i - 1] and run < (1 << nb) - 1 + thr:
            run += 1
        if run >= thr + 1:
            toks.append(('M', 1, run)); i += run
        else:
            toks.append(('L', units[i])); i += 1
    comd = b''; raw = b''
    for g in range(0, len(toks), 16):
        grp = toks[g:g + 16]; flags = 0; mw = b''
        for k, t in enumerate(grp):
            if t[0] == 'L':
                flags |= 1 << k; raw += t[1]
            else:
                mw += struct.pack('>H', (t[1] << nb) | (t[2] - thr))
        comd += struct.pack('>H', flags) + mw
    name = b'a.brtex\0'
    hdr_len = 0x20 + len(name)
    hdr = bytearray(0x20)
    hdr[0:4] = b'LZS\0'; hdr[4] = 5; hdr[5] = nb; hdr[6] = unit_bits; hdr[7] = 1
    struct.pack_into('>I', hdr, 8, hdr_len)
    struct.pack_into('>I', hdr, 0xc, hdr_len + len(comd))
    struct.pack_into('>I', hdr, 0x10, len(data))
    struct.pack_into('>I', hdr, 0x14, hdr_len + len(comd) + len(raw))
    hdr[0x19] = thr
    return bytes(hdr) + name + comd + raw

plain = b'bres\xfe\xff\x00\x00' + b'\x00' * 16 + b'ABCDEFGH'
members = [('s.bin', b'stored-bytes!!'), ('a.brtexz', lzs(plain, 16)), ('b.anmz', lzs(plain[:24] + b'xy' * 3, 8))]
names = b''.join(n.encode() + b'\0' for n, _ in members)
tab = 0x20; nm_off = tab + 0x20 * len(members)
data_off = (nm_off + len(names) + 0x1f) & ~0x1f
buf = bytearray(data_off)
pos = data_off; ents = []
noff = nm_off
for n, d in members:
    ents.append((pos, len(d), noff)); buf += d; pos += len(d); pad = -pos % 0x20; buf += b'\0' * pad; pos += pad
    noff += len(n) + 1
buf[nm_off:nm_off + len(names)] = names
struct.pack_into('>8I', buf, 0, 0x61646400, 0x04010000, 0x20, 0x20, len(members), len(buf), 0, 0)
for i, (o, s, no) in enumerate(ents):
    struct.pack_into('>IIIIIIII', buf, tab + 0x20 * i, o, s, 0, 0x55f3be46, no, 0, 0, 0)
open(os.path.join(out, 't.pac'), 'wb').write(bytes(buf))
open(os.path.join(out, 'plain.bin'), 'wb').write(plain)
