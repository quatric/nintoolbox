"""Synthetic Igor ".xga" (chkf/xga, XgmDecompAnimChannel): two channels of two keys. Writes <dir>/t.xga; "check <dir>" verifies
t.xga.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.xga.d", "t.json")))
        assert j["keys"] == 2 and [c["name"] for c in j["channels"]] == ["Root", "Head"], j
        assert j["channels"][1]["keys"][1] == [0, 0, 1, 0, 1, 2, 3, 1] and j["channels"][0]["kind"] == "Joint", j
        print("PASS")
        sys.exit(0)
    st = b"xga\0header\0animdata\0"
    cls = len(st); st += b"XgmDecompAnimChannel\0\0"
    e = cls + 21
    n1 = len(st); st += b"Root\0"
    n2 = len(st); st += b"Head\0"
    kind = len(st); st += b"Joint\0"
    keys = []
    for c in range(2):
        for k in range(2):
            keys.append(struct.pack(">8f", 0, 0, c * (k), 1 - c * k, c, c * 2, c * 3, 1))
    data = b"".join(keys)
    keys2 = struct.pack(">8f", 0, 0, 1, 0, 1, 2, 3, 1)
    data = data[:96] + keys2
    hdr = struct.pack(">8I", 0, 2, 4, 0, 0, 0, 0, len(data)) + struct.pack(">3I", 2, 2, 2)
    for i, nm in enumerate((n1, n2)):
        hdr += struct.pack(">5I", cls, e, nm, kind, i * 64)
    out = b"chkf" + struct.pack(">I", len(st)) + st + hdr + data
    open(os.path.join(sys.argv[1], "t.xga"), "wb").write(out)
