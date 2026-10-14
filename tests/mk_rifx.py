#!/usr/bin/env python3
# Synthetic big-endian RIFX WAVE t.wax: four 16-bit mono PCM samples at 48000 Hz.
import struct, sys, os
pcm = struct.pack('>4h', 1, -2, 0x1234, -0x1234)
fmt = struct.pack('>HHIIHH', 1, 1, 48000, 96000, 2, 16)
body = b'WAVE' + b'fmt ' + struct.pack('>I', 16) + fmt + b'data' + struct.pack('>I', len(pcm)) + pcm
open(os.path.join(sys.argv[1], 't.wax'), 'wb').write(b'RIFX' + struct.pack('>I', len(body)) + body)
