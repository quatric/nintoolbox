#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void trace_free(ccp f,ccp p,uint l,void *v){(void)f;(void)p;(void)l;free(v);}
void *trace_malloc(ccp f,ccp p,uint l,size_t n){(void)f;(void)p;(void)l;return malloc(n);}
void *trace_calloc(ccp f,ccp p,uint l,size_t n,size_t s){(void)f;(void)p;(void)l;return calloc(n,s);}
void *trace_realloc(ccp f,ccp p,uint l,void *v,size_t n){(void)f;(void)p;(void)l;return realloc(v,n);}
void dclib_free(void *v){free(v);}
void *dclib_malloc(size_t n){return malloc(n);}
void *dclib_calloc(size_t n,size_t s){return calloc(n,s);}
void *dclib_realloc(void *v,size_t n){return realloc(v,n);}
char *dclib_strdup(ccp s){return s?strdup(s):0;}

static int check(const u8 *in,uint in_size,const void *want,uint want_size)
{
    u8 *out = 0; uint out_size = 0;
    const int bad = DecodeLZO1XGrow(&out,&out_size,in,in_size) != ERR_OK
	|| out_size != want_size || memcmp(out,want,want_size);
    free(out);
    return bad;
}

int main(void)
{
    // Hand-authored streams from the published byte-layout description, not
    // an encoder: leading literals, all short-match forms, and the terminator.
    static const u8 literals[] = { 21,'A','B','C','D', 0x11,0,0 };
    if (check(literals,sizeof(literals),"ABCD",4)) return 1;
    static const u8 m1[] = { 18,'A', 0,0, 0x11,0,0 };
    if (check(m1,sizeof(m1),"AAA",3)) return 2;
    static const u8 m2[] = { 22,'A','B','C','D','E', 0x40,0, 0x11,0,0 };
    if (check(m2,sizeof(m2),"ABCDEEEE",8)) return 3;
    static const u8 m3[] = { 22,'A','B','C','D','E', 0x80,0, 0x11,0,0 };
    if (check(m3,sizeof(m3),"ABCDEEEEEE",10)) return 4;

    // Extended literal length followed by a 16 KiB-distance match. This also
    // exercises the special first-byte (< 16) literal form.
    u8 long_stream[2 + 64 + 16385 + 6];
    uint p = 0;
    long_stream[p++] = 0; memset(long_stream+p,0,64); p += 64;
    long_stream[p++] = 47; // 3 + 15 + 64*255 + 47 = 16385 literals
    for (uint i=0; i<16385; i++) long_stream[p++] = (u8)(i*29u);
    long_stream[p++] = 0x21; long_stream[p++] = 0; long_stream[p++] = 0x40;
    long_stream[p++] = 0x11; long_stream[p++] = 0; long_stream[p++] = 0;
    u8 *want = malloc(16388);
    for (uint i=0; i<16385; i++) want[i] = (u8)(i*29u);
    memcpy(want+16385,want,3);
    const int bad = p != sizeof(long_stream) || check(long_stream,p,want,16388);
    free(want);
    if (bad) return 5;

    // End marker must end the segment exactly.
    static const u8 junk[] = { 18,'A', 0,0, 0x11,0,0, 0 };
    u8 *out = 0; uint out_size = 0;
    const enumError err = DecodeLZO1XGrow(&out,&out_size,junk,sizeof(junk));
    free(out);
    if (err == ERR_OK) return 6;

    // A Prime-style CMPD block combines a raw signed segment and an LZO one.
    // This exercises the outer header plus the segment dispatcher, not just
    // the standalone bitstream decoder.
    static const u8 cmpd[] = {
	'C','M','P','D', 0,0,0,1,
	0xc0,0,0,13, 0,0,0,5,
	0xff,0xfe, 'X','Y', 0,7, 18,'A',0,0,0x11,0,0
    };
    out = DecompressRPAKEntry(cmpd,sizeof(cmpd),&out_size);
    const int cmpd_bad = !out || out_size != 5 || memcmp(out,"XYAAA",5);
    free(out);
    return cmpd_bad ? 7 : 0;
}
