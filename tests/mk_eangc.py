#!/usr/bin/env python3
# Synthetic EA GameCube stream: SCHl header (R3, mono, 44100 Hz) + one SCDl
# block holding a single raw (0xEE) 28-sample frame with samples 1..28.
import struct, sys, os
out = sys.argv[1]
patch = b'\xfd\x80\x01\x03\x82\x01\x01\x84\x02\xac\x44\xff'
schl_body = b'GSTR' + patch
schl_body += b'\0' * (-(len(schl_body) + 8) % 4)
schl = b'SCHl' + struct.pack('<I', 8 + len(schl_body)) + schl_body
frame = b'\xee' + struct.pack('>hh', 0, 0) + b''.join(struct.pack('>h', i + 1) for i in range(28))
pkt = struct.pack('>II', 28, 0) + frame
pkt += b'\0' * (-(len(pkt) + 8) % 4)
scdl = b'SCDl' + struct.pack('<I', 8 + len(pkt)) + pkt
open(os.path.join(out, 't.ngc'), 'wb').write(schl + scdl)
