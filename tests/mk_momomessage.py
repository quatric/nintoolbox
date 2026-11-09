#!/usr/bin/env python3
"""Synthetic Momotarou Dentetsu 16 message.bin. usage: mk_momomessage.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
g = lambda *v: b''.join(struct.pack('>H', x) for x in v)
b = g(*range(0x110, 0x11a)) + b'\x0c\x0f\x00' + g(*range(0x11a, 0x124)) + b'\x0c\x0f\x00' + g(*range(0x124, 0x128)) * 8
# hiragana a(0x15c) i(0x15d); katakana KA (0x1ad+5); 'B'; digit 3; space; '!'; kanji; command
b += b'\x0c\x0f\x00' + g(0x15c, 0x15d, 0x1ad + 5, 0x129, 0x113, 0x124, 0x125, 0x210) + b'\x0a\x11\x0c' + g(0x15e)
open(os.path.join(d, 'message.bin'), 'wb').write(b)
