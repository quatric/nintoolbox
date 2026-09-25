"""Write a synthetic Tenchu: Shadow Assassins voice-line manifest (.hd): a
small slot table (some real, some sentinel/unrecorded) plus a matching
parameter table, in the exact layout confirmed against all 102 real .hd
samples on the retail disc (see project/src/lib-hdvoice.h): total_slots and
a per-file "group" header, total_slots x 8-byte slots (category u16,
variant u16, index i32 -- sequential 0..n_real-1 or -1), then n_real x
44-byte parameter records.
"""
import struct, sys

def be32(v):
    return struct.pack(">I", v & 0xffffffff)

slots = [
    (1, 1, 0),
    (4, 1, -1),
    (1, 2, 1),
    (4, 1, -1),
    (1, 1, 2),
]
total_slots = len(slots)
n_real = sum(1 for s in slots if s[2] != -1)

header = be32(total_slots) + be32(1)
slot_table = b"".join(struct.pack(">HHi", cat, var, idx) for cat, var, idx in slots)

param_table = b""
for i in range(n_real):
    rec = be32(i) + be32(127) + be32(0) + be32(0x3f800000) + be32(0) \
        + be32(77) + be32(10) + be32(0) + be32(0) + be32(1) + be32(0)
    assert len(rec) == 44
    param_table += rec

open(sys.argv[1], "wb").write(header + slot_table + param_table)
