#!/usr/bin/env python3
# Synthetic Metroid Prime Hunters files: Test_Model.bin (one triangle, one 4x4
# 8-bit textured material) and a SNDFILE archive t.arc holding it plus a
# second member.
import struct, sys, os
out = sys.argv[1]

def u32(v): return struct.pack('<I', v & 0xffffffff)
def u16(v): return struct.pack('<H', v & 0xffff)

HDR, NODE, MAT, MESH, DL, TEX, PAL = 100, 240, 132, 4, 32, 40, 16
off_node = HDR
off_mat = off_node + NODE
off_mesh = off_mat + MAT
off_dl = off_mesh + MESH
off_tex = off_dl + DL
off_pal = off_tex + TEX
off_dlist = off_pal + PAL
cmds = [0x40, 0x23, 0x23, 0x23]
dlist = bytes(cmds) + u32(0)
for x, y, z in ((0, 0, 0), (4096, 0, 0), (0, 4096, 0)):
    dlist += u16(x) + u16(y) + u32(z & 0xffff)
dlist += bytes([0x41, 0, 0, 0])
off_img = off_dlist + len(dlist)
img = bytes(range(16))
off_pdata = off_img + len(img)
pdata = b''.join(u16(c) for c in [0x7c00, 0x03e0, 0x001f, 0x7fff] + [0] * 252)

hdr = bytearray(HDR)
struct.pack_into('<I', hdr, 0x00, 0)
struct.pack_into('<i', hdr, 0x04, 0x1000)
struct.pack_into('<I', hdr, 0x10, off_mat)
struct.pack_into('<I', hdr, 0x14, off_dl)
struct.pack_into('<I', hdr, 0x18, off_node)
struct.pack_into('<I', hdr, 0x24, off_mesh)
struct.pack_into('<H', hdr, 0x28, 1)
struct.pack_into('<I', hdr, 0x2c, off_tex)
struct.pack_into('<H', hdr, 0x30, 1)
struct.pack_into('<I', hdr, 0x34, off_pal)
struct.pack_into('<H', hdr, 0x48, 1)
struct.pack_into('<H', hdr, 0x4a, 1)
struct.pack_into('<H', hdr, 0x60, 1)

node = bytearray(NODE); node[0:4] = b'root'
node[0x44:0x46] = b'\xff\xff'  # parent -1 style padding; unused by the reader
mat = bytearray(MAT); mat[0:6] = b'matTri'
mat[0x42] = 31
struct.pack_into('<h', mat, 0x44, 0)
struct.pack_into('<h', mat, 0x46, 0)
mat[0x48] = 1; mat[0x49] = 1
mat[0x4a:0x4d] = bytes([200, 200, 200])
mesh = u16(0) + u16(0)
dl = u32(off_dlist) + u32(len(dlist)) + b'\0' * 24
tex = bytearray(TEX)
tex[0] = 2
struct.pack_into('<HH', tex, 2, 4, 4)
struct.pack_into('<I', tex, 8, off_img)
struct.pack_into('<I', tex, 12, 16)
struct.pack_into('<I', tex, 0x1c, 1)
pal = u32(off_pdata) + u32(len(pdata)) + b'\0' * 8

model = bytes(hdr) + bytes(node) + bytes(mat) + mesh + dl + bytes(tex) + pal + dlist + img + pdata
open(os.path.join(out, 'Test_Model.bin'), 'wb').write(model)

def arc(members):
    body_off = 32 + 64 * len(members)
    recs, body = b'', b''
    for name, data in members:
        padded = (len(data) + 31) // 32 * 32
        recs += name.ljust(32, b'\0') + struct.pack('>III', body_off + len(body), padded, len(data)) + b'\0' * 20
        body += data.ljust(padded, b'\0')
    total = body_off + len(body)
    return b'SNDFILE\0' + struct.pack('>II', len(members), total) + b'\0' * 16 + recs + body

open(os.path.join(out, 't.arc'), 'wb').write(arc([(b'Test_Model.bin', model), (b'hello.txt', b'mph member')]))
