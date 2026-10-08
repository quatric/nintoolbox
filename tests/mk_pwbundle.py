#!/usr/bin/env python3
# Synthetic Pipeworks bundle v1.03: one "Binary" type, one text member.
import struct, sys, os
out = sys.argv[1]
text = b'<VERSION>\r\n1.0\r\n'
b = bytearray(0x200)
banner = b'Pipeworks bundle v1.03 (big endian)   '
b[:len(banner)] = banner
struct.pack_into('>IIIIIIIII', b, 0x2c, 0, 1, 0x54, 0x10, 1, 0x60, 0, 0x6c, 0)
b[0x54:0x54 + 9] = b'\x10\x00Binary\x00'
data_off = 0x80
struct.pack_into('>III', b, 0x60, data_off, len(text), 0x12345678)
b[data_off:data_off + len(text)] = text
open(os.path.join(out, 'p.bdg'), 'wb').write(bytes(b))
