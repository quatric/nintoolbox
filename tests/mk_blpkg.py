#!/usr/bin/env python3
"""Synthetic Barking Lizards Technologies "pkg\\0" archive (see
project/src/lib-blpkg.h): "hello.lua" (gzip-compressed) + "data.bin"
(stored, uncompressed). usage: mk_blpkg.py OUTDIR -> OUTDIR/test.pkg"""
import gzip, io, os, struct, sys

lua = b'print("hello from blpkg")\n' * 4
stored = b'stored payload bytes\n'


def gzip_member(data):
	buf = io.BytesIO()
	# mtime=0 for a reproducible fixture; wszst only cares about the
	# standard gzip header/trailer, not the timestamp.
	with gzip.GzipFile(fileobj=buf, mode='wb', mtime=0) as f:
		f.write(data)
	return buf.getvalue()

gz = gzip_member(lua)

HEADER_SIZE = 16
STATS_SIZE = 32
ENTRY_SIZE = 32 + 20
n_entries = 2

table_end = HEADER_SIZE + STATS_SIZE + n_entries * ENTRY_SIZE
off_lua = table_end
off_stored = off_lua + len(gz)

def name32(s):
	b = s.encode('ascii')
	assert len(b) <= 32
	return b.ljust(32, b'\0')

entries = [
	# (name, type, decompressed_size, compressed_size, offset)
	('hello.lua', 1, len(lua), len(gz), off_lua),
	('data.bin', 0, len(stored), len(stored), off_stored),
]

stats = struct.pack('>8I',
	len(lua) + len(stored),   # total decompressed
	len(gz) + len(stored),    # total compressed
	max(len(lua), len(stored)),
	len(gz),                  # max compressed size among gzip members only
	3, 0, 0, 0)

out = b'pkg\0' + struct.pack('>3I', 0, n_entries, 1) + stats
for i, (name, typ, dsize, csize, off) in enumerate(entries):
	out += name32(name)
	out += struct.pack('>5I', (i << 16) | typ, dsize, csize, off, 0)
assert len(out) == table_end

out += gz + stored

os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.pkg'), 'wb').write(out)
