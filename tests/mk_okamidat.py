#!/usr/bin/env python3
# Synthetic Okami offset-table container: member 0 is an Okami .t texture (see mk_okamit.py), member 1 is opaque
import struct, sys, os, subprocess, tempfile
d = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
subprocess.check_call([sys.executable, os.path.join(here, 'mk_okamit.py'), tmp])
tex = open(os.path.join(tmp, 't.t'), 'rb').read()
other = b'OPAQUE-DATA'
tabend = 4 + 4 * 2
o0 = (tabend + 0x1f) & ~0x1f
o1 = (o0 + len(tex) + 0x1f) & ~0x1f
img = (struct.pack('>3I', 2, o0, o1)).ljust(o0, b'\0') + tex
img = img.ljust(o1, b'\0') + other
open(os.path.join(d, 't.dat'), 'wb').write(img)
