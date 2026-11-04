#!/usr/bin/env python3
# Synthetic Microsoft Cabinet (MSCF) with one stored member, named like the
# extensionless-by-purpose .bin packs in Tales of Symphonia: Dawn of the New World.
import struct, sys, os
out = sys.argv[1]
payload = b'mscf cabinet member'
name = b'item.txt\0'
hdr_len, folder_len = 36, 8
coff_files = hdr_len + folder_len
file_ent = struct.pack('<IIHHHH', len(payload), 0, 0, 0x2100, 0, 0x20) + name
coff_data = coff_files + len(file_ent)
data = struct.pack('<IHH', 0, len(payload), len(payload)) + payload
total = coff_data + len(data)
header = b'MSCF' + struct.pack('<IIIIIBBHHHHH', 0, total, 0, coff_files, 0, 3, 1, 1, 1, 0, 0, 0)
folder = struct.pack('<IHH', coff_data, 1, 0)
open(os.path.join(out, 't.bin'), 'wb').write(header + folder + file_ent + data)
