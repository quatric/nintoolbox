import struct, sys, os
d = sys.argv[1]
def ch(tag, payload):
    return tag + struct.pack(">I", 8 + len(payload)) + payload
leaf = ch(b"HEAD", struct.pack(">II", 2, 0x3f800000))
scen = ch(b"SCEN", struct.pack(">I", 1) + leaf + b"SCEN")
root_payload = struct.pack(">I", 0) + scen + b"CHNK"
root = ch(b"CHNK", root_payload)
open(os.path.join(d, "t.scene"), "wb").write(b"CHNK" + root)
