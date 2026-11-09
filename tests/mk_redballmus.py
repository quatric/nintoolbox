import struct, sys, os
d = sys.argv[1]
coef = struct.pack(">16h", *([0] * 16))
frames = bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77]) * 2
hdr = b"\xab" * 4 + coef + struct.pack(">III", 1, 0, len(frames) // 2)
open(os.path.join(d, "t.mus"), "wb").write(hdr + frames)
