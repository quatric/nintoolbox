#!/usr/bin/env python3
# Synthetic Atlus .pak (Trauma Center): 0x100-byte headers, 0x40-aligned payloads, an LZ10 member,
# a duplicate name and a 0x100-byte terminator.
import struct, sys, os
out = sys.argv[1]
def lz10(data):
    o = bytearray(b'\x10' + struct.pack('<I', len(data))[:3])
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        o += b'\x00' + chunk
    return bytes(o)
def entry(name, payload):
    h = name.encode().ljust(0xfc, b'\0') + struct.pack('<I', len(payload))
    b = h + payload
    return b + b'\0' * (-len(b) % 0x40)
pak = (entry('dir/a.txt', lz10(b'atlus pak member')) + entry('raw.txt', b'raw member')
       + entry('raw.txt', b'second raw') + b'\0' * 0x100)
open(os.path.join(out, 't.pak'), 'wb').write(pak)
