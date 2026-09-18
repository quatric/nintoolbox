#!/usr/bin/env python3
"""Build synthetic BNTX fixtures for the LegacySwitchLibraries ports.

  multi.bntx : 2-texture container (2nd BRTI is a byte copy of the 1st,
               sharing its name/data; mirrors BntxFile.Textures list use).
  ud.bntx    : single texture + per-texture UserData (Bntx UserData.cs):
               entry 0 is STRING ["hello","world!"], entry 1 is WSTRING ["Hie"].
               Payloads are `count` u64 offsets to u16-length-prefixed,
               NUL-terminated strings (BntxFileLoader.LoadStrings).

Usage: mk_bntx_extra.py base.bntx multi.bntx ud.bntx
(base.bntx comes from `wimgt ENCODE base.png`.)
"""
import struct
import sys


def u32(d, o):
    return struct.unpack('<I', d[o:o + 4])[0]


def u64(d, o):
    return struct.unpack('<Q', d[o:o + 8])[0]


def pad8(b):
    while len(b) % 8:
        b += b'\x00'
    return b


def ustr(s: bytes):
    return struct.pack('<H', len(s)) + s + b'\x00'


def wstr(s: str):
    enc = s.encode('utf-16-le')
    return struct.pack('<H', len(enc) // 2) + enc + b'\x00\x00'


def main():
    base_p, multi_p, ud_p = sys.argv[1:4]
    base = bytearray(open(base_p, 'rb').read())
    tc = 0x20
    assert u32(base, tc + 4) == 1, 'base must hold exactly 1 texture'
    info_ptrs = u64(base, tc + 8)
    blk = u64(base, info_ptrs)
    assert base[blk:blk + 4] == b'BRTI'
    BRTI_LEN = 16 + 0x90

    # ---- multi.bntx ----
    m = bytearray(base)
    new_brti = len(m)
    m += bytes(m[blk:blk + BRTI_LEN])
    m = pad8(m)
    new_ptrs = len(m)
    m += struct.pack('<QQ', blk, new_brti)
    struct.pack_into('<I', m, tc + 4, 2)
    struct.pack_into('<Q', m, tc + 8, new_ptrs)
    struct.pack_into('<I', m, 28, len(m))
    open(multi_p, 'wb').write(bytes(m))

    # ---- ud.bntx ----
    b = bytearray(base)
    ti = blk + 16
    assert u64(b, ti + 0x68) == 0 and u64(b, ti + 0x88) == 0
    b = pad8(b)
    ud_arr = len(b)
    b += b'\x00' * (2 * 0x40)
    b = pad8(b)
    ud_dict = len(b)
    b += b'_DIC' + struct.pack('<I', 2) + b'\x00' * ((2 + 1) * 16)
    name_str = len(b)
    b += ustr(b'test_str')
    name_wstr = len(b)
    b += ustr(b'test_wstr')
    b = pad8(b)
    str_payload = len(b)
    b += b'\x00' * 16
    s0 = len(b)
    b += ustr(b'hello')
    s1 = len(b)
    b += ustr(b'world!')
    b = pad8(b)
    wstr_payload = len(b)
    b += b'\x00' * 8
    w0 = len(b)
    b += wstr('Hi\u00e9')
    b = pad8(b)

    struct.pack_into('<Q', b, str_payload, s0)
    struct.pack_into('<Q', b, str_payload + 8, s1)
    struct.pack_into('<Q', b, wstr_payload, w0)

    struct.pack_into('<Q', b, ud_arr, name_str)
    struct.pack_into('<Q', b, ud_arr + 8, str_payload)
    struct.pack_into('<I', b, ud_arr + 0x10, 2)
    b[ud_arr + 0x14] = 2  # STRING
    struct.pack_into('<Q', b, ud_arr + 0x40, name_wstr)
    struct.pack_into('<Q', b, ud_arr + 0x48, wstr_payload)
    struct.pack_into('<I', b, ud_arr + 0x50, 1)
    b[ud_arr + 0x54] = 4  # WSTRING

    struct.pack_into('<Q', b, ti + 0x68, ud_arr)
    struct.pack_into('<Q', b, ti + 0x88, ud_dict)
    struct.pack_into('<I', b, 28, len(b))
    open(ud_p, 'wb').write(bytes(b))
    print(f'wrote {multi_p} ({len(m)} bytes) {ud_p} ({len(b)} bytes)')


main()
