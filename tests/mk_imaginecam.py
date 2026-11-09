import struct, sys, os
d = sys.argv[1]
m = struct.pack(">12f", 1, 0, 0, 5, 0, 1, 0, 6, 0, 0, 1, 7)
open(os.path.join(d, "t.cam"), "wb").write(struct.pack(">I", 2) + m + m)
