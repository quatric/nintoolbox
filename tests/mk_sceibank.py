import struct, sys, os
d = sys.argv[1]
# one "BANK" with one Vagi entry: 2 PS-ADPCM frames (shift 12, predictor 0, data bytes 0x21 => samples 1, 2, ...; second frame ends the sample)
bank = bytearray(0x800)
bank[0:4] = b"BANK"
struct.pack_into("<IIIIII", bank, 4, 1, 0x100, 0, 0x20, 0x800, 0)
bank[0x20:0x24] = b"WAVE"
bank[0x40:0x48] = b"IECSsreV"
struct.pack_into("<II", bank, 0x48, 0x10, 0x20000)
bank[0x50:0x58] = b"IECSdaeH"
struct.pack_into("<IIII", bank, 0x58, 0x40, 0x100, 32, 0)   # chunk size, hd size, bd size @0x10 (relative to 0x50: +0x10)
bank[0x90:0x98] = b"IECSigaV"
struct.pack_into("<IIII", bank, 0x98, 0x20, 1, 0x14, 0)
struct.pack_into("<IHBB", bank, 0x90 + 0x14, 0, 22050, 0, 0xff)
frame0 = bytes([0x0c, 0x00]) + bytes([0x21] * 14)
frame1 = bytes([0x0c, 0x01]) + bytes([0x00] * 14)
bd = (frame0 + frame1).ljust(0x800, b"\0")
init = b"INIT" + bytes(0x7fc)
open(os.path.join(d, "t.bin"), "wb").write(init + bytes(bank) + bd)
