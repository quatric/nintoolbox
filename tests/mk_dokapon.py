#!/usr/bin/env python3
# Synthetic Dokapon Kingdom GAME.PAH/GAME.PAC pair (two members) and a 4x4 8bpp PIM texture
import struct, sys, os
d = sys.argv[1]
mem = [(b'A.BIN', b'first member'), (b'B.PIM', None)]
pal = b''.join(bytes([i, 0, 0, 0x80]) for i in range(256))
pim = struct.pack('<8H', 4, 4, 8, 256, 16, 0, 1040, 0) + pal + bytes([1] * 16)
mem[1] = (mem[1][0], pim)
pac = b''
ents = []
names = b''
tab = 8 + 0  # entry table follows the 8-byte header
nbase = tab + 16 * len(mem)
for n, data in mem:
    ents.append((len(pac), len(data), 0, nbase + len(names)))
    names += n + b'\0'
    pac += data + bytes(-len(pac + data) % 16)
pah = struct.pack('<II', len(mem), tab) + b''.join(struct.pack('<IIII', *e) for e in ents) + names
open(os.path.join(d, 'GAME.PAH'), 'wb').write(pah)
open(os.path.join(d, 'GAME.PAC'), 'wb').write(pac)
open(os.path.join(d, 't.PIM'), 'wb').write(pim)
