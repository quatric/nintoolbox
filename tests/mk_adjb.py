"""Write a minimal Smash mesh adjacency (model.adjb) file.

Two meshes: id 3 with indices [10, 20, 30], id 7 with [40, 50].
Offsets are relative to the end of the id/offset table, as Adjb.cs reads.

    mk_adjb.py OUT.adjb
"""
import struct, sys

def main(out):
    buf = bytearray()
    buf += struct.pack("<i", 2)
    buf += struct.pack("<ii", 3, 0)
    buf += struct.pack("<ii", 7, 6)
    buf += struct.pack("<3H", 10, 20, 30)
    buf += struct.pack("<2H", 40, 50)
    open(out, "wb").write(buf)

if __name__ == "__main__":
    main(sys.argv[1])
