#!/usr/bin/env python3
"""Synthetic Nintendo RSO module (see project/src/lib-rso.h): one .text-like
payload section, one .data-like payload section, and one BSS (zero-offset,
no payload) section, plus a module name string. usage:
mk_rso.py OUTDIR -> OUTDIR/test.rso"""
import os, struct, sys

text = b'TEXT_SECTION_PAYLOAD\n'
data = b'data section payload\n'
name = b'C:\\build\\test.rso'
bss_size = 0x100

HDR = 0x20
num_sections = 3
section_info_offset = HDR
section_table_size = num_sections * 8

text_off = section_info_offset + section_table_size
data_off = text_off + len(text)
name_offset = data_off + len(data)
name_size = len(name)

hdr = struct.pack('>IIIIIIII',
    0,                    # next
    0,                    # prev
    num_sections,         # num_sections
    section_info_offset,  # section_info_offset
    name_offset,          # name_offset
    name_size,            # name_size
    1,                    # version
    bss_size)             # bss_size
assert len(hdr) == HDR

sections = struct.pack('>6I',
    text_off, len(text),  # payload section 0
    data_off, len(data),  # payload section 1
    0, bss_size)          # BSS section: offset 0, no on-disk payload
assert len(sections) == section_table_size

out = hdr + sections + text + data + name

os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.rso'), 'wb').write(out)
