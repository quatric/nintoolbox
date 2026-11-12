#!/usr/bin/env python3
"""Synthetic XLink2 effect link database (XLNK version 0x24, 64-bit, little endian).

OUTFILE (e.g. t.belnk) holds one user "TestUser" with:
  local property "Mode"; call tables Root (switch on Mode, children 1-2), A (asset, condition
  Mode == 1, AssetName "SE_Test", Volume 0.5 from the direct value table) and B (one-shot asset,
  default condition, AssetName "B"); action slot "Slot" -> action "Act" -> one trigger on Root
  from frame 5 to 10.
"""
import struct, sys, zlib


def main(out):
    names = [b"", b"Mode", b"A", b"B", b"Root", b"Slot", b"Act", b"SE_Test", b"TestUser"]
    pool, off = b"", {}
    for s in names:
        off[s] = len(pool)
        pool += s + b"\0"

    pdt_names = b"\0AssetName\0Volume\0"
    # param define table at 0x70: 0x18 header (padded), two 0x18 asset param defines, strings
    pdt = struct.pack("<5I", 0x18 + 2 * 0x18 + len(pdt_names), 0, 2, 0, 0).ljust(0x18, b"\0")
    pdt += struct.pack("<QIIQ", 1, 4, 0, 0)  # AssetName: string, default ""
    pdt += struct.pack("<QIIQ", 11, 1, 0, 0)  # Volume: float (double default 0)
    pdt += pdt_names
    pdt_pos = 0x70
    asset_param_pos = (pdt_pos + len(pdt) + 7) & ~7
    params = struct.pack("<QII", 3, 1 << 24 | off[b"SE_Test"], 0)  # A: string + direct value 0
    params += struct.pack("<QI", 1, 1 << 24 | off[b"B"]).ljust(16, b"\0")  # B: string
    tow_pos = asset_param_pos + len(params)
    lp_pos = tow_pos
    lp = struct.pack("<Q", off[b"Mode"])
    dv = struct.pack("<f", 0.5)
    ex_pos = (lp_pos + len(lp) + len(dv) + 7) & ~7
    user_pos = ex_pos

    user = bytearray()
    user += struct.pack("<IhH9I", 0, 1, 0, 3, 2, 0, 1, 1, 1, 0, 0, 0)
    user += struct.pack("<IQ", 0, 0)  # padding, trigger table position (patched below)
    user += struct.pack("<Q", off[b"Mode"])  # local properties
    user += struct.pack("<3H", 0, 1, 2)  # sorted asset indices
    user += b"\0" * (-len(user) % 4)
    NONE = 0xFFFFFFFF
    # name, asset index, flags, emit count, parent, guid, key hash, pad, param/container, condition
    user += struct.pack("<QhHiiIIIQQ", off[b"Root"], -1, 1, 1, -1, 0x1000, 0, 0, 0, NONE)
    user += struct.pack("<QhHiiIIIQQ", off[b"A"], 0, 0, -1, 0, 0x1001, 0, 0, 0, 0)
    user += struct.pack("<QhHiiIIIQQ", off[b"B"], 1, 8, -1, 0, 0x1002, 0, 0, 16, NONE)
    # switch container: type, blend-by, pad, children 1..2, pad, variable, unknown, index, global, action
    user += struct.pack("<BBHiiIQihBB", 0, 0, 0, 1, 2, 0, off[b"Mode"], 0, 0, 0, 0)
    struct.pack_into("<Q", user, 0x30, len(user))
    user += struct.pack("<QhhI", off[b"Slot"], 0, 0, 0)  # action slot
    user += struct.pack("<QhBBi", off[b"Act"], 0, 0, 0, 0)  # action, triggers 0..0
    user += struct.pack("<IiQQiHHQ", 0x2000, 0, 0, 5, 10, 0, 0, NONE)  # frames 5..10, call table 0

    cond_pos = user_pos + len(user)
    cond = struct.pack("<IBBBBIi", 0, 1, 0, 0, 0, 0, 1)  # switch: s32 == 1
    name_pos = cond_pos + len(cond)
    size = name_pos + len(pool)

    head = struct.pack("<4sIIIII", b"XLNK", size, 0x24, 3, 3, 0)
    head += struct.pack("<QQ", tow_pos, lp_pos)
    head += struct.pack("<6I", 1, 0, 1, 0, 0, 0)
    head += struct.pack("<QII", ex_pos, 1, 0)
    head += struct.pack("<QQ", cond_pos, name_pos)
    head += struct.pack("<I", zlib.crc32(b"TestUser")).ljust(8, b"\0")
    head += struct.pack("<Q", user_pos)
    assert len(head) == pdt_pos
    data = head + pdt
    data += b"\0" * (asset_param_pos - len(data))
    data += params + lp + dv
    data += b"\0" * (ex_pos - len(data))
    data += bytes(user) + cond + pool
    assert len(data) == size
    with open(out, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main(sys.argv[1])
