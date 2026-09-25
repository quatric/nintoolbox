"""Write a synthetic Battle of the Bands ".bag" asset container: the fixed
32-byte "1.00 <N>\\n" text header, a zlib-compressed blob, and a trailing
plain-text manifest record ("name,size,offset\\n" bracketed by '*' runs)
naming that blob -- the layout confirmed against all 215 real .bag samples
on the retail disc (see project/src/lib-botbbag.h).
"""
import struct, sys, zlib

blob = zlib.compress(b"BOTB synthetic payload bytes for regress" * 8, 6)

manifest = b"*" * 24 + b"asset.xmb," + str(len(blob)).encode() + b",32\n" + b"*" * 24

payload = blob + manifest
header = b"1.00 " + str(len(payload)).encode() + b"\n"
header = header + bytes(32 - len(header))

open(sys.argv[1], "wb").write(header + payload)
