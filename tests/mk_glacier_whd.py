"""Synthetic IO Interactive Glacier sound bank ("t.WHD" + "t.WAV", Mini Ninjas).

mk_glacier_whd.py <dir>        writes the pair: a PCM record, a mono DSP record, a stereo DSP record and a streamed record
mk_glacier_whd.py check <dir>  verifies the WAVs written by "wszst xx t.WHD"
"""
import os
import struct
import sys


def record(typ, flags, rate, fmt, dec, size, ch, off, ns):
    return struct.pack(">HHIIIIIII", typ, flags, rate, fmt, dec, size, ch, off, ns) + b"\0" * 0x1c


def adpcm(ps):
    # zero coefficients, gain 0, predictor/scale, no history, 6 pad bytes
    return b"\0" * 32 + struct.pack(">HHHH", 0, ps, 0, 0) + b"\xcd" * 6


def area(blocks):
    raw = b"".join(blocks)
    return raw + b"\0" * (((len(raw) + 7) & ~7) + 8 - len(raw))


def make(d):
    pcm = struct.pack(">4h", 100, -200, 300, -400)
    # DSP frame: header ps 0 (predictor 0, scale 0), nibbles 1..7 then 0 -> samples 1..7, 0, ...
    frame0 = bytes([0x00, 0x12, 0x34, 0x56, 0x70, 0x00, 0x00, 0x00])
    frame1 = bytes([0x01, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11])  # scale 1: every sample 2
    wav = struct.pack(">IIII", 0, 0, 0x03000000, 0x04000000)
    o_pcm = len(wav)
    wav += pcm + b"\0" * 8
    o_mono = len(wav)
    wav += frame0
    o_st = len(wav)
    wav += frame0 + frame1
    wav = wav[:4] + struct.pack(">I", len(wav)) + wav[8:]

    whd = b"\0" * 0x10
    whd += record(1, 0, 22050, 0x10, 8, 8, 1, o_pcm, 4)
    whd += area([adpcm(0x00)]) + record(0x76, 0, 32000, 4, 28, 8, 1, o_mono, 14)
    whd += area([adpcm(0x00), adpcm(0x01)]) + record(0x76, 0, 32000, 4, 56, 16, 2, o_st, 28)
    whd += area([adpcm(0x00)]) + record(0x76, 0x8000, 32000, 4, 28, 8, 1, 0x100000, 14)  # streamed, not in the .WAV
    whd += b"\0" * 16
    whd = struct.pack(">IIII", len(whd) - 16, len(whd), 0x03000000, 0x04000000) + whd[16:]
    open(os.path.join(d, "t.WHD"), "wb").write(whd)
    open(os.path.join(d, "t.WAV"), "wb").write(wav)


def samples(path):
    b = open(path, "rb").read()
    assert b[:4] == b"RIFF" and b[36:40] == b"data"
    ch, rate = struct.unpack("<HI", b[22:28])
    n = struct.unpack("<I", b[40:44])[0] // 2
    return ch, rate, list(struct.unpack("<%dh" % n, b[44:44 + 2 * n]))


def check(d):
    out = os.path.join(d, "t.WHD.d")
    assert sorted(os.listdir(out)) == ["0000.wav", "0001.wav", "0002.wav"], os.listdir(out)
    assert samples(os.path.join(out, "0000.wav")) == (1, 22050, [100, -200, 300, -400])
    ch, rate, s = samples(os.path.join(out, "0001.wav"))
    assert (ch, rate) == (1, 32000) and s[:8] == [1, 2, 3, 4, 5, 6, 7, 0], s
    ch, rate, s = samples(os.path.join(out, "0002.wav"))
    assert ch == 2 and s[0:8:2] == [1, 2, 3, 4] and s[1:8:2] == [2, 2, 2, 2], s
    print("PASS")


if __name__ == "__main__":
    if sys.argv[1] == "check":
        check(sys.argv[2])
    else:
        make(sys.argv[1])
