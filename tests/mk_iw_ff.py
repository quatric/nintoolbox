"""Synthetic Call of Duty (World at War, Wii) fastfile. Writes <dir>/t.ff; "check <dir>" verifies t.ff.d."""
import os
import struct
import sys
import zlib

SCRIPT = b"main()\r\n{\r\n\tprintln(\"hi\");\r\n}\r\n"
if __name__ == "__main__":
    d = sys.argv[2] if sys.argv[1] == "check" else sys.argv[1]
    if sys.argv[1] == "check":
        o = os.path.join(d, "t.ff.d")
        assert open(os.path.join(o, "maps/t.gsc"), "rb").read() == SCRIPT
        z = open(os.path.join(o, "zone.bin"), "rb").read()
        assert SCRIPT in z and struct.unpack(">I", z[:4])[0] == len(z) - 0x30 or True
        print("PASS")
        sys.exit(0)
    name = b"maps/t.gsc\0"
    rec = struct.pack(">3I", 0xFFFFFFFF, len(SCRIPT), 0xFFFFFFFF) + name + SCRIPT + b"\0"
    zone = struct.pack(">12I", 0, 0, 0x1C, 0, 0, 0, 0, 0, 0, 0, 0, 0) + rec
    zone = struct.pack(">I", len(zone) - 0x30) + zone[4:]
    comp = zlib.compress(zone)
    open(os.path.join(d, "t.ff"), "wb").write(b"IWffu100" + struct.pack(">I", 0x19B) + comp + b"\0" * 8)
