#!/usr/bin/env python3
"""Synthetic Toshi subtitle library (Nickelodeon Barnyard Wii, Data/Subs/<lang>/lib_*.trb): two sections in one TCES
hunk (section 0 = 16-byte "SubtitleVersion", section 1 = one symbol per sound {s32 line count, count x {s32 start ms,
s32 end ms, u32 ptr}} plus UTF-16BE strings), XRDH lists the section sizes.
usage: mk_toshi_subs.py OUTDIR  ->  OUTDIR/lib_test.trb"""
import os, struct, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
B = lambda *a: struct.pack('>%dI' % len(a), *a)
s0 = B(256, 0, 0, 0)
texts = ['Hello there!', 'Moo?']
# section 1: entries first (16 bytes each: count, start, end, ptr), then the strings
ent_size = 16 * len(texts)
sect1 = bytearray(ent_size)
pos = ent_size
spans = [(0, 1500), (1500, 2600)]
for i, t in enumerate(texts):
    sect1[16 * i:16 * i + 16] = B(1, spans[i][0], spans[i][1], pos)
    e = t.encode('utf-16-be') + b'\0\0'
    sect1 += e; pos += len(e)
names = [(b'SubtitleVersion', 0, 0), (b'snd_a', 1, 0), (b'snd_b', 1, 16)]
symb = B(len(names)); nm = b''; ents = b''
for n, sec, off in names:
    ents += struct.pack('>HHII', sec, len(nm), 0, off); nm += n + b'\0'
symb += ents + nm
def hunk(tag, pl): return tag + B(len(pl)) + pl + bytes(-len(pl) & 3)
xrdh = B(0x10001, 2) + B(0, len(s0), 0, 0) + B(0, len(sect1), 0, 0)
body = b'FBRT' + hunk(b'XRDH', xrdh) + hunk(b'TCES', s0 + bytes(sect1)) + hunk(b'BMYS', symb)
open(out + '/lib_test.trb', 'wb').write(b'TSFB' + B(len(body)) + body + bytes(1024))
