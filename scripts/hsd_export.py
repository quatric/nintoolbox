#!/usr/bin/env python3
"""Export the models of a GameCube HSD archive bundle (*.mdl / *.dat) to OBJ.

The input is one or more HSD ("scene_data") archives concatenated back to back
with 0xCD fill between them (seen in the Doraemon GameCube game). Each archive
becomes <name>_NNN.obj + .mtl + textures as PNG. Exported: positions, normals,
UV0 (normals also from NBT), materials/textures (CMPR, I4/I8, IA4/IA8, RGB565, RGB5A3, RGBA8, CI4/CI8).
Enveloped meshes are emitted in bind pose. Colour attributes are read when
present. A <name>_NNN_uv1.obj is written only if UV1 differs from UV0.

usage: hsd_export.py FILE OUTDIR [ARCHIVE_INDEX ...]
requires: pillow, numpy
"""
import sys, os, struct, math
import numpy as np
from PIL import Image
def rgb565(v): return ((v>>11&31)*255//31,(v>>5&63)*255//63,(v&31)*255//31,255)
def rgb5a3(v):
    if v&0x8000: return ((v>>10&31)*255//31,(v>>5&31)*255//31,(v&31)*255//31,255)
    return ((v>>8&15)*17,(v>>4&15)*17,(v&15)*17,(v>>12&7)*255//7)
def blocks(w,h,bw,bh):
    for by in range(0,h,bh):
        for bx in range(0,w,bw): yield bx,by
def decode(d,w,h,fmt,pal=None):
    img=np.zeros((h,w,4),np.uint8); p=0
    def put(x,y,c):
        if x<w and y<h: img[y,x]=c
    if fmt==0xE:
        for bx,by in blocks(w,h,8,8):
            for sy in (0,4):
                for sx in (0,4):
                    c0,c1=struct.unpack('>HH',d[p:p+4]); bits=struct.unpack('>4B',d[p+4:p+8]); p+=8
                    a=rgb565(c0); b=rgb565(c1)
                    if c0>c1: cols=[a,b,tuple((2*a[i]+b[i])//3 for i in range(3))+(255,),tuple((a[i]+2*b[i])//3 for i in range(3))+(255,)]
                    else: cols=[a,b,tuple((a[i]+b[i])//2 for i in range(3))+(255,),(0,0,0,0)]
                    for y in range(4):
                        for x in range(4): put(bx+sx+x,by+sy+y,cols[bits[y]>>(6-2*x)&3])
        return img
    spec={0:(8,8,4),1:(8,4,8),2:(8,4,8),3:(4,4,16),4:(4,4,16),5:(4,4,16),8:(8,8,4),9:(8,4,8)}
    if fmt==6:
        for bx,by in blocks(w,h,4,4):
            ar=d[p:p+32]; gb=d[p+32:p+64]; p+=64
            for i in range(16):
                put(bx+i%4,by+i//4,(ar[2*i+1],gb[2*i],gb[2*i+1],ar[2*i]))
        return img
    bw,bh,bpp=spec[fmt]
    for bx,by in blocks(w,h,bw,bh):
        for y in range(bh):
            for x in range(bw):
                if bpp==4:
                    v=d[p+(y*bw+x)//2]; v=v>>4 if x%2==0 else v&15
                elif bpp==8: v=d[p+y*bw+x]
                else: v=struct.unpack('>H',d[p+2*(y*bw+x):p+2*(y*bw+x)+2])[0]
                if fmt==0: c=(v*17,)*3+(v*17,)
                elif fmt==1: c=(v,v,v,v)
                elif fmt==2: c=((v&15)*17,)*3+((v>>4)*17,)
                elif fmt==3: c=(v&255,)*3+(v>>8,)
                elif fmt==4: c=rgb565(v)
                elif fmt==5: c=rgb5a3(v)
                else: c=pal[v]
                put(bx+x,by+y,c)
        p+=bw*bh*bpp//8
    return img

sys.setrecursionlimit(10000)
S=struct
class Arc:
    def __init__(s,d,o):
        fs,ds,rc,rt=S.unpack('>4I',d[o:o+16]); s.fs=fs
        s.data=d[o+0x20:o+0x20+ds]
        s.rel=set(S.unpack('>%dI'%rc,d[o+0x20+ds:o+0x20+ds+rc*4]))
        rt_off=o+0x20+ds+rc*4
        s.roots=[]
        for i in range(rt):
            ro,no=S.unpack('>II',d[rt_off+8*i:rt_off+8*i+8]); s.roots.append(ro)
    def u32(s,x): return S.unpack('>I',s.data[x:x+4])[0]
    def u16(s,x): return S.unpack('>H',s.data[x:x+2])[0]
    def f(s,x): return S.unpack('>f',s.data[x:x+4])[0]
    def ptr(s,x):
        return s.u32(x) if x in s.rel else 0
def split(d):
    off=0;out=[]
    while off+0x20<=len(d):
        fs,ds,rc=S.unpack('>3I',d[off:off+12])
        if fs==0 or fs>len(d)-off or 0x20+ds+rc*4>fs: break
        out.append(off); off+=fs
        while off<len(d) and d[off]==0xcd: off+=1
    return out
def mmul(a,b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def local(t,r,s):
    cx,sx=math.cos(r[0]),math.sin(r[0]);cy,sy=math.cos(r[1]),math.sin(r[1]);cz,sz=math.cos(r[2]),math.sin(r[2])
    Rx=[[1,0,0,0],[0,cx,-sx,0],[0,sx,cx,0],[0,0,0,1]]
    Ry=[[cy,0,sy,0],[0,1,0,0],[-sy,0,cy,0],[0,0,0,1]]
    Rz=[[cz,-sz,0,0],[sz,cz,0,0],[0,0,1,0],[0,0,0,1]]
    R=mmul(Rz,mmul(Ry,Rx))
    for i in range(3):
        for j in range(3): R[i][j]*=s[j]
        R[i][3]=t[i]
    return R
def xf(m,p): return tuple(m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]+m[i][3] for i in range(3))
CT={0:('B',1),1:('b',1),2:('H',2),3:('h',2),4:('f',4)}
def read_attrs(a,p):
    out=[]
    while True:
        at=a.u32(p)
        if at==0xff: break
        out.append(dict(at=at,ty=a.u32(p+4),cnt=a.u32(p+8),ct=a.u32(p+12),frac=a.data[p+16],stride=a.u16(p+18),base=a.ptr(p+20)))
        p+=24
    return out
def vsize(v):
    ty=v['ty']
    return 0 if ty==0 else 1 if ty in(1,2) and v['at']<=8 else None
def read_comp(a,v,idx):
    fmt,sz=CT.get(v['ct'],('f',4))
    n=v['cnt']+2 if v['at']==9 else (3 if v['at']==10 else v['cnt']) # crude
    return None
def parse_dl(a,pobj,tris,verts_out):
    attrs=read_attrs(a,a.ptr(pobj+8))
    dl=a.ptr(pobj+0x10); size=a.u16(pobj+0xE)*32
    d=a.data; p=dl; end=dl+size
    def field(v):
        # returns byte length in DL and reader
        ty=v['ty']
        if v['at']<=8 and ty==1: return 1
        if ty==0: return 0
        if ty==2: return 1
        if ty==3: return 2
        return None
    def ncomp(v):
        if v['at']==9: return 2 if v['cnt']==0 else 3
        if v['at']==10: return 3
        if v['at']==25: return 9 if v['cnt']==1 else 3
        if v['at']>=13: return 1 if v['cnt']==0 else 2
        return v['cnt']
    def direct_len(v):
        if v['at'] in (11,12):
            return {0:2,1:3,2:4,3:2,4:3,5:4}.get(v['ct'],4)
        fmt,sz=CT[v['ct']]; return sz*ncomp(v)
    def read_clr(v,off,idx=None):
        if idx is not None: off=v['base']+idx*v['stride']
        b=a.data[off:off+4]; t=v['ct']
        if t==0:
            x=S.unpack('>H',b[:2])[0]; return ((x>>11&31)/31,(x>>5&63)/63,(x&31)/31,1.0)
        if t in(1,2): return (b[0]/255,b[1]/255,b[2]/255,1.0)
        if t==3:
            x=S.unpack('>H',b[:2])[0]; return tuple((x>>s&15)/15 for s in (12,8,4,0))
        if t==4:
            x=int.from_bytes(b[:3],'big'); return tuple((x>>s&63)/63 for s in (18,12,6,0))
        return (b[0]/255,b[1]/255,b[2]/255,b[3]/255)
    def read_val(v,off,idx=None):
        fmt,sz=CT[v['ct']]; n=ncomp(v)
        if idx is not None: off=v['base']+idx*v['stride']
        vals=S.unpack('>%d%s'%(n,fmt),a.data[off:off+n*sz])
        if fmt!='f': vals=tuple(x/(1<<v['frac']) for x in vals)
        return vals
    while p<end:
        c=d[p]; p+=1
        op=c&0xf8
        if op==0x00 and c==0: continue
        if op in (0x80,0x90,0x98,0xa0,0xa8,0xb0,0xb8):
            cnt=S.unpack('>H',d[p:p+2])[0]; p+=2
            vs=[]
            for _ in range(cnt):
                pos=None; uv=(0.0,0.0); uv1=None; nrm=(0.0,0.0,1.0); clr=(1.0,1.0,1.0,1.0)
                for v in attrs:
                    ty=v['ty']
                    if v['at']<=8:
                        if ty==1: p+=1
                        continue
                    if ty==0: continue
                    if ty==1:
                        if v['at']==9: pos=read_val(v,p)
                        if v['at']==13: uv=read_val(v,p)
                        if v['at']==14: uv1=read_val(v,p)
                        if v['at'] in (10,25): nrm=read_val(v,p)[:3]
                        if v['at']==11: clr=read_clr(v,p)
                        p+=direct_len(v)
                    elif ty in(2,3):
                        w=1 if ty==2 else 2
                        i=int.from_bytes(d[p:p+w],'big'); p+=w
                        if v['at']==9: pos=read_val(v,None,i)
                        if v['at']==13: uv=read_val(v,None,i)
                        if v['at']==14: uv1=read_val(v,None,i)
                        if v['at'] in (10,25): nrm=read_val(v,None,i)[:3]
                        if v['at']==11: clr=read_clr(v,None,i)
                if pos is None: continue
                vs.append((pos if len(pos)==3 else pos+(0,), uv[:2], nrm, clr, (uv1 or uv)[:2]))
            if op==0x90:
                for i in range(0,len(vs)-2,3): tris.append((vs[i],vs[i+1],vs[i+2]))
            elif op==0x98:
                for i in range(len(vs)-2):
                    t=(vs[i],vs[i+1],vs[i+2]) if i%2==0 else (vs[i+1],vs[i],vs[i+2]); tris.append(t)
            elif op==0xa0:
                for i in range(1,len(vs)-1): tris.append((vs[0],vs[i],vs[i+1]))
            elif op==0x80:
                for i in range(0,len(vs)-3,4):
                    tris.append((vs[i],vs[i+1],vs[i+2])); tris.append((vs[i],vs[i+2],vs[i+3]))
        elif c==0x61: p+=4
        elif c==0x08: p+=5
        elif c==0x10:
            n=(S.unpack('>H',d[p:p+2])[0]+1); p+=4+4*n
        elif op in (0x20,0x28,0x30,0x38): p+=4
        else: break
IDENT=[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
def nx(m,n):
    r=[(m[i][0]*n[0]+m[i][1]*n[1]+m[i][2]*n[2]) for i in range(3)]
    # inverse-transpose for non-uniform scale: divide by squared axis scale
    sc=[m[0][i]**2+m[1][i]**2+m[2][i]**2 for i in range(3)]
    r=[ (m[0][0]*n[0]/sc[0]+m[1][0]*n[1]/sc[0]+m[2][0]*n[2]/sc[0]) if sc[0] else 0,
        (m[0][1]*n[0]/sc[1]+m[1][1]*n[1]/sc[1]+m[2][1]*n[2]/sc[1]) if sc[1] else 0,
        (m[0][2]*n[0]/sc[2]+m[1][2]*n[1]/sc[2]+m[2][2]*n[2]/sc[2]) if sc[2] else 0]
    # (M^-T) n = R S^-1 n ; with M=R*S columns scaled: components = col_i . n / |col_i|^2
    l=(r[0]**2+r[1]**2+r[2]**2)**.5 or 1
    return (r[0]/l,r[1]/l,r[2]/l)
def walk(a,j,parent,mesh,seen):
    while j and j not in seen:
        seen.add(j)
        flags=a.u32(j+4)
        r=(a.f(j+0x14),a.f(j+0x18),a.f(j+0x1c)); s=(a.f(j+0x20),a.f(j+0x24),a.f(j+0x28)); t=(a.f(j+0x2c),a.f(j+0x30),a.f(j+0x34))
        m=mmul(parent,local(t,r,s))
        if not (flags&0x4000):  # not SPLINE/PTCL
            dobj=a.ptr(j+0x10)
            while dobj:
                pobj=a.ptr(dobj+0xc)
                while pobj:
                    tr=[]; 
                    try: parse_dl(a,pobj,tr,None)
                    except Exception as e: pass
                    mat=(a.ptr(a.ptr(dobj+8)+8) if a.ptr(dobj+8) else 0)
                    pm=IDENT if (a.u16(pobj+0xc)&0x3000)==0x2000 else m  # enveloped verts are already in model space at bind pose
                    mesh.extend((mat,tuple((xf(pm,v[0]),v[1],nx(pm,v[2]),v[3],v[4]) for v in t3)) for t3 in tr)
                    pobj=a.ptr(pobj+4)
                dobj=a.ptr(dobj+4)
        walk(a,a.ptr(j+8),m,mesh,seen)
        j=a.ptr(j+0xc)
def extract(a):
    ident=[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
    root=a.roots[0]; mesh=[]; seen=set()
    lst=a.ptr(root)
    while lst and a.ptr(lst):
        jd=a.ptr(lst); j=a.ptr(jd)
        walk(a,j,ident,mesh,seen); lst+=4
        if lst not in a.rel: break
    return mesh

import sys, os, struct
def img_size(w,h,fmt):
    bw,bh,bpp={0:(8,8,4),1:(8,4,8),2:(8,4,8),3:(4,4,16),4:(4,4,16),5:(4,4,16),6:(4,4,32),8:(8,8,4),9:(8,4,8),0xE:(8,8,4)}[fmt]
    return ((w+bw-1)//bw)*((h+bh-1)//bh)*bw*bh*bpp//8
def tex_png(a,t,path):
    im=a.ptr(t+0x4c); w,h,fmt=a.u16(im+4),a.u16(im+6),a.u32(im+8)
    data=a.data[a.ptr(im):a.ptr(im)+img_size(w,h,fmt)]
    pal=None
    if fmt in (8,9,10):
        tl=a.ptr(t+0x50); n=a.u16(tl+0xc); lf=a.u32(tl+4); lo=a.ptr(tl)
        pal=[]
        for i in range(n):
            v=struct.unpack('>H',a.data[lo+2*i:lo+2*i+2])[0]
            pal.append(((v&255,)*3+(v>>8,)) if lf==0 else rgb565(v) if lf==1 else rgb5a3(v))
    Image.fromarray(decode(data,w,h,fmt,pal)).save(path)

def main():
    global fn,outdir
    fn,outdir=sys.argv[1],sys.argv[2]; sel=set(map(int,sys.argv[3:])); os.makedirs(outdir,exist_ok=True)
    d=open(fn,'rb').read(); base=os.path.basename(fn).split('.')[0]
    for n,o in enumerate(split(d)):
        if sel and n not in sel: continue
        a=Arc(d,o)
        try: mesh=extract(a)
        except Exception as e: print(n,'ERR',e); continue
        if not mesh: continue
        groups={}
        for t,tri in mesh: groups.setdefault(t,[]).append(tri)
        name='%s_%03d'%(base,n); mtl=[]; ntex=0
        with open('%s/%s.obj'%(outdir,name),'w') as f:
            f.write('mtllib %s.mtl\n'%name)
            vi=ti=0
            for t,tris in groups.items():
                mn='m%x'%t
                if t:
                    try: tex_png(a,t,'%s/%s_%x.png'%(outdir,name,t)); mtl.append('newmtl %s\nKd 1 1 1\nmap_Kd %s_%x.png\n'%(mn,name,t)); ntex+=1
                    except Exception as e: mtl.append('newmtl %s\nKd 0.8 0.8 0.8\n'%mn); print(n,'tex fail',hex(t),e)
                else: mtl.append('newmtl %s\nKd 0.8 0.8 0.8\n'%mn)
                f.write('usemtl %s\n'%mn)
                for tri in tris:
                    for p,uv,nn,c,_u in tri: f.write('v %f %f %f %.4f %.4f %.4f\nvt %f %f\nvn %f %f %f\n'%(p+c[:3]+(uv[0],1-uv[1])+nn))
                    f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n'%tuple(x for i in range(3) for x in (vi+i+1,)*3)); vi+=3
        if any(v[4]!=v[1] for _,tri in mesh for v in tri):
            with open('%s/%s_uv1.obj'%(outdir,name),'w') as f:
                f.write('mtllib %s.mtl\n'%name); vi=0
                for t,tris in groups.items():
                    f.write('usemtl m%x\n'%t)
                    for tri in tris:
                        for p,uv,nn,c,u1 in tri: f.write('v %f %f %f\nvt %f %f\nvn %f %f %f\n'%(p+(u1[0],1-u1[1])+nn))
                        f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n'%tuple(x for i in range(3) for x in (vi+i+1,)*3)); vi+=3
        open('%s/%s.mtl'%(outdir,name),'w').write('\n'.join(mtl))
        print(name,'tris',len(mesh),'materials',len(groups),'textures',ntex)


if __name__=='__main__':
    main()
