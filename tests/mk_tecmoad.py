#!/usr/bin/env python3
# Synthetic Tecmo resource pack t.mad: two members (tgs, XMDF) and one unused slot, offsets deliberately unsorted.
import struct, sys, os
b = bytearray(0x60)
b[0:4] = b'MAD\0'
struct.pack_into('>IIIII', b, 8, 3, 0, len(b), 0x20, 0x20)
struct.pack_into('>III', b, 0x20, 0x50, 0, 0x30)   # XMDF at 0x50..0x60 listed first, tgs at 0x30..0x50
b[0x30:0x34] = b'tgs\0'
b[0x50:0x54] = b'XMDF'
open(os.path.join(sys.argv[1], 't.mad'), 'wb').write(b)
