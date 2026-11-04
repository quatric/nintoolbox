#!/usr/bin/env python3
# Synthetic BGIB .gcs: LZ-compressed WAVS record + raw DSP-ADPCM tail blob (see extract/bgib.inc).
import struct, sys, os
out = sys.argv[1]
def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
fmt = struct.pack('<HHIIHHI', 0x666, 1, 32000, 0x17700, 2, 16, 0)[:20].ljust(20, b'\0')
coefs = b''.join(struct.pack('>h', v) for v in range(1, 17)) + b'\0' * 16
body = (b'WAVS' + chunk(b'guid', bytes(range(16))) + chunk(b'name', b'test.wav'.ljust(0x28, b'\0'))
        + chunk(b'fmt ', fmt) + chunk(b'fact', struct.pack('<I', 14)) + chunk(b'ngcc', coefs)
        + chunk(b'strm', struct.pack('<II', 0, 8)))
rec = b'RIFF' + struct.pack('<I', len(body)) + body
rec += b'\0' * (-len(rec) % 32)
plain = rec + b'abcdabcdabcdabcd'
blob = bytes([0x05, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77])

def lz(data):
    o = bytearray(); i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 65:
            run += 1
        if run >= 3:
            o += bytes([run + 0x3e, data[i]]); i += run; continue
        if data[i:i + 16] == b'abcdabcdabcdabcd':
            o += bytes([3]) + b'abcd' + bytes([0xff, 0xda]); i += 16; continue
        j = i
        while j < len(data) and j - i < 64:
            r = 1
            while j + r < len(data) and data[j + r] == data[j] and r < 3:
                r += 1
            if r >= 3 or data[j:j + 16] == b'abcdabcdabcdabcd':
                break
            j += 1
        if j == i: j = i + 1
        o += bytes([j - i - 1]) + data[i:j]; i = j
    return bytes(o)

comp = lz(plain)
hdr = bytearray(0x200)
hdr[0:4] = b'BGIB'
struct.pack_into('>I', hdr, 4, 0x1f0)
struct.pack_into('>I', hdr, 0x80, len(blob))
struct.pack_into('>I', hdr, 0x84, len(plain))
struct.pack_into('>I', hdr, 0x8c, len(comp))
open(os.path.join(out, 't.gcs'), 'wb').write(bytes(hdr) + comp + blob)
