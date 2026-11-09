import struct, sys, os
d = sys.argv[1]
pcm = struct.pack(">6h", 1, 2, 3, 4, 5, 6)
recs = struct.pack(">4I", 0x15622, 0, 4, 0) + struct.pack(">4I", 0x15622, 4, 2, 0)
head = 0x18
end = head + 8 + len(recs)  # one dummy 8 byte section then the records
wt = struct.pack(">6I", head, head, head, head, head + 8, end) + bytes(8) + recs
open(os.path.join(d, "t.wt"), "wb").write(wt)
open(os.path.join(d, "t.pcm"), "wb").write(pcm)
