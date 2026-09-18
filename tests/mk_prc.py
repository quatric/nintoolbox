"""Write a minimal Smash Ultimate parameter binary (.prc, "paracobn").

Root struct with two keys (hash table order is preserved, the decoder sorts
by label index like prc-rs):
  hash[0] = 0x1111111111 -> bool true
  hash[1] = 0x2222222222 -> list [ i8(-5), float(0.5), "hi" ]

    mk_prc.py OUT.prc
"""
import struct, sys

def main(out):
    h0, h1 = 0x1111111111, 0x2222222222

    # --- parameter section (built first, offsets are relative) ---
    # root struct at 0: type 12, count 2, key-table at ref offset 0
    #   key 0 -> +9 (bool), key 1 -> +11 (list)
    root = struct.pack("<BII", 12, 2, 0)
    assert len(root) == 9
    root += struct.pack("<BB", 1, 1)                # bool true at +9
    # list at +11: count 3, item offsets relative to the list type byte.
    # items follow the 5-byte header + 12 bytes of offsets: +17 (i8),
    # +19 (float), +24 (string).
    lst = struct.pack("<BI", 11, 3)
    lst += struct.pack("<III", 17, 19, 24)
    lst += struct.pack("<Bb", 2, -5)                # i8 -5
    lst += struct.pack("<Bf", 8, 0.5)               # float 0.5
    lst += struct.pack("<BI", 10, 16)               # string at ref +16
    param = root + lst

    # --- reference section: key table, then strings ---
    # key table at ref +0: (label 0, value +9), (label 1, value +11)
    ref = struct.pack("<IIII", 0, 9, 1, 11)
    ref += b"hi\0"

    hdr = b"paracobn" + struct.pack("<II", 16, len(ref))
    hdr += struct.pack("<QQ", h0, h1)
    open(out, "wb").write(hdr + ref + param)

if __name__ == "__main__":
    main(sys.argv[1])
