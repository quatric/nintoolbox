"""Write a Pokemon Stadium PERS-SZP container: a header around a Yay0 stream.

The Yay0 stream is emitted all-literal (every control bit set), which is a
valid stream and keeps this generator independent of any encoder under test.
"""
import struct, sys

payload = bytes(range(256)) * 4                      # 1024 bytes

# Yay0: magic, decompressed size, link offset, chunk offset, then the control
# bitmask words, the (here empty) link table, and the literal bytes.
n_mask_words = (len(payload) + 31) // 32
mask = b"".join(struct.pack(">I", 0xFFFFFFFF) for _ in range(n_mask_words))
link_off = 0x10 + len(mask)
chunk_off = link_off                                  # no back-references
yay0 = struct.pack(">4sIII", b"Yay0", len(payload), link_off, chunk_off)
yay0 += mask + payload

hdr = struct.pack(">8sIIII", b"PERS-SZP", 0x18, len(payload), len(payload), 0)
open(sys.argv[1], "wb").write(hdr + yay0)
