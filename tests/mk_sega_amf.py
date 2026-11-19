#!/usr/bin/env python3
"""Synthetic Sakura Wars movie pack (.amf with an "AM??" magic, here AMMP; see project/src/wszst_cmd/extract/sega_res.inc) holding one
HMOT member made by mk_hmot.py. Header: magic, payload size, header size 0x30, flags 0x80000000, member count, table offset 0x1c, 0,
then the u32 member offset table.
usage: mk_sega_amf.py HMOT_FILE OUT.amf"""
import struct, sys
m = open(sys.argv[1], 'rb').read()
h = bytearray(0x30)
h[0:4] = b'AMMP'
struct.pack_into('<IIII', h, 4, len(m), 0x30, 0x80000000, 1)
struct.pack_into('<III', h, 0x14, 0x1c, 0, 0x30)
open(sys.argv[2], 'wb').write(bytes(h) + m)
