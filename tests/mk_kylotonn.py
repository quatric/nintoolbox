import struct, sys, os
d = sys.argv[1]
files = [(b"config/a.cfg", b"hello world\n"), (b"movies/b.bin", bytes(range(200)))]
data = b""
offs = []
for n, c in files:
    offs.append((len(data) + 0x1c, len(c)))  # offset relative to 0x1c below
    data += c
ents = b""
for (n, c), (o, s) in zip(files, offs):
    ents += struct.pack("<8I", 0xffffffff, 0xffffffff, 0x12345678, 0xffffffff, 0, s, o - 0x1c, 0)
pool = b"".join(n + b"\0" for n, c in files) + b"\0\0"
text = b"TEXT" + struct.pack("<I", 0) + struct.pack("<II", len(files), 0) + ents
text = text[:4] + struct.pack("<I", len(text)) + text[8:]
tbuf = b"TBUF" + struct.pack("<I", len(pool) + 8) + pool
vers = b"VERS" + struct.pack("<II", 12, 0x1900)
name = b"NAME" + struct.pack("<I", 14) + b"\x01\x00\x02\x00\x00\x00"
sarr_body = vers + name + text + tbuf
# "TEXT" tag inside NAME payload is found first by the reader; mimic the real file by putting it there
name = b"NAME" + struct.pack("<I", 14) + b"\x01\x00\x02\x00\x00\x00TEXT"[:6]
sarr = b"SARR" + struct.pack("<I", len(sarr_body)) + sarr_body
payload_len = len(data) + 8
out = b"FVRT" + struct.pack("<I", 0x1d31f2aa) + b"VERS" + struct.pack("<II", 12, 0x1900) + b"FILE" + struct.pack("<I", payload_len) + data + sarr
open(os.path.join(d, "t.dat"), "wb").write(out)
