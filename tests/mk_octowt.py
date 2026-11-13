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
rec = struct.pack(">4I", 0x5622, 0, 14, 0)  # rate word bit 16 clear: DSP-ADPCM record (set = plain PCM16, as in gm16pcm.wt)
awt = struct.pack(">6I", head, head, head, head, head, head + 16) + rec + bytes(46)
open(os.path.join(d, "a.wt"), "wb").write(awt)
open(os.path.join(d, "a.pcm"), "wb").write(apcm)
# instrument maps: drum kit row 0 (key 36 -> region 0), program 3 key 60 -> region 0, one region
s0 = bytearray(b"\xff" * 0x8000); struct.pack_into(">H", s0, 2 * 36, 0)
s1 = bytearray(0x8000); struct.pack_into(">H", s1, 256 * 3 + 2 * 60, 0)
reg = struct.pack(">BBhhHIIII", 60, 0, -4, -100, 0, 1, 0, 0, 0)
o = [0x18, 0x8018, 0x10018, 0x10030, 0x10030]
mrec = struct.pack(">4I", 0x15622, 0, 2, 0)
mwt = struct.pack(">6I", *o, o[4] + 16) + bytes(s0) + bytes(s1) + reg + mrec
open(os.path.join(d, "m.wt"), "wb").write(mwt)
open(os.path.join(d, "m.pcm"), "wb").write(struct.pack(">2h", 7, 8))
