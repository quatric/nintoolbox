# Dev aid (not used by the build): runs a function of a retail Wii ELF in the Unicorn PPC emulator as a bit-exact oracle
# for format decoders (used for the Kylotonn .vid block codec, gx_crunch_unpack9 at 0x80189298). Needs a python with the "unicorn" module.
# Usage: set ELF to the extracted game ELF; Emu(W, H).run(stream_bytes, previous_frame_bytes) -> (frame_bytes, bytes_consumed).
import struct,sys
from unicorn import *
from unicorn.ppc_const import *
ELF='x/files/cocoto_release.elf'
e=open(ELF,'rb').read()
def lz3(s,outsize):
    i=0;o=bytearray()
    while i<len(s) and len(o)<outsize:
        c=ord(s[i]);i+=1
        if not c&0x80: n=c+1;o+=s[i:i+n];i+=n
        elif c&0xc0==0x80:
            n=(c&0x3f)+1;d=(ord(s[i])<<8)+ord(s[i+1])+1;i+=2
            for _ in range(n):o.append(o[-d])
        else:
            n=(c&0x3f)+1;d=ord(s[i])+1;i+=1
            for _ in range(n):o.append(o[-d])
    return str(bytes(o)),i
def frames(v):
    p=v.find('VIDE')
    out=[]
    while p+8<=len(v):
        t=v[p:p+4];s=struct.unpack('<I',v[p+4:p+8])[0]
        if t!=b'VIDE' and t!='VIDE': break
        out.append(v[p+8:p+s]);p+=s
    return out
class Emu:
    def __init__(s,W,H):
        s.W=W;s.H=H
        s.mu=Uc(UC_ARCH_PPC,UC_MODE_PPC32|UC_MODE_BIG_ENDIAN)
        mu=s.mu
        # text 0x80004000.. file off 0x1e0 size 0x2444 ; .text 0x800065c0 off 0x27a0 size 0x3fa4f8
        mu.mem_map(0x80004000,0x404000)
        mu.mem_write(0x80004000,e[0x1e0:0x1e0+0x2444])
        mu.mem_write(0x800065c0,e[0x27a0:0x27a0+0x3fa4f8])
        mu.mem_map(0x10000000,0x4000000)
        s.sp=0x13f00000
        s.src=0x10000000;s.ptrs=0x10fff000;s.prev=0x11000000;s.cur=0x12000000
    def run(s,data,prev):
        mu=s.mu;W,H=s.W,s.H
        mu.mem_write(s.src,data+b'\0'*64)
        mu.mem_write(s.ptrs,struct.pack('>I',s.src))        # src**
        mu.mem_write(s.ptrs+4,struct.pack('>I',s.cur))      # cur**
        mu.mem_write(s.prev,prev)
        mu.mem_write(s.cur,b'\0'*(W*H*2))
        mu.reg_write(UC_PPC_REG_1,s.sp)
        mu.reg_write(UC_PPC_REG_3,s.ptrs);mu.reg_write(UC_PPC_REG_4,s.prev)
        mu.reg_write(UC_PPC_REG_5,s.ptrs+4);mu.reg_write(UC_PPC_REG_6,W);mu.reg_write(UC_PPC_REG_7,H);mu.reg_write(UC_PPC_REG_8,1)
        mu.reg_write(UC_PPC_REG_LR,0x7ffff000)
        mu.mem_map(0x7ffff000,0x1000) if not getattr(s,'m',0) else None
        s.m=1
        mu.emu_start(0x80189298,0x7ffff000,count=200000000)
        return str(mu.mem_read(s.cur,W*H*2)),struct.unpack('>I',mu.mem_read(s.ptrs,4))[0]-s.src
