"""Write a synthetic Tenchu: Shadow Assassins "T4-*" tagged resource (.b):
the 12-byte "T4-<Name>\\0" tag field plus a 4-byte packed-BCD date, matching
the header confirmed against retail Common/Camera/*.b and Common/AI/*.b
samples (see project/src/lib-t4res.h). The record table after the header is
type-specific and not modeled here.
"""
import sys

name = b"T4-CamSet".ljust(12, b"\x00")
date = bytes([0x20, 0x08, 0x03, 0x14])
body = bytes(16)  # placeholder record table

open(sys.argv[1], "wb").write(name + date + body)
