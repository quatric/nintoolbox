import struct, sys, os
d = sys.argv[1]
pcm = struct.pack(">6h", 1, 2, 3, 4, 5, 6)
recs = struct.pack(">4I", 0x15622, 0, 4, 0) + struct.pack(">4I", 0x15622, 4, 2, 0)
head = 0x18
end = head + 8 + len(recs)  # one dummy 8 byte section then the records
wt = struct.pack(">6I", head, head, head, head, head + 8, end) + bytes(8) + recs
open(os.path.join(d, "t.wt"), "wb").write(wt)
open(os.path.join(d, "t.pcm"), "wb").write(pcm)
# ADPCM variant: one sample of 14 DSP-ADPCM nibbles 1..7,1..7 with zero coefficients (output = the nibbles), start in nibbles
apcm = bytes([0x00, 0x12, 0x34, 0x56, 0x71, 0x23, 0x45, 0x67])
rec = struct.pack(">4I", 0x15622, 0, 14, 0)
awt = struct.pack(">6I", head, head, head, head, head, head + 16) + rec + bytes(46)
open(os.path.join(d, "a.wt"), "wb").write(awt)
open(os.path.join(d, "a.pcm"), "wb").write(apcm)
