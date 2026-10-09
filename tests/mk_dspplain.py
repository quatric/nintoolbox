#!/usr/bin/env python3
# Plain mono GameCube DSP-ADPCM file named ".wav" (Wacky Races: Crash & Dash) as t.wav.
import struct, sys, os
d = sys.argv[1]
frames = 20
h = bytearray(0x60)
struct.pack_into('>III', h, 0, frames * 14, frames * 16, 32000)
struct.pack_into('>III', h, 0x10, 2, frames * 16 - 1, 2)
struct.pack_into('>I', h, 0x1c, 0x030600d6)
open(os.path.join(d, 't.wav'), 'wb').write(bytes(h) + bytes([0x35]) * (frames * 8))
