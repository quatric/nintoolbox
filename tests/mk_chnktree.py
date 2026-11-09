import struct, sys, os
d = sys.argv[1]
def ch(tag, payload):
    return tag + struct.pack(">I", 8 + len(payload)) + payload
leaf = ch(b"HEAD", struct.pack(">II", 2, 0x3f800000))
scen = ch(b"SCEN", struct.pack(">I", 1) + leaf + b"SCEN")
root_payload = struct.pack(">I", 0) + scen + b"CHNK"
root = ch(b"CHNK", root_payload)
open(os.path.join(d, "t.scene"), "wb").write(b"CHNK" + root)
# lip-sync .phn: root payload {0, 1.0f, count} + PHNM chunk + terminator
phnm = ch(b"PHNM", struct.pack(">IIfII", 1, 0, 0.5, 0x4b, 7) + struct.pack(">ffII", 0.5, 1.5, 0x4b, 9) + b"PHNM")
root2 = ch(b"CHNK", struct.pack(">IfI", 0, 1.0, 2) + phnm + b"CHNK")
open(os.path.join(d, "t.phn"), "wb").write(b"CHNK" + root2)
# CAMS leaf: u32 version, 76 byte records, tail tag
rec = b"c_test".ljust(16, b"\0") + struct.pack(">3f", 1.5, 0.5, 300.0) + struct.pack(">12f", 1, 0, 0, 2, 0, 1, 0, 3, 0, 0, 1, 4)
cams = ch(b"CAMS", struct.pack(">I", 2) + rec + b"CAMS")
scen2 = ch(b"SCEN", struct.pack(">I", 1) + cams + b"SCEN")
root3 = ch(b"CHNK", struct.pack(">I", 0) + scen2 + b"CHNK")
open(os.path.join(d, "c.scene"), "wb").write(b"CHNK" + root3)
# NULO (locators) + ENTY (entity) leaves: name padded with '0' to the next 4 byte boundary after the NUL
def nm(s):
    b = s.encode() + b"\0"
    while len(b) % 4 or len(b) < len(s) + 2:
        b += b"0"
    return b
m12 = struct.pack(">12f", 1, 0, 0, 2, 0, 1, 0, 3, 0, 0, 1, 4)
nulo = ch(b"NULO", struct.pack(">I", 2) + nm("PathLoc_0") + m12 + nm("PathLoc_100") + m12 + b"NULO")
enty = ch(b"ENTY", struct.pack(">III", 3, 1, 1) + nm("jump_1") + struct.pack(">3f", 1, 1, 1) + m12 + struct.pack(">6f", -1, -1, -1, 1, 1, 1)
    + struct.pack(">III", 1, 13, 0) + nm("RND/MDL/a.brmdl") + b"ENTY")
head = ch(b"HEAD", struct.pack(">If", 2, 1.0) + nm("shop_in") + struct.pack(">6I", 3, 2, 0, 0, 3, 1) + b"HEAD")
spln = ch(b"SPLN", struct.pack(">I", 2) + b"BC01" + nm("RND/SCN/a.spline") + b"trck" + nm("RND/SCN/b.spline") + b"SPLN")
scen3 = ch(b"SCEN", struct.pack(">I", 2) + head + spln + nulo + enty + b"SCEN")
root4 = ch(b"CHNK", struct.pack(">I", 0) + scen3 + b"CHNK")
open(os.path.join(d, "e.scene"), "wb").write(b"CHNK" + root4)
# ".spline" file: SPLN version 1 {name[4], count, count x xyz}
pts = struct.pack(">6f", 0, 0, 0, 1, 2, 3)
sp = ch(b"SPLN", struct.pack(">I", 1) + b"trck" + struct.pack(">I", 2) + pts + b"SPLN")
open(os.path.join(d, "t.spline"), "wb").write(b"CHNK" + ch(b"CHNK", struct.pack(">II", 0, 1) + sp + b"CHNK"))
# BSTR (BSP tree): bounds, 2 nodes, 3 leaves (child >= 2 = leaf + 2, -1 = empty), 4 indices
bsp_body = struct.pack(">I", 3) + struct.pack(">4i", 1, 2, 3, -1) + struct.pack(">6I", 2, 0, 1, 2, 1, 3) + struct.pack(">4I", 7, 8, 9, 10) + b"BSPD"
bspd = ch(b"BSPD", bsp_body)
bstr = ch(b"BSTR", struct.pack(">I", 2) + struct.pack(">6f", -1, -2, -3, 1, 2, 3) + struct.pack(">4I", 2, 3, 4, len(bsp_body) + 4 - 4 + 4) + bspd + b"BSTR")
open(os.path.join(d, "b.scene"), "wb").write(b"CHNK" + ch(b"CHNK", struct.pack(">I", 0) + ch(b"SCEN", struct.pack(">I", 1) + bstr + b"SCEN") + b"CHNK"))
