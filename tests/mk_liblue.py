#!/usr/bin/env python3
"""Synthetic Lost in Blue: Shipwrecked files: t.bin (WII scene archive with two members) and t.zms (one slot, one
Shift-JIS message "Hi" + newline + "OK"). usage: mk_liblue.py OUTDIR"""
import os, struct, sys

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
mem = [(b'tModel.bin', b'model data!'), (b'tSnd.bin', b'sound data')]
names = b''.join(n.ljust(0x10, b'\0') for n, _ in mem)
data = b''
for n, c in mem:
    data += c.ljust((len(c) + 0x1f) & ~0x1f, b'\0')
hdr = b'WII\0' + struct.pack('>fII', 1.0, len(mem), 2) + bytes(8 * len(mem)) + names
# member data must start behind the name table
base = (len(hdr) + 0x1f) & ~0x1f
ents = b''
pos = base
for n, c in mem:
    ents += struct.pack('>II', pos, len(c))
    pos += (len(c) + 0x1f) & ~0x1f
hdr = (b'WII\0' + struct.pack('>fII', 1.0, len(mem), 2) + ents + names).ljust(base, b'\0')
open(os.path.join(d, 't.bin'), 'wb').write(hdr + data)

text = b'Hi\nOK\0'
z = bytearray(0x40)
z[0:8] = b'ZMS\0WII\0'
struct.pack_into('>f', z, 8, 1.0)
z[0x20] = 1
struct.pack_into('>II', z, 0x24, 0x20, 1)
slot_tab = 0x40 - 0x20 + 8   # slot table entry sits at 0x40, the message table right behind it
z += struct.pack('>II', slot_tab, 0)           # slot 0 -> message table
z += struct.pack('>II', 0x40 + 8 + 8 - 0x20, 0)  # message 0 -> text right behind the table
z += text
open(os.path.join(d, 't.zms'), 'wb').write(bytes(z))
