
/***************************************************************************
 *                    __            __ _ ___________                       *
 *                    \ \          / /| |____   ____|                      *
 *                     \ \        / / | |    | |                           *
 *                      \ \  /\  / /  | |    | |                           *
 *                       \ \/  \/ /   | |    | |                           *
 *                        \  /\  /    | |    | |                           *
 *                         \/  \/     |_|    |_|                           *
 *                                                                         *
 *                           Wiimms ISO Tools                              *
 *                         https://wit.wiimm.de/                           *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the WIT project.                                *
 *   Visit https://wit.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2009-2021 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

// NKit (.nkit.iso) restoration -- GameCube only.
//
// NKit (Nanook, closed-source binary, but the v1 source that actually wrote
// this format is public: github.com/Nanook/NKitv1) shrinks a GC/Wii ISO by
// (a) dropping the disc's "junk data" -- padding between files that the
// original SDK filled with output from a Lagged Fibonacci Generator instead
// of zeros, to defeat naive compression -- and (b) recording just enough
// per-gap metadata to regenerate it byte-exact later.
//
// This file ports NkitReaderGc.cs, NkitFormat.cs and Gaps.cs from that
// source directly (same variable names/shapes kept where practical, so a
// diff against the original is possible), plus a from-scratch port of the
// id+disc+block-index junk seeding in JunkStream.cs.fillBlock().
//
// The Wii path (NkitReaderWii.cs) lives in its own file, x-nkit-wii.c, and
// IS implemented and verified -- see that file's header. lib-xfile.c picks
// between the two by disc magic.
//
// This GC path, by contrast, is still UNVERIFIED against a real file: every
// .nkit sample available locally happens to be Wii. See the XExtractNKitGC
// comment below.
//
// Known gap vs. the verified Wii port: nkit_junk_get() below has no
// equivalent of JunkStream's _junkLength truncation (Math2.Align(length,
// 0x8000), i.e. rounded DOWN -- everything at or past it reads as NUL rather
// than generator output; see nkit_wii_junk_t in x-nkit-wii.c, where getting
// this wrong produced a mismatch on every partition tail). It is deliberately
// not "fixed" here on spec, because the GC reader passes no length through
// this generator at all and there is no GC sample to confirm the correct
// value against -- doing it blind would just trade one unverified behaviour
// for another. Revisit together, with a real GC sample in hand.
//
// The junk generator itself needed no new primitive: lib-lfg.c already
// implements exactly this LFG (see its own header comment, which already
// credited NKit). The only new piece is deriving lib-lfg.c's 68-byte seed
// from (game id, disc number, 32 KiB block index) the way JunkStream.cs's
// fillBlock()/a10002710() does -- RVZ (lib-wia.c) instead stores that
// 68-byte seed literally, captured from a real disc, so it never needed
// this derivation.

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "x-formats.h"
#include "lib-lfg.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    NKit junk generator (ported)		///////////////
///////////////////////////////////////////////////////////////////////////////

// Every 0x8000 (32 KiB) aligned block of the junk stream is independently
// seeded -- ported from JunkStream.cs: fillBlock() re-derives 'sample' fresh
// every 0x8000 bytes (id/disc/block-index only, no carry-over from earlier
// blocks), so blocks can be generated in any order with no replay needed.

typedef struct nkit_junk_t
{
    u8		id[4];		// 4 game-id bytes (JunkStream's "id")
    u8		disc;		// disc number (0 for GC, matches JunkStream's "disc")
    lfg_t	lfg;		// current LFG state
    u64		block_start;	// byte offset this LFG state is seeded/positioned at
    bool	valid;		// false until the first block is seeded
}
nkit_junk_t;

///////////////////////////////////////////////////////////////////////////////

static void nkit_junk_init ( nkit_junk_t *nj, const u8 id[4], u8 disc )
{
    memcpy(nj->id,id,4);
    nj->disc  = disc;
    nj->valid = false;
}

///////////////////////////////////////////////////////////////////////////////

// Port of a10002710(): expand a 32 bit sample into the 17 word (68 byte) LFG
// seed lib-lfg.c's InitializeLFG() expects. The multiply-shift loop is a
// simple LCG-driven bit generator (32 output bits per word, one bit per LCG
// step); the xor of word 16 and the buf[17..520] recurrence that follows in
// the C# source are IDENTICAL to what InitializeLFG() already does with its
// own 17-word seed, so this function only needs to produce those 17 words --
// InitializeLFG() takes it from there unmodified.
//
// Regenerate-count check (this is the detail most likely to be silently
// wrong, so it's spelled out): a10002710() itself calls the regenerate
// step 3 times; JunkStream.fillBlock() then always increments its own
// word-index counter (num2) to 0x209 immediately after calling
// a10002710(), which triggers one MORE regenerate before the first byte is
// read -- 4 total, exactly matching InitializeLFG()'s own "advance 4 times
// before the first byte is valid". So reusing InitializeLFG() unmodified
// here, instead of open-coding a 3-times variant, is correct.

static void nkit_seed_block ( nkit_junk_t *nj, u64 block32k )
{
    u32 sample = (u32)( ( (u32)( (nj->id[2] << 8 | nj->id[1]) << 16 )
			 | (u32)( (nj->id[3] + nj->id[2]) << 8 )
			 | (u32)( nj->id[0] + nj->id[1] ) ) );
    sample = (u32)( sample ^ nj->disc ) * 0x260bcd5u;
    sample ^= (u32)( block32k * 0x1ef29123u );	// 32 bit wraparound multiply, same as C#'s uint*uint

    u32 words[LFG_SEED_WORDS];
    u32 s = sample;
    u32 num = 0;
    for ( int w = 0; w < LFG_SEED_WORDS; w++ )
    {
	for ( int i = 0; i < 32; i++ )
	{
	    s *= 0x5d588b65u;
	    s += 1;				// C#'s pre-increment ++sample
	    num = num >> 1 | ( s & 0x80000000u );
	}
	words[w] = num;
    }
    words[16] ^= words[0] >> 9 ^ words[16] << 23;

    u8 seed[LFG_SEED_SIZE];
    for ( int w = 0; w < LFG_SEED_WORDS; w++ )
    {
	seed[w*4+0] = (u8)( words[w] >> 24 );
	seed[w*4+1] = (u8)( words[w] >> 16 );
	seed[w*4+2] = (u8)( words[w] >>  8 );
	seed[w*4+3] = (u8)( words[w] );
    }

    InitializeLFG(&nj->lfg,seed);
    nj->block_start = block32k * 0x8000;
    nj->valid = true;
}

///////////////////////////////////////////////////////////////////////////////

// Fill 'dest' with 'size' bytes of junk starting at absolute position 'pos'.
// 'pos' must be >= the position of the previous call for the same nj (the
// generator only runs forward within a 32 KiB block; crossing into a new
// block is handled by reseeding).

static void nkit_junk_get ( nkit_junk_t *nj, u64 pos, void *dest, u32 size )
{
    u8 *out = dest;
    while (size)
    {
	const u64 block32k    = pos / 0x8000;
	const u64 block_start = block32k * 0x8000;

	if ( !nj->valid || block_start != nj->block_start )
	    nkit_seed_block(nj,block32k);

	const u64 off_in_block = pos - block_start;

	// The LFG's own 'pos' tracks bytes emitted since the last reseed, so
	// as long as callers only ever move forward within a block (true for
	// this reader -- see the file comment), it already equals
	// off_in_block and no explicit ForwardLFG() is needed except right
	// after a fresh reseed when the caller doesn't start at the block's
	// first byte.
	if ( nj->lfg.pos == 0 && off_in_block != 0 )
	    ForwardLFG(&nj->lfg,(u32)off_in_block);

	const u64 avail = 0x8000 - off_in_block;
	const u32 chunk = size < avail ? size : (u32)avail;
	GetBytesLFG(&nj->lfg,out,chunk);

	out  += chunk;
	pos  += chunk;
	size -= chunk;
    }
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		 gap/block format (ported from Gaps.cs)	///////////////
///////////////////////////////////////////////////////////////////////////////

// [[nkit_gap_type_t]]
typedef enum nkit_gap_type_t
{
    NKIT_GAP_ALL_JUNK		= 0b00,
    NKIT_GAP_ALL_SCRUBBED	= 0b01,
    NKIT_GAP_MIXED		= 0b10,
    NKIT_GAP_JUNK_FILE		= 0b11,
}
nkit_gap_type_t;

// [[nkit_block_type_t]]
typedef enum nkit_block_type_t
{
    NKIT_BLOCK_JUNK		= 0b00,
    NKIT_BLOCK_NONJUNK		= 0b01,
    NKIT_BLOCK_BYTEFILL		= 0b10,
    NKIT_BLOCK_REPEAT		= 0b11,
}
nkit_block_type_t;

#define NKIT_GAP_BLOCK_SIZE	0x100	// Gap.BlockSize in the C# source

//
///////////////////////////////////////////////////////////////////////////////
///////////////			 GC FST (ported)		///////////////
///////////////////////////////////////////////////////////////////////////////

// [[nkit_fst_file_t]]
typedef struct nkit_fst_file_t
{
    u32		data_offset;	// FstFile.DataOffset (GC: same as raw offset)
    u32		length;		// FstFile.Length
    u32		off_in_fst;	// FstFile.OffsetInFstFile: byte offset of this entry's data-offset field in fst.bin
    bool	is_dir;
}
nkit_fst_file_t;

// Standard 12 byte GC FST entry parser (type<<24|name_off, data_offset,
// length), same layout FileSystem.cs's recurseFst() reads. Files are
// returned sorted by data_offset like the C# LINQ OrderBy() in
// NkitFormat.GetConvertFstFiles().

static int fst_file_cmp ( const void *pa, const void *pb )
{
    const nkit_fst_file_t *a = pa, *b = pb;
    if ( a->data_offset != b->data_offset )
	return a->data_offset < b->data_offset ? -1 : 1;
    return a->length < b->length ? -1 : a->length > b->length ? 1 : 0;
}

static enumError nkit_parse_fst_gc
(
    const u8		*fst,
    u32			fst_size,
    nkit_fst_file_t	**res_files,
    uint		*res_n
)
{
    if ( fst_size < 12 )
	return ERROR0(ERR_WIA_INVALID,"fst.bin too small\n");

    const u32 n_entries = be32(fst);
    if ( (u64)n_entries * 12 > fst_size )
	return ERROR0(ERR_WIA_INVALID,"fst.bin entry count out of range\n");

    nkit_fst_file_t *files = MALLOC(n_entries*sizeof(*files));
    uint n_files = 0;

    // Flat walk: every entry after index 0 (the root dir) is visited once,
    // in file order; that's all NkitFormat.GetConvertFstFiles() needs
    // (it only wants a flat, offset-sorted file list, not the tree).
    for ( u32 i = 1; i < n_entries; i++ )
    {
	const u8 *e = fst + i*12;
	const u8 type = e[0];
	if (type)
	    continue;	// directory entry, no data

	nkit_fst_file_t *f = files + n_files++;
	f->data_offset = be32(e+4);
	f->length      = be32(e+8);
	f->off_in_fst  = i*12 + 4;
	f->is_dir      = false;
    }

    qsort(files,n_files,sizeof(*files),fst_file_cmp);
    *res_files = files;
    *res_n = n_files;
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		   restoration (ported from			///////////////
///////////////////////////////////////////////////////////////////////////////
///////////////	     NkitReaderGc.cs / NkitFormat.cs / Gaps.cs)	///////////////
///////////////////////////////////////////////////////////////////////////////

// GC/Wii disc header field offsets NKit reuses to smuggle its own tag into
// (boot.bin's own layout is unrelated to NKit; these are the same public,
// well known constants NkitReaderGc.cs reads):
#define NKIT_TAG_OFF		0x200	// 8 byte "NKIT v01" tag
#define NKIT_CRC_OFF		0x208	// u32 be: CRC of the restored image
#define NKIT_SIZE_OFF		0x210	// u32 be: restored image size
#define NKIT_JUNKID_OFF		0x214	// 4 bytes: junk id override, or zero
#define GC_DOL_OFF		0x420	// u32 be: main.dol offset
#define GC_FST_OFF		0x424	// u32 be: fst.bin offset
#define GC_FST_SIZE_OFF		0x428	// u32 be: fst.bin size

// NKit always reads a fixed size "header" region up front covering
// boot.bin+bi2.bin (0x2440 bytes -- the same conservative "definitely
// before the FST" constant used throughout the GC/Wii homebrew scene, not
// NKit specific), then a further region up to the real (stored) FST offset.
#define NKIT_HDR_SIZE		0x2440

static enumError nkit_read_exact ( FILE *f, void *buf, size_t n, ccp fname )
{
    if ( n && fread(buf,1,n,f) != n )
	return ERROR1(ERR_READ_FAILED,"Truncated NKit file: %s\n",fname);
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Port of NkitReaderGc.writeGap(): decode one gap record from 'src' (already
// positioned right after the preceding file's data) and write the restored
// bytes to 'dest'. Returns the number of source AND dest bytes advanced
// (same value -- gaps are never resized) via *dest_len, or an error.

static enumError nkit_decode_gap
(
    FILE		*src,
    FILE		*dest,
    ccp			fname,
    nkit_junk_t		*junk,
    u64			dst_pos,	// current absolute offset in the restored image
    u64			*out_len	// bytes written
)
{
    *out_len = 0;

    u8 hdr[4];
    enumError err = nkit_read_exact(src,hdr,4,fname);
    if (err)
	return err;

    u32 word = be32(hdr);
    nkit_gap_type_t gt = (nkit_gap_type_t)( word & 0b11 );
    u64 size = word & 0xFFFFFFFCu;

    if ( size == 0xFFFFFFFCu )		// Wii-only 64 bit extension; never emitted for GC
    {
	u8 ext[4];
	if ((err = nkit_read_exact(src,ext,4,fname)))
	    return err;
	size = 0xFFFFFFFCull + be32(ext);
    }

    static u8 zeros[0x10000];
    u64 written = 0;

    if ( gt == NKIT_GAP_ALL_JUNK )
    {
	u8 buf[0x10000];
	u64 rest = size;
	u64 pos = dst_pos;
	while (rest)
	{
	    const u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
	    nkit_junk_get(junk,pos,buf,chunk);
	    if ( fwrite(buf,1,chunk,dest) != chunk )
		return ERROR1(ERR_WRITE_FAILED,"Write failed: NKit restore\n");
	    pos += chunk;
	    rest -= chunk;
	}
	written = size;
    }
    else if ( gt == NKIT_GAP_ALL_SCRUBBED )
    {
	u64 rest = size;
	while (rest)
	{
	    const u32 chunk = rest < sizeof(zeros) ? (u32)rest : sizeof(zeros);
	    if ( fwrite(zeros,1,chunk,dest) != chunk )
		return ERROR1(ERR_WRITE_FAILED,"Write failed: NKit restore\n");
	    rest -= chunk;
	}
	written = size;
    }
    else // NKIT_GAP_MIXED: a stream of 4 byte block records follows
    {
	u64 prg = size;
	u64 pos = dst_pos;
	nkit_block_type_t bt = NKIT_BLOCK_JUNK;
	u8 fill_byte = 0;

	while (prg)
	{
	    u8 be[4];
	    if ((err = nkit_read_exact(src,be,4,fname)))
		return err;
	    const u32 blk = be32(be);
	    const nkit_block_type_t bt_read = (nkit_block_type_t)( blk >> 30 );
	    const bool is_repeat = bt_read == NKIT_BLOCK_REPEAT;
	    if (!is_repeat)
		bt = bt_read;

	    u64 cnt = blk & 0x3FFFFFFFu;
	    u64 bytes;

	    if ( bt == NKIT_BLOCK_NONJUNK )
	    {
		bytes = cnt * NKIT_GAP_BLOCK_SIZE;
		if ( bytes > prg ) bytes = prg;
		u8 buf[0x10000];
		u64 rest = bytes;
		while (rest)
		{
		    const u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
		    if ((err = nkit_read_exact(src,buf,chunk,fname)))
			return err;
		    if ( fwrite(buf,1,chunk,dest) != chunk )
			return ERROR1(ERR_WRITE_FAILED,"Write failed: NKit restore\n");
		    rest -= chunk;
		}
	    }
	    else if ( bt == NKIT_BLOCK_BYTEFILL )
	    {
		if (!is_repeat)
		{
		    fill_byte = (u8)( cnt & 0xFF );
		    cnt >>= 8;
		}
		bytes = cnt * NKIT_GAP_BLOCK_SIZE;
		if ( bytes > prg ) bytes = prg;
		u8 buf[0x10000];
		memset(buf,fill_byte,sizeof(buf));
		u64 rest = bytes;
		while (rest)
		{
		    const u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
		    if ( fwrite(buf,1,chunk,dest) != chunk )
			return ERROR1(ERR_WRITE_FAILED,"Write failed: NKit restore\n");
		    rest -= chunk;
		}
	    }
	    else // NKIT_BLOCK_JUNK
	    {
		bytes = cnt * NKIT_GAP_BLOCK_SIZE;
		if ( bytes > prg ) bytes = prg;
		u8 buf[0x10000];
		u64 rest = bytes, p = pos;
		while (rest)
		{
		    const u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
		    nkit_junk_get(junk,p,buf,chunk);
		    if ( fwrite(buf,1,chunk,dest) != chunk )
			return ERROR1(ERR_WRITE_FAILED,"Write failed: NKit restore\n");
		    p += chunk;
		    rest -= chunk;
		}
	    }

	    prg -= bytes;
	    pos += bytes;
	}
	written = size;
    }

    *out_len = written;
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Full GameCube restoration, direct port of NkitReaderGc.cs's Read(). Wii
// (isGc=false in the original) is NOT implemented -- see the file header
// comment for why.

enumError XExtractNKitGC ( ccp source, ccp dest )
{
    FILE *src = fopen(source,"rb");
    if (!src)
	return ERROR1(ERR_CANT_OPEN,"Can't open file: %s\n",source);

    u8 *hdr = MALLOC(NKIT_HDR_SIZE);
    enumError err = nkit_read_exact(src,hdr,NKIT_HDR_SIZE,source);
    if (err)
	goto abort;

    if ( memcmp(hdr+NKIT_TAG_OFF,"NKIT v01",8) )
    {
	err = ERROR0(ERR_WRONG_FILE_TYPE,"Not an NKit v01 GC image: %s\n",source);
	goto abort;
    }

    const u32 nkit_crc  = be32(hdr+NKIT_CRC_OFF);
    const u32 image_size = be32(hdr+NKIT_SIZE_OFF);
    (void)nkit_crc; // TODO: verify against the restored image once a real GC sample exists to test with

    u8 junk_id[4];
    memcpy(junk_id,hdr+NKIT_JUNKID_OFF,4);
    bool has_junk_override = memcmp(junk_id,"\0\0\0\0",4) != 0;

    // NKit blanks its own tag fields before restoring so the output is a
    // pristine disc header again.
    memset(hdr+NKIT_TAG_OFF,0,0x18);

    const u32 dol_off      = be32(hdr+GC_DOL_OFF);
    const u32 fst_off      = be32(hdr+GC_FST_OFF);
    const u32 fst_size_raw = be32(hdr+GC_FST_SIZE_OFF);
    const u32 fst_size     = fst_size_raw + ( fst_size_raw % 4 ? 4 - fst_size_raw % 4 : 0 );

    if ( fst_off < NKIT_HDR_SIZE )
    {
	err = ERROR0(ERR_WIA_INVALID,"Implausible fst offset in NKit header: %s\n",source);
	goto abort;
    }

    u8 *hdr_to_fst = MALLOC(fst_off - NKIT_HDR_SIZE);
    if ((err = nkit_read_exact(src,hdr_to_fst,fst_off-NKIT_HDR_SIZE,source)))
	goto abort;

    u8 *fst = MALLOC(fst_size);
    if ((err = nkit_read_exact(src,fst,fst_size,source)))
	goto abort;

    u64 src_pos = fst_off + fst_size;

    FILE *out = fopen(dest,"w+b");
    if (!out)
    {
	err = ERROR1(ERR_CANT_CREATE,"Can't create file: %s\n",dest);
	goto abort;
    }

    fwrite(hdr,1,NKIT_HDR_SIZE,out);
    fwrite(hdr_to_fst,1,fst_off-NKIT_HDR_SIZE,out);
    fwrite(fst,1,fst_size,out);

    u64 dst_pos   = fst_off + fst_size;
    u32 dol_off_out = dol_off;	// patched below if the DOL moves

    nkit_fst_file_t *files;
    uint n_files;
    if ((err = nkit_parse_fst_gc(fst,fst_size,&files,&n_files)))
    {
	fclose(out);
	goto abort;
    }

    // NKit stores the GC game id at the very start of the disc header, and
    // the disc number one byte further along -- same layout every GC/Wii
    // header reader in wit already uses elsewhere.
    u8 game_id[4];
    memcpy(game_id,hdr,4);
    u8 disc_no = hdr[6];
    if (has_junk_override)
	memcpy(game_id,junk_id,4);

    nkit_junk_t junk;
    nkit_junk_init(&junk,game_id,disc_no);

    bool first = true;
    for ( uint i = 0; i < n_files; i++ )
    {
	nkit_fst_file_t *f = files + i;

	if (!first)
	{
	    if ( src_pos < f->data_offset )
	    {
		fseeko(src,(off_t)(f->data_offset-src_pos),SEEK_CUR);
		src_pos = f->data_offset;
	    }

	    if ( f->data_offset == dol_off )
		dol_off_out = (u32)dst_pos;

	    u8 be_off[4];
	    write_be32(be_off,(u32)dst_pos);
	    memcpy(fst+f->off_in_fst,be_off,4);

	    // copyFile(): copy the file's data verbatim, padded to a 4 byte
	    // boundary, capped to the image size (the "Star Fox Assault E3
	    // demo" edge case NkitReaderGc.cs itself calls out).
	    if ( f->length )
	    {
		u64 size = f->length + ( f->length % 4 ? 4 - f->length % 4 : 0 );
		if ( image_size - dst_pos < size )
		    size = image_size - dst_pos;

		u8 buf[0x10000];
		u64 rest = size;
		while (rest)
		{
		    const u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
		    if ((err = nkit_read_exact(src,buf,chunk,source)))
		    {
			fclose(out);
			goto abort;
		    }
		    if ( fwrite(buf,1,chunk,out) != chunk )
		    {
			err = ERROR1(ERR_WRITE_FAILED,"Write failed: %s\n",dest);
			fclose(out);
			goto abort;
		    }
		    rest -= chunk;
		}
		src_pos += size;
		dst_pos += size;
	    }
	}

	// Gap after this file (present whenever the next file doesn't
	// start right where this one ended -- the .nkit.iso stream always
	// carries a gap record there, even a zero-length one, mirroring
	// writeGap()'s own "file.GapLength == 0" early-out which still
	// consumes nothing but still must run for the null-file bookkeeping).
	if ( dst_pos < image_size && i+1 < n_files )
	{
	    // A non-zero gap is only present in the stream when the
	    // reconstructed layout actually has one; detecting that without
	    // re-deriving GetConvertFstFiles()'s full gap bookkeeping isn't
	    // reliable from the restore side alone -- every real GC gap
	    // sample needed to nail this down precisely wasn't available
	    // locally (only Wii samples exist here). Until verified, treat
	    // every inter-file boundary as carrying a gap record, which
	    // matches the common case in the original reader.
	    u64 written;
	    if ((err = nkit_decode_gap(src,out,source,&junk,dst_pos,&written)))
	    {
		fclose(out);
		goto abort;
	    }
	    dst_pos += written;
	}

	first = false;
    }

    FREE(files);

    write_be32(hdr+GC_DOL_OFF,dol_off_out);
    fseeko(out,0,SEEK_SET);
    fwrite(hdr,1,NKIT_HDR_SIZE,out);
    fseeko(out,fst_off,SEEK_SET);
    fwrite(fst,1,fst_size,out);

    fclose(out);

    if ( dst_pos != image_size )
	err = ERROR0(ERR_WIA_INVALID,
	    "NKit restore produced %llu bytes, expected %u: %s\n",
	    (u64)dst_pos, image_size, source );

 abort:
    if (src) fclose(src);
    FREE(hdr);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
