#!/usr/bin/env python3
# Synthetic Shin'en t.ogl: a short libvorbis stream (ffmpeg sine) re-framed as {u16 size << 2 | flags, packet}. Writes
# the reference stream as t.ref.ogg.
import struct, sys, os, subprocess
d = sys.argv[1]
ref = os.path.join(d, 't.ref.ogg')
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=0.5:sample_rate=22050', '-c:a', 'libvorbis',
                '-ac', '1', ref], check=True)
data = open(ref, 'rb').read()
packets, cur, last_gran, p = [], b'', 0, 0
while p < len(data):
    nl = data[p + 26]
    lace = data[p + 27:p + 27 + nl]
    body = p + 27 + nl
    gran = struct.unpack('<q', data[p + 6:p + 14])[0]
    if gran > 0:
        last_gran = gran
    for l in lace:
        cur += data[body:body + l]
        body += l
        if l < 255:
            packets.append(cur)
            cur = b''
    p = body
out = bytearray(struct.pack('<5I', 0, 0, 0, last_gran, 0))
for i, pk in enumerate(packets):
    out += struct.pack('<H', len(pk) << 2 | (1 if i == 0 else 0)) + pk
struct.pack_into('<I', out, 0x10, len(out))
out += bytes(16)
open(os.path.join(d, 't.ogl'), 'wb').write(out)
