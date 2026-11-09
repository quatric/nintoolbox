import struct, sys, os
d = sys.argv[1]
q = struct.pack(">4f", 0, 0, 0, 1) + struct.pack(">4f", 0.5, 0.5, 0.5, 0.5)
p = struct.pack(">3f", 1, 2, 3)
keys = struct.pack(">HHHH", 0, 0, 1, 1) * 2  # 2 frames x 2 nodes
body = b"Plane01".ljust(32, b"\0") + struct.pack(">5I", 2, 2, 30, len(q), len(p)) + q + p + keys
raw = struct.pack(">HI", 0x0403, len(body) + 4 + 2 - 2 + 0) + body
raw = struct.pack(">HI", 0x0403, 0) + body
raw = struct.pack(">HI", 0x0403, len(raw) - 2) + body
open(os.path.join(d, "t.anm"), "wb").write(raw)
