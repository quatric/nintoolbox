import struct, sys, os
d = sys.argv[1]
# decoded blob: table container {count 2, 3 zero words, offsets 0x20, 0x40}, member 0 = "AAAA..." (32 bytes), member 1 = 32 bytes
mem0 = b"ABCD" * 8
mem1 = bytes(range(32))
tab = struct.pack(">4I", 2, 0, 0, 0) + struct.pack(">II", 0x20, 0x40)
tab = tab.ljust(0x20, b"\0")
raw = tab + mem0 + mem1
# compress: literals, plus one match (len 6, distance 32) for the first 6 bytes of mem1? keep it simple: all literal except a run copy
out = bytearray(); flags = []
body = bytearray()
i = 0
toks = []
while i < len(raw):
    # try a match at distance 32 (mem0 repeats ABCD every 4 bytes: distance 4)
    best = 0
    for dist in (4, 32):
        l = 0
        while i + l < len(raw) and i - dist >= 0 and l < 17 and raw[i + l] == raw[i - dist + l]:
            l += 1
        if l >= 3 and l > best:
            best, bd = l, dist
    if best:
        toks.append(("m", best, bd)); i += best
    else:
        toks.append(("l", raw[i])); i += 1
stream = bytearray()
for k in range(0, len(toks), 32):
    grp = toks[k:k + 32]
    fl = 0; payload = bytearray()
    for j, t in enumerate(grp):
        if t[0] == "l":
            fl |= 1 << (31 - j); payload.append(t[1])
        else:
            v = ((t[1] - 2) << 12) | (t[2] - 1)
            payload += struct.pack(">H", v)
    stream += struct.pack(">I", fl) + payload
blob = b"cmps" + struct.pack(">II", 5, len(raw)) + stream
open(os.path.join(d, "t.bin"), "wb").write(blob.ljust((len(blob) + 0x7ff) & ~0x7ff, b"\0"))
