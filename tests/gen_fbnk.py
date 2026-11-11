# Synthetic NintendoWare FBNK + FWAR (one PCM16 sine wave, one instrument) plus a one-note MIDI.
# usage: gen_fbnk.py <be|le> <out_dir>   (be = Wii U layout, le = Switch/3DS layout)
import struct, math, sys, os
os.chdir(sys.argv[2])
LE = '<' if sys.argv[1]=='le' else '>'
bom = b'\xff\xfe' if LE=='<' else b'\xfe\xff'
def p(fmt,*a): return struct.pack(LE+fmt,*a)
def pad(b,n=0x20): return b+b'\0'*((-len(b))%n)
# --- FWAV (PCM16 mono, 32000 Hz, sine, looped)
N=3200
pcm=b''.join(p('h',int(12000*math.sin(2*math.pi*440*i/32000))) for i in range(N))
info_body=bytearray()
chtb_off=0x1C  # relative to info block start
info=bytearray(b'INFO'+p('I',0)+bytes([1,1,0,0])+p('I',32000)+p('I',0)+p('I',N))
info+=p('I',0)+p('I',1)              # orig loop start @0x18, channel count @0x1C
info+=p('HHi',0x1F00,0,12)           # ref to channel info (rel chtb)
chnf_pos=len(info)
info+=p('HHI',0x1F00,0,0)+p('I',0)+p('i',-1)  # chnf: type, data off(0), pad, adpcm ref -1
info=bytearray(pad(bytes(info),0x20))
info[4:8]=p('I',len(info))
# chnf starts at chtb+8 = 0x24 -> data offset field at +4. data off=0, adpcm @+0xC = -1 (set above at +8? fix)
info=bytearray(info)
# layout chnf: +0 u16 type,+2 pad,+4 u32 data off,+8 u32 pad,+0xC s32 adpcm ref
c=0x28
info[c:c+16]=p('HH',0x1F00,0)+p('I',0)+p('I',0)+p('i',-1)
data=pad(b'DATA'+p('I',0)+pcm,0x20); data=bytearray(data); data[4:8]=p('I',len(data))
hdr_len=0x40
fwav=bytearray(b'FWAV'+bom+p('H',0x40)+p('I',0x00010000)+p('I',0)+p('HH',2,0))
fwav+=p('HHiI',0x7000,0,hdr_len,len(info))+p('HHiI',0x7001,0,hdr_len+len(info),len(data))
fwav=bytearray(pad(bytes(fwav),0x40))
fwav+=info+data
fwav[0xC:0x10]=p('I',len(fwav))
open('wave.bfwav','wb').write(fwav)
# --- FWAR
fl=bytes(fwav)
ib=bytearray(b'INFO'+p('I',0)+p('I',1)+p('HHiI',0x1F00,0,0,len(fl))); ib=bytearray(pad(bytes(ib)))
ib[4:8]=p('I',len(ib))
fb=bytearray(b'FILE'+p('I',0)+fl); fb=bytearray(pad(bytes(fb))); fb[4:8]=p('I',len(fb))
h=bytearray(b'FWAR'+bom+p('H',0x40)+p('I',0x00010000)+p('I',0)+p('HH',2,0))
h+=p('HHiI',0x6800,0,0x40,len(ib))+p('HHiI',0x6801,0,0x40+len(ib),len(fb))
h=bytearray(pad(bytes(h),0x40))
war=h+ib+fb
war[0xC:0x10]=p('I',len(war))
open('waves.bfwar','wb').write(war)
# --- FBNK
body=bytearray(0x10)
itab=len(body)
body+=p('I',1)+p('HHi',0x5900,0,12)       # table (rel table start): inst at +12
I=len(body)
body+=p('HHi',0x6000,0,8)                 # inst ref -> D = I+8
D=len(body)
body+=p('HHi',0x5901,0,8)                 # D: ref to key region KR=D+8
KR=len(body)
body+=p('HHi',0x6000,0,8)                 # KR: ref to D2
D2=len(body)
body+=p('HHi',0x5902,0,8)                 # D2: ref to VR
VR=len(body)
body+=p('I',0)+p('I',0x21F)               # wave slot 0, flags
body+=p('I',60)+p('I',127)+p('I',64)+p('f',1.0)+p('I',0)+p('I',0x20)
body+=p('HHi',0,0,8)+bytes([100,100,100,0,100,0,0,0])
wt=len(body)
body+=p('I',1)+p('II',0x05000000,0)
body[0:8]=p('HHi',0x0100,0,wt)
body[8:16]=p('HHi',0x0101,0,itab)
blk=bytearray(b'INFO'+p('I',8+len(body)))+body
blk=bytearray(pad(bytes(blk),0x20)); blk[4:8]=p('I',len(blk))
h=bytearray(b'FBNK'+bom+p('H',0x20)+p('I',0x00010000)+p('I',0)+p('HH',1,0)+p('HHiI',0x5800,0,0x20,len(blk)))
h=bytearray(pad(bytes(h),0x20))
bank=h+blk
bank[0xC:0x10]=p('I',len(bank))
open('bank.bfbnk','wb').write(bank)
# MIDI: one note C4 for 1s at ch0 prog 0
trk=bytes([0,0xC0,0, 0,0x90,60,100, 0x83,0x60,0x80,60,0, 0,0xFF,0x2F,0])
open('song.mid','wb').write(b'MThd'+struct.pack('>IHHH',6,0,1,96)+b'MTrk'+struct.pack('>I',len(trk))+trk)
