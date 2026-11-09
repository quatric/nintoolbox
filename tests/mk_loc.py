import struct,sys,os
d=sys.argv[1]
texts=[b"",b"Hello",b"Gr\xfc\xdfe\nZwei",b"tab\tx"]
n=len(texts)+3
tab_end=0x10+4*n
offs=[0,tab_end+0,0x10]  # leading entries point at zero bytes / header
body=b"";pos=tab_end
for t in texts:
    offs.append(pos);body+=t+b"\0";pos+=len(t)+1
# make first three offsets point at NUL bytes inside body: use first text (empty)
offs[0]=offs[1]=offs[2]=tab_end
b=b".LOCV1.0"+struct.pack("<I",0x37b802cd)+b"!DE\0"+struct.pack("<%dI"%n,*offs)+body
open(os.path.join(d,"t.LOC"),"wb").write(b)
