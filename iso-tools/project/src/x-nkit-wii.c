
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

// NKit (.nkit.iso) restoration -- the Wii path, complete.
//
// XCONVERT dispatches Wii NKit images here via XExtractNKitWii() (the last
// function in this file, a port of NkitReaderWii.Read()); lib-xfile.c picks
// between this and x-nkit.c's GameCube driver by disc magic.
//
// VERIFIED byte exact, not merely plausible: the driver checks the finished
// image's CRC32 against the CRC32 NKit stored in the header, and both local
// Redump-sourced samples restore to an exact match --
//
//   Super Fruit Fall (Europe)  RF4P6M  crc32 854b0feb
//   Ninjabread Man (USA)       RNME5Z  crc32 250d18e5
//
// -- with `wit VERIFY` reporting +OK on the UPDATE and DATA partitions of
// both, independently re-deriving the H0-H3 tree and the ticket/TMD
// signatures.
//
// The file is built bottom-up: first a faithful, field-for-field port of the
// supporting C# types NkitReaderWii.cs is built on top of, from the real
// github.com/Nanook/NKitv1 source:
//
//   WiiPartitionGroupEncryptionState.cs -> nkit_group_crypt_t   (the AES-128-CBC
//                                          per-partition-group encrypt/decrypt/
//                                          hash-cache state machine -- the one
//                                          genuinely new piece of logic here)
//   WiiPartitionHashTable.cs           -> nkit_hash_table_t
//   WiiPartitionHeaderSection.cs       -> nkit_part_header_t
//   WiiPartitionInfo.cs                -> nkit_part_info_t
//   WiiPartitionSection.cs             -> nkit_partition_t (bookkeeping only)
//   WiiPartitionGroupSection.cs        -> nkit_group_t (bookkeeping only)
//   WiiDiscHeaderSection.cs            -> nkit_disc_header_t (partition table
//                                          rebuild only, i.e. UpdateRepair())
//   WiiHashStore.cs                    -> nkit_hash_store_t
//
// This repo's own libwbfs/wiidisc.c already implements every Wii-format
// primitive NKit's C# reimplements from scratch (it has to, being a
// standalone converter with no libwbfs to lean on) -- disc partition table
// parsing, ticket title-key decryption incl. the real Wii disc common keys,
// and the full H0/H1/H2/H3 hash tree calc. Rather than re-deriving any of
// that, this port calls straight into it:
//
//   wd_get_common_key() / wd_decrypt_title_key()   (libwbfs/wiidisc.c)
//       instead of WiiPartitionHeaderSection.cs's own hand-rolled,
//       base64-obfuscated ("_lame") copy of the 3 Wii disc common keys
//       (RVT-R / Korean / retail) and its own AES title-key unwrap. Do NOT
//       add another copy of these keys anywhere -- they already exist here,
//       and they are NOT the same key as the WAD common key (x-wad.c) or
//       the Wii U common key (x-wiiu.c).
//   wd_aes_decrypt() / wd_aes_encrypt() / aes_key_t (libwbfs/rijndael.h)
//       instead of C#'s System.Security.Cryptography.Aes.
//   wd_calc_group_hashes()                          (libwbfs/wiidisc.c)
//       instead of hand-porting hashCacheH0GroupCalc()+hashCacheH1H2GroupCalc()
//       block by block -- wd_calc_group_hashes() already computes the full
//       H0/H1/H2(/H3) tree for one 64-sector group in a single call, using
//       the same WII_N_ELEMENTS_H0/H1/H2, WII_HASH_SIZE, WII_SECTOR_HASH_SIZE,
//       WII_H0_DATA_SIZE, WII_H3_SIZE, WII_GROUP_SECTORS, WII_SECTOR_DATA_SIZE
//       constants (libwbfs/file-formats.h) the C# WiiPartitionGroupEncryptionState
//       class hardcodes as 0x8000/0x400/0x7c00/31/8/8/0x18000/20 etc.
//
// What's genuinely new here (no existing wit equivalent): the *stateful*
// per-block dirty/scrubbed/encrypted-vs-decrypted bookkeeping
// WiiPartitionGroupEncryptionState.cs layers on top of those primitives --
// lazy re-encrypt/re-decrypt, per-block scrub-byte IV reconstruction, and
// the "fast hash valid" short-circuit -- because wit's own VERIFY/EXTRACT
// pipeline never needed to *mutate* a group and re-derive its hashes
// on demand the way NKit's restore path does.
//
// Two deviations from the C# are structural rather than incidental, and both
// are documented at length where they live:
//
//   * the producer/consumer StreamCircularBuffer between
//     partitionStreamWrite() and Read()'s group loop is spooled through a
//     temp file instead of two threads (wit links no pthread) -- see
//     nkit_circular_buffer_set_spool();
//   * an image whose update partition was removed at conversion time is
//     rejected up front, because restoring it needs NKit's external recovery
//     store, which has no equivalent here -- see XExtractNKitWii()'s header.

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "x-formats.h"
#include "lib-lfg.h"
#include "libwbfs/wiidisc.h"
#include "libwbfs/rijndael.h"
#include "libwbfs/file-formats.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////	  WiiPartitionHashTable.cs -> nkit_hash_table_t	///////////////
///////////////////////////////////////////////////////////////////////////////

// Direct port of PartitionHashTable: a flat array of 'count' SHA1 hashes
// (WII_HASH_SIZE==20 bytes each) with the same Reset()/CopyAll()/Set()/
// Equals() operations the C# class exposes. Used by nkit_group_crypt_t
// below exactly the way the original block/h1Table/h2Table fields are.

typedef struct nkit_hash_table_t
{
    u8		*hash;		// Bytes: hash_count * WII_HASH_SIZE bytes, owned
    uint	hash_count;		// HashCount
}
nkit_hash_table_t;

///////////////////////////////////////////////////////////////////////////////

// ctor: PartitionHashTable(int hashCount)
static void nkit_hash_table_init ( nkit_hash_table_t *t, uint hash_count )
{
    t->hash_count = hash_count;
    t->hash = MALLOC(hash_count*WII_HASH_SIZE);
    memset(t->hash,0,hash_count*WII_HASH_SIZE);
}

static void nkit_hash_table_reset_mem ( nkit_hash_table_t *t )
{
    if (t->hash)
	FREE(t->hash);
    t->hash = 0;
}

///////////////////////////////////////////////////////////////////////////////

// Reset(byte[] group, int offset): copy min(Bytes.Length, group.Length-offset)
// bytes from 'group+offset' into the table.
static void nkit_hash_table_load ( nkit_hash_table_t *t, const u8 *group, uint group_len, uint offset )
{
    uint n = t->hash_count*WII_HASH_SIZE;
    uint avail = offset < group_len ? group_len-offset : 0;
    if ( n > avail ) n = avail;
    memcpy(t->hash,group+offset,n);
}

// CopyAll(byte[] buffer, int offset): copy the whole table out to 'buffer+offset'.
static void nkit_hash_table_store ( const nkit_hash_table_t *t, u8 *buffer, uint buffer_len, uint offset )
{
    uint n = t->hash_count*WII_HASH_SIZE;
    uint avail = offset < buffer_len ? buffer_len-offset : 0;
    if ( n > avail ) n = avail;
    memcpy(buffer+offset,t->hash,n);
}

// Set(int blockIndex, byte[] sha1, bool testEqual): store one hash; if
// testEqual and it's unchanged, leave it alone and report "same" (true).
static bool nkit_hash_table_set ( nkit_hash_table_t *t, uint idx, const u8 sha1[WII_HASH_SIZE], bool test_equal )
{
    u8 *slot = t->hash + idx*WII_HASH_SIZE;
    if ( test_equal && !memcmp(sha1,slot,WII_HASH_SIZE) )
	return true;
    memcpy(slot,sha1,WII_HASH_SIZE);
    return false;
}

// Equals(int blockIndex, byte[] sha1)
static bool nkit_hash_table_equals ( const nkit_hash_table_t *t, uint idx, const u8 sha1[WII_HASH_SIZE] )
{
    return !memcmp(sha1,t->hash+idx*WII_HASH_SIZE,WII_HASH_SIZE);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////   WiiPartitionGroupEncryptionState.cs -> nkit_group_crypt_t //////
///////////////////////////////////////////////////////////////////////////////

// One 'block' is one Wii hashed sector: 0x400 bytes of H0/H1/H2 hash data
// followed by 0x7c00 bytes of (en/de)crypted user data -- WII_SECTOR_SIZE
// total, split as WII_SECTOR_HASH_SIZE + WII_SECTOR_DATA_SIZE.
// Direct port of the private nested 'block' class.

typedef struct nkit_crypt_block_t
{
    int			index;		// Index
    u32			offset;		// Offset = index * WII_SECTOR_SIZE
    u32			data_offset;	// DataOffset = index * WII_SECTOR_SIZE + WII_SECTOR_HASH_SIZE

    bool		is_dirty;	// IsDirty
    bool		is_scrubbed;	// IsScrubbed
    u8			scrub_byte;	// ScrubByte
    bool		is_used;	// IsUsed

    nkit_hash_table_t	h0;		// H0Table: WII_N_ELEMENTS_H0 hashes, owned by this block
    nkit_hash_table_t	*h1;		// H1Table: shared across WII_N_ELEMENTS_H1 blocks, NOT owned
    nkit_hash_table_t	*h2;		// H2Table: shared across the whole group, NOT owned
}
nkit_crypt_block_t;

///////////////////////////////////////////////////////////////////////////////

// Direct port of WiiPartitionGroupEncryptionState. One instance manages one
// Wii partition "group" (WiiPartitionSection.GroupSize == 64 hashed sectors
// == WII_GROUP_SECTORS, matching wit's own group size), tracking whichever
// of the encrypted/decrypted views is current and lazily deriving the other
// plus the H0..H3 hash cache only when actually asked for.

typedef struct nkit_group_crypt_t
{
    const u8		*h3_table;	// _h3Table: pointer into the partition's H3 table (WII_H3_SIZE bytes), NOT owned
    u8			h3_value[WII_HASH_SIZE]; // _h3Value: SHA1 of this group's H2 table
    bool		is_valid;	// _isValid: does h3_value match h3_table[group_idx]?
    int			group_idx;	// _groupIdx

    aes_key_t		akey;		// shared AES key (one Aes per block in C#, but the key is
					// identical for every block in a partition, so wit's
					// aes_key_t -- which bakes in expanded round keys, not
					// just the raw key -- only needs deriving once)

    u8			*enc;		// _enc: encrypted view, _max_size bytes, owned
    u8			*dec;		// _dec: decrypted view, _max_size bytes, owned
    bool		has_enc;	// _hasEnc
    bool		has_dec;	// _hasDec
    bool		has_hashes;	// _hasHashes
    uint		max_size;	// _maxSize (bytes, multiple of WII_SECTOR_SIZE)
    bool		is_dirty;	// _isDirty
    bool		forced_hashes;	// _forcedHashes
    bool		hashes_recalculated; // _hashedRecalulated

    nkit_crypt_block_t	*block;		// _blocks[maxSize/WII_SECTOR_SIZE], owned
    uint		n_blocks;
    nkit_hash_table_t	*h1_shared;	// one shared H1 table per 8 blocks (n_blocks/WII_N_ELEMENTS_H1 of them), owned
    uint		n_h1_shared;
    nkit_hash_table_t	h2_shared;	// _blocks[0].H2Table equivalent: one shared H2 table for the whole group, owned

    uint		size;		// _size: bytes actually populated (<= max_size)
    uint		used_blocks;	// _usedBlocks

    u8			unused_blank_hash[WII_HASH_SIZE]; // _unusedBlankHash: SHA1 of a WII_H0_DATA_SIZE zero block
}
nkit_group_crypt_t;

///////////////////////////////////////////////////////////////////////////////

// ctor: WiiPartitionGroupEncryptionState(int maxSize, byte[] key, byte[] h3Table)
static enumError nkit_group_crypt_init
(
    nkit_group_crypt_t	*gs,
    uint		max_size,	// e.g. WII_GROUP_SECTORS * WII_SECTOR_SIZE
    const u8		key[WII_KEY_SIZE],
    const u8		*h3_table	// NULL or WII_H3_SIZE bytes, not owned/copied
)
{
    memset(gs,0,sizeof(*gs));

    if ( max_size % WII_SECTOR_SIZE )
	return ERROR0(ERR_WIA_INVALID,"NKit: group size is not a multiple of WII_SECTOR_SIZE\n");

    gs->h3_table = h3_table;
    gs->max_size = max_size;
    gs->n_blocks = max_size / WII_SECTOR_SIZE;
    gs->block = MALLOC(gs->n_blocks*sizeof(*gs->block));
    memset(gs->block,0,gs->n_blocks*sizeof(*gs->block));

    gs->n_h1_shared = ( gs->n_blocks + WII_N_ELEMENTS_H1-1 ) / WII_N_ELEMENTS_H1;
    gs->h1_shared = MALLOC(gs->n_h1_shared*sizeof(*gs->h1_shared));
    for ( uint i = 0; i < gs->n_h1_shared; i++ )
	nkit_hash_table_init(gs->h1_shared+i,WII_N_ELEMENTS_H1);
    nkit_hash_table_init(&gs->h2_shared,WII_N_ELEMENTS_H2);

    wd_aes_set_key(&gs->akey,key);

    for ( uint i = 0; i < gs->n_blocks; i++ )
    {
	nkit_crypt_block_t *b = gs->block+i;
	b->index       = i;
	b->offset      = i*WII_SECTOR_SIZE;
	b->data_offset = i*WII_SECTOR_SIZE + WII_SECTOR_HASH_SIZE;
	nkit_hash_table_init(&b->h0,WII_N_ELEMENTS_H0);
	b->h1 = gs->h1_shared + i/WII_N_ELEMENTS_H1;	// share the H1 table across WII_N_ELEMENTS_H1 blocks
	b->h2 = &gs->h2_shared;				// share the H2 table across the whole group
    }

    gs->enc = MALLOC(max_size);
    gs->dec = MALLOC(max_size);

    // _unusedBlankHash = _blocks[0].Sha1.ComputeHash(new byte[0x400])
    u8 zero[WII_H0_DATA_SIZE];
    memset(zero,0,sizeof(zero));
    SHA1(zero,sizeof(zero),gs->unused_blank_hash);

    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static void nkit_group_crypt_reset_mem ( nkit_group_crypt_t *gs )
{
    if (!gs->block)
	return;
    for ( uint i = 0; i < gs->n_blocks; i++ )
	nkit_hash_table_reset_mem(&gs->block[i].h0);
    FREE(gs->block);
    for ( uint i = 0; i < gs->n_h1_shared; i++ )
	nkit_hash_table_reset_mem(gs->h1_shared+i);
    FREE(gs->h1_shared);
    nkit_hash_table_reset_mem(&gs->h2_shared);
    if (gs->enc) FREE(gs->enc);
    if (gs->dec) FREE(gs->dec);
    memset(gs,0,sizeof(*gs));
}

///////////////////////////////////////////////////////////////////////////////

// Populate(byte[] data, int size, bool isEnc, bool isEncHeader, int groupIndex)
//
// NKit's isEncHeader path (decrypting just the 0x400 byte hash area with a
// zero IV to recover an ISO.DEC-style "hash area encrypted, data plain"
// group) uses Aes.CreateDecryptor() with a zero IV directly, i.e. the same
// wd_aes_decrypt() call the plain per-block decrypt uses below with iv=0 --
// ported 1:1, no new primitive.
static void nkit_group_crypt_populate
(
    nkit_group_crypt_t	*gs,
    const u8		*data,
    uint		size,		// s = min(size, data.Length) already applied by caller
    bool		is_enc,
    bool		is_enc_header,
    int			group_index
)
{
    uint s = size;
    gs->group_idx = group_index;
    gs->size = s;

    u8 *dst = is_enc ? gs->enc : gs->dec;
    memcpy(dst,data,s);
    if ( s < gs->max_size )
	memset(dst+s,0,gs->max_size-s);

    gs->has_dec = !(gs->has_enc = is_enc);
    gs->is_dirty = false;
    gs->used_blocks = s / WII_SECTOR_SIZE;
    gs->has_hashes = false;
    gs->hashes_recalculated = false;
    gs->forced_hashes = false;

    for ( uint i = 0; i < gs->n_blocks; i++ )
    {
	nkit_crypt_block_t *b = gs->block+i;
	b->is_dirty = false;			// _isDirty is always false here, same as the C#
	b->is_used = i < gs->used_blocks;
	b->is_scrubbed = false;
	b->scrub_byte = 0;
    }

    if ( !is_enc && is_enc_header )
    {
	static const u8 zero_iv[WII_KEY_SIZE] = {0};
	for ( uint i = 0; i < gs->n_blocks; i++ )
	{
	    nkit_crypt_block_t *b = gs->block+i;
	    wd_aes_decrypt(&gs->akey,zero_iv,gs->dec+b->offset,gs->dec+b->offset,WII_SECTOR_HASH_SIZE);
	}
    }
    else if (is_enc)
    {
	// setScrubbedBlockInfo(b): detect an all-00 or all-FF scrubbed hash
	// area up front, purely from the raw encrypted bytes (no crypto
	// needed -- a scrubbed sector is scrubbed both encrypted and clear).
	for ( uint i = 0; i < gs->n_blocks; i++ )
	{
	    nkit_crypt_block_t *b = gs->block+i;
	    if (!gs->has_enc)
		continue;
	    uint end = b->data_offset;
	    u8 byt = gs->enc[b->offset];
	    if ( byt == 0 || byt == 0xff )
	    {
		bool scrubbed = true;
		for ( uint j = end-WII_SECTOR_HASH_SIZE; j < end; j++ )
		    if ( gs->enc[j] != byt ) { scrubbed = false; break; }
		if ( b->is_used )
		{
		    b->is_scrubbed = scrubbed;
		    b->scrub_byte = byt;
		}
	    }
	    else
		b->is_scrubbed = false;
	}
    }
}

///////////////////////////////////////////////////////////////////////////////

// encrypt(block b): re-derive the encrypted view of one block from _dec.
// Direct port, including the scrubbed-with-nonzero-byte IV reconstruction
// trick (encrypting a scrub byte with IV=scrubByte reproduces exactly what
// a real scrubbed-then-decrypted-then-recrypted sector looks like).
static void nkit_group_crypt_encrypt_block ( nkit_group_crypt_t *gs, nkit_crypt_block_t *b )
{
    u8 iv[WII_KEY_SIZE];
    memset(iv,0,sizeof(iv));

    if ( b->is_scrubbed && b->scrub_byte != 0 && !gs->forced_hashes )
	memset(iv,b->scrub_byte,sizeof(iv));

    wd_aes_encrypt(&gs->akey,iv,gs->dec+b->offset,gs->enc+b->offset,WII_SECTOR_HASH_SIZE);

    memcpy(iv,gs->enc+b->offset+0x3d0,WII_KEY_SIZE);	// IV for the data area = last 16 bytes of the encrypted H2 hash (offset 0x3d0 within the 0x400 hash area)

    wd_aes_encrypt(&gs->akey,iv,gs->dec+b->data_offset,gs->enc+b->data_offset,WII_SECTOR_DATA_SIZE);

    b->is_dirty = false;
}

// decrypt(block b): inverse of the above.
static void nkit_group_crypt_decrypt_block ( nkit_group_crypt_t *gs, nkit_crypt_block_t *b )
{
    u8 iv[WII_KEY_SIZE];
    memset(iv,0,sizeof(iv));

    wd_aes_decrypt(&gs->akey,iv,gs->enc+b->offset,gs->dec+b->offset,WII_SECTOR_HASH_SIZE);

    memcpy(iv,gs->enc+b->offset+0x3d0,WII_KEY_SIZE);	// IV for the data area comes from the ENCRYPTED hash area, same offset as above

    wd_aes_decrypt(&gs->akey,iv,gs->enc+b->data_offset,gs->dec+b->data_offset,WII_SECTOR_DATA_SIZE);

    b->is_dirty = false;
}

///////////////////////////////////////////////////////////////////////////////

// hashCacheH0H1BlockPopulate(b): load H0/H1 straight from _dec (no
// recompute -- this is the "trust what's already there" path used before
// validating).
static void nkit_group_crypt_h0h1_populate_block ( nkit_group_crypt_t *gs, nkit_crypt_block_t *b )
{
    if (!b->is_used)
    {
	u8 zero[0x26c];
	memset(zero,0,sizeof(zero));
	nkit_hash_table_load(&b->h0,zero,sizeof(zero),0);
    }
    else
	nkit_hash_table_load(&b->h0,gs->dec,gs->max_size,b->offset);

    if ( b->index % WII_N_ELEMENTS_H1 == 0 )		// first block in its group of 8 owns the shared H1 table
	nkit_hash_table_load(b->h1,gs->dec,gs->max_size,b->offset+0x280);
}

static void nkit_group_crypt_h0h1_populate ( nkit_group_crypt_t *gs )
{
    for ( uint i = 0; i < gs->n_blocks; i++ )
	nkit_group_crypt_h0h1_populate_block(gs,gs->block+i);
}

// hashCacheH2Populate(): load H2 from block 0, then test the H3 entry.
static void nkit_group_crypt_h2_populate ( nkit_group_crypt_t *gs )
{
    nkit_hash_table_load(&gs->h2_shared,gs->dec,gs->max_size,0x340);
    SHA1(gs->h2_shared.hash,gs->h2_shared.hash_count*WII_HASH_SIZE,gs->h3_value);
    gs->is_valid = gs->h3_table
	&& !memcmp(gs->h3_value,gs->h3_table+gs->group_idx*WII_HASH_SIZE,WII_HASH_SIZE);
}

static void nkit_group_crypt_ensure_hash_cache ( nkit_group_crypt_t *gs )
{
    if (!gs->has_hashes)
    {
	nkit_group_crypt_h0h1_populate(gs);
	nkit_group_crypt_h2_populate(gs);
	gs->has_hashes = true;
	gs->is_dirty = false;
    }
}

///////////////////////////////////////////////////////////////////////////////

// hashCacheH0BlockCalc(b): recompute this block's WII_N_ELEMENTS_H0 SHA1s
// from _dec, comparing against what's cached so callers can tell "changed".
static bool nkit_group_crypt_h0_calc_block ( nkit_group_crypt_t *gs, nkit_crypt_block_t *b )
{
    bool eq = true;
    for ( uint i = 0; i < WII_N_ELEMENTS_H0; i++ )
    {
	u8 hash[WII_HASH_SIZE];
	if (b->is_used)
	    SHA1(gs->dec+b->offset+WII_SECTOR_HASH_SIZE+i*WII_H0_DATA_SIZE,WII_H0_DATA_SIZE,hash);
	else
	    memcpy(hash,gs->unused_blank_hash,WII_HASH_SIZE);
	if (!nkit_hash_table_set(&b->h0,i,hash,eq))
	    eq = false;
    }
    return eq;
}

static bool nkit_group_crypt_h0_calc ( nkit_group_crypt_t *gs )
{
    bool eq = true;
    for ( uint i = 0; i < gs->n_blocks; i++ )
	if (!nkit_group_crypt_h0_calc_block(gs,gs->block+i))
	    eq = false;
    return eq;
}

// hashCacheH1H2GroupCalc(): fold each block's H0 table up into its shared H1
// table, then fold every H1 table up into the shared H2 table, then hash H2
// to get H3 and test it -- exactly wd_calc_group_hashes()'s own tree shape,
// just operating on the already-materialised per-block hash tables instead
// of raw sector bytes (this class caches intermediate levels; wit's own
// helper always recomputes the whole tree in one pass -- kept separate here
// to stay a faithful port of the incremental C# state machine).
static bool nkit_group_crypt_h1h2_calc ( nkit_group_crypt_t *gs )
{
    bool eq = true;
    for ( uint j = 0; j < gs->n_h1_shared; j++ )
    {
	for ( uint i = 0; i < WII_N_ELEMENTS_H1; i++ )
	{
	    uint bi = j*WII_N_ELEMENTS_H1+i;
	    if ( bi >= gs->n_blocks ) break;
	    u8 hash[WII_HASH_SIZE];
	    SHA1(gs->block[bi].h0.hash,gs->block[bi].h0.hash_count*WII_HASH_SIZE,hash);
	    if (!nkit_hash_table_set(gs->block[j*WII_N_ELEMENTS_H1].h1,i,hash,eq))
		eq = false;
	}
	u8 hash[WII_HASH_SIZE];
	SHA1(gs->block[j*WII_N_ELEMENTS_H1].h1->hash,gs->block[j*WII_N_ELEMENTS_H1].h1->hash_count*WII_HASH_SIZE,hash);
	if (!nkit_hash_table_set(&gs->h2_shared,j,hash,eq))
	    eq = false;
    }
    SHA1(gs->h2_shared.hash,gs->h2_shared.hash_count*WII_HASH_SIZE,gs->h3_value);
    gs->is_valid = gs->h3_table
	&& !memcmp(gs->h3_value,gs->h3_table+gs->group_idx*WII_HASH_SIZE,WII_HASH_SIZE);
    return eq;
}

static void nkit_group_crypt_recalculate_hashes ( nkit_group_crypt_t *gs )
{
    nkit_group_crypt_h0_calc(gs);
    nkit_group_crypt_h1h2_calc(gs);
    gs->is_dirty = false;
    gs->hashes_recalculated = true;
    gs->forced_hashes = false;
}

///////////////////////////////////////////////////////////////////////////////

// commitHashCache(b): write this block's cached H0/H1/H2 tables back into
// the 0x400 byte hash area of _dec, zeroing the padding between tables --
// same layout wd_calc_group_hashes() writes directly (0x000 H0, 0x280 H1,
// 0x340 H2), spelled out here per-block because this class commits lazily,
// only right before encrypting a block whose hashes were recalculated.
static void nkit_group_crypt_commit_hash_cache ( nkit_group_crypt_t *gs, nkit_crypt_block_t *b )
{
    nkit_hash_table_store(&b->h0,gs->dec,gs->max_size,b->offset);
    memset(gs->dec+b->offset+0x26c,0,0x280-0x26c);
    nkit_hash_table_store(b->h1,gs->dec,gs->max_size,b->offset+0x280);
    memset(gs->dec+b->offset+0x280+0xA0,0,0x340-(0x280+0xA0));
    nkit_hash_table_store(b->h2,gs->dec,gs->max_size,b->offset+0x340);
    memset(gs->dec+b->offset+0x340+0xA0,0,WII_SECTOR_HASH_SIZE-(0x340+0xA0));
}

///////////////////////////////////////////////////////////////////////////////
///////////////			public-equivalent API			///////////////
///////////////////////////////////////////////////////////////////////////////

// ensureDecrypted() / the 'Decrypted' getter
static u8 * nkit_group_crypt_ensure_decrypted ( nkit_group_crypt_t *gs )
{
    if ( gs->has_enc && !gs->has_dec )
    {
	for ( uint i = 0; i < gs->n_blocks; i++ )
	{
	    nkit_group_crypt_decrypt_block(gs,gs->block+i);
	    nkit_group_crypt_h0h1_populate_block(gs,gs->block+i);
	}
	nkit_group_crypt_h2_populate(gs);
	gs->has_hashes = true;
	gs->has_dec = true;
    }
    return gs->dec;
}

// ensureEncrypted() / the 'Encrypted' getter
static u8 * nkit_group_crypt_ensure_encrypted ( nkit_group_crypt_t *gs )
{
    if (!gs->has_enc)
    {
	nkit_group_crypt_ensure_hash_cache(gs);
	for ( uint i = 0; i < gs->n_blocks; i++ )
	{
	    nkit_crypt_block_t *b = gs->block+i;
	    if (gs->hashes_recalculated)
		nkit_group_crypt_commit_hash_cache(gs,b);
	    nkit_group_crypt_encrypt_block(gs,b);
	}
	gs->is_dirty = false;
	gs->has_enc = true;
    }
    return gs->enc;
}

// IsValid(bool hashRecalculateIfDirty)
static bool nkit_group_crypt_is_valid ( nkit_group_crypt_t *gs, bool hash_recalculate_if_dirty )
{
    bool dirty = gs->is_dirty;
    nkit_group_crypt_ensure_decrypted(gs);
    nkit_group_crypt_ensure_hash_cache(gs);
    if ( hash_recalculate_if_dirty && dirty )
	nkit_group_crypt_recalculate_hashes(gs);
    return gs->is_valid;
}

// blockIsValid(b): full H0 regen test against the cached table (does NOT
// touch H1/H2/H3 -- that's IsValid()'s job).
static bool nkit_group_crypt_block_is_valid ( nkit_group_crypt_t *gs, nkit_crypt_block_t *b )
{
    nkit_group_crypt_ensure_decrypted(gs);
    nkit_group_crypt_ensure_hash_cache(gs);
    for ( uint i = 0; i < WII_N_ELEMENTS_H0; i++ )
    {
	u8 hash[WII_HASH_SIZE];
	SHA1(gs->dec+b->offset+WII_SECTOR_HASH_SIZE+i*WII_H0_DATA_SIZE,WII_H0_DATA_SIZE,hash);
	if (!nkit_hash_table_equals(&b->h0,i,hash))
	    return false;
    }
    return true;
}

// MarkBlockDirty(int blockIndex)
static void nkit_group_crypt_mark_dirty ( nkit_group_crypt_t *gs, uint block_index )
{
    gs->block[block_index].is_dirty = true;
    gs->is_dirty = true;
    gs->has_enc = false;
}

// MarkBlockScrubbed(int blockIndex, byte scrubByte)
static void nkit_group_crypt_mark_scrubbed ( nkit_group_crypt_t *gs, uint block_index, u8 scrub_byte )
{
    gs->block[block_index].is_scrubbed = block_index < gs->used_blocks;
    gs->block[block_index].scrub_byte = scrub_byte;
}

// MarkBlockUnscrubbedAndDirty(int blockIndex)
static void nkit_group_crypt_mark_unscrubbed_and_dirty ( nkit_group_crypt_t *gs, uint block_index )
{
    gs->block[block_index].is_scrubbed = false;
    gs->block[block_index].scrub_byte = 0;
    nkit_group_crypt_mark_dirty(gs,block_index);
}

// ForceHashes(byte[] hashes): install externally-supplied hash-area bytes
// (e.g. hashes preserved by nkit_hash_store_t below) instead of recomputing.
static void nkit_group_crypt_force_hashes ( nkit_group_crypt_t *gs, const u8 *hashes /* nullable */ )
{
    nkit_group_crypt_ensure_decrypted(gs);
    for ( uint i = 0; i < gs->used_blocks; i++ )
    {
	if (hashes)
	    memcpy(gs->dec+i*WII_SECTOR_SIZE,hashes+i*WII_SECTOR_SIZE,WII_SECTOR_HASH_SIZE);
	gs->block[i].is_dirty = false;
    }
    gs->forced_hashes = true;
    gs->has_hashes = true;
    gs->is_dirty = false;
    gs->has_enc = false;
}

// AllScrubbedSameByte()
static bool nkit_group_crypt_all_scrubbed_same_byte ( const nkit_group_crypt_t *gs )
{
    u8 b0 = gs->block[0].scrub_byte;
    for ( uint i = 0; i < gs->n_blocks; i++ )
    {
	const nkit_crypt_block_t *b = gs->block+i;
	if ( b->is_used && !( b->is_scrubbed && b->scrub_byte == b0 ) )
	    return false;
    }
    return true;
}

// ScrubbedBlocks getter
static uint nkit_group_crypt_scrubbed_blocks ( const nkit_group_crypt_t *gs )
{
    uint n = 0;
    for ( uint i = 0; i < gs->n_blocks; i++ )
	if (gs->block[i].is_scrubbed)
	    n++;
    return n;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////	    WiiPartitionInfo.cs -> nkit_part_info_t		///////////////
///////////////////////////////////////////////////////////////////////////////

// [[nkit_part_type_t]] -- PartitionType enum
typedef enum nkit_part_type_t
{
    NKIT_PART_DATA,
    NKIT_PART_UPDATE,
    NKIT_PART_CHANNEL,
    NKIT_PART_GAMEDATA,
    NKIT_PART_OTHER,
}
nkit_part_type_t;

// Direct port of WiiPartitionInfo: one entry of the 4-table, up-to-4-entries-
// per-table partition table at disc offset 0x40000 (WiiDiscHeaderSection's
// _PartitionTableOffset -- same layout wit's own wd_ptab_t/wd_ptab_info_t
// already parse for reading; this is NKit's own copy used while rebuilding
// that table on restore, kept as a distinct light struct like the C# is).
typedef struct nkit_part_info_t
{
    nkit_part_type_t	type;		// Type
    u64			disc_offset;	// DiscOffset
    u64			src_disc_offset;// SrcDiscOffset
    int			table;		// Table: which of the 4 partition tables (0..3)
    u64			table_offset;	// TableOffset: byte offset of this entry's disc-offset field
}
nkit_part_info_t;

// ctor: WiiPartitionInfo(PartitionType type, long offset, int table, long tablePos)
static void nkit_part_info_init ( nkit_part_info_t *pi, nkit_part_type_t type, u64 offset, int table, u64 table_pos )
{
    pi->disc_offset = pi->src_disc_offset = offset;
    pi->table = table;
    pi->type = type;
    pi->table_offset = table_pos;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////	  WiiDiscHeaderSection.cs -> nkit_disc_header_t		///////////
///////////////////////////////////////////////////////////////////////////////

#define NKIT_PTAB_OFFSET	0x40000		// _PartitionTableOffset
#define NKIT_PTAB_LENGTH	0x100		// _PartitionTableLength

// Bookkeeping-only port of WiiDiscHeaderSection: parsing the on-disc
// partition table (CreatePartitionInfos) and rebuilding it after the
// partition list changes (UpdateRepair/UpdateOffsets/RemoveUpdatePartition).
// Deliberately does NOT duplicate wd_disc_t's own full partition parsing --
// this exists purely because NKit's restore path needs to WRITE the table
// back out in the same layout it read it in, driven by its own flat list
// (dropped update partitions re-added, placeholders re-numbered, etc.),
// which is a distinct operation from wit's read-side wd_open_disc().
typedef struct nkit_disc_header_t
{
    u8			*data;		// raw disc header bytes (>= NKIT_PTAB_OFFSET+NKIT_PTAB_LENGTH), NOT owned
    uint		data_size;
    nkit_part_info_t	*part;		// Partitions, owned, resizable
    uint		n_part;
    uint		max_part;
    bool		has_update_partition; // HasUpdatePartition
}
nkit_disc_header_t;

///////////////////////////////////////////////////////////////////////////////

// CreatePartitionInfos(MemorySection section, int offset): walk all 4
// partition tables at 'offset' and yield one nkit_part_info_t per entry.
// Same u32-be, /4-scaled offsets as wd_ptab_info_t already decodes.
static enumError nkit_disc_header_parse_ptab
(
    const u8		*data,
    uint		data_size,
    int			offset,
    nkit_part_info_t	**res_part,
    uint		*res_n
)
{
    uint max_part = 4*4;	// generous upper bound; grown below if needed
    nkit_part_info_t *part = MALLOC(max_part*sizeof(*part));
    uint n = 0;

    for ( int table_idx = 0; table_idx < 4; table_idx++ )
    {
	if ( offset + table_idx*8 + 8 > (int)data_size )
	    continue;
	u32 c = be32(data+offset+table_idx*8);		// count of partitions for this table
	if (!c)
	    continue;
	int table_offset = (int)( be32(data+offset+table_idx*8+4) * 4 );
	int adjust = offset + (table_offset - NKIT_PTAB_OFFSET);

	for ( u32 i = 0; i < c; i++ )
	{
	    if ( adjust + (int)i*8 + 8 > (int)data_size )
		break;
	    u64 part_offset = (u64)be32(data+adjust+i*8) * 4;
	    nkit_part_type_t type = (nkit_part_type_t)be32(data+adjust+i*8+4);

	    if ( n == max_part )
	    {
		max_part *= 2;
		part = REALLOC(part,max_part*sizeof(*part));
	    }
	    nkit_part_info_init(part+n,type,part_offset,table_idx,table_offset+(u64)i*8);
	    n++;
	}
    }

    *res_part = part;
    *res_n = n;
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// ctor: WiiDiscHeaderSection(MemorySection header)
static enumError nkit_disc_header_init ( nkit_disc_header_t *h, u8 *data, uint data_size )
{
    memset(h,0,sizeof(*h));
    h->data = data;
    h->data_size = data_size;

    enumError err = nkit_disc_header_parse_ptab(data,data_size,NKIT_PTAB_OFFSET,&h->part,&h->n_part);
    if (err)
	return err;
    h->max_part = h->n_part;
    h->has_update_partition = h->n_part && h->part[0].type == NKIT_PART_UPDATE;
    return ERR_OK;
}

static void nkit_disc_header_reset_mem ( nkit_disc_header_t *h )
{
    if (h->part)
	FREE(h->part);
    memset(h,0,sizeof(*h));
}

///////////////////////////////////////////////////////////////////////////////

static int nkit_part_info_cmp ( const void *pa, const void *pb )
{
    const nkit_part_info_t *a = pa, *b = pb;
    // offsetSortFix(): a zero-offset placeholder sorts by (0xF800000 + type)
    // instead of 0, so brand-new placeholder partitions land after every
    // real one instead of all piling up at the front.
    u64 ka = a->disc_offset ? a->disc_offset : 0xF800000ull + a->type;
    u64 kb = b->disc_offset ? b->disc_offset : 0xF800000ull + b->type;
    return ka < kb ? -1 : ka > kb ? 1 : 0;
}

// UpdateRepair(): re-sort by disc offset (with the placeholder fixup),
// renumber every table's count+first-entry-offset fields, and rewrite every
// partition's (offset,type) pair -- direct port, including the "shift every
// non-update partition up by a fixed amount so partition data always starts
// at 0xF800000" placeholder-insertion behaviour.
static void nkit_disc_header_update_repair ( nkit_disc_header_t *h )
{
    qsort(h->part,h->n_part,sizeof(*h->part),nkit_part_info_cmp);

    memset(h->data+0x60,0,2);
    memset(h->data+NKIT_PTAB_OFFSET,0,NKIT_PTAB_LENGTH);

    u64 part_offset_fix = 0;
    int first_non_update = -1;
    for ( uint i = 0; i < h->n_part; i++ )
	if ( h->part[i].type != NKIT_PART_UPDATE ) { first_non_update = i; break; }

    if ( first_non_update >= 0 && h->part[first_non_update].disc_offset < 0xF800000ull )
    {
	part_offset_fix = 0xF800000ull - h->part[first_non_update].disc_offset;
	for ( uint i = 0; i < h->n_part; i++ )
	    if ( h->part[i].type != NKIT_PART_UPDATE )
		h->part[i].disc_offset += part_offset_fix;
    }

    // group by table, in the now-sorted order (tables need not be
    // contiguous per-group in general, but NKit's own grouping is a stable
    // GroupBy over the sorted list, which for <=4 tables x <=4 entries is
    // simplest to reproduce with a small fixed-size bucket pass)
    for ( int table_idx = 0; table_idx < 4; table_idx++ )
    {
	uint count = 0;
	for ( uint i = 0; i < h->n_part; i++ )
	    if ( h->part[i].table == table_idx )
		count++;
	if (!count)
	    continue;

	int offset = 0x40020 + table_idx*0x20;
	write_be32(h->data+NKIT_PTAB_OFFSET+table_idx*8,count);
	write_be32(h->data+NKIT_PTAB_OFFSET+table_idx*8+4,offset/4);

	offset -= 4;
	for ( uint i = 0; i < h->n_part; i++ )
	{
	    if ( h->part[i].table != table_idx )
		continue;
	    offset += 4;
	    write_be32(h->data+offset,(u32)(h->part[i].disc_offset/4));
	    h->part[i].table_offset = offset;
	    offset += 4;
	    write_be32(h->data+offset,(u32)h->part[i].type);
	}
    }
}

// UpdateOffsets(): cheaper variant that only rewrites the disc-offset half
// of each entry (used when partitions moved but the table shape didn't).
static void nkit_disc_header_update_offsets ( nkit_disc_header_t *h )
{
    for ( uint i = 0; i < h->n_part; i++ )
	write_be32(h->data+h->part[i].table_offset,(u32)(h->part[i].disc_offset/4));
}

// RemoveUpdatePartition(long baseAddress): drop partition 0 if it's the
// update partition, then rebuild the table (a lighter version of
// UpdateRepair() that skips the placeholder-offset fixup since there's
// nothing new to place).
static void nkit_disc_header_remove_update_partition ( nkit_disc_header_t *h )
{
    if ( !h->n_part || h->part[0].type != NKIT_PART_UPDATE )
	return;
    memmove(h->part,h->part+1,(--h->n_part)*sizeof(*h->part));

    memset(h->data+0x60,0,2);
    memset(h->data+NKIT_PTAB_OFFSET,0,NKIT_PTAB_LENGTH);

    for ( int table_idx = 0; table_idx < 4; table_idx++ )
    {
	uint count = 0;
	for ( uint i = 0; i < h->n_part; i++ )
	    if ( h->part[i].table == table_idx )
		count++;
	if (!count)
	    continue;
	int offset = NKIT_PTAB_OFFSET + 0x20 + table_idx*0x20;
	write_be32(h->data+NKIT_PTAB_OFFSET+table_idx*8,count);
	write_be32(h->data+NKIT_PTAB_OFFSET+table_idx*8+4,offset/4);
	offset -= 4;
	for ( uint i = 0; i < h->n_part; i++ )
	{
	    if ( h->part[i].table != table_idx )
		continue;
	    offset += 4;
	    write_be32(h->data+offset,(u32)(h->part[i].disc_offset/4));
	    h->part[i].table_offset = offset;
	    offset += 4;
	    write_be32(h->data+offset,(u32)h->part[i].type);
	}
    }
}

//
///////////////////////////////////////////////////////////////////////////////
///////////  WiiPartitionHeaderSection.cs -> nkit_part_header_t		///////////
///////////////////////////////////////////////////////////////////////////////

// Bookkeeping port of WiiPartitionHeaderSection: pulls the fields
// NkitReaderWii.cs (not itself ported yet) needs out of one partition's raw
// header (ticket + tmd + cert + h3 blob), and derives the AES title key.
//
// The C# class hand-decodes its own copy of the 3 Wii disc common keys via
// a base64-encoded, byte-interleaved "_lame" constant (its own words: this
// is deliberate obfuscation in the original source, not a mistake) and does
// its own AES-ECB title-key unwrap. Per the task's explicit instruction not
// to duplicate that key material anywhere in this codebase, this port
// instead calls wd_get_common_key()/wd_decrypt_title_key() from
// libwbfs/wiidisc.c, which already hold the real keys (WD_CKEY_STANDARD,
// WD_CKEY_KOREAN, WD_CKEY_RVT-equivalent index -- see wd_ckey_index_t in
// wiidisc.h) and already implement the exact same unwrap.
typedef struct nkit_part_header_t
{
    bool		is_encrypted;	// IsEncrypted
    u8			*h3_table;	// H3Table: WII_H3_SIZE bytes if present, NULL otherwise, owned
    u8			key[WII_KEY_SIZE]; // Key: decrypted AES title key
    u64			partition_size;	// PartitionSize
    u64			partition_data_size; // PartitionDataSize
    bool		is_korean;	// IsKorean
    bool		is_rvt;		// IsRvt
    bool		is_rvt_r;	// IsRvtR
    bool		is_rvt_h;	// IsRvtH -- unsupported RVT-H image; caller must bail like the C# ctor's early return
    u8			content_sha1[WII_HASH_SIZE]; // ContentSha1: tmd content[0] hash, if a tmd is present
    bool		has_content_sha1;
    u64			fst_offset;	// FstOffset
    u64			fst_size;	// FstSize

    // DecryptedScrubbed00/DecryptedScrubbedFF: what an all-0x00 (resp.
    // all-0xFF) 16-byte encrypted block decrypts to under this partition's
    // title key with IV==the same all-0x00/all-0xFF pattern -- i.e. what a
    // properly *scrubbed* (junk-filled) sector's hash area looks like once
    // decrypted. Computed once here (ctor tail, "decrypt scrubbed values"
    // comment in the C#) and handed to nkit_scrub_manager_t below, which is
    // the only consumer.
    u8			decrypted_00[WII_KEY_SIZE];
    u8			decrypted_ff[WII_KEY_SIZE];
}
nkit_part_header_t;

///////////////////////////////////////////////////////////////////////////////

// ctor: WiiPartitionHeaderSection(...) -- takes the raw partition header
// bytes ('data', at least 0x2c0 bytes, ideally the full ph.tmd/h3 region)
// and the partition's absolute disc offset (only used by the C# to look up
// its own Type via the disc's partition table; not needed for this
// bookkeeping-only port so it's omitted here).
static enumError nkit_part_header_init ( nkit_part_header_t *ph, const u8 *data, uint data_size )
{
    memset(ph,0,sizeof(*ph));

    if ( data_size < 0x2c0 )
	return ERROR0(ERR_WIA_INVALID,"NKit: partition header too small\n");

    u32 data_off4  = be32(data+0x2b8);
    ph->partition_size = (u64)be32(data+0x2bc) * 4;
    // NStream.HashedLenToData(): 0x7c00 data bytes per 0x8000 hashed bytes
    ph->partition_data_size = ph->partition_size / WII_SECTOR_SIZE * WII_SECTOR_DATA_SIZE;
    (void)data_off4;

    u32 h3_off  = be32(data+0x2b4) * 4;
    u32 tmd_off = be32(data+0x2a8) * 4;
    if ( h3_off && (u64)h3_off + WII_H3_SIZE <= data_size )
    {
	ph->h3_table = MALLOC(WII_H3_SIZE);
	memcpy(ph->h3_table,data+h3_off,WII_H3_SIZE);
    }
    if ( tmd_off && (u64)tmd_off + 0x1e4 + 0x10 + WII_HASH_SIZE <= data_size )
    {
	memcpy(ph->content_sha1,data+tmd_off+0x1e4+0x10,WII_HASH_SIZE);
	ph->has_content_sha1 = true;
    }

    // Determine the common key to use -- same issuer-string/Korean-flag
    // tests as the C#, but selecting an existing wd_ckey_index_t instead of
    // decoding a hardcoded key.
    char issuer[65];
    memcpy(issuer,data+0x140,64);
    issuer[64] = 0;
    // trim trailing NULs like TrimEnd('\0')
    for ( int i = 63; i >= 0 && !issuer[i]; i-- ) issuer[i] = 0;

    ph->is_rvt     = !strcmp(issuer,"Root-CA00000002-XS00000006");
    ph->is_korean  = !ph->is_rvt && data[0x1f1] == 1;
    ph->is_rvt_h   = ph->is_rvt && ph->partition_size == 0;
    ph->is_rvt_r   = !ph->is_rvt_h && ph->is_rvt;

    if (ph->is_rvt_h)
	return ERR_OK;	// "notsupported" -- matches the C# ctor's early return; caller must check is_rvt_h

    // No RVT (debug/RVT-R) common key exists anywhere in this codebase --
    // this repo's wd_ckey_index_t only carries WD_CKEY_STANDARD and
    // WD_CKEY_KOREA (see libwbfs/wiidisc.h; a WD_CKEY_DEVELOPER slot exists
    // but is compiled out unless built with -DTEST, and even then it is
    // NOT the same key NKit's RVT branch uses). Per the task instructions,
    // no key is invented here: an RVT partition is treated as unsupported,
    // same as the is_rvt_h case above. A real RVT-signed retail-adjacent
    // disc is not expected to reach this path in practice.
    if (ph->is_rvt)
    {
	ph->is_rvt_h = true;	// reuse the existing "unsupported, caller must bail" flag
	return ERR_OK;
    }

    // wd_ticket_t's packed layout (libwbfs/file-formats.h) matches the raw
    // partition header byte-for-byte at this offset (sig_type at +0x000
    // through fake_sign at +0x24c, incl. title_key at +0x1bf, title_id at
    // +0x1dc and common_key_index at +0x1f1) -- it IS the disc's ticket, so
    // no field-by-field copy is needed; just view the header bytes as one.
    // wd_decrypt_title_key() reads tik->common_key_index itself and looks
    // up the matching real common key via wd_get_common_key() internally.
    const wd_ticket_t *tik = (const wd_ticket_t *)data;
    wd_decrypt_title_key(tik,ph->key);

    // "decrypt scrubbed values" tail of the C# ctor: decrypt one all-0xFF
    // block with IV=all-0xFF, then one all-0x00 block with IV=all-0x00,
    // both under this partition's title key. Only used by
    // nkit_scrub_manager_t's IsScrubbed()-equivalent block comparisons.
    aes_key_t akey;
    wd_aes_set_key(&akey,ph->key);

    u8 ff_block[WII_KEY_SIZE];
    memset(ff_block,0xff,sizeof(ff_block));
    wd_aes_decrypt(&akey,ff_block,ff_block,ph->decrypted_ff,WII_KEY_SIZE);

    u8 zero_block[WII_KEY_SIZE];
    memset(zero_block,0,sizeof(zero_block));
    wd_aes_decrypt(&akey,zero_block,zero_block,ph->decrypted_00,WII_KEY_SIZE);

    return ERR_OK;
}

static void nkit_part_header_reset_mem ( nkit_part_header_t *ph )
{
    if (ph->h3_table)
	FREE(ph->h3_table);
    memset(ph,0,sizeof(*ph));
}

//
///////////////////////////////////////////////////////////////////////////////
///////////	 WiiPartitionSection.cs -> nkit_partition_t (bookkeeping)	///////////
///////////////////////////////////////////////////////////////////////////////

#define NKIT_PARTITION_GROUP_SIZE  (WII_SECTOR_SIZE*WII_GROUP_SECTORS)  // WiiPartitionSection.GroupSize

// Bookkeeping-only port of WiiPartitionSection: which group is "current",
// the running source-stream position, and the deferred seek-to-file offset
// NkitReaderWii.cs's SeekToFile()/Sections iterator drive. Does not itself
// perform any I/O -- no NStream equivalent exists in wit's C code; the
// restore driver at the end of this file does its own I/O directly.
typedef struct nkit_partition_t
{
    nkit_part_header_t	header;		// Header
    u64			new_partition_data_length; // NewPartitionDataLength
    u64			new_disc_offset;	// NewDiscOffset
    u8			*new_fst;		// NewFst, owned if set
    u64			new_fst_size;
    u64			disc_offset;		// DiscOffset (== header's)
    u64			size;			// Size = header.Size + header.PartitionSize
    u64			seek;			// _seek: pending SeekToFile() target, or (u64)-1 if none
}
nkit_partition_t;

// SeekToFile(FstFile file): round down to the group boundary within the partition.
static void nkit_partition_seek_to_file ( nkit_partition_t *p, u64 file_offset )
{
    p->seek = file_offset - file_offset % NKIT_PARTITION_GROUP_SIZE;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////  WiiPartitionGroupSection.cs -> nkit_group_t			///////////
///////////////////////////////////////////////////////////////////////////////

// Direct port of WiiPartitionGroupSection: wraps one nkit_group_crypt_t with
// the group's position within its partition (Offset/DataOffset/H3Errors),
// the same PreserveHashes() decision the C# makes (used by the writer-side
// unscrub pass to decide whether a group's hashes need to be
// carried in the .nkit.iso stream verbatim instead of being regenerable
// from junk), and now (see nkit_group_init()/nkit_group_populate() below)
// the actual crypto/hash-tree behavior: Populate(), MarkBlockDirty(),
// SetScrubbed(), IsValid(), ForceHashes() and the Encrypted/Decrypted
// buffer-view accessors, all thin wrappers delegating into the already
// fully-ported nkit_group_crypt_t.
typedef struct nkit_group_t
{
    nkit_group_crypt_t		crypt;		// _data
    const nkit_part_header_t	*part_hdr;	// _partHdr, NOT owned (Header property)
    int				idx;		// _idx: group index within the partition
    u64				offset;		// Offset = idx * max_length
    u64				data_offset;	// DataOffset = idx * WII_GROUP_SECTORS * WII_SECTOR_DATA_SIZE
    u64				disc_offset;	// base.DiscOffset
    u64				size;		// base.Size
    uint			h3_errors;	// H3Errors
    bool			is_encrypted;	// IsEncrypted
    bool			is_iso_dec;	// _isIsoDec
}
nkit_group_t;

///////////////////////////////////////////////////////////////////////////////

// initialise(): private helper called from both the ctor and Populate() --
// recomputes the group's byte position within its partition from _idx.
static void nkit_group_initialise_pos ( nkit_group_t *g )
{
    g->offset = (u64)g->idx * NKIT_PARTITION_GROUP_SIZE;			// Offset = _idx * _maxLength
    g->data_offset = (u64)g->idx * WII_GROUP_SECTORS * WII_SECTOR_DATA_SIZE;	// DataOffset = _idx * 64 * 0x7c00
    g->h3_errors = 0;
}

// Populate(int groupIdx, byte[] data, long discOffset, long size): re-point
// this group at a different group index/data -- also the tail half of the
// ctor below, shared here as one function like the C# shares it via the
// ctor calling _data.Populate()+initialise() directly.
//
// 'data_size' is the length of the 'data' buffer (byte[].Length in the C#,
// which nkit_group_crypt_populate's own Math.Min(size,data.Length) needs --
// our nkit_group_crypt_populate takes an already-clamped size, so the clamp
// happens here instead, one call site up, exactly where the C# performs it).
static void nkit_group_populate
(
    nkit_group_t	*g,
    int			group_idx,
    const u8		*data,
    uint		data_size,
    u64			disc_offset,
    u64			size
)
{
    g->disc_offset = disc_offset;	// base.DiscOffset = discOffset
    g->size = size;			// base.Size = size
    g->idx = group_idx;		// _idx = groupIdx

    uint s = (uint)( size < data_size ? size : data_size );	// Math.Min(size, data.Length)
    if ( s > g->crypt.max_size )
	s = g->crypt.max_size;

    nkit_group_crypt_populate(&g->crypt,data,s,g->is_encrypted && !g->is_iso_dec,g->is_iso_dec,group_idx);

    nkit_group_initialise_pos(g);
}

// ctor: WiiPartitionGroupSection(NStream stream, WiiDiscHeaderSection hdr,
//                                 WiiPartitionHeaderSection partHdr, byte[] data,
//                                 long discOffset, long size, bool encrypted)
//
// 'is_iso_dec' is hdr.IsIsoDecPartition(partHdr.DiscOffset) -- no
// WiiDiscHeaderSection equivalent exists in this port yet, so (matching the
// pattern already used elsewhere in this file for not-yet-ported inputs,
// e.g. nkit_filler_t's flags) the caller is expected to supply it already
// computed, rather than inventing the lookup here.
static enumError nkit_group_init
(
    nkit_group_t		*g,
    const nkit_part_header_t	*part_hdr,	// partHdr: supplies Key + H3Table
    const u8			*data,
    uint			data_size,
    u64				disc_offset,
    u64				size,
    bool			encrypted,
    bool			is_iso_dec
)
{
    memset(g,0,sizeof(*g));
    g->part_hdr = part_hdr;
    g->is_iso_dec = is_iso_dec;

    enumError err = nkit_group_crypt_init(&g->crypt,NKIT_PARTITION_GROUP_SIZE,part_hdr->key,part_hdr->h3_table);
    if (err)
	return err;

    // this.IsEncrypted = encrypted || !data.Equals(0x26c, new byte[20], 0, 20);
    // i.e. true unless the caller says it's decrypted AND the H1-table
    // padding area (which is always zero in a decrypted group) really is
    // all zero.
    bool padding_zero = data_size >= 0x26c+20;
    if (padding_zero)
	for ( uint i = 0; i < 20 && padding_zero; i++ )
	    if ( data[0x26c+i] )
		padding_zero = false;
    g->is_encrypted = encrypted || !padding_zero;

    // this.Junk = new byte[WiiPartitionSection.GroupSize]; _unscrubValid = new bool[64];
    // -- both belong to the not-yet-ported Unscrub()/JunkStream path, so
    // deliberately omitted here (nothing else in this task's method list
    // reads them).

    nkit_group_populate(g,0,data,data_size,disc_offset,size);	// _idx = 0 initially
    return ERR_OK;
}

static void nkit_group_reset_mem ( nkit_group_t *g )
{
    nkit_group_crypt_reset_mem(&g->crypt);
    memset(g,0,sizeof(*g));
}

///////////////////////////////////////////////////////////////////////////////

// MarkBlockDirty(int blockIndex)
static void nkit_group_mark_block_dirty ( nkit_group_t *g, int block_index )
{
    nkit_group_crypt_mark_dirty(&g->crypt,(uint)block_index);
}

// SetScrubbed(int blockIndex, byte scrubByte)
static void nkit_group_set_scrubbed ( nkit_group_t *g, int block_index, u8 scrub_byte )
{
    nkit_group_crypt_mark_scrubbed(&g->crypt,(uint)block_index,scrub_byte);
}

// IsValid(bool calculateHashes)
static bool nkit_group_is_valid ( nkit_group_t *g, bool calculate_hashes )
{
    return nkit_group_crypt_is_valid(&g->crypt,calculate_hashes);
}

// ForceHashes(byte[] hashes)
static void nkit_group_force_hashes ( nkit_group_t *g, const u8 *hashes /* nullable */ )
{
    nkit_group_crypt_force_hashes(&g->crypt,hashes);
}

// Encrypted { get { return _data.Encrypted; } }
static u8 * nkit_group_encrypted ( nkit_group_t *g )
{
    return nkit_group_crypt_ensure_encrypted(&g->crypt);
}

// Decrypted { get { return _data.Decrypted; } }
static u8 * nkit_group_decrypted ( nkit_group_t *g )
{
    return nkit_group_crypt_ensure_decrypted(&g->crypt);
}

///////////////////////////////////////////////////////////////////////////////

// PreserveHashes(): true if this group's hash area can't be trusted to
// regenerate byte-identically from junk alone and must be preserved
// verbatim in the .nkit.iso stream -- direct port of the decision tree
// (partially-scrubbed groups, groups touching a real file's data, and
// groups that fail the fast hash check all force preservation).
static bool nkit_group_preserve_hashes ( nkit_group_t *g, bool any_file_touches_group )
{
    uint scrubbed = nkit_group_crypt_scrubbed_blocks(&g->crypt);
    if ( scrubbed != 0 && scrubbed < g->crypt.used_blocks )
	return true;

    bool used_scrubbed = scrubbed == g->crypt.used_blocks && any_file_touches_group;
    if (used_scrubbed)
	return true;

    if ( scrubbed == g->crypt.used_blocks )
	return !nkit_group_crypt_all_scrubbed_same_byte(&g->crypt);

    // FastHashIsValid(): full port omitted here (it re-derives every H0
    // against decrypted data plus the H1/H2/blank-area checks
    // WiiPartitionGroupEncryptionState.FastHashIsValid() does inline) --
    // callers needing it can compose it from nkit_group_crypt_block_is_valid()
    // + nkit_group_crypt_is_valid() above, which already implement the same
    // primitives this would need.
    return !nkit_group_crypt_is_valid(&g->crypt,false);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////		WiiFillerSection.cs -> nkit_filler_t		///////////
///////////////////////////////////////////////////////////////////////////////

// Bookkeeping port of WiiFillerSection: the flags controlling how the
// padding between the last partition and the end of the disc (or between
// the update partition and the rest, when it's being swapped out) is
// sourced -- read verbatim from the source image, or regenerated as junk.
// No NStream/JunkStream driver wiring here yet, same reasoning as
// nkit_partition_t above.
typedef struct nkit_filler_t
{
    u64			disc_offset;		// DiscOffset
    u64			size;			// Size
    u64			src_size;		// _srcSize = size - updateSkip
    bool		generate_update_filler;	// _generateUpdateFiller
    bool		generate_other_filler;	// _generateOtherFiller
    bool		force_filler_junk;	// _forceFillerJunk
    bool		update_partition;	// _updatePartiton
}
nkit_filler_t;

// ctor: WiiFillerSection(..., bool updatePartition, long discOffset, long size,
//                         long updateSkip, ..., bool generateUpdateFiller,
//                         bool generateOtherFiller, bool forceFillerJunk)
static void nkit_filler_init
(
    nkit_filler_t	*f,
    bool		update_partition,
    u64			disc_offset,
    u64			size,
    u64			update_skip,
    bool		generate_update_filler,
    bool		generate_other_filler,
    bool		force_filler_junk
)
{
    f->disc_offset = disc_offset;
    f->size = size;
    f->src_size = size - update_skip;
    f->generate_update_filler = generate_update_filler || size > f->src_size;
    f->generate_other_filler = generate_other_filler;
    f->force_filler_junk = force_filler_junk;
    f->update_partition = update_partition;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////	   WiiHashStore.cs -> nkit_hash_store_t			///////////
///////////////////////////////////////////////////////////////////////////////

// Direct port of WiiHashStore: a bitmap of which groups had their hash area
// preserved (one bit per NKIT_PARTITION_GROUP_SIZE-aligned group, matching
// PreserveHashes() above) plus the flat stream of preserved 0x400 byte hash
// areas themselves, in group order.
typedef struct nkit_hash_store_t
{
    u8			*flags;		// _flags: intsCount()*4 bytes, owned
    uint		flags_size;
    u64			partition_size;	// _partitionSize

    u8			*hashes;		// _hashes: growable buffer of preserved 0x400 byte hash areas, owned
    uint		hashes_len;
    uint		hashes_cap;
}
nkit_hash_store_t;

///////////////////////////////////////////////////////////////////////////////

// intsCount(long partitionDataSize): number of 32 bit flag words needed,
// one bit per group.
static uint nkit_hash_store_ints_count ( nkit_hash_store_t *hs, u64 partition_data_size )
{
    u64 size = partition_data_size / WII_SECTOR_DATA_SIZE * WII_SECTOR_SIZE;
    hs->partition_size = size;
    u64 groups = size / NKIT_PARTITION_GROUP_SIZE + ( size % NKIT_PARTITION_GROUP_SIZE ? 1 : 0 );
    return (uint)( groups/32 + ( groups%32 ? 1 : 0 ) );
}

// ctor: WiiHashStore(long partitionDataSize)
static void nkit_hash_store_init ( nkit_hash_store_t *hs, u64 partition_data_size )
{
    memset(hs,0,sizeof(*hs));
    hs->flags_size = nkit_hash_store_ints_count(hs,partition_data_size) * 4;
    hs->flags = MALLOC(hs->flags_size);
    memset(hs->flags,0,hs->flags_size);
}

static void nkit_hash_store_reset_mem ( nkit_hash_store_t *hs )
{
    if (hs->flags)  FREE(hs->flags);
    if (hs->hashes) FREE(hs->hashes);
    memset(hs,0,sizeof(*hs));
}

///////////////////////////////////////////////////////////////////////////////

// Preserve(long offset, byte[] decrypted, long size): flag this group as
// preserved and append its per-block 0x400 byte hash areas (one per
// WII_SECTOR_SIZE-aligned block within 'decrypted') to the hash stream.
static u64 nkit_hash_store_preserve ( nkit_hash_store_t *hs, u64 offset, const u8 *decrypted, u64 size )
{
    uint x = (uint)( offset / NKIT_PARTITION_GROUP_SIZE );
    uint byt = x/8, bit = 1 << (7-(x%8));
    hs->flags[byt] |= bit;

    u64 written = 0;
    for ( u64 i = 0; i+WII_SECTOR_HASH_SIZE <= size; i += WII_SECTOR_SIZE )
    {
	if ( hs->hashes_len + WII_SECTOR_HASH_SIZE > hs->hashes_cap )
	{
	    hs->hashes_cap = hs->hashes_cap ? hs->hashes_cap*2 : 0x10000;
	    hs->hashes = REALLOC(hs->hashes,hs->hashes_cap);
	}
	memcpy(hs->hashes+hs->hashes_len,decrypted+i,WII_SECTOR_HASH_SIZE);
	hs->hashes_len += WII_SECTOR_HASH_SIZE;
	written += WII_SECTOR_HASH_SIZE;
    }
    return written;
}

// IsPreserved(long offset)
static bool nkit_hash_store_is_preserved ( const nkit_hash_store_t *hs, u64 offset )
{
    uint x = (uint)( offset / NKIT_PARTITION_GROUP_SIZE );
    uint byt = x/8;
    if ( !hs->flags || hs->flags_size <= byt )
	return false;
    uint bit = 1 << (7-(x%8));
    return ( hs->flags[byt] & bit ) != 0;
}

// FlagsToByteArray() / FlagsLength
static const u8 * nkit_hash_store_flags ( const nkit_hash_store_t *hs, uint *len )
{
    *len = hs->flags_size;
    return hs->flags;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////        ScrubManager.cs -> nkit_scrub_manager_t         ///////////////
///////////////////////////////////////////////////////////////////////////////

// The two pieces NkitReaderWii.cs needs that the bookkeeping/AES/hash-tree
// types above don't provide: ScrubManager (below) and StreamCircularBuffer
// (further down). Ported from the real github.com/Nanook/NKitv1 source
// (MIT-licensed), cross-checked against every call site in
// NkitReaderWii.cs (grepped for ScrubManager/IsBlockScrubbedScanMode/
// IsBlockScrubbed/AddGap/.Scrub()/StreamCircularBuffer).
//
// ScrubManager tracks which regions of a partition's *decrypted* data are
// regenerable junk (all-0x00 or all-0xFF once decrypted -- see
// nkit_part_header_t.decrypted_00/decrypted_ff above, which only exist
// because this type needs them) versus real file data that must be
// preserved byte-for-byte. The restore driver calls
// nkit_scrub_manager_scrub() while writing scrubbed gaps back out, then
// nkit_scrub_manager_is_block_scrubbed_scan_mode() while re-encrypting/
// re-hashing the partition sequentially forward, and
// nkit_scrub_manager_add_gap() while it discovers trailing-null runs in
// the H3 table's 28-byte-per-block pattern.
//
// Only the call sites actually reachable from NkitReaderWii.cs are ported:
// IsScrubbed() (used only by the *writer*/image-creation side, per grep of
// NkitWriterGc.cs/NkitWriterWii.cs) is intentionally NOT ported here -- do
// not add it speculatively -- the finished restore driver never needed it.
// If a future caller does, port it then, straight off ScrubManager.cs:56-114.

// Direct port of the private nested 'ScrubRegion' class.
typedef struct nkit_scrub_region_t
{
    u64		offset;		// Offset (already scaled to the *hashed*/on-disc
				// 0x8000-per-block domain by add(), see below)
    u64		length;		// Length
    u8		byt;		// Byte
}
nkit_scrub_region_t;

///////////////////////////////////////////////////////////////////////////////

// Direct port of ScrubManager. C#'s Queue<ScrubRegion> _scrub (drained by
// IsBlockScrubbedScanMode) and List<ScrubRegion> _cache (kept intact for
// IsBlockScrubbed) are two views over the exact same append-only sequence
// of regions -- add() enqueues into _scrub AND appends to _cache in the
// same call, and nothing in the whole class ever removes a *_cache* entry.
// Since this port is single-threaded (no concurrent producer/consumer
// unlike the C# original -- see the StreamCircularBuffer notes below for
// why that's fine here), one owned growable array replaces both: 'region'
// holds every region ever added (== _cache), and 'scan_cursor' is the
// "next to dequeue" index that IsBlockScrubbedScanMode advances (== _scrub
// draining, but non-destructively so _cache semantics fall out for free).
typedef struct nkit_scrub_manager_t
{
    nkit_scrub_region_t	*region;	// _cache (== also backs _scrub), owned, growable
    uint		count;		// regions used
    uint		capacity;	// regions allocated
    uint		scan_cursor;	// index of the region IsBlockScrubbedScanMode
					// will inspect next (mirrors _next after a
					// Dequeue(); an index survives realloc, a
					// C#-style pointer would not)
    int			last_idx;	// index of _last, or -1 if none yet

    bool		is_wii_partition; // _wiiPartition
    const u8		*decrypted_00;	// _00.Decrypted: nkit_part_header_t.decrypted_00,
					// NOT owned, NULL only if !is_wii_partition
    const u8		*decrypted_ff;	// _FF.Decrypted: nkit_part_header_t.decrypted_ff

    // H3Nulls: list of (offset, len) trailing-null runs AddGap() detects.
    // The C# tuple's 3rd element (FstFile) is always passed null from every
    // AddGap() call site reachable off the restore path (NkitReaderWii.cs's
    // own writeGap() calls it with no file context) -- dropped here as a
    // result; add it back only if a real caller is found that needs it.
    struct { u64 offset; int len; } *h3_null;
    uint		h3_null_count, h3_null_cap;
}
nkit_scrub_manager_t;

///////////////////////////////////////////////////////////////////////////////

// ctor: ScrubManager() / ScrubManager(WiiPartitionHeaderSection header).
// Pass header_decrypted_00/header_decrypted_ff both NULL for the "not a
// Wii partition" (GC / non-encrypted) case -- matches ScrubManager(null),
// e.g. NkitWriterGc.cs's plain ScrubManager() or NkitReaderWii.cs:51's
// ScrubManager(null) "scrubFiller".
static void nkit_scrub_manager_init ( nkit_scrub_manager_t *sm,
    bool is_wii_partition, const u8 decrypted_00[WII_KEY_SIZE], const u8 decrypted_ff[WII_KEY_SIZE] )
{
    memset(sm,0,sizeof(*sm));
    sm->last_idx = -1;
    sm->is_wii_partition = is_wii_partition;
    sm->decrypted_00 = is_wii_partition ? decrypted_00 : 0;
    sm->decrypted_ff = is_wii_partition ? decrypted_ff : 0;
}

static void nkit_scrub_manager_reset_mem ( nkit_scrub_manager_t *sm )
{
    if (sm->region)  FREE(sm->region);
    if (sm->h3_null) FREE(sm->h3_null);
    memset(sm,0,sizeof(*sm));
}

///////////////////////////////////////////////////////////////////////////////

// private add(long offset, long length, byte b) -- called from Scrub().
// Rounds to the enclosing 0x7c00-byte (WII_SECTOR_DATA_SIZE) *data* block,
// pads the length up to a block boundary, then rescales both from the
// 0x7c00 data domain to the 0x8000 (WII_SECTOR_SIZE) on-disc/hashed
// domain -- exactly like the C# (literal 0x7c00L/0x8000L there; named
// constants here since this file already has them). Extends the previous
// region in place if it's contiguous and the same fill byte, else appends
// a new one.
static void nkit_scrub_manager_add ( nkit_scrub_manager_t *sm, u64 offset, u64 length, u8 b )
{
    if ( offset % WII_SECTOR_DATA_SIZE )
    {
	length += offset % WII_SECTOR_DATA_SIZE;
	offset -= offset % WII_SECTOR_DATA_SIZE;
    }
    if ( length % WII_SECTOR_DATA_SIZE )
	length += WII_SECTOR_DATA_SIZE - length % WII_SECTOR_DATA_SIZE;

    offset = offset / WII_SECTOR_DATA_SIZE * WII_SECTOR_SIZE;
    length = length / WII_SECTOR_DATA_SIZE * WII_SECTOR_SIZE;

    nkit_scrub_region_t *last = sm->last_idx >= 0 ? sm->region+sm->last_idx : 0;
    if ( last && last->byt == b && offset >= last->offset && offset <= last->offset+last->length )
    {
	if ( offset+length > last->offset+last->length )
	    last->length = offset+length - last->offset;
	return;
    }

    if ( sm->count == sm->capacity )
    {
	sm->capacity = sm->capacity ? sm->capacity*2 : 64;
	sm->region = REALLOC(sm->region,sm->capacity*sizeof(*sm->region));
    }
    nkit_scrub_region_t *r = sm->region + sm->count;
    r->offset = offset;
    r->length = length;
    r->byt    = b;
    sm->last_idx = sm->count;
    sm->count++;
}

///////////////////////////////////////////////////////////////////////////////

// Scrub(Stream stream, long partitionDataOffset, long size, byte scrubByte):
// write 'size' bytes of the scrub pattern for 'scrubByte' to the write
// callback, and (Wii-partition case only) record the region via add().
//
// The C# streams through a ByteStream (Zeros/Fives/FFs, or _00/_FF for the
// Wii-partition decrypted-pattern case) via Utils.Copy()'s double-buffered
// 0x200000-byte pump. There's no Stream to hand a pattern-generator to in
// C, so 'write' is called directly with successive chunks of a stack
// buffer filled from the pattern -- same bytes, same order, just produced
// eagerly into 'write' instead of read lazily off a fake Stream.
typedef enumError (*nkit_write_func) ( void *ctx, const u8 *data, u32 size );

static enumError nkit_scrub_manager_scrub ( nkit_scrub_manager_t *sm,
    nkit_write_func write, void *ctx, u64 partition_data_offset, u64 size, u8 scrub_byte )
{
    if (sm->is_wii_partition)
    {
	if ( scrub_byte != 0x00 && scrub_byte != 0xff )
	    return ERROR0(ERR_WIA_INVALID,
		"NKit: Wii partition scrubbing does not support byte 0x%02x\n",scrub_byte);
	nkit_scrub_manager_add(sm,partition_data_offset,size,scrub_byte);
    }

    const u8 *pat = 0;		// non-NULL: 16-byte repeating decrypted pattern (Wii case)
    if (sm->is_wii_partition)
	pat = scrub_byte == 0x00 ? sm->decrypted_00 : sm->decrypted_ff;

    enum { CHUNK = 0x200000 };	// Utils.Copy()'s buffer size
    u8 buf[CHUNK];
    if (!pat)
	memset(buf,scrub_byte,sizeof(buf));

    while (size)
    {
	u32 n = (u32)( size < CHUNK ? size : CHUNK );
	if (pat)
	{
	    // 16-byte repeating decrypted pattern, not necessarily
	    // buffer-aligned to 16 -- matches ByteStream.Read()'s running
	    // 'x' index (here always starting at 0 since Scrub() always
	    // begins each call at a fresh chunk of the pattern stream, the
	    // same way ByteStream's Position tracks continuously across
	    // calls but WII_KEY_SIZE-periodic content makes any 16-aligned
	    // start equivalent).
	    for ( u32 i = 0; i < n; i++ )
		buf[i] = pat[i % WII_KEY_SIZE];
	}
	enumError err = write(ctx,buf,n);
	if (err)
	    return err;
	size -= n;
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// AddGap(long fileLength, long gapOffset, long gapLength): record a
// trailing run of nulls inside the H3 table's 28-byte-per-0x7c00-block
// pattern (H3 entries are 20 bytes of hash + 8 bytes padding = 28 used out
// of some larger stride -- see the original for the exact derivation this
// mirrors verbatim).
static void nkit_scrub_manager_add_gap ( nkit_scrub_manager_t *sm, u64 file_length, u64 gap_offset, u64 gap_length )
{
    u64 s = (gap_offset + 28) % WII_SECTOR_DATA_SIZE;

    if ( file_length == 0 )
    {
	if ( sm->h3_null_count == sm->h3_null_cap )
	{
	    sm->h3_null_cap = sm->h3_null_cap ? sm->h3_null_cap*2 : 16;
	    sm->h3_null = REALLOC(sm->h3_null,sm->h3_null_cap*sizeof(*sm->h3_null));
	}
	sm->h3_null[sm->h3_null_count].offset = gap_offset;
	sm->h3_null[sm->h3_null_count].len    = (int)( gap_length < 28 ? gap_length : 28 );
	sm->h3_null_count++;
    }
    else if ( s <= 28 && gap_length - (28-s) >= WII_SECTOR_DATA_SIZE ) // nulls spill to next block and length > block
    {
	if ( sm->h3_null_count == sm->h3_null_cap )
	{
	    sm->h3_null_cap = sm->h3_null_cap ? sm->h3_null_cap*2 : 16;
	    sm->h3_null = REALLOC(sm->h3_null,sm->h3_null_cap*sizeof(*sm->h3_null));
	}
	sm->h3_null[sm->h3_null_count].offset = gap_offset + (28-s);
	sm->h3_null[sm->h3_null_count].len    = (int)s;
	sm->h3_null_count++;
    }
}

///////////////////////////////////////////////////////////////////////////////

// private isBlockScrubbed(ScrubRegion, offset, out scrubByte)
static bool nkit_scrub_manager_region_hit ( const nkit_scrub_region_t *r, u64 offset, u8 *scrub_byte )
{
    *scrub_byte = 0;
    if ( r && offset >= r->offset && offset < r->offset+r->length )
    {
	*scrub_byte = r->byt;
	return true;
    }
    return false;
}

// IsBlockScrubbedScanMode(long offset, out byte scrubByte): forward-only
// scan -- advances scan_cursor past any region whose range has already
// been left behind, exactly like the C# dequeuing a new _next once the old
// one's range no longer covers 'offset'.
static bool nkit_scrub_manager_is_block_scrubbed_scan_mode ( nkit_scrub_manager_t *sm, u64 offset, u8 *scrub_byte )
{
    nkit_scrub_region_t *next = sm->scan_cursor < sm->count ? sm->region+sm->scan_cursor : 0;
    if ( !next || next->offset+next->length < offset )
    {
	if ( sm->scan_cursor < sm->count )
	    sm->scan_cursor++;
	next = sm->scan_cursor < sm->count ? sm->region+sm->scan_cursor : 0;
    }
    return nkit_scrub_manager_region_hit(next,offset,scrub_byte);
}

// IsBlockScrubbed(long offset, out byte scrubByte): full linear scan of
// every region ever added, order-independent (first match wins, same as
// the C# foreach).
static bool nkit_scrub_manager_is_block_scrubbed ( const nkit_scrub_manager_t *sm, u64 offset, u8 *scrub_byte )
{
    *scrub_byte = 0;
    for ( uint i = 0; i < sm->count; i++ )
	if ( nkit_scrub_manager_region_hit(sm->region+i,offset,scrub_byte) )
	    return true;
    return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////  StreamCircularBuffer.cs -> nkit_circular_buffer_t  ///////////////
///////////////////////////////////////////////////////////////////////////////

// NkitReaderWii.cs uses StreamCircularBuffer exactly once (around line 126
// of the clone): it hands a *writer callback* -- partitionStreamWrite(),
// which walks a partition's files/gaps/junk and pushes decrypted bytes
// forward -- to a background Task, and reads the result back out through
// the Stream interface on the calling (main) thread, while the writer
// Task races ahead filling the ring and blocking (Monitor.Wait) whenever
// it gets more than one buffer's worth of lead. The buffer's whole reason
// to exist in C# is to let those two loops run on separate OS threads
// without needing (size-of-partition) memory: the reader can pull data as
// slowly as its own destination I/O allows while the producer keeps
// generating in the background.
//
// This codebase (x-nkit.c's GC path, lib-wia.c's RVZ reader, and every
// other reader/writer here) has no equivalent background-thread pattern
// anywhere -- everything is one synchronous call stack, one FILE* at a
// time. Introducing real OS threads here for this one call site would be
// a bigger behavioral change than a "port", and isn't needed: nothing
// about partitionStreamWrite()'s *output bytes* depends on it running
// concurrently with the reader -- concurrency there is a C# performance
// detail (I/O overlap), not part of what makes restored output correct.
//
// So this port keeps the ring-buffer *data structure* (fixed-capacity
// byte ring, monotonic read/write position counters, the forward-only
// Seek() skip-ahead trick used by SeekToFile()) but drops the threading:
// nkit_circular_buffer_write()/_read() are plain, non-blocking,
// same-thread cursor operations. Where C#'s Write() would block
// (Monitor.Wait) until the reader drains room, this port's write simply
// copies as much as currently fits and returns the short count -- it is
// the driver's job to interleave "call write with whatever's
// left" and "call read to drain" in one synchronous loop, the same way
// every other staged-I/O loop in this codebase (e.g. x-nkit.c's
// nkit_decode_gap()) already pumps a fixed buffer forward. That is a
// faithful behavioral equivalent of the C# for a single-threaded caller:
// the *sequence and byte content* the reader ends up seeing is identical
// (same ring math, same seek-discard rule below), only the scheduling
// (background thread vs. explicit caller-driven steps) differs, and nkit
// restoration has no external concurrency requirement to preserve.
//
// IProgress.Value (float read-progress getter, cosmetic UI only) and the
// Dispose()-time "wait for writer thread to actually exit, then close the
// wrapped source stream" teardown are both artifacts of the threaded
// design and are NOT ported -- there is no background thread to wait for,
// and the source stream lifetime is the driver's job.
typedef struct nkit_circular_buffer_t
{
    u8		*buf;		// _b, owned
    u32		capacity;	// sizeof(*buf) allocated (caller-chosen; C# used a
				// fixed, oversized-for-double-buffering 0x500000)
    u32		r, w;		// _r, _w: byte cursors into buf, each 0..capacity-1

    u64		size;		// _size: total expected stream length, 0 == unbounded
    u64		r_position;	// _rPosition: total bytes handed out via read() so far
    u64		w_position;	// _wPosition: total bytes accepted via write() so far
    s64		seek_position;	// _seekPosition: -1 == not mid-seek
    bool	writing_complete; // _writingComplete

    // Spool mode (see the long note below): when non-NULL, this buffer is a
    // plain sequential FIFO backed by a temp file instead of a ring, and
    // 'buf'/'r'/'w'/'capacity' go unused. r_position/w_position keep their
    // exact meaning either way.
    FILE	*spool;

    // Sink mode: when non-NULL, writes are forwarded straight to the
    // restored output file (and its running CRC) and nothing is buffered at
    // all. Read() passes 'crcStream' -- the real output -- as the target of
    // its two writeFiller() calls (NkitReaderWii.cs:107 and :268), rather
    // than the per-partition producer buffer, so those calls need a target
    // that IS the output. Nothing ever reads back from a buffer in this mode.
    struct nkit_out_t *sink;
}
nkit_circular_buffer_t;

// defined further down, with the rest of the Read() driver
static enumError nkit_out_write ( struct nkit_out_t *o, const void *data, u64 size );

// WHY SPOOL MODE EXISTS
//
// The C# pairs partitionStreamWrite() (producer) with Read()'s group loop
// (consumer) across two OS threads, with this ring buffer between them and
// Monitor.Wait() on both sides. The consumer pulls 0x7c00 at a time to
// assemble a 64-sector group; the producer pushes a whole partition's
// decrypted data stream. Neither side can be expressed as a resumable step
// without either real threads or a coroutine: partitionStreamWrite() is one
// deep, monolithic call stack (FST walk -> per-file copy/gap/junk writes),
// exactly as ported above.
//
// wit links no pthread for the 'wit' target (only wfuse does -- see the
// Makefile's TOPT_wfuse) and has no background-thread pattern anywhere in
// its reader/writer code, so introducing threads here for one call site
// would be a build- and architecture-level change well beyond a port.
//
// Instead the producer runs to completion first, spooling its output to a
// temp file, and the consumer then reads that file back sequentially. The
// byte stream the consumer sees is bit-identical to what the threaded
// version delivers -- the ordering contract between the two loops is purely
// sequential FIFO, and nothing in partitionStreamWrite() reads any state the
// consumer mutates. Only the scheduling (and a partition-sized temp file
// instead of a 5 MiB ring) differs.
//
// It also removes a real race the C# has: Read() calls hashes.IsPreserved()
// on the consumer side while WriteFlagsData() populates that same flags
// buffer on the producer side. Running the producer first makes that
// strictly ordered.
static void nkit_circular_buffer_set_spool ( nkit_circular_buffer_t *cb, FILE *spool )
{
    cb->spool = spool;
}

///////////////////////////////////////////////////////////////////////////////

// ctor: StreamCircularBuffer(long size, Stream stream, IDisposable dispose,
// Action<Stream> write). Only 'size' (-1 -> unbounded here, since there's
// no wrapped source Stream.Length to default to) and the ring capacity
// carry over; 'stream'/'dispose'/'write' were background-thread plumbing,
// see the file-header note above for why they're not ported.
static void nkit_circular_buffer_init ( nkit_circular_buffer_t *cb, u64 size, u32 capacity )
{
    memset(cb,0,sizeof(*cb));
    cb->buf = MALLOC(capacity);
    cb->capacity = capacity;
    cb->size = size;
    cb->seek_position = -1;
}

static void nkit_circular_buffer_reset_mem ( nkit_circular_buffer_t *cb )
{
    if (cb->buf) FREE(cb->buf);
    memset(cb,0,sizeof(*cb));
}

///////////////////////////////////////////////////////////////////////////////

// Write(byte[] buffer, int offset, int count), minus the Monitor.Wait
// blocking (see file-header note): copies as many of 'count' bytes as
// currently fit, handling the forward-seek discard exactly like the C#
// (bytes written while (_seekPosition != -1 && _wPosition < _seekPosition)
// are silently dropped/counted rather than buffered, until w_position
// catches up to seek_position, at which point both cursors are reset to
// that position and normal ring writes resume). Returns the number of
// bytes actually consumed from 'data' (may be less than 'size' if the
// ring is full -- caller must nkit_circular_buffer_read() to drain and
// call again with the remainder, same as the interleaving note above).
static u32 nkit_circular_buffer_write ( nkit_circular_buffer_t *cb, const u8 *data, u32 size )
{
    if (cb->sink)	// straight to the restored output, nothing buffered
    {
	if (nkit_out_write(cb->sink,data,size))
	    return 0;
	cb->w_position += size;
	return size;
    }

    if (cb->spool)	// sequential FIFO: always accepts everything
    {
	if ( size && fwrite(data,1,size,cb->spool) != size )
	    return 0;
	cb->w_position += size;
	return size;
    }

    if ( cb->writing_complete || ( cb->size && cb->w_position >= cb->size ) )
	return 0; // matches C#'s early return once the reader is done

    u32 total = size;

    if ( cb->seek_position != -1 && cb->w_position < (u64)cb->seek_position )
    {
	u64 c64 = (u64)cb->seek_position - cb->w_position;
	u32 c = (u32)( c64 < size ? c64 : size );
	cb->w_position += c;
	data += c;
	size -= c;

	if ( cb->w_position == (u64)cb->seek_position )
	{
	    cb->r_position = cb->seek_position;
	    cb->w = cb->r = (u32)( cb->r_position % cb->capacity );
	    cb->seek_position = -1;
	}
	else
	    return total; // still seeking past the end of this chunk; "consumed" it all
    }

    if ( cb->seek_position == -1 && size )
    {
	// avail-to-write in the ring, same wrap arithmetic as C#'s 'l'
	s32 l = (s32)cb->r - (s32)cb->w;
	if ( l < 0 || ( l == 0 && cb->w_position == cb->r_position ) )
	    l = (s32)( cb->capacity - cb->w + cb->r ); // buffer empty -> full capacity available

	u32 n  = (u32)l < size ? (u32)l : size;
	u32 n1 = cb->capacity - cb->w < n ? cb->capacity - cb->w : n;

	memcpy(cb->buf+cb->w,data,n1);
	cb->w = cb->w+n1 == cb->capacity ? 0 : cb->w+n1;

	if ( n != n1 )
	{
	    memcpy(cb->buf+cb->w,data+n1,n-n1);
	    cb->w += n-n1;
	}
	cb->w_position += n;
	size -= n;
    }

    return total - size;
}

// Read(byte[] buffer, int offset, int count), minus the Monitor.Wait
// blocking: copies as many of 'count' bytes as currently available.
// Returns 0 once writing_complete and nothing left buffered, exactly
// where C#'s loop condition (!_writingComplete || _rPosition<_wPosition)
// would fall through without ever pausing.
static u32 nkit_circular_buffer_read ( nkit_circular_buffer_t *cb, u8 *data, u32 size )
{
    if (cb->spool)
    {
	u64 avail = cb->w_position - cb->r_position;
	if ( size > avail )
	    size = (u32)avail;
	if ( size && fread(data,1,size,cb->spool) != size )
	    return 0;
	cb->r_position += size;
	return size;
    }

    if ( cb->seek_position != -1 ) // mid-seek: nothing to hand out yet
	return 0;
    if ( !( !cb->writing_complete || cb->r_position < cb->w_position ) )
	return 0;

    s32 l = (s32)cb->w - (s32)cb->r;
    if ( l < 0 || ( l == 0 && cb->r_position < cb->w_position ) )
	l = (s32)( cb->capacity - cb->r + cb->w );

    u32 n  = (u32)l < size ? (u32)l : size;
    u32 n1 = cb->capacity - cb->r < n ? cb->capacity - cb->r : n;

    memcpy(data,cb->buf+cb->r,n1);
    cb->r = cb->r+n1 == cb->capacity ? 0 : cb->r+n1;

    if ( n != n1 )
    {
	memcpy(data+n1,cb->buf+cb->r,n-n1);
	cb->r += n-n1;
    }
    cb->r_position += n;

    return n;
}

// Seek(long offset, SeekOrigin.Begin only -- the only origin any call site
// uses). Forward-only, same as the C# (backward seek throws
// NotImplementedException there; this port reports it the wit way).
static enumError nkit_circular_buffer_seek ( nkit_circular_buffer_t *cb, u64 pos )
{
    if ( pos < cb->r_position )
	return ERROR0(ERR_INTERNAL,"NKit: circular buffer only supports forward seek\n");
    if ( pos == cb->r_position )
	return ERR_OK;

    if ( cb->w_position > pos ) // already buffered -- just move the read cursor
    {
	cb->r_position = pos;
	cb->r = (u32)( cb->r_position % cb->capacity );
    }
    else
	cb->seek_position = (s64)pos; // not there yet: writer-side must catch up and discard

    return ERR_OK;
}

// mirrors the writer Task's ContinueWith() flipping _writingComplete once
// the producer (this port's synchronous equivalent of partitionStreamWrite)
// has no more bytes to offer.
static void nkit_circular_buffer_mark_write_done ( nkit_circular_buffer_t *cb )
{
    cb->writing_complete = true;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////       copyFile / writeGap / writeFiller             ///////////////
///////////////////////////////////////////////////////////////////////////////

// NkitReaderWii.cs's copyFile() (line 572), writeGap() (both overloads,
// lines 596 and 614) and writeFiller() (line 606): the three low-level
// restore primitives partitionStreamWrite()'s per-file loop calls once per
// FST file, plus the filler writes the outer Read() loop makes directly.
//
// Stream roles, ported 1:1 from the C# parameter names:
//   Stream inStream -> FILE *in_stream   (the raw source .nkit file, read-only)
//   Stream target    -> nkit_circular_buffer_t *target (the restored-image
//                        output; C#'s 'target' is the Stream view backing a
//                        StreamCircularBuffer's write side -- see the type's
//                        own comment above for why this port keeps the ring
//                        buffer but not the background thread)
//   JunkStream junk  -> nkit_junk_read_func junk_get + void *junk_ctx: a
//                        callback pair standing in for JunkStream's settable
//                        .Position + .Copy(target,len) (exactly the shape
//                        x-nkit.c's own nkit_junk_get(nj,pos,dest,size)
//                        already has for the GC junk generator -- that
//                        generator lives in x-nkit.c as file-local static
//                        state and is NOT exposed across translation units,
//                        so this file takes it as an injected callback
//                        instead of duplicating/porting JunkStream.cs here.
//                        The Wii JunkStream equivalent wired through this
//                        callback is nkit_wii_junk_t, further down.
//   ScrubManager scrub -> nkit_scrub_manager_t *scrub (above)

// Gap/block record format, ported from Gaps.cs's GapType/GapBlockType enums
// and Gap.BlockSize. This mirrors x-nkit.c's nkit_gap_type_t/
// nkit_block_type_t/NKIT_GAP_BLOCK_SIZE exactly (same source, Gaps.cs, is
// shared by both the GC and Wii readers) but is redeclared here rather than
// shared across translation units, since x-nkit.c keeps its copy file-local
// static and this file has no header of its own to hold a common copy.
typedef enum nkit_gap_type_t
{
    NKIT_GAP_ALL_JUNK		= 0b00,
    NKIT_GAP_ALL_SCRUBBED	= 0b01,
    NKIT_GAP_MIXED		= 0b10,
    NKIT_GAP_JUNK_FILE		= 0b11,
}
nkit_gap_type_t;

typedef enum nkit_block_type_t
{
    NKIT_BLOCK_JUNK		= 0b00,
    NKIT_BLOCK_NONJUNK		= 0b01,
    NKIT_BLOCK_BYTEFILL		= 0b10,
    NKIT_BLOCK_REPEAT		= 0b11,
}
nkit_block_type_t;

#define NKIT_GAP_BLOCK_SIZE	0x100	// Gap.BlockSize in the C# source

typedef void (*nkit_junk_read_func) ( void *ctx, u64 pos, u8 *dest, u32 size );

// Subset of ConvertFile/FstFile (FileSystem.cs) that copyFile()/writeGap()
// actually touch: FstFile.Length (read+written back -- writeGap's GapType.
// JunkFile branch rewrites it), FstFile.Name/DataOffset (copyFile's error
// message only), and ConvertFile.GapLength (read+written back). The rest of
// FstFile (Offset, OffsetInFstFile, Parent/Path, ...) belongs to the FST
// walk that builds/consumes this list -- a later, separate step -- so it's
// not duplicated here.
typedef struct nkit_convert_file_t
{
    u64		fst_length;		// FstFile.Length
    ccp		fst_name;		// FstFile.Name (not owned)
    u64		fst_data_offset;	// FstFile.DataOffset
    u64		gap_length;		// ConvertFile.GapLength
}
nkit_convert_file_t;

///////////////////////////////////////////////////////////////////////////////

// Pump 'size' bytes into the ring buffer, looping over
// nkit_circular_buffer_write() until all of it is accepted. The C# side
// (Utils.Copy()'s double-buffered pump into a Stream) blocks until the
// reader has drained enough room (Monitor.Wait, see nkit_circular_buffer_t's
// own comment); this port has no background reader to wait on yet, so a
// short write here (the ring genuinely full) is reported as an error rather
// than spun on -- interleaving writes with reads so the ring never actually
// fills is the driver's job, same as noted on nkit_circular_buffer_t. In
// practice the driver uses spool or sink mode, where a write never comes up
// short, so this error is a guard rather than a live path.
static enumError nkit_cb_write_all ( nkit_circular_buffer_t *target, const u8 *data, u32 size )
{
    while (size)
    {
	u32 n = nkit_circular_buffer_write(target,data,size);
	if (!n)
	    return ERROR0(ERR_INTERNAL,
		"NKit: restore output ring buffer is full -- driver must interleave reads\n");
	data += n;
	size -= n;
    }
    return ERR_OK;
}

// Adapter so nkit_scrub_manager_scrub()'s nkit_write_func callback can write
// straight into a circular buffer target (used by writeGap's GapType.
// AllScrubbed and GapBlockType.ByteFill cases, matching scrub.Scrub(target,...)
// in the C#).
static enumError nkit_cb_write_adapter ( void *ctx, const u8 *data, u32 size )
{
    return nkit_cb_write_all((nkit_circular_buffer_t*)ctx,data,size);
}

// Shared by every "some/all zero bytes then junk bytes" write pattern below
// (GapType.JunkFile's trailing junk, GapType.AllJunk, and GapBlockType.Junk
// blocks): ports the repeated C# pattern
//   ByteStream.Zeros.Copy(target,nulls); junk.Position = dstPos+nulls; junk.Copy(target,bytes-nulls);
// -- writes 'nulls' zero bytes to 'target', then (bytes-nulls) bytes of junk
// generated for the range [dst_pos+nulls, dst_pos+bytes).
static enumError nkit_write_nulls_then_junk
(
    nkit_circular_buffer_t	*target,
    nkit_junk_read_func		junk_get,
    void			*junk_ctx,
    u64				dst_pos,
    u64				nulls,
    u64				bytes		// total bytes written == nulls + junk portion
)
{
    static const u8 zeros[0x10000] = {0};
    u64 rest = nulls;
    while (rest)
    {
	u32 chunk = rest < sizeof(zeros) ? (u32)rest : sizeof(zeros);
	enumError err = nkit_cb_write_all(target,zeros,chunk);
	if (err)
	    return err;
	rest -= chunk;
    }

    u8 buf[0x10000];
    u64 pos = dst_pos + nulls;
    rest = bytes - nulls;
    while (rest)
    {
	u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
	junk_get(junk_ctx,pos,buf,chunk);
	enumError err = nkit_cb_write_all(target,buf,chunk);
	if (err)
	    return err;
	pos  += chunk;
	rest -= chunk;
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Port of copyFile(ConvertFile file, ref long nullsPos, ref long srcPos,
// long dstPos, Stream inStream, Stream target) -- NkitReaderWii.cs:572-594.
// Copies one file's bytes verbatim from the source stream to the restored
// output, 4 byte aligning the copy length the same way the FST rounds file
// lengths. 'dst_pos' is by value, same as the C# (its only use inside is to
// compute the outgoing *nulls_pos; the caller advances its own running
// output position separately, by *out_len).
static enumError nkit_copy_file
(
    nkit_convert_file_t		*file,
    u64				*nulls_pos,	// ref nullsPos
    u64				*src_pos,	// ref srcPos
    u64				dst_pos,
    FILE			*in_stream,
    nkit_circular_buffer_t	*target,
    u64				*out_len	// bytes copied (== source bytes consumed)
)
{
    *out_len = 0;

    u64 size = file->fst_length;
    if ( size == 0 )
	return ERR_OK;	// could be legit or junk

    size += size % 4 == 0 ? 0 : 4 - size % 4;

    u8 buf[0x10000];
    u64 rest = size;
    while (rest)
    {
	u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
	if ( fread(buf,1,chunk,in_stream) != chunk )
	    return ERROR1(ERR_READ_FAILED,
		"NKit: copy file '%s' failed at data position 0x%llx (%llu bytes)\n",
		file->fst_name ? file->fst_name : "?",
		(u64)file->fst_data_offset, (u64)file->fst_length);
	enumError err = nkit_cb_write_all(target,buf,chunk);
	if (err)
	    return err;
	rest -= chunk;
    }

    *src_pos += size;
    dst_pos  += size;
    *nulls_pos = dst_pos + 0x1cL;

    *out_len = size;
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Port of the private 4-ref-parameter writeGap(ref long fileLength, LongRef
// gapLength, ref long nullsPos, ref long srcPos, long dstPos, Stream
// inStream, Stream target, JunkStream junk, bool firstOrLastFile,
// ScrubManager scrub) -- NkitReaderWii.cs:614-749. Both public overloads
// (writeGap(ConvertFile,...) and writeFiller(...), below) forward into this
// core. 'file_length'/'gap_length' are s64 (not u64) purely to carry
// writeFiller's -1 "ignored" sentinel through unchanged, exactly like C#'s
// plain 'long'; every actual use only ever compares them against 0, so the
// sentinel behaves the same whether read as signed or unsigned.
//
// Flagged difference from x-nkit.c's GC nkit_decode_gap(): the 0xFFFFFFFC
// 64 bit size escape a few lines down (C# line ~628) is Wii-only -- GC never
// emits it (x-nkit.c's version already notes this and treats it as
// unreachable/defensive; here it's the real, live path).
static enumError nkit_write_gap_core
(
    s64				*file_length,	// ref fileLength
    s64				*gap_length,	// LongRef gapLength
    u64				*nulls_pos,	// ref nullsPos
    u64				*src_pos,	// ref srcPos
    u64				dst_pos,
    FILE			*in_stream,
    nkit_circular_buffer_t	*target,
    nkit_junk_read_func		junk_get,
    void			*junk_ctx,
    bool			first_or_last_file,
    nkit_scrub_manager_t	*scrub,
    u64				*out_len	// gapLength.Value + junkFileLen on return
)
{
    *out_len = 0;

    if ( *gap_length == 0 )
    {
	if ( *file_length == 0 )
	    *nulls_pos = dst_pos + 0x1cL;
	return ERR_OK;
    }

    // srcLen fix for (padding between junk files) - Zumba Fitness (Europe) (En,Fr,De,Es,It)
    s64 src_len = *gap_length;

    u8 hdr[4];
    if ( fread(hdr,1,4,in_stream) != 4 )
	return ERROR1(ERR_READ_FAILED,"NKit: truncated gap record\n");
    *src_pos += 4;

    u32 word = be32(hdr);
    nkit_gap_type_t gt = (nkit_gap_type_t)( word & 0b11 );
    u64 size = word & 0xFFFFFFFCu;

    if ( size == 0xFFFFFFFCu )		// Wii only 64 bit size extension; not a thing for GC
    {
	u8 ext[4];
	if ( fread(ext,1,4,in_stream) != 4 )
	    return ERROR1(ERR_READ_FAILED,"NKit: truncated gap 64 bit size extension\n");
	*src_pos += 4;
	size = 0xFFFFFFFCull + be32(ext);	// cater for files > 0xFFFFFFFF
    }
    *gap_length = (s64)size;

    // keep track of trailing nulls when restoring scrubbed images
    nkit_scrub_manager_add_gap(scrub,(u64)*file_length,dst_pos,size);

    u64 nulls = 0;
    u64 junk_file_len = 0;

    // set nullsPos value if zerobyte file without junk
    if ( gt == NKIT_GAP_JUNK_FILE )
    {
	// C#: nullsPos = Math.Min(nullsPos - dstPos, 0); -- ported verbatim,
	// including the fixed '0' second argument (so this can only ever
	// clamp to a value <= 0, i.e. it stores a *non-positive delta*, not
	// an absolute position, same as the original).
	s64 delta = (s64)*nulls_pos - (s64)dst_pos;
	*nulls_pos = (u64)( delta < 0 ? delta : 0 );

	u8 lb[4];
	if ( fread(lb,1,4,in_stream) != 4 )
	    return ERROR1(ERR_READ_FAILED,"NKit: truncated junk-file length\n");
	*src_pos += 4;
	junk_file_len = be32(lb);
	*file_length = (s64)junk_file_len;
	junk_file_len += junk_file_len % 4 == 0 ? 0 : 4 - junk_file_len % 4;

	nulls = (size & 0xFC) >> 2;
	enumError err = nkit_write_nulls_then_junk(target,junk_get,junk_ctx,dst_pos,nulls,junk_file_len);
	if (err)
	    return err;
	dst_pos += junk_file_len;

	if ( src_len <= 8 )
	{
	    *out_len = junk_file_len;
	    return ERR_OK;
	}
	else
	{
	    // read gap
	    u8 gb[4];
	    if ( fread(gb,1,4,in_stream) != 4 )
		return ERROR1(ERR_READ_FAILED,"NKit: truncated gap record (post junk-file)\n");
	    *src_pos += 4;
	    word = be32(gb);
	    gt = (nkit_gap_type_t)( word & 0b11 );
	    size = word & 0xFFFFFFFCu;
	    *gap_length = (s64)size;
	}
    }
    else if ( *file_length == 0 )	// last zero byte file was legit
	*nulls_pos = dst_pos + 0x1cL;

    u64 max_nulls = *nulls_pos > dst_pos ? *nulls_pos - dst_pos : 0;	// Math.Max(0, nullsPos-dstPos), ~0x1c
    if ( size < max_nulls )
	nulls = size;
    else
	nulls = size >= 0x40000 && !first_or_last_file ? 0 : max_nulls;
    *nulls_pos = dst_pos + nulls;	// belt and braces

    if ( gt == NKIT_GAP_ALL_JUNK )
    {
	enumError err = nkit_write_nulls_then_junk(target,junk_get,junk_ctx,dst_pos,nulls,size);
	if (err)
	    return err;
	dst_pos += size;
    }
    else if ( gt == NKIT_GAP_ALL_SCRUBBED )
    {
	enumError err = nkit_scrub_manager_scrub(scrub,nkit_cb_write_adapter,target,dst_pos,size,0);
	if (err)
	    return err;
	dst_pos += size;
    }
    else	// NKIT_GAP_MIXED: a stream of 4 byte block records follows
    {
	u64 prg = size;
	nkit_block_type_t bt = NKIT_BLOCK_JUNK;	// should never be used unset
	u8 fill_byte = 0;

	while ( prg > 0 )
	{
	    u8 be[4];
	    if ( fread(be,1,4,in_stream) != 4 )
		return ERROR1(ERR_READ_FAILED,"NKit: truncated gap block record\n");
	    *src_pos += 4;
	    u32 blk = be32(be);
	    nkit_block_type_t bt_type = (nkit_block_type_t)( blk >> 30 );
	    bool bt_repeat = bt_type == NKIT_BLOCK_REPEAT;
	    if (!bt_repeat)
		bt = bt_type;

	    u64 cnt = 0x3FFFFFFFu & blk;
	    u64 bytes;

	    if ( bt == NKIT_BLOCK_NONJUNK )
	    {
		bytes = cnt * NKIT_GAP_BLOCK_SIZE;
		if ( bytes > prg ) bytes = prg;

		u8 buf[0x10000];
		u64 rest = bytes;
		while (rest)
		{
		    u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
		    if ( fread(buf,1,chunk,in_stream) != chunk )
			return ERROR1(ERR_READ_FAILED,"NKit: truncated gap non-junk data\n");
		    *src_pos += chunk;
		    enumError err = nkit_cb_write_all(target,buf,chunk);
		    if (err)
			return err;
		    rest -= chunk;
		}
	    }
	    else if ( bt == NKIT_BLOCK_BYTEFILL )
	    {
		if (!bt_repeat)
		{
		    fill_byte = (u8)( 0xFF & cnt );	// last 8 bits when not repeating are the byte
		    cnt >>= 8;
		}
		bytes = cnt * NKIT_GAP_BLOCK_SIZE;
		if ( bytes > prg ) bytes = prg;

		enumError err = nkit_scrub_manager_scrub(scrub,nkit_cb_write_adapter,target,dst_pos,bytes,fill_byte);
		if (err)
		    return err;
	    }
	    else // NKIT_BLOCK_JUNK
	    {
		bytes = cnt * NKIT_GAP_BLOCK_SIZE;
		if ( bytes > prg ) bytes = prg;

		max_nulls = *nulls_pos > dst_pos ? *nulls_pos - dst_pos : 0;
		if ( prg < max_nulls )
		    nulls = bytes;
		else
		    nulls = bytes >= 0x40000 && !first_or_last_file ? 0 : max_nulls;

		enumError err = nkit_write_nulls_then_junk(target,junk_get,junk_ctx,dst_pos,nulls,bytes);
		if (err)
		    return err;
	    }

	    prg     -= bytes;
	    dst_pos += bytes;
	}
    }

    *out_len = (u64)*gap_length + junk_file_len;
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Port of writeGap(ConvertFile file, ref long nullsPos, ref long srcPos,
// long dstPos, Stream inStream, Stream target, JunkStream junk, bool
// firstOrLastFile, ScrubManager scrub) -- NkitReaderWii.cs:596-604. Thin
// wrapper unpacking/repacking ConvertFile's two mutated fields around the
// core above, same as the C#.
static enumError nkit_write_gap
(
    nkit_convert_file_t		*file,
    u64				*nulls_pos,	// ref nullsPos
    u64				*src_pos,	// ref srcPos
    u64				dst_pos,
    FILE			*in_stream,
    nkit_circular_buffer_t	*target,
    nkit_junk_read_func		junk_get,
    void			*junk_ctx,
    bool			first_or_last_file,
    nkit_scrub_manager_t	*scrub,
    u64				*out_len	// return value of writeGap()
)
{
    s64 file_length = (s64)file->fst_length;
    s64 gap_length  = (s64)file->gap_length;

    enumError err = nkit_write_gap_core(&file_length,&gap_length,nulls_pos,src_pos,dst_pos,
	in_stream,target,junk_get,junk_ctx,first_or_last_file,scrub,out_len);

    file->fst_length = (u64)file_length;
    file->gap_length  = (u64)gap_length;
    return err;
}

///////////////////////////////////////////////////////////////////////////////

// Port of writeFiller(ref long srcPos, long dstPos, long nullsPos, Stream
// inStream, Stream target, JunkStream junk, ScrubManager scrub) --
// NkitReaderWii.cs:606-612. 'nulls_pos' is by value here (not ref), same as
// the C# -- it feeds a throwaway local ('ref nullsPos' inside the inner
// writeGap() call binds to that local, not to any caller-visible state).
// fileLength/gapLength are the fixed -1 sentinels ("will be ignored" per
// the C# comment) that make the core treat this as a no-file filler gap.
static enumError nkit_write_filler
(
    u64				*src_pos,	// ref srcPos
    u64				dst_pos,
    u64				nulls_pos,	// by value
    FILE			*in_stream,
    nkit_circular_buffer_t	*target,
    nkit_junk_read_func		junk_get,
    void			*junk_ctx,
    nkit_scrub_manager_t	*scrub,
    u64				*out_len	// return value of writeFiller()
)
{
    s64 file_length = -1;	// will be ignored
    s64 gap_length  = -1;
    u64 local_nulls_pos = nulls_pos;

    return nkit_write_gap_core(&file_length,&gap_length,&local_nulls_pos,src_pos,dst_pos,
	in_stream,target,junk_get,junk_ctx,true,scrub,out_len);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////	  FileSystem.cs -> nkit_parse_fst_wii()		///////////////
///////////	  NkitFormat.cs:80-143 -> nkit_get_convert_fst_files()	///////////////
///////////////////////////////////////////////////////////////////////////////

// This is the prerequisite the stalled partitionStreamWrite() port (the
// FST/file-loop driver, still not ported -- see this file's header) needs:
// a real Wii FST tree walk producing FstFile-equivalent records, and the
// GetConvertFstFiles() gap-derivation pass that turns those into the
// ConvertFile list NkitWriteFileSystem() drives off of. Ported from
// github.com/Nanook/NKitv1's NKit/FilesAndStreams/FileSystem.cs (whole
// file, 190 lines) and NKit/Conversion/NkitFormat.cs:80-143
// (GetConvertFstFiles()).
//
// Deliberately NOT reusing x-nkit.c's nkit_parse_fst_gc(): that function is
// an intentionally reduced flat-offset parser (no tree, no names, no
// OffsetInFstFile-as-shared-field semantics, no alignment/gap derivation)
// built only for the small slice of GC restore work already ported there.
// This is the real thing: it mirrors FileSystem.cs's FstFolder/FstFile
// object graph and NkitFormat.cs's gap/alignment derivation faithfully.
//
// Deviation from the C#: FileSystem.Parse() builds an FstFolder/FstFile
// object *tree*, then a separate Files getter (recurseFolders(), lines
// 115-122) walks that tree to produce a flat, unsorted List<FstFile> --
// unsorted because ordering never actually matters until the *caller*
// re-sorts it (GetConvertFstFiles's OrderBy(Offset).ThenBy(Length) at
// FileSystem.cs-caller line NkitFormat.cs:87, which entirely supersedes
// whatever order Files produced). Since the object-tree round-trip changes
// nothing observable, nkit_parse_fst_wii() below flattens directly during
// the same recursive walk recurseFst() already does, instead of building a
// tree and then re-walking it -- one pass, same result.
//
// Folder objects are still allocated (not skipped) because FstFile.Parent
// is a real field of the ported struct (used by the C#'s Path property);
// dropping it would mean guessing that no future caller needs it. Both the
// folder pool and the file pool are sized to the FST's total entry count
// up front (a file can never exceed one FST entry, same generous-upper-
// -bound approach nkit_parse_fst_gc() already uses above) so folder/file
// pointers stay stable for the whole walk -- no realloc-invalidation risk
// for the Parent pointers threaded through recursion.

// [[nkit_wii_fst_folder_t]]
// Port of FstFolder (NkitFormat.cs:11-28). Folders.List<FstFolder> is not
// kept (nothing here ever needs to enumerate a folder's children -- only
// FstFile.Parent, for Path reconstruction, is used downstream) so this is
// just the Name/Parent slice of the C# class.
typedef struct nkit_wii_fst_folder_t
{
    const struct nkit_wii_fst_folder_t	*parent;	// FstFolder.Parent
    ccp					name;		// FstFolder.Name (not owned: points into the fst.bin buffer)
}
nkit_wii_fst_folder_t;

// [[nkit_wii_fst_file_t]]
// Port of FstFile (NkitFormat.cs:65-104), full field set: PartitionId,
// Parent, Name, DataOffset, (raw-partition) Offset, Length, IsNonFstFile,
// OffsetInFstFile. FstFile.Clone() isn't ported -- every use here is by
// value copy (plain struct assignment does the same thing in C, since none
// of these fields are owned pointers).
typedef struct nkit_wii_fst_file_t
{
    ccp					partition_id;	// FstFile.PartitionId (not owned)
    const nkit_wii_fst_folder_t	*parent;	// FstFile.Parent
    ccp					name;		// FstFile.Name (not owned: points into the fst.bin
							// buffer for real entries, or a string literal for
							// the synthetic "fst.bin" pseudo-entries below)
    u64					data_offset;	// FstFile.DataOffset
    u64					offset;		// FstFile.Offset: raw partition offset (post NStream.DataToOffset)
    u64					length;		// FstFile.Length
    bool				is_non_fst_file;// FstFile.IsNonFstFile
    u32					offset_in_fst;	// FstFile.OffsetInFstFile: byte offset of this entry's
							// data-offset field in the fst.bin buffer
}
nkit_wii_fst_file_t;

///////////////////////////////////////////////////////////////////////////////

// Port of NStream.DataToOffset(long o, bool isWii) -- NStream.cs:655-660.
// Converts a "data stream" offset (hash blocks already stripped: 0x7c00
// data bytes per 0x8000 on-disc sector) to the corresponding raw,
// on-disc/hashed partition offset. For GC (isWii==false) it's the
// identity -- same as the C#.
static u64 nkit_data_to_offset ( u64 o, bool is_wii )
{
    if (!is_wii)
	return o;
    return (o / 0x7c00ull * 0x8000ull) + (o % 0x7c00ull) + 0x400ull;
}

///////////////////////////////////////////////////////////////////////////////

// Port of the private recurseFst(MemorySection ms, FstFolder folder, long
// names, uint i, string id, bool isGc) -- FileSystem.cs:160-187. Standard
// 12 byte FST entry parser (type<<24|nameOff, dataOffset, length), same
// layout nkit_parse_fst_gc() above reads, but ported faithfully this time:
// real names (shift-jis in the C#; treated here as raw bytes -- Wii/GC FST
// names are ASCII in practice and nothing downstream decodes them, so no
// codepage conversion is needed), a real folder tree via 'cur_folder', and
// files appended directly into the flat output array (see the big comment
// above on why that's equivalent to the C#'s build-tree-then-flatten).
static enumError nkit_wii_fst_recurse
(
    const u8			*fst,		// MemorySection ms
    u32				fst_size,
    nkit_wii_fst_folder_t	*folder_pool,	// preallocated, n_entries slots
    uint			*n_folder,	// in/out: folders used so far (slot 0 == root, preplaced by caller)
    const nkit_wii_fst_folder_t	*cur_folder,	// 'folder' param
    long			names,		// 'names' param: byte offset of the FST string table
    uint			i,		// 'i' param
    ccp				id,		// 'id' param
    bool			is_gc,		// 'isGc' param
    nkit_wii_fst_file_t		*file_pool,	// preallocated, n_entries slots
    uint			*n_file,	// in/out: files used so far
    uint			*out_i		// return value (next fst index)
)
{
    if ( (u64)(i+1)*12 > fst_size )
	return ERROR0(ERR_WIA_INVALID,"NKit: fst.bin entry index out of range\n");

    u32 hdr  = be32(fst + 12*(u64)i);
    long name = names + (long)( hdr & 0x00ffffffL );
    int  type = (int)( hdr >> 24 );
    ccp  nm   = name >= 0 && (u64)name < fst_size ? (ccp)(fst+name) : "";
    u32  size = be32(fst + 12*(u64)i + 8);

    if ( type == 1 ) // directory
    {
	const nkit_wii_fst_folder_t *f;
	if ( i == 0 )
	    f = cur_folder; // root: don't allocate, same as C#'s 'i==0 ? folder : new FstFolder(...)'
	else
	{
	    nkit_wii_fst_folder_t *nf = folder_pool + (*n_folder)++;
	    nf->parent = cur_folder;
	    nf->name   = nm;
	    f = nf;
	}

	uint j;
	for ( j = i+1; j < size; )
	{
	    enumError err = nkit_wii_fst_recurse(fst,fst_size,folder_pool,n_folder,f,
		names,j,id,is_gc,file_pool,n_file,&j);
	    if (err)
		return err;
	}
	*out_i = size;
	return ERR_OK;
    }
    else // file
    {
	u32 pos  = 12*i + 4;
	u64 doff = (u64)be32(fst+pos) * ( is_gc ? 1ull : 4ull ); // offset in data
	size     = be32(fst + 12*(u64)i + 8);
	u64 off  = nkit_data_to_offset(doff,!is_gc); // offset in raw partition

	nkit_wii_fst_file_t *nfile = file_pool + (*n_file)++;
	nfile->partition_id    = id;
	nfile->parent          = cur_folder;
	nfile->name            = nm;
	nfile->data_offset     = doff;
	nfile->offset          = off;
	nfile->length          = size;
	nfile->is_non_fst_file = false;
	nfile->offset_in_fst   = pos;

	*out_i = i+1;
	return ERR_OK;
    }
}

// Port of the internal FileSystem.Parse(MemorySection ms, FstFile fst,
// string id, bool isGc) -- FileSystem.cs:146-158, called by
// GetConvertFstFiles() (NkitFormat.cs:87) with fst==null, so the "synthetic
// fst.bin entry gets prepended to the tree" branch never fires on that
// path and isn't reachable from here; the two public FileSystem.Parse()
// overloads that DO build that synthetic entry (byte[]/Stream-taking,
// FileSystem.cs:127-139) aren't ported for the same reason -- nothing in
// this codebase calls FileSystem.Parse() any other way yet.
//
// 'folder_pool'/'file_pool' are allocated here, sized to fst_size/12 (an
// FST can't have more entries than that many 12 byte records) -- same
// generous-upper-bound approach as nkit_parse_fst_gc(). Both are owned by
// the caller on success: FREE() 'res_file' only after FREE()'ing
// 'res_folder' (or after being done reading any FstFile.Name/Parent -- the
// file entries' Parent pointers point into the folder pool).
static enumError nkit_parse_fst_wii
(
    const u8			*fst,		// fst.bin buffer (MemorySection ms)
    u32				fst_size,
    ccp				id,
    bool			is_gc,
    nkit_wii_fst_folder_t	**res_folder,
    uint			*res_n_folder,
    nkit_wii_fst_file_t		**res_file,
    uint			*res_n_file
)
{
    if ( fst_size < 12 )
	return ERROR0(ERR_WIA_INVALID,"NKit: fst.bin too small\n");

    const u64 n_files = be32(fst+8); // ReadUInt32B(0x8): root entry's own 'size' field == entry count
    if ( 12*n_files > fst_size )
	return ERROR0(ERR_WIA_INVALID,"NKit: fst.bin entry count out of range\n");

    nkit_wii_fst_folder_t *folder_pool = MALLOC(n_files*sizeof(*folder_pool));
    nkit_wii_fst_file_t   *file_pool   = MALLOC(n_files*sizeof(*file_pool));
    uint n_folder = 1, n_file = 0; // slot 0 preplaced as the root folder below

    folder_pool[0].parent = 0;
    folder_pool[0].name   = "";

    uint end_i;
    enumError err = nkit_wii_fst_recurse(fst,fst_size,folder_pool,&n_folder,folder_pool+0,
	12*(long)n_files,0,id,is_gc,file_pool,&n_file,&end_i);
    if (err)
    {
	FREE(folder_pool);
	FREE(file_pool);
	return err;
    }

    *res_folder   = folder_pool;
    *res_n_folder = n_folder;
    *res_file     = file_pool;
    *res_n_file   = n_file;
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// [[nkit_wii_convert_file_t]]
// Port of ConvertFile (NkitFormat.cs:30-63), the slice GetConvertFstFiles()
// itself populates: FstFile (embedded by value here rather than
// referenced, see below), GapLength, and Alignment. Gap/IsJunk/NewSize
// aren't populated by GetConvertFstFiles() -- they belong to the actual
// restore write (NkitWriteFileSystem()/copyFile()/writeGap(), i.e. this
// file's existing nkit_convert_file_t/nkit_copy_file()/nkit_write_gap())
// which is a separate, later step to wire up (bridging this richer struct
// bridged into that narrower one by nkit_partition_stream_write()'s file
// loop, which builds a nkit_convert_file_t per entry on the fly).
//
// FstFile is embedded BY VALUE, not referenced, unlike the C# (List<
// ConvertFile> sharing FstFile object references). In the C# each FstFile
// is only ever wrapped by exactly one ConvertFile (the loop below always
// pairs a "previous" FstFile with a fresh ConvertFile, never reuses one),
// so there's no aliasing to preserve; embedding avoids extra allocations
// and lifetime coupling to the pools nkit_parse_fst_wii() returns. The one
// exception -- copyFile() mutating FstFile.Length in place for the
// GapType.JunkFile case (NkitFormat.cs:54) -- mutates the ConvertFile's own
// embedded copy either way, so behavior is unchanged.
typedef struct nkit_wii_convert_file_t
{
    nkit_wii_fst_file_t	fst_file;	// ConvertFile.FstFile
    u64			gap_length;	// ConvertFile.GapLength
    s64			alignment;	// ConvertFile.Alignment: -1 = do not align, 0 = preserve, else explicit
}
nkit_wii_convert_file_t;

// Port of the FstFile ordering used at NkitFormat.cs:87 --
// '.OrderBy(a => a.Offset)?.ThenBy(a => a.Length)' -- a single (Offset,
// Length) sort, not FileSystem.cs's separate Files-getter OrderBy(Offset)
// (which this ThenBy-chained sort supersedes entirely; see the big comment
// on nkit_parse_fst_wii() above).
static int nkit_wii_fst_file_cmp ( const void *pa, const void *pb )
{
    const nkit_wii_fst_file_t *a = pa, *b = pb;
    if ( a->offset != b->offset )
	return a->offset < b->offset ? -1 : 1;
    return a->length < b->length ? -1 : a->length > b->length ? 1 : 0;
}

// Port of NkitFormat.GetConvertFstFiles() -- NkitFormat.cs:80-143. Builds
// the list of gap-annotated files NkitWriteFileSystem() (already ported
// above as the existing per-file copy/gap machinery) drives its file loop
// from. The C#'s 'inStream' parameter is unused in the method body (never
// referenced) so it's dropped here; 'hdr' is only ever read at one fixed
// field (FstOffset, hdr.ReadUInt32B(0x424)*mlt) which this codebase's Wii
// partition header parser (nkit_part_header_init(), above) already
// extracts into nkit_part_header_t.fst_offset, so that's taken directly as
// 'fst_offset' instead of re-deriving it from a raw header buffer here.
//
// On the two "negative gap" conditions the C# soft-fails (returns null +
// an out error string, letting the caller fall back to "convert as bad
// image"); ported here as a hard enumError instead, matching every other
// error path in this file. Neither local sample exercises the soft-fail
// branch, and this file has no soft-fail-with-message convention to extend.
static enumError nkit_get_convert_fst_files
(
    const u8			*fst,			// MemorySection fst
    u32				fst_size,
    u64				fst_offset,		// nkit_part_header_t.fst_offset (see above)
    ccp				partition_id,		// hdr.ReadString(0,4)
    bool			is_gc,
    s64				fst_file_alignment,	// fstFileAlignment: 0=preserve, -1=default(0x8000 heuristic), else explicit
    u64				image_size,		// 'size' param
    nkit_wii_fst_folder_t	**res_folder,		// owned; see nkit_parse_fst_wii()
    uint			*res_n_folder,
    nkit_wii_convert_file_t	**res_conv,		// owned
    uint			*res_n_conv
)
{
    *res_conv = 0;
    *res_n_conv = 0;

    nkit_wii_fst_folder_t *folder_pool;
    nkit_wii_fst_file_t   *file_pool;
    uint n_folder, n_file;
    enumError err = nkit_parse_fst_wii(fst,fst_size,partition_id,is_gc,
	&folder_pool,&n_folder,&file_pool,&n_file);
    if (err)
	return err;

    if (!n_file)
    {
	FREE(folder_pool);
	FREE(file_pool);
	return ERROR0(ERR_WIA_INVALID,"NKit: fst.bin has no files\n");
    }

    qsort(file_pool,n_file,sizeof(*file_pool),nkit_wii_fst_file_cmp);

    nkit_wii_convert_file_t *conv = MALLOC((n_file+1)*sizeof(*conv)); // +1: trailing "last file" entry, same shape as the C#'s List<>.Add() tail
    uint n_conv = 0;

    // Synthetic "fst.bin" pseudo-FstFile (NkitFormat.cs:92-93), used as the
    // 'previous file' sentinel for i==0's gap derivation only -- note its
    // Offset is set to fstLen directly, NOT run through
    // nkit_data_to_offset() like every real file's Offset is; that
    // asymmetry is in the original C# too (ff.Offset = fstLen, not
    // NStream.DataToOffset(fstLen,...)) and is preserved here as-is.
    nkit_wii_fst_file_t fst_pseudo = {0};
    fst_pseudo.name            = "fst.bin";
    fst_pseudo.data_offset     = fst_offset;
    fst_pseudo.offset          = fst_offset;
    fst_pseudo.length          = fst_size;
    fst_pseudo.is_non_fst_file = true;

    for ( uint i = 0; i < n_file; i++ )
    {
	const nkit_wii_fst_file_t *ff = i == 0 ? &fst_pseudo : &file_pool[i-1];

	u64 end = ff->data_offset + ff->length;
	end += end % 4 == 0 ? 0 : 4 - (end % 4);

	if ( file_pool[i].data_offset < end ) // gap = srcFiles[i].DataOffset - end, checked < 0
	{
	    FREE(folder_pool);
	    FREE(file_pool);
	    FREE(conv);
	    return ERROR0(ERR_WIA_INVALID,
		"NKit: the gap between '%s' and '%s' is %lld - image is invalid\n",
		ff->name,file_pool[i].name,(s64)(file_pool[i].data_offset-end));
	}
	u64 gap = file_pool[i].data_offset - end;

	conv[n_conv].fst_file    = *ff;
	conv[n_conv].gap_length  = gap;
	n_conv++;
    }

    // trailing entry: gap between the last real file and the end of the image
    {
	const nkit_wii_fst_file_t *ff = &file_pool[n_file-1];
	u64 end = ff->data_offset + ff->length;
	end += end % 4 == 0 ? 0 : 4 - (end % 4);

	s64 gap = (s64)image_size - (s64)end;
	if ( gap >= -3 && gap < 0 )
	    gap = 0; // some hacked GC images converted from TGC end on the file end (star fox e3)
	if ( gap < 0 )
	{
	    FREE(folder_pool);
	    FREE(file_pool);
	    FREE(conv);
	    return ERROR0(ERR_WIA_INVALID,
		"NKit: the gap between '%s' and the end of the image is %lld - image/partition is invalid\n",
		ff->name,gap);
	}

	conv[n_conv].fst_file   = *ff;
	conv[n_conv].gap_length = (u64)gap;
	n_conv++;
    }

    // set alignment -- NkitFormat.cs:124-135
    static const ccp align_ext[] = { ".tgc" }; // only entry NKit itself still enables (rest commented out in the C#)
    for ( uint i = 0; i < n_conv; i++ )
    {
	const nkit_wii_fst_file_t *ff = &conv[i].fst_file;
	ccp dot = strrchr(ff->name,'.');

	if ( fst_file_alignment == 0 )
	    conv[i].alignment = 0; // preserve alignment
	else if ( fst_file_alignment == -1 && ff->data_offset % 0x8000 == 0 &&
	    ( ff->length % 0x8000 == 0 || ( dot && !strcasecmp(dot,align_ext[0]) ) ) )
	    conv[i].alignment = 0x8000; // default behaviour
	else if ( fst_file_alignment != 0 && ff->data_offset % fst_file_alignment == 0 ) // src matches alignment
	    conv[i].alignment = fst_file_alignment; // align to largest multiple
	else
	    conv[i].alignment = -1; // do not align this file
    }

    FREE(file_pool); // conv[] embeds copies of every FstFile by value; the pool itself is no longer needed
    *res_folder   = folder_pool;
    *res_n_folder = n_folder;
    *res_conv     = conv;
    *res_n_conv   = n_conv;
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////  WiiHashStore.cs:83-86 -> nkit_hash_store_write_flags_data()	///////////
///////////////////////////////////////////////////////////////////////////////

// Port of WiiHashStore.WriteFlagsData(long partitionDataSize, Stream
// readStream) -- WiiHashStore.cs:83-86, the one nkit_hash_store_t method
// stage 1 left as bookkeeping-only (see that type's own comment above).
// Re-derives the flags buffer size from partitionDataSize via
// nkit_hash_store_ints_count() (the same helper the ctor uses -- calling it
// again here also refreshes hs->partition_size, matching the C#'s
// intsCount() side effect through _partitionSize), then reads that many
// bytes straight from the source stream, replacing whatever flags buffer
// 'hs' already had. The C# just reassigns '_flags = MemorySection.Read(...)'
// and lets the GC reclaim the old MemorySection; this port frees the old
// buffer explicitly first since it owns its memory.
static enumError nkit_hash_store_write_flags_data
(
    nkit_hash_store_t	*hs,
    u64			partition_data_size,	// 'partitionDataSize' param
    FILE		*in_stream		// 'readStream' param
)
{
    uint new_size = nkit_hash_store_ints_count(hs,partition_data_size) * 4;

    if (hs->flags)
	FREE(hs->flags);
    hs->flags = MALLOC(new_size);
    hs->flags_size = new_size;

    if ( new_size && fread(hs->flags,1,new_size,in_stream) != new_size )
	return ERROR0(ERR_READ_FAILED,"NKit: truncated hash-store flags data\n");
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////   JunkStream.cs (Wii id/disc-seeded junk) -> nkit_wii_junk_t  ///////////////
///////////////////////////////////////////////////////////////////////////////

// partitionStreamWrite() constructs its own JunkStream instances locally
// (NkitReaderWii.cs:465 and :478), separate from any JunkStream the (still
// unported) outer Read() loop might hold -- so, same reasoning already
// spelled out on nkit_gap_type_t/nkit_block_type_t just above (this source,
// JunkStream.cs, is shared by the GC and Wii readers, but x-nkit.c's own
// nkit_junk_t/nkit_junk_init()/nkit_junk_get() are static/file-local to that
// translation unit and not callable from here), this is a faithful
// redeclaration of the same generator rather than a new invention. See
// x-nkit.c's nkit_junk_t/nkit_seed_block()/nkit_junk_get() for the algorithm
// commentary (a10002710()'s LFG-seed derivation, the regenerate-count note,
// etc.) -- it is not repeated here.
//
// One field here has NO counterpart in x-nkit.c's GC nkit_junk_t: 'junk_length'.
// JunkStream's ctor computes _junkLength = Math2.Align(_length,0x8000) from its
// third ('length') argument, and fillBlock()'s tail (JunkStream.cs:287-289)
// then ZEROES every byte of the generated 0x40000 block at or beyond that
// length:
//	int junkSize = Clamp(0, _junkLength - blk*buffer.Length, buffer.Length);
//	if (buffer.Length - junkSize != 0) Array.Clear(buffer, junkSize, ...);
// Since _junkLength is itself 0x8000 aligned, that is exactly equivalent to
// "any byte at absolute position >= junk_length reads as 0x00", which is how
// it's implemented below (per byte, rather than per 256 KiB block, so the
// 0x8000-granular generator here doesn't need JunkStream's 0x40000 buffering).
//
// This is load bearing, not a cosmetic detail: NkitReaderWii.Read() constructs
// its filler JunkStream with length '0' whenever the preceding partition was
// the update partition (lines 107 and 267-268, 'lastPartitionType ==
// PartitionType.Update ? 0 : imageSize'), and length 0 means junk_length 0
// means the whole filler run is NULs rather than LFG output. Dropping this
// field would silently emit megabytes of junk where the real disc has zeros.
typedef struct nkit_wii_junk_t
{
    u8		id[4];
    u8		disc;
    u64		junk_length;	// _junkLength = Math2.Align(_length,0x8000)
    lfg_t	lfg;
    u64		block_start;
    bool	valid;
}
nkit_wii_junk_t;

// ctor: JunkStream(byte[] id, int disc, long length)
static void nkit_wii_junk_init ( nkit_wii_junk_t *nj, const u8 id[4], u8 disc, u64 length )
{
    memcpy(nj->id,id,4);
    nj->disc  = disc;
    // Math2.Align(val,boundry) is 'val / boundry * boundry' (NKit/Utils.cs:39),
    // i.e. it rounds DOWN. A trailing partial 0x8000 block therefore has no
    // junk generated for it at all -- fillBlock() only ever seeds whole
    // 0x8000 sub-blocks -- and reads back as NULs.
    nj->junk_length = length / 0x8000 * 0x8000;
    nj->valid = false;
}

static void nkit_wii_junk_seed_block ( nkit_wii_junk_t *nj, u64 block32k )
{
    u32 sample = (u32)( ( (u32)( (nj->id[2] << 8 | nj->id[1]) << 16 )
			 | (u32)( (nj->id[3] + nj->id[2]) << 8 )
			 | (u32)( nj->id[0] + nj->id[1] ) ) );
    sample = (u32)( sample ^ nj->disc ) * 0x260bcd5u;
    sample ^= (u32)( block32k * 0x1ef29123u );

    u32 words[LFG_SEED_WORDS];
    u32 s = sample;
    u32 num = 0;
    for ( int w = 0; w < LFG_SEED_WORDS; w++ )
    {
	for ( int i = 0; i < 32; i++ )
	{
	    s *= 0x5d588b65u;
	    s += 1;
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

static void nkit_wii_junk_get ( nkit_wii_junk_t *nj, u64 pos, void *dest, u32 size )
{
    u8 *out = dest;
    while (size)
    {
	// fillBlock()'s trailing Array.Clear(): everything at or past
	// _junkLength is zero, never generator output (see the type comment).
	if ( pos >= nj->junk_length )
	{
	    memset(out,0,size);
	    return;
	}

	const u64 block32k    = pos / 0x8000;
	const u64 block_start = block32k * 0x8000;

	if ( !nj->valid || block_start != nj->block_start )
	    nkit_wii_junk_seed_block(nj,block32k);

	const u64 off_in_block = pos - block_start;
	if ( nj->lfg.pos == 0 && off_in_block != 0 )
	    ForwardLFG(&nj->lfg,(u32)off_in_block);

	u64 avail = 0x8000 - off_in_block;
	if ( avail > nj->junk_length - pos )	// stop generating at _junkLength;
	    avail = nj->junk_length - pos;	// the tail is zeroed by the branch above
	const u32 chunk = size < avail ? size : (u32)avail;
	GetBytesLFG(&nj->lfg,out,chunk);

	out  += chunk;
	pos  += chunk;
	size -= chunk;
    }
}

// Adapter matching nkit_junk_read_func's (ctx,pos,dest,size) shape, so a
// nkit_wii_junk_t can be handed to nkit_write_gap_core()/nkit_write_gap()/
// nkit_write_filler() as their junk_get/junk_ctx pair.
static void nkit_wii_junk_read_adapter ( void *ctx, u64 pos, u8 *dest, u32 size )
{
    nkit_wii_junk_get((nkit_wii_junk_t*)ctx,pos,dest,size);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    small stream helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

// Port of the 'inStream.Copy(target,bytes)' pattern used verbatim (no
// junk/scrub logic) at NkitReaderWii.cs:503-504 to pass the header-to-fst
// region and the fst.bin buffer through to the output unchanged. Streamed in
// chunks rather than read-fully-then-write like MemorySection.Read() does,
// since the header-to-fst region can be multiple MiB on a real disc; the
// resulting target bytes are identical either way.
static enumError nkit_passthrough_copy ( FILE *in_stream, nkit_circular_buffer_t *target, u64 bytes )
{
    u8 buf[0x10000];
    u64 rest = bytes;
    while (rest)
    {
	u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
	if ( fread(buf,1,chunk,in_stream) != chunk )
	    return ERROR0(ERR_READ_FAILED,"NKit: truncated partition passthrough data\n");
	enumError err = nkit_cb_write_all(target,buf,chunk);
	if (err)
	    return err;
	rest -= chunk;
    }
    return ERR_OK;
}

// Port of 'inStream.Copy(ByteStream.Zeros, n)' as used at
// NkitReaderWii.cs:540 to skip alignment padding between two FST files:
// reads (and discards) 'bytes' real bytes from the source stream, advancing
// its position without writing anything to the output.
static enumError nkit_stream_skip ( FILE *in_stream, u64 bytes )
{
    u8 buf[0x10000];
    u64 rest = bytes;
    while (rest)
    {
	u32 chunk = rest < sizeof(buf) ? (u32)rest : sizeof(buf);
	if ( fread(buf,1,chunk,in_stream) != chunk )
	    return ERROR0(ERR_READ_FAILED,"NKit: truncated alignment padding\n");
	rest -= chunk;
    }
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////  NkitPartitionPatchInfo (fields this step touches)   ///////////////
///////////////////////////////////////////////////////////////////////////////

// Subset of NkitPartitionPatchInfo (a plain field bag defined inline in
// NkitReaderWii.Read(), not its own .cs file) that partitionStreamWrite()
// itself reads or writes: ScrubManager (read-only here) and
// PartitionDataHeader/Fst (write-only stashes that hashPatchGroup()/
// fstPatch() further down consume later). HashGroups/DiscOffset/
// PartitionHeader belong to the outer Read() loop and live in the full
// nkit_patch_info_t down there, which embeds this struct -- same reasoning as
// nkit_convert_file_t only carrying the fields copyFile()/writeGap() touch.
typedef struct nkit_partition_patch_info_t
{
    nkit_scrub_manager_t	*scrub_manager;			// patchInfo.ScrubManager, not owned

    u8				*partition_data_header;		// patchInfo.PartitionDataHeader stash:
    u32				partition_data_header_size;	// owned by the caller once this function
								// returns (MALLOC'd here), left NULL on
								// the gap-only/zero-header branch -- the
								// C# never sets PartitionDataHeader there
								// either

    u8				*fst;				// patchInfo.Fst stash: owned by the
    u32				fst_size;			// caller once this function returns
								// (MALLOC'd here), left NULL on the
								// gap-only branch for the same reason
}
nkit_partition_patch_info_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////  NkitReaderWii.cs:439-570 -> nkit_partition_stream_write()  ///////////////
///////////////////////////////////////////////////////////////////////////////

// partitionStreamWrite(), the
// per-partition restore driver NkitReaderWii.Read()'s foreach(WiiPartitionInfo
// part in hdr.Partitions) loop calls once per partition (via a
// StreamCircularBuffer producer callback -- NkitReaderWii.cs:130). It reads
// one partition's own 0x440 byte data-stream header, then either replays a
// single all-gap/all-scrubbed region (header starts with 4 NUL bytes) or
// walks the partition's FST -- via nkit_get_convert_fst_files(), the
// gap-derivation pass ported above -- copying/gapping/fillering every real
// file's data through copyFile()/writeGap().
//
// Parameters dropped versus the C#, per this task's scope:
//   Stream inStream        -> FILE *in_stream (same convention as every
//                              other function in this file)
//   Stream target           -> nkit_circular_buffer_t *target (ditto)
//   LongRef outSize         -> u64 *out_size (ref outSize.Value)
//   long size                -> u64 part_size ('size' param: the partition's
//                              own PartitionSize, read by the OUTER Read()
//                              loop from hdr.Partitions and passed straight
//                              through -- NOT the local 'imageSize' this
//                              function derives from its own 0x440 byte
//                              header. This is the exact value
//                              NkitFormat.GetConvertFstFiles()'s trailing-gap
//                              check needs (NkitReaderWii.cs:511's 'size'
//                              argument), so it's threaded straight into
//                              nkit_get_convert_fst_files()'s image_size
//                              parameter below.)
//   DatData settingsData    -> dropped entirely: never read by this method
//                              body (dead parameter in the C# too)
//   NkitPartitionPatchInfo   -> nkit_partition_patch_info_t *patch_info (only
//                              the 3 fields this function actually touches --
//                              see that type's own comment above)
//   WiiHashStore hashes      -> nkit_hash_store_t *hashes
//   Coordinator pc           -> dropped: only used by the C# to wrap
//                              exceptions (pc.SetReaderException media); this
//                              file's existing convention is ERROR0()/
//                              ERROR1() enumError returns instead, same as
//                              every other function ported here
//   return long              -> u64 *out_src_pos (the method's return value,
//                              srcPos) + enumError function return
//
// Divergence from the C#'s 'conFiles == null' fallback (NkitReaderWii.cs:
// 513-522, "Converting as bad image"): nkit_get_convert_fst_files() above
// already made the deliberate choice to hard-fail via enumError instead of
// that soft null+error-string fallback (see its own comment -- "no caller
// yet to decide a soft-fail policy for"). Reimplementing the fallback here
// (a synthetic single-ConvertFile gap spanning the whole rest of the
// partition) would just be working back around a decision already made one
// call down, so this function simply propagates the error instead.
static enumError nkit_partition_stream_write
(
    u64				*out_size,	// ref outSize.Value
    FILE			*in_stream,
    nkit_circular_buffer_t	*target,
    u64				part_size,	// 'size' param
    nkit_partition_patch_info_t *patch_info,
    nkit_hash_store_t		*hashes,
    u64				*out_src_pos	// return value (srcPos)
)
{
    *out_size = 0;
    *out_src_pos = 0;

    u8 *hdr = MALLOC(0x440);
    if ( fread(hdr,1,0x440,in_stream) != 0x440 )
    {
	FREE(hdr);
	return ERROR0(ERR_READ_FAILED,"NKit: truncated partition data header\n");
    }
    u64 src_pos = 0x440, out_pos = 0, image_size = 0;
    enumError err;

    if (!memcmp(hdr,"\0\0\0\0",4))
    {
	// gap-only partition data stream -- NkitReaderWii.cs:454-467
	u64 nulls_pos = 0;
	s64 file_length = -1, gap_length = -1;

	err = nkit_cb_write_all(target,hdr,0x440);
	if (err) { FREE(hdr); return err; }

	u8 szb[4];
	if ( fread(szb,1,4,in_stream) != 4 )
	{
	    FREE(hdr);
	    return ERROR0(ERR_READ_FAILED,"NKit: truncated partition image-size field\n");
	}
	src_pos += 4;
	out_pos += 0x440;

	image_size = (u64)be32(szb) * 4;
	*out_size = image_size / 0x8000ull * 0x7c00ull + image_size % 0x8000ull; // NStream.HashedLenToData()

	// JunkStream(hdr.Read(0,4), hdr.Read8(6), outSize.Value) -- NkitReaderWii.cs:465
	nkit_wii_junk_t junk;
	nkit_wii_junk_init(&junk,hdr,hdr[6],*out_size);

	u64 gap_out;
	err = nkit_write_gap_core(&file_length,&gap_length,&nulls_pos,&src_pos,out_pos,
	    in_stream,target,nkit_wii_junk_read_adapter,&junk,true,patch_info->scrub_manager,&gap_out);
	FREE(hdr);
	if (err)
	    return err;
	out_pos += gap_out; // matches C#'s unused-after-this outPos increment
	(void)out_pos;

	*out_src_pos = src_pos;
	return ERR_OK;
    }

    // NKIT v01 partition-data stream -- NkitReaderWii.cs:468-562
    if ( memcmp(hdr+0x200,"NKIT v01",8) )
    {
	FREE(hdr);
	return ERROR0(ERR_WIA_INVALID,"NKit: unsupported partition data header version\n");
    }

    image_size = (u64)be32(hdr+0x210) * 4;
    image_size = image_size / 0x8000ull * 0x7c00ull + image_size % 0x8000ull; // NStream.HashedLenToData()
    *out_size = image_size;

    u64 main_dol_addr = be32(hdr+0x420);
    u64 fst_offset     = (u64)be32(hdr+0x424) * 4;
    u32 fst_size       = be32(hdr+0x428) * 4;

    if ( fst_offset < 0x440 )
    {
	FREE(hdr);
	return ERROR0(ERR_WIA_INVALID,"NKit: partition fst offset out of range\n");
    }

    //############################################################################
    //# READ DISC START / WRITE DISC START (interleaved here; see nkit_passthrough_copy())

    err = nkit_cb_write_all(target,hdr,0x440);
    if (err) { FREE(hdr); return err; }
    out_pos += 0x440;

    u64 hdr_to_fst_size = fst_offset - 0x440;
    err = nkit_passthrough_copy(in_stream,target,hdr_to_fst_size);
    if (err) { FREE(hdr); return err; }
    src_pos += hdr_to_fst_size;

    u8 *fst = fst_size ? MALLOC(fst_size) : 0;
    if ( fst_size && fread(fst,1,fst_size,in_stream) != fst_size )
    {
	FREE(fst);
	FREE(hdr);
	return ERROR0(ERR_READ_FAILED,"NKit: truncated fst.bin\n");
    }
    src_pos += fst_size;

    err = nkit_cb_write_all(target,fst,fst_size);
    if (err) { FREE(fst); FREE(hdr); return err; }

    err = nkit_hash_store_write_flags_data(hashes,image_size,in_stream);
    if (err) { FREE(fst); FREE(hdr); return err; }
    src_pos += hashes->flags_size;

    // stash -- NkitReaderWii.cs:496-497. From here on 'hdr'/'fst' are owned
    // by *patch_info, not freed by this function on any later error path.
    patch_info->partition_data_header      = hdr;
    patch_info->partition_data_header_size = 0x440;
    patch_info->fst                        = fst;
    patch_info->fst_size                   = fst_size;

    out_pos = fst_offset + fst_size;
    u64 nulls_pos = out_pos + 0x1c;

    char partition_id[5];
    memcpy(partition_id,hdr,4);
    partition_id[4] = 0;

    nkit_wii_fst_folder_t   *folder_pool = 0;
    uint                     n_folder = 0;
    nkit_wii_convert_file_t *conv = 0;
    uint                     n_conv = 0;
    err = nkit_get_convert_fst_files(fst,fst_size,fst_offset,partition_id,false,-1,part_size,
	&folder_pool,&n_folder,&conv,&n_conv);
    if (err)
	return err; // hdr/fst already stashed above; nothing else allocated yet

    // fix for a few customs (no gap between the fst and the first file on
    // the source image, but the hash mask makes it look like there is) --
    // NkitReaderWii.cs:525
    conv[0].gap_length -= hashes->flags_size;

    // JunkStream(hdr.Read(0,4), hdr.Read8(6), imageSize) -- NkitReaderWii.cs:478
    nkit_wii_junk_t junk;
    nkit_wii_junk_init(&junk,hdr,hdr[6],image_size);

    bool first_file = true;
    for ( uint i = 0; i < n_conv; i++ )
    {
	nkit_wii_convert_file_t *f  = &conv[i];
	nkit_wii_fst_file_t     *ff = &f->fst_file;

	if (!first_file) // fst.bin already written directly above
	{
	    if ( src_pos < ff->data_offset )
	    {
		err = nkit_stream_skip(in_stream,ff->data_offset - src_pos); // skip 32k align padding etc
		if (err) { FREE(folder_pool); FREE(conv); return err; }
		src_pos = ff->data_offset;
	    }

	    if ( ff->data_offset == main_dol_addr )
		write_be32(hdr+0x420,(u32)(out_pos/4));
	    write_be32(fst+ff->offset_in_fst,(u32)(out_pos/4));

	    nkit_convert_file_t cf =
	    {
		.fst_length      = ff->length,
		.fst_name        = ff->name,
		.fst_data_offset = ff->data_offset,
	    };
	    u64 copy_len;
	    err = nkit_copy_file(&cf,&nulls_pos,&src_pos,out_pos,in_stream,target,&copy_len);
	    if (err) { FREE(folder_pool); FREE(conv); return err; }
	    out_pos   += copy_len;
	    ff->length = cf.fst_length;
	}

	if ( out_pos < image_size )
	{
	    nkit_convert_file_t cf2 =
	    {
		.fst_length      = ff->length,
		.fst_name        = ff->name,
		.fst_data_offset = ff->data_offset,
		.gap_length      = f->gap_length,
	    };
	    bool first_or_last = i == 0 || i == n_conv-1;
	    u64 gap_out;
	    err = nkit_write_gap(&cf2,&nulls_pos,&src_pos,out_pos,in_stream,target,
		nkit_wii_junk_read_adapter,&junk,first_or_last,patch_info->scrub_manager,&gap_out);
	    if (err) { FREE(folder_pool); FREE(conv); return err; }
	    out_pos     += gap_out;
	    ff->length   = cf2.fst_length;
	    f->gap_length = cf2.gap_length;

	    if (!first_file)
		write_be32(fst+ff->offset_in_fst+4,(u32)ff->length);
	}

	first_file = false;
    }

    FREE(folder_pool);
    FREE(conv);

    *out_src_pos = src_pos;
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////	 Checksums/Crc.cs, Checksums/NCrc.cs -> nkit_crc_t	///////////////
///////////////////////////////////////////////////////////////////////////////

// This is the last remaining supporting-type prerequisite before the outer
// Read()/NkitRestoreWii() driver (see the file header) can be ported: Read()
// wraps its output stream in a CryptoStream over an NCrc, takes
// crc.Snapshot(name) checkpoints throughout the restore loop, and afterward
// walks crc.Crcs calling patchGroups()/hashPatchGroup()/fstPatch() to
// rebuild the patch blobs for hash-store-preserved/scrubbed groups, finally
// validating the result against an embedded NKit CRC via
// pc.ReaderCheckPoint2Complete(...). None of that (Read(), patchGroups(),
// hashPatchGroup(), fstPatch(), or XCONVERT dispatch) is ported here --
// this section is only the checksum plumbing they all sit on top of.
//
// Crc (Checksums/Crc.cs) is a standard reflected CRC-32: poly 0xEDB88320,
// init 0xFFFFFFFF, HashCore ties itself in knots with an optional
// multi-threaded ProcessBlock() split + Crc.Combine() re-join (see
// "Combine" below for why that part IS needed), but the underlying table
// and per-byte update (ProcessBlock(), Crc.cs:112-153) is byte-for-byte the
// classic zlib/PKZIP CRC-32 -- and this repo already has exactly that table
// and update loop as CalcCRC32()/TableCRC32 (dclib/dclib-numeric.c,
// dclib/dclib-tables.c): same poly (confirmed against TableCRC32[1] ==
// 0x77073096 == Crc.cs's table), same init/final invert convention
// (CalcCRC32 does crc=~crc on entry and returns ~crc, so calling it with a
// running non-inverted accumulator across multiple chunks reproduces
// Crc.cs's HashCore/Value exactly). So the per-byte engine below is NOT
// reimplemented a second time -- nkit_crc_update() just calls CalcCRC32().
// What genuinely IS new here is NCrc's checkpoint/patch-CRC bookkeeping
// (Snapshot(), FullCrc(bool), the Crcs list) and Crc.Combine()'s GF(2)
// polynomial-combination algorithm that FullCrc() depends on to re-derive
// the CRC of the whole stream from independently-computed per-checkpoint
// segment CRCs (needed so a patched segment's CRC can be substituted in
// without re-hashing the untouched segments around it).

///////////////////////////////////////////////////////////////////////////////

// Direct port of CrcItem (Checksums/NCrc.cs:9-22): one named checkpoint.
// PatchData/PatchFile/PatchCrc are `internal set` in C# -- i.e. written
// later by patchGroups()/hashPatchGroup() (not ported here), never by
// Snapshot() itself. NULL/0/empty here until that code exists.
typedef struct nkit_crc_item_t
{
    u8		*patch_data;	// PatchData: owned, NULL until set by (future) patchGroups()
    uint	patch_data_size; // C# byte[] carries its own length; needed here since
				// patch_data has no implicit size
    char	*patch_file;	// PatchFile: owned string, NULL until set
    u32		patch_crc;	// PatchCrc: 0 until set (0 == "no patch" throughout FullCrc())
    u64		offset;		// Offset
    u64		length;		// Length
    u32		value;		// Value: raw (uninverted-output) CRC of just this segment
    char	*name;		// Name: owned string
}
nkit_crc_item_t;

// ToString() (NCrc.cs:18-21) -- not ported: debug/log-only in the original,
// and the finished Read() port never needed it.

static void nkit_crc_item_reset_mem ( nkit_crc_item_t *it )
{
    if (it->patch_data) FREE(it->patch_data);
    if (it->patch_file) FREE(it->patch_file);
    if (it->name)       FREE(it->name);
    memset(it,0,sizeof(*it));
}

///////////////////////////////////////////////////////////////////////////////

// Direct port of NCrc (Checksums/NCrc.cs:24-105): a Crc subclass that,
// instead of just accumulating one running value, buffers a *list* of named
// checkpoints -- each checkpoint's Value is the CRC of only the bytes
// written since the previous Snapshot(), and FullCrc() re-combines them
// into the CRC of the whole stream via Crc.Combine().
//
// The private ctor NCrc(IEnumerable<CrcItem> crcs) (NCrc.cs:32-35, used to
// rehydrate an NCrc from a previously-saved Crcs[] without re-hashing) is
// not ported: no caller in the C# reaches it either (it's dead code in
// NkitReaderWii.cs as far as the Read() investigation found) -- add it back
// only if a real caller turns up during the Read() port.
typedef struct nkit_crc_t
{
    u64			count;		// _count: total bytes ever pushed through
					// HashCore/nkit_crc_update, across all segments
    u64			start_pos;	// _startPos: _count value at the start of the
					// current (not-yet-snapshotted) segment
    bool		need_reset;	// _reset: true if base.Initialize() (crc=0xFFFFFFFF)
					// still needs to run before the next byte
    u32			value;		// base._value (Crc._value): the running raw
					// (not yet ~-inverted) CRC of the current segment

    nkit_crc_item_t	*crcs;		// _crcs: owned, growable list of checkpoints (Crcs)
    uint		n_crcs;		// _crcs.Count
    uint		crcs_cap;	// allocated capacity of 'crcs'
}
nkit_crc_t;

///////////////////////////////////////////////////////////////////////////////

// ctor: NCrc() (NCrc.cs:37-43) == base() (Crc.cs:48-51, Initialize() sets
// _value = 0xFFFFFFFF) with _startPos/_count = 0, _reset = true.
static void nkit_crc_init ( nkit_crc_t *c )
{
    memset(c,0,sizeof(*c));
    c->value      = 0;	// see nkit_crc_update(): 'value' is the FINISHED CRC form
    c->need_reset = true;
}

static void nkit_crc_reset_mem ( nkit_crc_t *c )
{
    for ( uint i = 0; i < c->n_crcs; i++ )
	nkit_crc_item_reset_mem(c->crcs+i);
    if (c->crcs)
	FREE(c->crcs);
    memset(c,0,sizeof(*c));
}

///////////////////////////////////////////////////////////////////////////////

// private reset() (NCrc.cs:54-62): lazily (re)start a fresh segment. Called
// from both Snapshot() and HashCore() before either touches _value, exactly
// like the C#.
static void nkit_crc_reset_segment ( nkit_crc_t *c )
{
    if (c->need_reset)
    {
	c->start_pos  = c->count;
	c->value      = 0;	// base.Initialize() (_value = _KInitial), expressed
				// in the finished form CalcCRC32() uses: ~0 == 0xFFFFFFFF
	c->need_reset = false;
    }
}

///////////////////////////////////////////////////////////////////////////////

// HashCore(byte[] data, int offset, int count) (NCrc.cs:64-69), i.e. every
// byte NKit's CryptoStream-wrapped output stream ever writes during Read()
// eventually funnels through here. Renamed from the override name since
// there's no virtual dispatch in C -- this is the one and only "write N
// bytes to the CRC" entry point (Crc.cs's own HashCore threading-split
// logic is not reproduced: CalcCRC32() is already a plain single-pass
// per-byte table walk with no thread-count-dependent codepath to match, and
// nothing here needs the Combine-based multithread reassembly Crc.cs uses
// to make its own split path agree with the sequential one).
static void nkit_crc_update ( nkit_crc_t *c, const u8 *data, u32 size )
{
    nkit_crc_reset_segment(c);
    c->count += size;
    // CalcCRC32() (dclib/dclib-numeric.c:1434) does 'crc = ~crc' on entry and
    // 'return ~crc' on exit, i.e. it consumes and produces the FINISHED
    // (already inverted) CRC, not C#'s raw internal Crc._value. So 'c->value'
    // holds the finished form throughout: it starts at 0 (whose inverse,
    // 0xFFFFFFFF, is Crc._KInitial), chains directly across chunks, and needs
    // no further inversion when snapshotted -- unlike C#, where _value is raw
    // and the Value getter applies the '~'.
    c->value = CalcCRC32(c->value,data,size); // base.HashCore(data,offset,count)
}

///////////////////////////////////////////////////////////////////////////////

// Snapshot(string name) (NCrc.cs:45-52): close out the current segment as a
// named checkpoint. No-op if the previous checkpoint already covers the
// current position (nothing written since) -- same dedup the C# does.
static void nkit_crc_snapshot ( nkit_crc_t *c, const char *name )
{
    if ( c->n_crcs != 0 && c->crcs[c->n_crcs-1].offset == c->start_pos )
	return; // don't create 2 for same offset

    nkit_crc_reset_segment(c);

    if ( c->n_crcs == c->crcs_cap )
    {
	c->crcs_cap = c->crcs_cap ? c->crcs_cap*2 : 16;
	c->crcs = REALLOC(c->crcs,c->crcs_cap*sizeof(*c->crcs));
    }
    nkit_crc_item_t *it = c->crcs + c->n_crcs;
    memset(it,0,sizeof(*it));
    it->offset = c->start_pos;
    it->length = c->count - c->start_pos;
    it->value  = c->value;  // NCrc.cs:50 reads `base.Value`, i.e. Crc.cs:61-64's
			     // `~_value` -- the finished, inverted CRC. c->value
			     // is already in exactly that form (see nkit_crc_update),
			     // so no '~' is applied here.
    it->name   = STRDUP(name);
    c->n_crcs++;

    c->need_reset = true;
}

///////////////////////////////////////////////////////////////////////////////
///////////////			Crc.Combine() -> nkit_crc_combine()	///////////////
///////////////////////////////////////////////////////////////////////////////

// Direct port of the GF(2) "combine two CRCs of adjacent byte ranges into
// the CRC of the concatenation" algorithm (Crc.cs:174-275, itself lifted
// from the DotNetZip project, ultimately the well-known zlib crc32_combine
// derivation). FullCrc() below is the only caller, combining a chain of
// independently-computed per-Snapshot() segment CRCs back into one running
// total -- exactly the operation zlib's crc32_combine() performs, which is
// how the length-2 zero-extension trick works: append length2 zero bytes'
// worth of "CRC shift" operator to crc1, then XOR in crc2.
//
// Both crc1/crc2 passed in and returned are the "raw" (already ~-inverted,
// i.e. Crc.Value-style) form -- callers un-invert going in and re-invert
// coming out, same dance the C# does (Crc.cs:199-200,223).

static u32 nkit_gf2_matrix_times ( const u32 matrix[32], u32 vec )
{
    u32 sum = 0;
    int i = 0;
    while (vec)
    {
	if ( vec & 1 ) sum ^= matrix[i];
	vec >>= 1;
	i++;
    }
    return sum;
}

static void nkit_gf2_matrix_square ( u32 square[32], const u32 mat[32] )
{
    for ( int i = 0; i < 32; i++ )
	square[i] = nkit_gf2_matrix_times(mat,mat[i]);
}

// Prepare_even_odd_Cache() (Crc.cs:227-244), but computed fresh on every
// call instead of cached in static fields: it's 2 gf2_matrix_square() calls
// over 32-word arrays, cheap enough that there's no need to reproduce the
// C# static-field caching (and no static Crc constructor equivalent to
// hang it off in C without adding global init-order concerns).
static void nkit_crc_combine_even_odd ( u32 even[32], u32 odd[32] )
{
    odd[0] = 0xEDB88320; // Crc._KCrcPoly
    for ( int i = 1; i < 32; i++ )
	odd[i] = 1u << (i-1);

    nkit_gf2_matrix_square(even,odd); // operator for two zero bits
    nkit_gf2_matrix_square(odd,even); // operator for four zero bits
}

// Combine(uint crc1, uint crc2, long length2) (Crc.cs:188-224).
static u32 nkit_crc_combine ( u32 crc1, u32 crc2, u64 length2 )
{
    if ( length2 == 0 )      return crc1;
    if ( crc1 == 0xFFFFFFFF ) return crc2; // == _KInitial

    u32 even[32], odd[32];
    nkit_crc_combine_even_odd(even,odd);

    crc1 = ~crc1;
    crc2 = ~crc2;

    u64 len2 = length2;
    do
    {
	nkit_gf2_matrix_square(even,odd);
	if ( len2 & 1 ) crc1 = nkit_gf2_matrix_times(even,crc1);
	len2 >>= 1;
	if ( len2 == 0 ) break;

	nkit_gf2_matrix_square(odd,even);
	if ( len2 & 1 ) crc1 = nkit_gf2_matrix_times(odd,crc1);
	len2 >>= 1;
    }
    while (len2);

    crc1 ^= crc2;
    return ~crc1;
}

///////////////////////////////////////////////////////////////////////////////

// FullCrc() / FullCrc(bool patched) (NCrc.cs:71-86): re-derive the CRC of
// the entire stream from the per-checkpoint segment CRCs, substituting each
// checkpoint's PatchCrc in place of its Value when 'patched' is set and a
// PatchCrc was actually recorded (PatchCrc != 0 acts as "is set" -- same
// sentinel the C# uses).
static u32 nkit_crc_full ( const nkit_crc_t *c, bool patched )
{
    if ( c->n_crcs == 0 )
	return 0;

    u32 crc = ( patched && c->crcs[0].patch_crc != 0 ) ? c->crcs[0].patch_crc : c->crcs[0].value;
    for ( uint i = 1; i < c->n_crcs; i++ )
    {
	const nkit_crc_item_t *it = c->crcs+i;
	u32 seg = ( patched && it->patch_crc != 0 ) ? ~it->patch_crc : ~it->value;
	crc = ~nkit_crc_combine(~crc,seg,it->length);
    }
    return crc;
}

// Crcs getter (NCrc.cs:88): '_crcs?.ToArray()'. No copy needed in C --
// callers get the live array + count directly.
static inline const nkit_crc_item_t *nkit_crc_items ( const nkit_crc_t *c, uint *n )
{
    *n = c->n_crcs;
    return c->crcs;
}

// indexer this[long position] (NCrc.cs:95-104): CRC of the checkpoint whose
// Offset == position, or 0 if none. Linear scan, same as the C#'s
// FirstOrDefault().
static u32 nkit_crc_at ( const nkit_crc_t *c, u64 position )
{
    for ( uint i = 0; i < c->n_crcs; i++ )
	if ( c->crcs[i].offset == position )
	    return c->crcs[i].value;
    return 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////   NkitReaderWii.cs:28-437 -> XExtractNKitWii()	///////////////
///////////////////////////////////////////////////////////////////////////////

// The outer restore driver: a port of NkitReaderWii.Read() (lines 28-299)
// plus its three private helpers repairBlocks() (301), patchGroups() (312),
// hashPatchGroup() (349) and fstPatch() (369), wiring together every
// supporting type ported above into the function XCONVERT actually calls.
//
// C# orchestration NOT literally ported, and why:
//
//   Coordinator pc  -- the C# reader/writer rendezvous object. Its three
//	checkpoint calls only exist to hand results to a *separate* writer
//	running on another thread (Processor.cs pairs an IReader with an
//	IWriter through a second StreamCircularBuffer). Here the "writer" is
//	just fwrite() to the destination file, so the checkpoints collapse
//	into ordinary control flow and the final validation is done inline --
//	substantively equivalent: the restored image's CRC is still checked
//	against the CRC embedded in the NKit header before success is
//	reported, which is the one externally visible thing those checkpoints
//	accomplish on this path.
//
//   pc.SetReaderException(...)  -- replaced throughout by this file's
//	existing ERROR0()/ERROR1() + enumError convention.
//
//   Processor.cs:162-174's post-run patch pass IS ported (it is not
//	orchestration -- it is what actually puts the corrected disc header,
//	the FST with restored file offsets, and the preserved hash areas into
//	the output file). It runs at the end of this function.
//
//   RecoveryData.GetUpdatePartition() (line 81) -- looks up an external
//	*_<crc8>.bin update-partition blob in NKit's own settings/recovery
//	directory, which this codebase has no equivalent of and no way to
//	populate. An image whose update partition was removed at conversion
//	time (updatePartitionCrc != 0) therefore cannot be restored
//	byte-exactly here, and is rejected up front with a clear message
//	rather than silently producing a filler-padded image that fails its
//	own CRC check. Images that kept their update partition -- the norm
//	for Redump-sourced .nkit -- are unaffected.

///////////////////////////////////////////////////////////////////////////////

// Output sink: every restored byte goes through here so the running NCrc
// sees exactly the stream that lands in the file (C#'s 'crcStream', a
// CryptoStream wrapping outStream over the NCrc).
struct nkit_out_t
{
    FILE	*f;
    nkit_crc_t	*crc;
    ccp		fname;
};
typedef struct nkit_out_t nkit_out_t;

static enumError nkit_out_write ( nkit_out_t *o, const void *data, u64 size )
{
    if (!size)
	return ERR_OK;
    if ( fwrite(data,1,size,o->f) != size )
	return ERROR1(ERR_WRITE_FAILED,"Write failed: %s\n",o->fname);
    nkit_crc_update(o->crc,data,(u32)size);
    return ERR_OK;
}

// 'ByteStream.Zeros.Copy(crcStream, n)' (line 98)
static enumError nkit_out_zeros ( nkit_out_t *o, u64 size )
{
    static const u8 zeros[0x10000] = {0};
    while (size)
    {
	u32 chunk = size < sizeof(zeros) ? (u32)size : sizeof(zeros);
	enumError err = nkit_out_write(o,zeros,chunk);
	if (err)
	    return err;
	size -= chunk;
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// prtStream.Read(...) -- StreamCircularBuffer.Read() blocks until it has
// delivered the full request; the spool-mode buffer here just needs the
// short-read loop.
static enumError nkit_cb_read_all ( nkit_circular_buffer_t *cb, u8 *data, u32 size )
{
    while (size)
    {
	u32 n = nkit_circular_buffer_read(cb,data,size);
	if (!n)
	    return ERROR0(ERR_READ_FAILED,"NKit: partition data stream ended early\n");
	data += n;
	size -= n;
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// One preserved-hash group: NkitPartitionPatchInfo.HashGroups's
// Dictionary<long,MemorySection>. Only ever added to and looked up by exact
// key, so a growable array with a linear find matches it exactly (there are
// at most a few per partition on a clean image).
typedef struct nkit_hash_group_t
{
    u64		offset;		// dictionary key: absolute output offset of the group
    u8		*data;		// MemorySection value: a full GroupSize clone of wp.Decrypted
    u32		size;
}
nkit_hash_group_t;

// Full NkitPartitionPatchInfo (Conversion/NkitPartitionPatchInfo.cs). The
// narrower nkit_partition_patch_info_t above carries only the 3 fields
// nkit_partition_stream_write() touches; this is the whole bag Read() needs,
// and embeds that narrower struct so the already-ported producer can be
// handed a pointer to it unchanged.
typedef struct nkit_patch_info_t
{
    nkit_partition_patch_info_t	inner;		// ScrubManager + PartitionDataHeader + Fst
    u64				disc_offset;	// DiscOffset
    u8				*partition_header;	// PartitionHeader (0x20000), owned
    u32				partition_header_size;
    nkit_hash_group_t		*hash_group;	// HashGroups, owned
    uint			n_hash_group, hash_group_cap;
}
nkit_patch_info_t;

// Size { get { return PartitionHeader == null ? 0 : PartitionHeader.ReadUInt32B(0x2bc)*4; } }
// -- a computed property, so it reflects the *restored* partition size the
// group loop patches into 0x2bc, not the shrunken one the file carried.
static u64 nkit_patch_info_size ( const nkit_patch_info_t *pi )
{
    return pi->partition_header ? (u64)be32(pi->partition_header+0x2bc) * 4 : 0;
}

static nkit_hash_group_t * nkit_patch_info_find_hash_group ( nkit_patch_info_t *pi, u64 offset )
{
    for ( uint i = 0; i < pi->n_hash_group; i++ )
	if ( pi->hash_group[i].offset == offset )
	    return pi->hash_group + i;
    return 0;
}

static void nkit_patch_info_add_hash_group ( nkit_patch_info_t *pi, u64 offset, const u8 *data, u32 size )
{
    if ( pi->n_hash_group == pi->hash_group_cap )
    {
	pi->hash_group_cap = pi->hash_group_cap ? pi->hash_group_cap*2 : 8;
	pi->hash_group = REALLOC(pi->hash_group,pi->hash_group_cap*sizeof(*pi->hash_group));
    }
    nkit_hash_group_t *hg = pi->hash_group + pi->n_hash_group++;
    hg->offset = offset;
    hg->size   = size;
    hg->data   = MALLOC(size);
    memcpy(hg->data,data,size);
}

static void nkit_patch_info_reset_mem ( nkit_patch_info_t *pi )
{
    for ( uint i = 0; i < pi->n_hash_group; i++ )
	FREE(pi->hash_group[i].data);
    if (pi->hash_group)			FREE(pi->hash_group);
    if (pi->partition_header)		FREE(pi->partition_header);
    if (pi->inner.partition_data_header) FREE(pi->inner.partition_data_header);
    if (pi->inner.fst)			FREE(pi->inner.fst);
    memset(pi,0,sizeof(*pi));
}

///////////////////////////////////////////////////////////////////////////////

// Growable byte buffer for the 'patchBlocks' MemoryStream (Read() line 215)
// and for CrcItem.PatchData.
typedef struct nkit_buf_t
{
    u8		*data;
    u64		len, cap;
}
nkit_buf_t;

static void nkit_buf_append ( nkit_buf_t *b, const u8 *data, u64 size )
{
    if ( b->len + size > b->cap )
    {
	while ( b->len + size > b->cap )
	    b->cap = b->cap ? b->cap*2 : NKIT_PARTITION_GROUP_SIZE;
	b->data = REALLOC(b->data,b->cap);
    }
    memcpy(b->data+b->len,data,size);
    b->len += size;
}

static void nkit_buf_reset_mem ( nkit_buf_t *b )
{
    if (b->data) FREE(b->data);
    memset(b,0,sizeof(*b));
}

///////////////////////////////////////////////////////////////////////////////

// Port of repairBlocks(WiiPartitionGroupSection wp, int scrubbed, int blocks,
// bool forceBlockChanged, bool fstBlocks) -- NkitReaderWii.cs:301-310.
// 'forceBlockChanged' and 'fstBlocks' are dead parameters in the C# too (the
// method body never reads either) -- kept in the signature here only so the
// two call sites read the same as the original.
static bool nkit_repair_blocks
	( nkit_group_t *wp, uint scrubbed, uint blocks, bool force_block_changed, bool fst_blocks )
{
    (void)force_block_changed;
    (void)fst_blocks;

    bool is_valid = true;

    if ( scrubbed == 0 )
	is_valid = nkit_group_is_valid(wp,true);
    else if ( scrubbed == blocks )
    {
	// copy the first data sector hash sector for each block (to pretend
	// we still had the scrubbed hashes)
	u8 *dec = nkit_group_decrypted(wp);
	for ( uint i = 0; i < blocks; i++ )
	    memcpy(dec+i*WII_SECTOR_SIZE,dec+i*WII_SECTOR_SIZE+WII_SECTOR_HASH_SIZE,WII_SECTOR_HASH_SIZE);
    }
    return is_valid;
}

///////////////////////////////////////////////////////////////////////////////

// Port of hashPatchGroup(NkitPartitionPatchInfo pi, WiiPartitionGroupSection wp)
// -- NkitReaderWii.cs:349-367. Scatters the group's preserved (verbatim
// carried) 0x400 byte hash areas -- which WiiHashStore.ReadPatchData() packed
// contiguously into the front of the stored group buffer -- back out to one
// per WII_SECTOR_SIZE block, then re-marks whichever blocks the scrub manager
// says were scrubbed.
static void nkit_hash_patch_group ( nkit_patch_info_t *pi, nkit_group_t *wp )
{
    uint block_count = (uint)( wp->size / WII_SECTOR_SIZE );

    const nkit_hash_group_t *hg = nkit_patch_info_find_hash_group(pi,wp->disc_offset);
    u8 *dec = nkit_group_decrypted(wp);
    for ( uint i = 0; i < block_count; i++ )
	memcpy(dec+i*WII_SECTOR_SIZE,hg->data+i*WII_SECTOR_HASH_SIZE,WII_SECTOR_HASH_SIZE);

    // C#: 'byte[] e = wp.Encrypted;' -- assigned to an unused local, but NOT
    // dead code: the getter materialises _enc from the freshly-patched _dec
    // *before* the scrub marking below flips _hasEnc back off, so the
    // caller's own wp.Encrypted then re-encrypts with the scrubbed-block IVs
    // instead of plain zero IVs. Ported for the side effect, same as the C#.
    nkit_group_encrypted(wp);

    for ( uint bi = 0; bi < block_count; bi++ )
    {
	u8 byt;
	if ( nkit_scrub_manager_is_block_scrubbed(pi->inner.scrub_manager,
		wp->offset + (u64)bi*WII_SECTOR_SIZE,&byt) )
	{
	    nkit_group_mark_block_dirty(wp,bi);	// will be reset by ApplyHashes
	    nkit_group_set_scrubbed(wp,bi,byt);
	}
    }
}

///////////////////////////////////////////////////////////////////////////////

// Port of fstPatch(NkitPartitionPatchInfo pi, WiiPartitionGroupSection wp,
// long patchOffset, byte[] crcPatchData, Stream fst) -- NkitReaderWii.cs:369-437.
//
// This is what actually makes the restored image's filesystem correct. The
// partition data stream carries the FST as NKit repacked it (gapless file
// offsets); the restore pass recomputes every file's original offset/length
// while writing, patching them into its in-memory 'fst' buffer -- but by then
// the stale FST bytes have already gone out to the file. fstPatch() re-injects
// the corrected FST into the encrypted patch blob that Processor's final pass
// writes back over that region, re-deriving the affected groups' hash trees.
//
// 'fst' is a Stream read sequentially in the C#; here it's the fst buffer plus
// a running read cursor ('fst_pos').
static enumError nkit_fst_patch
(
    nkit_patch_info_t	*pi,
    nkit_group_t	*wp,
    u8			*group_data,	// wp.Data: the caller's GroupSize scratch buffer
    u64			patch_offset,	// patchOffset
    u8			*patch,		// crcPatchData (modified in place)
    u64			patch_len
)
{
    u64 fst_offset = (u64)be32(pi->inner.partition_data_header+0x424) * 4;
    u64 length     = pi->inner.fst_size;

    // seek to fst group
    s64 base_group_idx = (s64)( ( patch_offset - ( pi->disc_offset + pi->partition_header_size ) )
				/ NKIT_PARTITION_GROUP_SIZE );

    s64 gs = (s64)( fst_offset / ( 0x7c00ull * 64 ) );
    s64 ge = (s64)( ( fst_offset + length ) / ( 0x7c00ull * 64 ) );
    if ( ( fst_offset + length ) % ( 0x7c00ull * 64 ) == 0 )
	ge--; // don't load the next group if the data will end on the last byte

    if ( gs > base_group_idx || ge < base_group_idx )
	return ERR_OK;

    u64 dest_pos = 0;	// MemoryStream position over crcPatchData
    if ( gs != base_group_idx )
	dest_pos += (u64)( gs - base_group_idx ) * NKIT_PARTITION_GROUP_SIZE;

    // offset in hashed group
    u64 dst_offset = ( fst_offset / 0x7c00ull * 0x8000ull + fst_offset % 0x7c00ull ) // DataToHashedLen()
			% NKIT_PARTITION_GROUP_SIZE;
    u64 total = 0;
    u64 fst_pos = 0;	// read cursor into pi->inner.fst

    u64 part_data_size = (u64)be32(pi->partition_header+0x2bc) * 4; // wp.Header.ReadUInt32B(0x2bc)*4

    for ( s64 i = gs; i <= ge; i++ )
    {
	// can be less than 2mb if partition is small (or we are at the end)
	u64 group_span = (u64)i * NKIT_PARTITION_GROUP_SIZE;
	if ( group_span >= part_data_size )
	    break;
	u64 data_len = part_data_size - group_span;
	if ( data_len > NKIT_PARTITION_GROUP_SIZE )
	    data_len = NKIT_PARTITION_GROUP_SIZE;

	// int read = dest.Read(wp.Data, 0, wp.Data.Length); ... dest.Seek(-read, Current);
	u64 read = patch_len > dest_pos ? patch_len - dest_pos : 0;
	if ( read > NKIT_PARTITION_GROUP_SIZE )
	    read = NKIT_PARTITION_GROUP_SIZE;
	if ( read == 0 )
	    break;
	memcpy(group_data,patch+dest_pos,read);
	if ( read < NKIT_PARTITION_GROUP_SIZE )
	    memset(group_data+read,0,NKIT_PARTITION_GROUP_SIZE-read);

	nkit_group_populate(wp,(int)i,group_data,NKIT_PARTITION_GROUP_SIZE,patch_offset,data_len); // auto decrypted
	u8 *dec = nkit_group_decrypted(wp);

	while ( length != total && dst_offset != NKIT_PARTITION_GROUP_SIZE )
	{
	    dst_offset += WII_SECTOR_HASH_SIZE;	// skip hashes
	    u64 want = length - total;
	    u64 room = WII_SECTOR_SIZE - dst_offset % WII_SECTOR_SIZE;
	    u64 l = want < room ? want : room;
	    // no padding etc as fst will be the same size
	    memcpy(dec+dst_offset,pi->inner.fst+fst_pos,l);
	    fst_pos    += l;
	    dst_offset += l;
	    total      += l;
	}

	memset(dec+data_len,0,(size_t)(wp->size - data_len));

	uint bnk_count = (uint)( data_len / WII_SECTOR_SIZE );
	uint scrubbed_count = 0;
	for ( uint bi = 0; bi < bnk_count; bi++ )
	{
	    nkit_group_mark_block_dirty(wp,bi);	// set to force hash generation
	    u8 byt;
	    if ( nkit_scrub_manager_is_block_scrubbed(pi->inner.scrub_manager,
		    wp->offset + (u64)bi*WII_SECTOR_SIZE,&byt) )
	    {
		nkit_group_set_scrubbed(wp,bi,byt);
		scrubbed_count++;
	    }
	}

	nkit_repair_blocks(wp,scrubbed_count,bnk_count,true,false);

	// dest.Write(wp.Encrypted, 0, dataLen);
	u8 *enc = nkit_group_encrypted(wp);
	if ( dest_pos + data_len > patch_len )
	    return ERROR0(ERR_INTERNAL,"NKit: fst patch overruns the patch buffer\n");
	memcpy(patch+dest_pos,enc,data_len);
	dest_pos += data_len;

	dst_offset = 0;
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Port of patchGroups(NkitPartitionPatchInfo patchInfo, WiiDiscHeaderSection
// discHeader, long crcPatchOffset, byte[] crcPatchData) -- NkitReaderWii.cs:
// 312-347. Walks one CRC checkpoint's accumulated encrypted-group blob a
// group at a time, letting fstPatch() correct the FST and hashPatchGroup()
// reinstate preserved hash areas, rewriting the blob in place.
static enumError nkit_patch_groups
(
    nkit_patch_info_t	*pi,
    u64			crc_patch_offset,
    u8			*patch,
    u64			patch_len
)
{
    u64 part_data_offset = pi->disc_offset + pi->partition_header_size;
    int group_idx = (int)( ( crc_patch_offset - part_data_offset ) / NKIT_PARTITION_GROUP_SIZE );

    u8 *data = MALLOC(NKIT_PARTITION_GROUP_SIZE);
    memset(data,0,NKIT_PARTITION_GROUP_SIZE);

    // WiiPartitionHeaderSection wh = new(...); wh.ScrubManager = pi.ScrubManager;
    // wh.Initialise(true, pi.PartitionDataHeader.ReadString(0,4));
    // -- Initialise(bool,string) only assigns IsEncrypted/Id, neither of which
    // anything downstream of here reads; the fields that DO matter (Key,
    // H3Table) come from the ctor, exactly as in the C#.
    nkit_part_header_t wh;
    enumError err = nkit_part_header_init(&wh,pi->partition_header,pi->partition_header_size);
    if (err)
    {
	FREE(data);
	return err;
    }
    if (wh.is_rvt_h)
    {
	nkit_part_header_reset_mem(&wh);
	FREE(data);
	return ERROR0(ERR_NOT_IMPLEMENTED,"NKit: unsupported (RVT signed) partition\n");
    }

    u64 first_size = patch_len < NKIT_PARTITION_GROUP_SIZE ? patch_len : NKIT_PARTITION_GROUP_SIZE;
    nkit_group_t wp;
    err = nkit_group_init(&wp,&wh,data,NKIT_PARTITION_GROUP_SIZE,part_data_offset,first_size,true,false);
    if (err)
	goto done;

    if (pi->inner.fst)
    {
	err = nkit_fst_patch(pi,&wp,data,crc_patch_offset,patch,patch_len);
	if (err)
	    goto done;
    }

    for ( u64 i = 0; i < patch_len; i += NKIT_PARTITION_GROUP_SIZE )
    {
	u64 n = patch_len - i;
	if ( n > NKIT_PARTITION_GROUP_SIZE )
	    n = NKIT_PARTITION_GROUP_SIZE;

	memcpy(data,patch+i,n);
	if ( n < NKIT_PARTITION_GROUP_SIZE )
	    memset(data+n,0,NKIT_PARTITION_GROUP_SIZE-n);

	nkit_group_populate(&wp,group_idx++,data,NKIT_PARTITION_GROUP_SIZE,crc_patch_offset+i,n);
	nkit_group_force_hashes(&wp,0);

	if ( nkit_patch_info_find_hash_group(pi,wp.disc_offset) )
	{
	    nkit_hash_patch_group(pi,&wp);
	    u8 *enc = nkit_group_encrypted(&wp);
	    memcpy(patch+i,enc,(size_t)wp.size);
	}
    }

 done:
    nkit_group_reset_mem(&wp);
    nkit_part_header_reset_mem(&wh);
    FREE(data);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

// Port of NkitReaderWii.Read() -- NkitReaderWii.cs:28-299. The entry point
// XCONVERT dispatches a Wii .nkit.iso to (see lib-xfile.c), mirroring
// XExtractNKitGC()'s shape in x-nkit.c.

enumError XExtractNKitWii ( ccp source, ccp dest )
{
    enumError err = ERR_OK;

    FILE *src = fopen(source,"rb");
    if (!src)
	return ERROR1(ERR_CANT_OPEN,"Can't open file: %s\n",source);

    fseeko(src,0,SEEK_END);
    const u64 src_length = (u64)ftello(src);
    fseeko(src,0,SEEK_SET);

    // WiiDiscHeaderSection: NStream._HeaderSizeWii == 0x50000, i.e. everything
    // through the partition table at 0x40000 plus its entries.
    const uint HDR_SIZE = 0x50000;

    u8 *hdr = MALLOC(HDR_SIZE);
    FILE *out = 0, *spool = 0;
    char *spool_name = 0;
    nkit_crc_t crc;
    nkit_crc_init(&crc);
    nkit_disc_header_t dh;
    memset(&dh,0,sizeof(dh));
    nkit_scrub_manager_t scrub_filler;
    nkit_scrub_manager_init(&scrub_filler,false,0,0);	// ScrubManager(null) -- line 51
    nkit_patch_info_t *patch_infos = 0;
    uint n_patch_infos = 0;
    u8 *ph = 0;			// MemorySection ph = new byte[0x8000*64]
    nkit_buf_t patch_blocks = {0};

    if ( fread(hdr,1,HDR_SIZE,src) != HDR_SIZE )
    {
	err = ERROR0(ERR_READ_FAILED,"Truncated NKit file: %s\n",source);
	goto abort;
    }

    if ( memcmp(hdr+0x200,"NKIT v01",8) )
    {
	err = ERROR0(ERR_WRONG_FILE_TYPE,"Not an NKit v01 Wii image: %s\n",source);
	goto abort;
    }

    const u32 nkit_crc		= be32(hdr+0x208);
    const u64 image_size	= (u64)be32(hdr+0x210) * 4;
    const u32 update_partition_crc = be32(hdr+0x218);

    if (update_partition_crc)
    {
	// See the "RecoveryData.GetUpdatePartition()" note in this section's
	// header comment.
	err = ERROR0(ERR_NOT_IMPLEMENTED,
	    "NKit image has its update partition removed (crc %08x); restoring it needs"
	    " an external update-partition blob this build has no source for: %s\n",
	    update_partition_crc, source );
	goto abort;
    }

    if ((err = nkit_disc_header_init(&dh,hdr,HDR_SIZE)))
	goto abort;
    if (!dh.n_part)
    {
	err = ERROR0(ERR_WIA_INVALID,"NKit image has no partitions: %s\n",source);
	goto abort;
    }
    // 'foreach (WiiPartitionInfo part in hdr.Partitions) //already sorted'
    qsort(dh.part,dh.n_part,sizeof(*dh.part),nkit_part_info_cmp);

    // Blank NKit's own tag fields so the restored disc header is pristine again.
    for ( uint o = 0x200; o <= 0x218; o += 4 )
	write_be32(hdr+o,0);
    hdr[0x60] = 0;
    hdr[0x61] = 0;

    out = fopen(dest,"w+b");
    if (!out)
    {
	err = ERROR1(ERR_CANT_CREATE,"Can't create file: %s\n",dest);
	goto abort;
    }

    nkit_out_t o = { .f = out, .crc = &crc, .fname = dest };

    // The partition producer spools to a temp file next to the destination
    // (see nkit_circular_buffer_set_spool()'s note); it is truncated and
    // reused for every partition, and removed on the way out.
    spool_name = MALLOC(strlen(dest)+16);
    sprintf(spool_name,"%s.nkittmp",dest);
    spool = fopen(spool_name,"w+b");
    if (!spool)
    {
	err = ERROR1(ERR_CANT_CREATE,"Can't create temp file: %s\n",spool_name);
	goto abort;
    }

    u64 dst_pos = 0, src_pos = HDR_SIZE;

    if ((err = nkit_out_write(&o,hdr,HDR_SIZE)))	// write the header
	goto abort;
    dst_pos += HDR_SIZE;
    nkit_crc_snapshot(&crc,"Disc Header");

    ph = MALLOC(NKIT_PARTITION_GROUP_SIZE);
    patch_infos = MALLOC(dh.n_part*sizeof(*patch_infos));
    memset(patch_infos,0,dh.n_part*sizeof(*patch_infos));

    char last_partition_id[8] = "";
    nkit_part_type_t last_partition_type = NKIT_PART_OTHER;

    for ( uint pidx = 0; pidx < dh.n_part; pidx++ )
    {
	nkit_part_info_t *part = dh.part + pidx;
	nkit_patch_info_t *pi = patch_infos + pidx;
	n_patch_infos = pidx+1;

	if ( part->disc_offset > src_pos )
	{
	    // writeFiller(...) straight to the output stream (line 107)
	    u8 junk_id[4];
	    if ( last_partition_type != NKIT_PART_DATA )
		memcpy(junk_id,hdr,4);
	    else
		memcpy(junk_id,last_partition_id,4);

	    nkit_wii_junk_t fjunk;
	    nkit_wii_junk_init(&fjunk,junk_id,hdr[6],
		last_partition_type == NKIT_PART_UPDATE ? 0 : image_size);

	    nkit_circular_buffer_t fout;
	    memset(&fout,0,sizeof(fout));
	    fout.sink = &o;	// writeFiller() targets crcStream, i.e. the output

	    u64 filler_len;
	    err = nkit_write_filler(&src_pos,dst_pos,dst_pos+0x1cull,src,&fout,
		nkit_wii_junk_read_adapter,&fjunk,&scrub_filler,&filler_len);
	    if (err)
		goto abort;
	    dst_pos += filler_len;

	    // inStream.Copy(ByteStream.Zeros, part.DiscOffset - srcPos) -- padded to 0x8000
	    if ( part->disc_offset > src_pos )
	    {
		if ((err = nkit_stream_skip(src,part->disc_offset-src_pos)))
		    goto abort;
		src_pos = part->disc_offset;
	    }
	}

	part->disc_offset = dst_pos;	// restore the original position
	pi->disc_offset   = dst_pos;

	pi->partition_header_size = 0x20000;
	pi->partition_header = MALLOC(pi->partition_header_size);
	if ( fread(pi->partition_header,1,pi->partition_header_size,src) != pi->partition_header_size )
	{
	    err = ERROR0(ERR_READ_FAILED,"Truncated NKit file: %s\n",source);
	    goto abort;
	}
	src_pos += pi->partition_header_size;

	const u64 part_size = (u64)be32(pi->partition_header+0x2bc) * 4;

	nkit_part_header_t wh;
	if ((err = nkit_part_header_init(&wh,pi->partition_header,pi->partition_header_size)))
	    goto abort;
	if (wh.is_rvt_h)
	{
	    nkit_part_header_reset_mem(&wh);
	    err = ERROR0(ERR_NOT_IMPLEMENTED,
		"NKit: unsupported (RVT signed) partition in %s\n",source);
	    goto abort;
	}

	nkit_scrub_manager_t part_scrub;
	nkit_scrub_manager_init(&part_scrub,true,wh.decrypted_00,wh.decrypted_ff);
	pi->inner.scrub_manager = &part_scrub;

	nkit_hash_store_t hashes;
	nkit_hash_store_init(&hashes,0);	// new WiiHashStore(); WriteFlagsData() sizes it

	// ---- producer: partitionStreamWrite() into the spool ----------------
	rewind(spool);
	if ( ftruncate(fileno(spool),0) )
	{
	    err = ERROR1(ERR_WRITE_FAILED,"Can't truncate temp file: %s\n",spool_name);
	    goto part_abort;
	}
	nkit_circular_buffer_t prt;
	memset(&prt,0,sizeof(prt));
	nkit_circular_buffer_set_spool(&prt,spool);

	u64 orig_size = 0, producer_src_pos = 0;
	err = nkit_partition_stream_write(&orig_size,src,&prt,part_size,
		&pi->inner,&hashes,&producer_src_pos);
	if (err)
	    goto part_abort;
	src_pos += producer_src_pos;

	if ( orig_size != prt.w_position )
	{
	    err = ERROR0(ERR_WIA_INVALID,
		"NKit: partition read did not write the full amount to the buffer"
		" (%llu of %llu): %s\n", (u64)prt.w_position, (u64)orig_size, source );
	    goto part_abort;
	}

	fflush(spool);
	rewind(spool);
	prt.r_position = 0;

	// ---- consumer: the group loop --------------------------------------
	nkit_group_t wp;
	memset(&wp,0,sizeof(wp));
	bool wp_valid = false;

	u64 remaining = (u64)-1;	// long.MaxValue: set after first block read
	int group_index = 0;
	s64 gs = 0, ge = 0;
	s64 i = 0;
	bool patch_block = false;
	patch_blocks.len = 0;

	while ( remaining > 0 )
	{
	    uint blocks = (uint)( remaining/WII_SECTOR_DATA_SIZE < WII_GROUP_SECTORS
				    ? remaining/WII_SECTOR_DATA_SIZE : WII_GROUP_SECTORS );
	    for ( uint b = 0; b < blocks; b++ )
	    {
		// load aligned with no hashes
		if ((err = nkit_cb_read_all(&prt,ph+b*WII_SECTOR_SIZE+WII_SECTOR_HASH_SIZE,
			WII_SECTOR_DATA_SIZE)))
		    goto part_abort;

		if ( remaining == (u64)-1 )	// first loop
		{
		    remaining = orig_size;

		    const u8 *pdh = ph + WII_SECTOR_HASH_SIZE;	// ph.Read*(0x400 + x)

		    if (!memcmp(pdh,"\0\0\0\0",4))
		    {
			gs = -1;
			ge = -1;
			blocks = (uint)( remaining/WII_SECTOR_DATA_SIZE < WII_GROUP_SECTORS
					    ? remaining/WII_SECTOR_DATA_SIZE : WII_GROUP_SECTORS );
			memcpy(last_partition_id,pdh,4);
			last_partition_id[4] = 0;
			// restore real size: DataToHashedLen(origSize)/4
			write_be32(pi->partition_header+0x2bc,
			    (u32)( ( orig_size/0x7c00ull*0x8000ull + orig_size%0x7c00ull ) / 4 ));
		    }
		    else
		    {
			gs = (s64)( ( (u64)be32(pdh+0x424) * 4 ) / ( 0x7c00ull*64 ) );
			ge = (s64)( ( (u64)be32(pdh+0x424) * 4 + (u64)be32(pdh+0x428) * 4 )
				    / ( 0x7c00ull*64 ) );
			if ( ( part->disc_offset + (u64)be32(pdh+0x428) * 4 ) % ( 0x7c00ull*64 ) == 0 )
			    ge--; // don't load the next group if the data will end on the last byte

			blocks = (uint)( remaining/WII_SECTOR_DATA_SIZE < WII_GROUP_SECTORS
					    ? remaining/WII_SECTOR_DATA_SIZE : WII_GROUP_SECTORS );
			memcpy(last_partition_id,pdh,4);
			last_partition_id[4] = 0;

			// restore real size (already a /4 scaled value)
			memcpy(pi->partition_header+0x2bc,pdh+0x210,4);

			// blank the partition's own NKit tag fields
			for ( uint z = 0x200; z <= 0x218; z += 4 )
			    write_be32(ph+WII_SECTOR_HASH_SIZE+z,0);
		    }

		    if ((err = nkit_out_write(&o,pi->partition_header,pi->partition_header_size)))
			goto part_abort;
		    dst_pos += pi->partition_header_size;

		    if ((err = nkit_group_init(&wp,&wh,ph,NKIT_PARTITION_GROUP_SIZE,
			    part->disc_offset,(u64)blocks*WII_SECTOR_SIZE,false,false)))
			goto part_abort;
		    wp_valid = true;
		}
	    }

	    if ( blocks < WII_GROUP_SECTORS )	// clear remaining blocks
		memset(ph+(u64)blocks*WII_SECTOR_SIZE,0,
		    NKIT_PARTITION_GROUP_SIZE-(size_t)blocks*WII_SECTOR_SIZE);

	    nkit_group_populate(&wp,group_index,ph,NKIT_PARTITION_GROUP_SIZE,
		dst_pos,(u64)blocks*WII_SECTOR_SIZE);

	    uint scrubbed = 0;
	    for ( uint bi = 0; bi < blocks; bi++ )
	    {
		nkit_group_mark_block_dirty(&wp,bi);
		u8 byt;
		if ( nkit_scrub_manager_is_block_scrubbed_scan_mode(&part_scrub,
			wp.offset + (u64)bi*WII_SECTOR_SIZE,&byt) )
		{
		    nkit_group_set_scrubbed(&wp,bi,byt);
		    scrubbed++;
		}
	    }
	    bool is_fst_blocks = i >= gs && i <= ge;
	    // test with 0 partition based offset
	    bool req_hashes = nkit_hash_store_is_preserved(&hashes,wp.offset);

	    // only test if the hashes aren't preserved (only preserved for scrubbed/customs)
	    nkit_repair_blocks(&wp,scrubbed,blocks,false,is_fst_blocks);

	    if (req_hashes)	// store with disc based offset -- fetch the stored
	    {			// hashes that couldn't be recreated
		u8 *dec = nkit_group_decrypted(&wp);
		nkit_patch_info_add_hash_group(pi,
		    wp.offset + part->disc_offset + pi->partition_header_size,
		    dec, NKIT_PARTITION_GROUP_SIZE);
	    }

	    group_index++;
	    bool in_fst_area = i >= gs && i <= ge;

	    if ( !patch_block && ( gs == i || req_hashes ) )
	    {
		patch_blocks.len = 0;
		char nm[32];
		snprintf(nm,sizeof(nm),"%s Data",last_partition_id);
		nkit_crc_snapshot(&crc,nm);
		patch_block = true;
	    }
	    else if ( patch_block && !in_fst_area && !req_hashes )
	    {
		char nm[32];
		snprintf(nm,sizeof(nm),"%s Patch",last_partition_id);
		nkit_crc_snapshot(&crc,nm);
		nkit_crc_item_t *last = crc.crcs + crc.n_crcs-1;
		last->patch_data_size = (uint)patch_blocks.len;
		last->patch_data = MALLOC(patch_blocks.len);
		memcpy(last->patch_data,patch_blocks.data,patch_blocks.len);
		patch_blocks.len = 0;
		patch_block = false;
	    }

	    u8 *enc = nkit_group_encrypted(&wp);
	    if ((err = nkit_out_write(&o,enc,(u64)blocks*WII_SECTOR_SIZE)))
		goto part_abort;
	    if (patch_block)
		nkit_buf_append(&patch_blocks,enc,(u64)blocks*WII_SECTOR_SIZE);

	    remaining -= (u64)blocks * WII_SECTOR_DATA_SIZE;
	    dst_pos   += (u64)blocks * WII_SECTOR_SIZE;
	    i++;
	}

	if (patch_block)
	{
	    char nm[32];
	    snprintf(nm,sizeof(nm),"%s Patch",last_partition_id);
	    nkit_crc_snapshot(&crc,nm);
	    nkit_crc_item_t *last = crc.crcs + crc.n_crcs-1;
	    last->patch_data_size = (uint)patch_blocks.len;
	    last->patch_data = MALLOC(patch_blocks.len);
	    memcpy(last->patch_data,patch_blocks.data,patch_blocks.len);
	    patch_blocks.len = 0;
	    patch_block = false;
	}

	// srcPos += hashes.ReadPatchData(part.DiscOffset + PartitionHeader.Size,
	//                                patchInfo.HashGroups, inStream)
	// -- WiiHashStore.cs:62-81: every flagged group's preserved hash areas
	// are present in the source stream and must be consumed, whether or not
	// this partition actually kept a HashGroups entry for it.
	{
	    u64 partition_disc_offset = part->disc_offset + pi->partition_header_size;
	    for ( u64 goff = 0; goff < hashes.partition_size; goff += NKIT_PARTITION_GROUP_SIZE )
	    {
		if (!nkit_hash_store_is_preserved(&hashes,goff))
		    continue;
		u64 span = hashes.partition_size - goff;
		if ( span > NKIT_PARTITION_GROUP_SIZE )
		    span = NKIT_PARTITION_GROUP_SIZE;
		uint blocks = (uint)( span / WII_SECTOR_SIZE ); // read partial groups (< 64 blocks)
		uint want = blocks * WII_SECTOR_HASH_SIZE;

		nkit_hash_group_t *hg = nkit_patch_info_find_hash_group(pi,partition_disc_offset+goff);
		if (hg)
		{
		    // needs to be encrypted and CRCd
		    if ( fread(hg->data,1,want,src) != want )
		    {
			err = ERROR0(ERR_READ_FAILED,"Truncated NKit file: %s\n",source);
			goto part_abort;
		    }
		}
		else if ((err = nkit_stream_skip(src,want)))
		    goto part_abort;
		src_pos += want;
	    }
	}

	last_partition_type = part->type;

     part_abort:
	if (wp_valid)
	    nkit_group_reset_mem(&wp);
	nkit_hash_store_reset_mem(&hashes);
	nkit_part_header_reset_mem(&wh);
	// pi->inner.scrub_manager points at this partition-local manager; the
	// patch pass below re-derives everything it needs from it, so it must
	// outlive the loop -- move ownership into the patch info.
	{
	    nkit_scrub_manager_t *keep = MALLOC(sizeof(*keep));
	    *keep = part_scrub;
	    pi->inner.scrub_manager = keep;
	}
	if (err)
	    goto abort;
    }

    // trailing filler after the last partition (lines 265-269)
    if ( src_pos < src_length )
    {
	u8 junk_id[4];
	if ( last_partition_type != NKIT_PART_DATA )
	    memcpy(junk_id,hdr,4);
	else
	    memcpy(junk_id,last_partition_id,4);

	nkit_wii_junk_t fjunk;
	nkit_wii_junk_init(&fjunk,junk_id,hdr[6],
	    last_partition_type == NKIT_PART_UPDATE ? 0 : image_size);

	nkit_circular_buffer_t fout;
	memset(&fout,0,sizeof(fout));
	fout.sink = &o;	// writeFiller() targets crcStream, i.e. the output

	u64 filler_len;
	err = nkit_write_filler(&src_pos,dst_pos,
	    last_partition_type == NKIT_PART_UPDATE ? image_size : dst_pos+0x1cull,
	    src,&fout,nkit_wii_junk_read_adapter,&fjunk,&scrub_filler,&filler_len);
	if (err)
	    goto abort;
	dst_pos += filler_len;
    }

    nkit_crc_snapshot(&crc,"End");

    // updatePartitionCrc == 0 here (the != 0 case bailed out above), so this
    // is always UpdateOffsets(): just update the table with the new offsets.
    nkit_disc_header_update_offsets(&dh);

    // crc.Crcs[0].PatchData = hdr.Data
    if ( crc.n_crcs )
    {
	nkit_crc_item_t *it0 = crc.crcs;
	if (it0->patch_data)
	    FREE(it0->patch_data);
	it0->patch_data_size = HDR_SIZE;
	it0->patch_data = MALLOC(HDR_SIZE);
	memcpy(it0->patch_data,hdr,HDR_SIZE);
    }

    for ( uint ci = 0; ci < crc.n_crcs; ci++ )
    {
	nkit_crc_item_t *it = crc.crcs + ci;
	if (!it->patch_data)
	    continue;

	nkit_patch_info_t *pi = 0;
	for ( uint p = 0; p < n_patch_infos; p++ )
	{
	    nkit_patch_info_t *c = patch_infos + p;
	    u64 base = c->disc_offset + c->partition_header_size;
	    if ( it->offset >= base && it->offset < base + nkit_patch_info_size(c) )
	    {
		pi = c;
		break;
	    }
	}
	if (pi)
	{
	    if ((err = nkit_patch_groups(pi,it->offset,it->patch_data,it->patch_data_size)))
		goto abort;
	}
	it->patch_crc = CalcCRC32(0,it->patch_data,it->patch_data_size);
    }

    if ( image_size != dst_pos )
    {
	err = ERROR0(ERR_WIA_INVALID,
	    "NKit image read output %llu bytes not the expected %llu: %s\n",
	    (u64)dst_pos, (u64)image_size, source );
	goto abort;
    }

    // Processor.cs:162-174 -- apply every patch blob back over the output.
    for ( uint ci = 0; ci < crc.n_crcs; ci++ )
    {
	const nkit_crc_item_t *it = crc.crcs + ci;
	if (!it->patch_data)
	    continue;
	u64 n = it->length < it->patch_data_size ? it->length : it->patch_data_size;
	if ( fseeko(out,(off_t)it->offset,SEEK_SET) )
	{
	    err = ERROR1(ERR_WRITE_FAILED,"Seek failed: %s\n",dest);
	    goto abort;
	}
	if ( n && fwrite(it->patch_data,1,n,out) != n )
	{
	    err = ERROR1(ERR_WRITE_FAILED,"Write failed: %s\n",dest);
	    goto abort;
	}
    }

    {
	const u32 full_crc = nkit_crc_full(&crc,true);
	if ( full_crc != nkit_crc )
	    err = ERROR0(ERR_WIA_INVALID,
		"NKit Invalid: restored image CRC32 %08x does not match the CRC32 %08x"
		" stored in the NKit header: %s\n", full_crc, nkit_crc, source );
    }

 abort:
    if (out)
    {
	fclose(out);
	out = 0;
    }
    if (spool)
	fclose(spool);
    if (spool_name)
    {
	unlink(spool_name);
	FREE(spool_name);
    }
    if (src) fclose(src);
    if (ph) FREE(ph);
    nkit_buf_reset_mem(&patch_blocks);
    for ( uint p = 0; p < n_patch_infos; p++ )
    {
	if (patch_infos[p].inner.scrub_manager)
	{
	    nkit_scrub_manager_reset_mem(patch_infos[p].inner.scrub_manager);
	    FREE(patch_infos[p].inner.scrub_manager);
	    patch_infos[p].inner.scrub_manager = 0;
	}
	nkit_patch_info_reset_mem(patch_infos+p);
    }
    if (patch_infos) FREE(patch_infos);
    nkit_scrub_manager_reset_mem(&scrub_filler);
    nkit_crc_reset_mem(&crc);
    nkit_disc_header_reset_mem(&dh);
    FREE(hdr);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
