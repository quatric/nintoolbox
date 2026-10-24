# Synthetic Capcom MT Framework effect sprite animation ("\0NAE") with one 3 frame animation as t.ean.
import struct, sys, os
d = sys.argv[1]
rects = b"".join(struct.pack(">HHHH", x, 0, 32, 32) for x in (0, 32, 64))
rec = struct.pack(">IHHIIIIII", 0x20, 3, 0, 0x30, 0x30, 0, 0, 0x30003, 0x200020)
body = rec + rects
f = b"\0NAE" + struct.pack(">III", 0x20060501, len(body) , 1) + body
open(os.path.join(d, "t.ean"), "wb").write(f)
