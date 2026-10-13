#!/usr/bin/env python3
# Synthetic PlatinumGames sound pack (MadWorld .rsp): an outer PACK holding a nested PACK (with the
# RedSpark t.rsd of mk_dspmisc.py) followed by a second plain RedSpark member. Usage: mk_rsp.py dir
# (dir must already contain t.rsd).
import struct, sys, os
d = sys.argv[1]
rsd = open(os.path.join(d, 't.rsd'), 'rb').read()
rsd += b'\0' * (-len(rsd) % 4)
def pack(members):
    hdr = 8 + 8 * len(members)
    out = b'PACK' + struct.pack('>I', len(members))
    body = b''
    off = (hdr + 0x1f) & ~0x1f
    for m in members:
        out += struct.pack('>II', off + len(body), len(m))
        body += m + b'\0' * (-len(m) % 32)
    return out + b'\0' * (off - len(out)) + body
inner = pack([rsd])
open(os.path.join(d, 't.rsp'), 'wb').write(pack([inner, rsd]))
