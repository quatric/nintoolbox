
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
 *   This file is part of the WIT project.                                 *
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

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "x-formats.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    Nintendo DS cartridges		///////////////
///////////////////////////////////////////////////////////////////////////////

// A DS cartridge dump is a header, two or four CPU binaries, an optional
// banner, and a file system made of three pieces: FNT (the directory tree and
// all names), FAT (start/end offset of every file) and the file data itself.
// File ids are global and the FAT is indexed by them, so the tree and the
// data are only connected through those ids.

#define NDS_HEAD_SIZE		0x200	// the part every cartridge has
#define NDS_DSI_HEAD_SIZE	0x4000	// DSi carts extend it
#define NDS_ALIGN		0x200	// everything is sector aligned
#define NDS_ARM9_FOOTER_MAGIC	0xdec00621

// Offsets inside the header.  Everything is little endian.
#define NDS_OFF_TITLE		0x000
#define NDS_OFF_GAMECODE	0x00c
#define NDS_OFF_MAKERCODE	0x010
#define NDS_OFF_UNITCODE	0x012
#define NDS_OFF_ARM9_ROM	0x020
#define NDS_OFF_ARM9_SIZE	0x02c
#define NDS_OFF_ARM7_ROM	0x030
#define NDS_OFF_ARM7_SIZE	0x03c
#define NDS_OFF_FNT_ROM		0x040
#define NDS_OFF_FNT_SIZE	0x044
#define NDS_OFF_FAT_ROM		0x048
#define NDS_OFF_FAT_SIZE	0x04c
#define NDS_OFF_OVL9_ROM	0x050
#define NDS_OFF_OVL9_SIZE	0x054
#define NDS_OFF_OVL7_ROM	0x058
#define NDS_OFF_OVL7_SIZE	0x05c
#define NDS_OFF_BANNER_ROM	0x068
#define NDS_OFF_ROM_SIZE	0x080
#define NDS_OFF_HEAD_SIZE	0x084
#define NDS_OFF_LOGO_CRC	0x15c
#define NDS_OFF_HEAD_CRC	0x15e

// Names used inside an extracted directory.
#define NDS_FN_HEADER		"header.bin"
#define NDS_FN_ARM9		"arm9.bin"
#define NDS_FN_ARM9_FOOTER	"arm9_footer.bin"
#define NDS_FN_ARM7		"arm7.bin"
#define NDS_FN_OVL9		"arm9_overlay.bin"
#define NDS_FN_OVL7		"arm7_overlay.bin"
#define NDS_FN_BANNER		"banner.bin"
#define NDS_DIR_DATA		"data"
#define NDS_DIR_OVERLAY		"overlay"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     CRC16			///////////////
///////////////////////////////////////////////////////////////////////////////

// The DS BIOS checks the header with a plain CRC-16/ARC (poly 0xa001,
// init 0xffff).  It is used twice: over the 156 byte Nintendo logo, where the
// result is the constant 0xcf56 that identifies the format, and over the
// first 0x15e header bytes.

static u16 nds_crc16 ( const void *data, uint size )
{
    const u8 *d = data;
    u16 crc = 0xffff;
    while ( size-- )
    {
	crc ^= *d++;
	for ( int i = 0; i < 8; i++ )
	    crc = crc & 1 ? crc >> 1 ^ 0xa001 : crc >> 1;
    }
    return crc;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			  small helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static u8 * load_file ( ccp fname, u64 *size, bool silent )
{
    FILE *f = fopen(fname,"rb");
    if (!f)
    {
	if (!silent)
	    ERROR1(ERR_CANT_OPEN,"Can't open file: %s\n",fname);
	return 0;
    }

    struct stat st;
    if (fstat(fileno(f),&st))
    {
	fclose(f);
	if (!silent)
	    ERROR1(ERR_READ_FAILED,"Can't stat file: %s\n",fname);
	return 0;
    }

    u8 *data = MALLOC((size_t)st.st_size+1);
    if ( st.st_size && fread(data,1,st.st_size,f) != (size_t)st.st_size )
    {
	FREE(data);
	fclose(f);
	if (!silent)
	    ERROR1(ERR_READ_FAILED,"Can't read file: %s\n",fname);
	return 0;
    }
    fclose(f);
    data[st.st_size] = 0;
    if (size)
	*size = st.st_size;
    return data;
}

///////////////////////////////////////////////////////////////////////////////

static enumError save_file ( ccp fname, const void *data, size_t size )
{
    enumError err = CreatePath(fname,false);
    if (err)
	return err;

    FILE *f = fopen(fname,"wb");
    if (!f)
	return ERROR1(ERR_CANT_CREATE,"Can't create file: %s\n",fname);
    const bool ok = !size || fwrite(data,1,size,f) == size;
    fclose(f);
    return ok ? ERR_OK : ERROR1(ERR_WRITE_FAILED,"Can't write file: %s\n",fname);
}

///////////////////////////////////////////////////////////////////////////////

// Copy 'size' bytes at 'off' out of the loaded image into a new file.
// Ranges that leave the image are an error rather than a short file.

static enumError extract_range
	( ccp fname, const u8 *image, u64 image_size, u32 off, u32 size )
{
    if ( !off || !size )
	return ERR_OK;
    if ( (u64)off + size > image_size )
	return ERROR0(ERR_INVALID_FILE,
		"Range 0x%x+0x%x is outside the image: %s\n",off,size,fname);
    return save_file(fname,image+off,size);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     XINFO			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XInfoNDS ( ccp source )
{
    u64 size = 0;
    u8 *image = load_file(source,&size,false);
    if (!image)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    if ( size < NDS_HEAD_SIZE )
	err = ERROR0(ERR_INVALID_FILE,"File is too short: %s\n",source);
    else
    {
	char title[13];
	memcpy(title,image+NDS_OFF_TITLE,12);
	title[12] = 0;

	printf("      title:    %s\n",title);
	printf("      gamecode: %.4s   maker: %.2s   unit: 0x%02x\n",
		image+NDS_OFF_GAMECODE, image+NDS_OFF_MAKERCODE,
		image[NDS_OFF_UNITCODE] );
	printf("      arm9:     0x%06x + 0x%06x\n",
		le32(image+NDS_OFF_ARM9_ROM), le32(image+NDS_OFF_ARM9_SIZE));
	printf("      arm7:     0x%06x + 0x%06x\n",
		le32(image+NDS_OFF_ARM7_ROM), le32(image+NDS_OFF_ARM7_SIZE));
	printf("      fnt:      0x%06x + 0x%06x\n",
		le32(image+NDS_OFF_FNT_ROM), le32(image+NDS_OFF_FNT_SIZE));
	printf("      fat:      0x%06x + 0x%06x  (%u files)\n",
		le32(image+NDS_OFF_FAT_ROM), le32(image+NDS_OFF_FAT_SIZE),
		le32(image+NDS_OFF_FAT_SIZE)/8 );
	printf("      used:     %u of %llu bytes\n",
		le32(image+NDS_OFF_ROM_SIZE), size );

	const u16 crc = nds_crc16(image,NDS_OFF_HEAD_CRC);
	const u16 stored = le16(image+NDS_OFF_HEAD_CRC);
	printf("      head crc: 0x%04x (%s)\n",
		stored, crc == stored ? "ok" : "MISMATCH" );
    }

    FREE(image);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XEXTRACT			///////////////
///////////////////////////////////////////////////////////////////////////////

// Walk one FNT subtable and write out every file it names.  Directory
// entries recurse; 'file_id' tracks the next file id of the *current*
// directory, which is where the FAT lookup comes from.

static enumError extract_dir
(
    ccp		dest,		// base directory of the extraction
    ccp		path,		// path of this directory, relative to 'dest'
    const u8	*image,		// the whole image
    u64		image_size,	// its size
    u32		fnt_off,	// offset of the FNT
    u32		fnt_size,	// size of the FNT
    u32		fat_off,	// offset of the FAT
    u32		n_files,	// number of FAT entries
    uint	dir_id,		// directory to walk, 0xf000 based
    uint	depth		// recursion guard
)
{
    if ( depth > 64 )
	return ERROR0(ERR_INVALID_FILE,"FNT nested too deeply\n");

    const uint idx = dir_id & 0xfff;
    if ( (u64)idx*8 + 8 > fnt_size )
	return ERROR0(ERR_INVALID_FILE,"FNT entry %u is out of range\n",idx);

    const u8 *entry = image + fnt_off + idx*8;
    u32 sub = le32(entry);
    u16 file_id = le16(entry+4);

    char buf[PATH_MAX];
    enumError err = ERR_OK;

    while ( !err )
    {
	if ( sub >= fnt_size )
	    return ERROR0(ERR_INVALID_FILE,"FNT subtable is out of range\n");

	const u8 type = image[fnt_off+sub++];
	if (!type)
	    break;					// end of this directory

	const uint name_len = type & 0x7f;
	if ( !name_len || sub + name_len > fnt_size )
	    return ERROR0(ERR_INVALID_FILE,"FNT name is out of range\n");

	char name[128];
	memcpy(name,image+fnt_off+sub,name_len);
	name[name_len] = 0;
	sub += name_len;

	// A name that escapes the destination directory would be a way to
	// write anywhere on disk, so reject it instead of sanitizing it.
	if ( !strcmp(name,".") || !strcmp(name,"..") || strchr(name,'/') )
	    return ERROR0(ERR_INVALID_FILE,"Unsafe FNT name: %s\n",name);

	char rel[PATH_MAX];
	snprintf(rel,sizeof(rel),"%s/%s",path,name);

	if ( type & 0x80 )
	{
	    // Sub directory: the id follows the name.
	    if ( sub + 2 > fnt_size )
		return ERROR0(ERR_INVALID_FILE,"FNT dir id is out of range\n");
	    const u16 sub_id = le16(image+fnt_off+sub);
	    sub += 2;

	    PathCatPP(buf,sizeof(buf),dest,rel);
	    err = CreatePath(buf,true);
	    if (!err)
		err = extract_dir(dest,rel,image,image_size,fnt_off,fnt_size,
				fat_off,n_files,sub_id,depth+1);
	}
	else
	{
	    if ( file_id >= n_files )
		return ERROR0(ERR_INVALID_FILE,
			"FNT references file %u, but the FAT has only %u\n",
			file_id, n_files );

	    const u8 *fat = image + fat_off + (u32)file_id*8;
	    const u32 beg = le32(fat), end = le32(fat+4);
	    if ( end < beg || end > image_size )
		return ERROR0(ERR_INVALID_FILE,
			"FAT entry %u is invalid (0x%x..0x%x)\n",file_id,beg,end);

	    PathCatPP(buf,sizeof(buf),dest,rel);
	    err = save_file(buf,image+beg,end-beg);
	    file_id++;
	}
    }
    return err;
}

///////////////////////////////////////////////////////////////////////////////

// Overlays are ordinary files as far as the FAT is concerned: the overlay
// table just gives their file ids.  They are written to their own directory
// because they have no names in the FNT.

static enumError extract_overlays
(
    ccp		dest,		// base directory
    ccp		prefix,		// "arm9" or "arm7"
    const u8	*image,		// the whole image
    u64		image_size,	// its size
    u32		ovl_off,	// offset of the overlay table
    u32		ovl_size,	// its size
    u32		fat_off,	// offset of the FAT
    u32		n_files		// number of FAT entries
)
{
    char buf[PATH_MAX];
    for ( u32 pos = 0; pos + 32 <= ovl_size; pos += 32 )
    {
	const u8 *ovl = image + ovl_off + pos;
	const u32 id  = le32(ovl+24);		// file id of this overlay
	if ( id >= n_files )
	    return ERROR0(ERR_INVALID_FILE,
		"Overlay references file %u, but the FAT has only %u\n",id,n_files);

	const u8 *fat = image + fat_off + id*8;
	const u32 beg = le32(fat), end = le32(fat+4);
	if ( end < beg || end > image_size )
	    return ERROR0(ERR_INVALID_FILE,
		"FAT entry %u is invalid (0x%x..0x%x)\n",id,beg,end);

	char name[64];
	snprintf(name,sizeof(name),"%s/%s_%04u.bin",NDS_DIR_OVERLAY,prefix,
			(uint)( pos / 32 ));
	PathCatPP(buf,sizeof(buf),dest,name);
	const enumError err = save_file(buf,image+beg,end-beg);
	if (err)
	    return err;
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError XExtractNDS ( ccp source, ccp dest )
{
    u64 size = 0;
    u8 *image = load_file(source,&size,false);
    if (!image)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    char buf[PATH_MAX];

    if ( size < NDS_HEAD_SIZE )
    {
	err = ERROR0(ERR_INVALID_FILE,"File is too short: %s\n",source);
	goto abort;
    }

    err = CreatePath(dest,true);
    if (err)
	goto abort;

    //--- header

    {
	u32 head_size = le32(image+NDS_OFF_HEAD_SIZE);
	if ( head_size != NDS_HEAD_SIZE && head_size != NDS_DSI_HEAD_SIZE )
	    head_size = NDS_HEAD_SIZE;
	if ( head_size > size )
	    head_size = NDS_HEAD_SIZE;

	PathCatPP(buf,sizeof(buf),dest,NDS_FN_HEADER);
	err = save_file(buf,image,head_size);
	if (err)
	    goto abort;
    }

    //--- CPU binaries

    const u32 arm9_off = le32(image+NDS_OFF_ARM9_ROM);
    const u32 arm9_size = le32(image+NDS_OFF_ARM9_SIZE);
    PathCatPP(buf,sizeof(buf),dest,NDS_FN_ARM9);
    err = extract_range(buf,image,size,arm9_off,arm9_size);
    if (err)
	goto abort;

    // Most carts store a 12 byte "nitro footer" right behind arm9; it is not
    // covered by arm9_size, so it has to be carried separately.
    if ( (u64)arm9_off + arm9_size + 12 <= size
	&& le32(image+arm9_off+arm9_size) == NDS_ARM9_FOOTER_MAGIC )
    {
	PathCatPP(buf,sizeof(buf),dest,NDS_FN_ARM9_FOOTER);
	err = save_file(buf,image+arm9_off+arm9_size,12);
	if (err)
	    goto abort;
    }

    PathCatPP(buf,sizeof(buf),dest,NDS_FN_ARM7);
    err = extract_range(buf,image,size,
		le32(image+NDS_OFF_ARM7_ROM), le32(image+NDS_OFF_ARM7_SIZE));
    if (err)
	goto abort;

    //--- overlay tables and banner

    PathCatPP(buf,sizeof(buf),dest,NDS_FN_OVL9);
    err = extract_range(buf,image,size,
		le32(image+NDS_OFF_OVL9_ROM), le32(image+NDS_OFF_OVL9_SIZE));
    if (err)
	goto abort;

    PathCatPP(buf,sizeof(buf),dest,NDS_FN_OVL7);
    err = extract_range(buf,image,size,
		le32(image+NDS_OFF_OVL7_ROM), le32(image+NDS_OFF_OVL7_SIZE));
    if (err)
	goto abort;

    {
	// The banner has no size field; 0xa00 covers every known version.
	const u32 banner = le32(image+NDS_OFF_BANNER_ROM);
	if ( banner && (u64)banner + 0xa00 <= size )
	{
	    PathCatPP(buf,sizeof(buf),dest,NDS_FN_BANNER);
	    err = save_file(buf,image+banner,0xa00);
	    if (err)
		goto abort;
	}
    }

    //--- file system

    // Keep the XEXTRACT layout valid even for cartridges with an empty FAT.
    // XCREATE always scans data/ as the file-system root.
    PathCatPP(buf,sizeof(buf),dest,NDS_DIR_DATA);
    err = CreatePath(buf,true);
    if (err)
	goto abort;

    {
	const u32 fnt_off  = le32(image+NDS_OFF_FNT_ROM);
	const u32 fnt_size = le32(image+NDS_OFF_FNT_SIZE);
	const u32 fat_off  = le32(image+NDS_OFF_FAT_ROM);
	const u32 fat_size = le32(image+NDS_OFF_FAT_SIZE);
	const u32 n_files  = fat_size / 8;

	if ( fnt_off && fnt_size && fat_off && n_files )
	{
	    if ( (u64)fnt_off + fnt_size > size
	      || (u64)fat_off + fat_size > size )
	    {
		err = ERROR0(ERR_INVALID_FILE,
			"FNT or FAT is outside the image: %s\n",source);
		goto abort;
	    }

	    err = extract_overlays(dest,"arm9",image,size,
			le32(image+NDS_OFF_OVL9_ROM),
			le32(image+NDS_OFF_OVL9_SIZE), fat_off, n_files );
	    if (!err)
		err = extract_overlays(dest,"arm7",image,size,
			le32(image+NDS_OFF_OVL7_ROM),
			le32(image+NDS_OFF_OVL7_SIZE), fat_off, n_files );
	    if (err)
		goto abort;

	    err = extract_dir(dest,NDS_DIR_DATA,image,size,
		    fnt_off,fnt_size,fat_off,n_files,0xf000,0);
	    if (err)
		goto abort;
	}
    }

    if ( verbose >= 0 )
	printf("  extracted %s -> %s\n",source,dest);

 abort:
    FREE(image);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    XCREATE			///////////////
///////////////////////////////////////////////////////////////////////////////

// Building an image means assigning file ids first -- the FNT and the FAT
// both depend on the final numbering -- and only then laying out the data.
// The numbering rule is the one the DS itself relies on: all files of a
// directory get consecutive ids, and directories are numbered breadth first
// starting at the root.

typedef struct nds_file_t
{
    char	*path;		// full path on disk, alloced
    char	*name;		// name inside the cartridge, points into 'path'
    u32		id;		// file id
    u32		off;		// offset in the image, filled in during layout
    u32		size;		// file size
}
nds_file_t;

typedef struct nds_dir_t
{
    char	*path;		// full path on disk, alloced
    char	*name;		// name inside the cartridge, points into 'path'
    uint	id;		// directory id, 0xf000 based
    uint	parent;		// id of the parent directory
    uint	first_file;	// id of the first file of this directory
    uint	n_files;	// number of files directly in this directory
    uint	*sub;		// alloced list of child indices into 'dir'
    uint	n_subs;		// number of sub directories
}
nds_dir_t;

typedef struct nds_fs_t
{
    nds_dir_t	*dir;		// alloced list of directories, [0] is the root
    uint	n_dirs;
    uint	dirs_alloced;

    nds_file_t	*file;		// alloced list of files, ordered by file id
    uint	n_files;
    uint	files_alloced;
}
nds_fs_t;

///////////////////////////////////////////////////////////////////////////////

static void reset_fs ( nds_fs_t *fs )
{
    for ( uint i = 0; i < fs->n_dirs; i++ )
    {
	FREE(fs->dir[i].path);
	FREE(fs->dir[i].sub);
    }
    for ( uint i = 0; i < fs->n_files; i++ )
	FREE(fs->file[i].path);
    FREE(fs->dir);
    FREE(fs->file);
    memset(fs,0,sizeof(*fs));
}

///////////////////////////////////////////////////////////////////////////////

// Entries are ordered case insensitively, which is what the original build
// tools did: it is the only rule under which retail name orders come out
// right, because it sorts '_' (0x5f) before any lower case letter.

static int cmp_name ( const void *a, const void *b )
{
    ccp n1 = *(ccp*)a, n2 = *(ccp*)b;
    const int stat = strcasecmp(n1,n2);
    return stat ? stat : strcmp(n1,n2);
}

// Read one directory and append its entries to 'fs'.
//
// Directories are numbered depth first: a sub directory gets the next free id
// the moment the walk descends into it, so its whole subtree is numbered
// before the next sibling.  That is the order retail cartridges use, and the
// file ids follow the same walk.

static enumError scan_dir ( nds_fs_t *fs, uint dir_index, uint depth )
{
    if ( depth > 64 )
	return ERROR0(ERR_INVALID_FILE,"Directories nested too deeply\n");

    DIR *d = opendir(fs->dir[dir_index].path);
    if (!d)
	return ERROR1(ERR_CANT_OPEN,"Can't read directory: %s\n",
			fs->dir[dir_index].path);

    // Collect the names first so the result does not depend on the order the
    // file system happens to return them in.
    ccp *names = 0;
    uint n_names = 0, alloced = 0;
    struct dirent *de;
    while ( ( de = readdir(d) ) )
    {
	if ( *de->d_name == '.' )
	    continue;
	if ( n_names == alloced )
	{
	    alloced = alloced ? 2*alloced : 32;
	    names = REALLOC(names,alloced*sizeof(*names));
	}
	names[n_names++] = STRDUP(de->d_name);
    }
    closedir(d);
    qsort(names,n_names,sizeof(*names),cmp_name);

    enumError err = ERR_OK;
    char buf[PATH_MAX];

    // Pass 1: files, so that they get consecutive ids.
    fs->dir[dir_index].first_file = fs->n_files;
    for ( uint i = 0; i < n_names && !err; i++ )
    {
	PathCatPP(buf,sizeof(buf),fs->dir[dir_index].path,names[i]);
	struct stat st;
	if ( stat(buf,&st) || !S_ISREG(st.st_mode) )
	    continue;
	if ( st.st_size > 0xffffffffull )
	{
	    err = ERROR0(ERR_INVALID_FILE,"File is too large: %s\n",buf);
	    break;
	}

	if ( fs->n_files == fs->files_alloced )
	{
	    fs->files_alloced = fs->files_alloced ? 2*fs->files_alloced : 256;
	    fs->file = REALLOC(fs->file,fs->files_alloced*sizeof(*fs->file));
	}
	nds_file_t *f = fs->file + fs->n_files;
	memset(f,0,sizeof(*f));
	f->path = STRDUP(buf);
	f->name = f->path + strlen(f->path) - strlen(names[i]);
	f->id	= fs->n_files;
	f->size = (u32)st.st_size;
	fs->n_files++;
	fs->dir[dir_index].n_files++;
    }

    // Pass 2: sub directories, each fully processed before the next one.
    // 'fs->dir' is reallocated below, so nothing may cache a pointer into it.
    for ( uint i = 0; i < n_names && !err; i++ )
    {
	PathCatPP(buf,sizeof(buf),fs->dir[dir_index].path,names[i]);
	struct stat st;
	if ( stat(buf,&st) || !S_ISDIR(st.st_mode) )
	    continue;

	if ( fs->n_dirs >= 0x1000 )
	{
	    err = ERROR0(ERR_INVALID_FILE,
		    "Too many directories, the FNT allows 4096\n");
	    break;
	}
	if ( fs->n_dirs == fs->dirs_alloced )
	{
	    fs->dirs_alloced = fs->dirs_alloced ? 2*fs->dirs_alloced : 64;
	    fs->dir = REALLOC(fs->dir,fs->dirs_alloced*sizeof(*fs->dir));
	}

	const uint sub_index = fs->n_dirs++;
	nds_dir_t *sub = fs->dir + sub_index;
	memset(sub,0,sizeof(*sub));
	sub->path   = STRDUP(buf);
	sub->name   = sub->path + strlen(sub->path) - strlen(names[i]);
	sub->id	    = 0xf000 + sub_index;
	sub->parent = fs->dir[dir_index].id;

	nds_dir_t *parent = fs->dir + dir_index;
	parent->sub = REALLOC(parent->sub,(parent->n_subs+1)*sizeof(*parent->sub));
	parent->sub[parent->n_subs++] = sub_index;

	err = scan_dir(fs,sub_index,depth+1);
    }

    for ( uint i = 0; i < n_names; i++ )
	FREE((char*)names[i]);
    FREE(names);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

// A growing byte buffer; the FNT is built by appending to one of these.

typedef struct nds_buf_t
{
    u8		*data;
    uint	size;
    uint	alloced;
}
nds_buf_t;

static void buf_need ( nds_buf_t *b, uint add )
{
    if ( b->size + add > b->alloced )
    {
	b->alloced = b->alloced ? 2*b->alloced : 4096;
	while ( b->size + add > b->alloced )
	    b->alloced *= 2;
	b->data = REALLOC(b->data,b->alloced);
    }
}

static void buf_add ( nds_buf_t *b, const void *data, uint size )
{
    buf_need(b,size);
    memcpy(b->data+b->size,data,size);
    b->size += size;
}

static void buf_u8 ( nds_buf_t *b, u8 val )
{
    buf_need(b,1);
    b->data[b->size++] = val;
}

///////////////////////////////////////////////////////////////////////////////

static enumError build_fnt ( nds_fs_t *fs, nds_buf_t *fnt )
{
    // The main table has one 8 byte entry per directory and comes first, so
    // the subtables start behind it.
    const uint main_size = fs->n_dirs * 8;
    buf_need(fnt,main_size);
    memset(fnt->data,0,main_size);
    fnt->size = main_size;

    for ( uint i = 0; i < fs->n_dirs; i++ )
    {
	const nds_dir_t *dir = fs->dir + i;

	u8 *entry = fnt->data + i*8;
	write_le32(entry,fnt->size);
	write_le16(entry+4,dir->first_file);
	// The root entry holds the directory count instead of a parent id.
	write_le16(entry+6,i ? dir->parent : fs->n_dirs);

	uint file_index = dir->first_file;
	for ( uint j = 0; j < dir->n_files; j++, file_index++ )
	{
	    const nds_file_t *f = fs->file + file_index;
	    const uint len = strlen(f->name);
	    if ( !len || len > 127 )
		return ERROR0(ERR_INVALID_FILE,
			"File name length %u is not usable: %s\n",len,f->path);
	    buf_u8(fnt,len);
	    buf_add(fnt,f->name,len);
	}

	for ( uint j = 0; j < dir->n_subs; j++ )
	{
	    const nds_dir_t *sub = fs->dir + dir->sub[j];
	    const uint len = strlen(sub->name);
	    if ( !len || len > 127 )
		return ERROR0(ERR_INVALID_FILE,
			"Directory name length %u is not usable: %s\n",
			len,sub->path);
	    buf_u8(fnt,0x80|len);
	    buf_add(fnt,sub->name,len);
	    u8 id[2];
	    write_le16(id,sub->id);
	    buf_add(fnt,id,2);
	}

	buf_u8(fnt,0);		// end of subtable
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Append 'size' bytes to the image and return the offset they were placed
// at, padding to the DS sector size first.

static u32 place ( nds_buf_t *img, const void *data, u32 size )
{
    const uint pad = ( NDS_ALIGN - img->size % NDS_ALIGN ) % NDS_ALIGN;
    if (pad)
    {
	buf_need(img,pad);
	memset(img->data+img->size,0xff,pad);
	img->size += pad;
    }
    const u32 off = img->size;
    if (size)
	buf_add(img,data,size);
    return off;
}

///////////////////////////////////////////////////////////////////////////////

enumError XCreateNDS ( ccp source, ccp dest )
{
    char buf[PATH_MAX];
    enumError err = ERR_OK;

    //--- the header carries everything that can not be derived from the
    //--- directory, so it is required

    PathCatPP(buf,sizeof(buf),source,NDS_FN_HEADER);
    u64 head_size = 0;
    u8 *head = load_file(buf,&head_size,false);
    if (!head)
	return ERR_CANT_OPEN;
    if ( head_size < NDS_HEAD_SIZE )
    {
	FREE(head);
	return ERROR0(ERR_INVALID_FILE,"Header is too short: %s\n",buf);
    }

    if ( head[NDS_OFF_UNITCODE] & 2 )
	ERROR0(ERR_WARNING,
	    "This is a DSi cartridge; the DSi specific binaries are not"
	    " rebuilt and the result will only work as a DS image.\n");

    nds_fs_t fs;
    memset(&fs,0,sizeof(fs));
    nds_buf_t img, fnt;
    memset(&img,0,sizeof(img));
    memset(&fnt,0,sizeof(fnt));

    u8 *arm9 = 0, *arm9_footer = 0, *arm7 = 0;
    u8 *ovl9 = 0, *ovl7 = 0, *banner = 0;
    u64 arm9_size = 0, arm9_footer_size = 0, arm7_size = 0;
    u64 ovl9_size = 0, ovl7_size = 0, banner_size = 0;
    u8 *fat = 0;

    //--- load the fixed parts

    PathCatPP(buf,sizeof(buf),source,NDS_FN_ARM9);
    arm9 = load_file(buf,&arm9_size,false);
    if (!arm9)
    {
	err = ERR_CANT_OPEN;
	goto abort;
    }

    PathCatPP(buf,sizeof(buf),source,NDS_FN_ARM7);
    arm7 = load_file(buf,&arm7_size,false);
    if (!arm7)
    {
	err = ERR_CANT_OPEN;
	goto abort;
    }

    PathCatPP(buf,sizeof(buf),source,NDS_FN_ARM9_FOOTER);
    arm9_footer = load_file(buf,&arm9_footer_size,true);

    PathCatPP(buf,sizeof(buf),source,NDS_FN_OVL9);
    ovl9 = load_file(buf,&ovl9_size,true);
    PathCatPP(buf,sizeof(buf),source,NDS_FN_OVL7);
    ovl7 = load_file(buf,&ovl7_size,true);
    PathCatPP(buf,sizeof(buf),source,NDS_FN_BANNER);
    banner = load_file(buf,&banner_size,true);

    //--- scan the file system
    //
    // The overlay files come first: their ids are referenced by the overlay
    // tables, which the DS resolves before the FNT even matters, and the
    // original numbering always puts them at the front.

    {
	fs.dirs_alloced = 64;
	fs.dir = CALLOC(fs.dirs_alloced,sizeof(*fs.dir));
	PathCatPP(buf,sizeof(buf),source,NDS_DIR_DATA);
	fs.dir[0].path = STRDUP(buf);
	fs.dir[0].name = fs.dir[0].path;
	fs.dir[0].id   = 0xf000;
	fs.n_dirs = 1;
    }

    {
	// Overlays are not part of the tree, so they are added by hand.
	PathCatPP(buf,sizeof(buf),source,NDS_DIR_OVERLAY);
	DIR *d = opendir(buf);
	if (d)
	{
	    ccp *names = 0;
	    uint n_names = 0, alloced = 0;
	    struct dirent *de;
	    while ( ( de = readdir(d) ) )
	    {
		if ( *de->d_name == '.' )
		    continue;
		if ( n_names == alloced )
		{
		    alloced = alloced ? 2*alloced : 32;
		    names = REALLOC(names,alloced*sizeof(*names));
		}
		names[n_names++] = STRDUP(de->d_name);
	    }
	    closedir(d);
	    qsort(names,n_names,sizeof(*names),cmp_name);

	    char ovl_dir[PATH_MAX];
	    StringCopyS(ovl_dir,sizeof(ovl_dir),buf);
	    fs.files_alloced = n_names + 256;
	    fs.file = CALLOC(fs.files_alloced,sizeof(*fs.file));

	    for ( uint i = 0; i < n_names; i++ )
	    {
		PathCatPP(buf,sizeof(buf),ovl_dir,names[i]);
		struct stat st;
		if ( !stat(buf,&st) && S_ISREG(st.st_mode) )
		{
		    nds_file_t *f = fs.file + fs.n_files;
		    memset(f,0,sizeof(*f));
		    f->path = STRDUP(buf);
		    f->name = f->path;
		    f->id   = fs.n_files;
		    f->size = (u32)st.st_size;
		    fs.n_files++;
		}
		FREE((char*)names[i]);
	    }
	    FREE(names);
	}
    }

    err = scan_dir(&fs,0,0);
    if (err)
	goto abort;

    err = build_fnt(&fs,&fnt);
    if (err)
	goto abort;

    //--- lay out the image

    place(&img,head,head_size);

    {
	const u32 off = place(&img,arm9,arm9_size);
	write_le32(head+NDS_OFF_ARM9_ROM,off);
	write_le32(head+NDS_OFF_ARM9_SIZE,arm9_size);
	if ( arm9_footer && arm9_footer_size )
	    buf_add(&img,arm9_footer,arm9_footer_size);
    }

    {
	const u32 off = place(&img,arm7,arm7_size);
	write_le32(head+NDS_OFF_ARM7_ROM,off);
	write_le32(head+NDS_OFF_ARM7_SIZE,arm7_size);
    }

    if ( ovl9 && ovl9_size )
    {
	write_le32(head+NDS_OFF_OVL9_ROM,place(&img,ovl9,ovl9_size));
	write_le32(head+NDS_OFF_OVL9_SIZE,ovl9_size);
    }
    else
    {
	write_le32(head+NDS_OFF_OVL9_ROM,0);
	write_le32(head+NDS_OFF_OVL9_SIZE,0);
    }

    if ( ovl7 && ovl7_size )
    {
	write_le32(head+NDS_OFF_OVL7_ROM,place(&img,ovl7,ovl7_size));
	write_le32(head+NDS_OFF_OVL7_SIZE,ovl7_size);
    }
    else
    {
	write_le32(head+NDS_OFF_OVL7_ROM,0);
	write_le32(head+NDS_OFF_OVL7_SIZE,0);
    }

    write_le32(head+NDS_OFF_FNT_ROM,place(&img,fnt.data,fnt.size));
    write_le32(head+NDS_OFF_FNT_SIZE,fnt.size);

    // The FAT can only be filled in once the file data is placed, but it has
    // to sit in front of it, so reserve the space now.
    const u32 fat_size = fs.n_files * 8;
    fat = CALLOC(fs.n_files?fs.n_files:1,8);
    const u32 fat_off = place(&img,fat,fat_size);
    write_le32(head+NDS_OFF_FAT_ROM,fat_off);
    write_le32(head+NDS_OFF_FAT_SIZE,fat_size);

    if ( banner && banner_size )
	write_le32(head+NDS_OFF_BANNER_ROM,place(&img,banner,banner_size));
    else
	write_le32(head+NDS_OFF_BANNER_ROM,0);

    for ( uint i = 0; i < fs.n_files && !err; i++ )
    {
	nds_file_t *f = fs.file + i;
	u64 fsize = 0;
	u8 *data = load_file(f->path,&fsize,false);
	if (!data)
	{
	    err = ERR_CANT_OPEN;
	    break;
	}
	f->off = place(&img,data,(u32)fsize);
	write_le32(fat+i*8,f->off);
	write_le32(fat+i*8+4,f->off+(u32)fsize);
	FREE(data);
    }
    if (err)
	goto abort;

    memcpy(img.data+fat_off,fat,fat_size);

    //--- finish the header

    write_le32(head+NDS_OFF_ROM_SIZE,img.size);
    write_le16(head+NDS_OFF_HEAD_CRC,nds_crc16(head,NDS_OFF_HEAD_CRC));
    memcpy(img.data,head,head_size);

    err = save_file(dest,img.data,img.size);
    if ( !err && verbose >= 0 )
	printf("  created %s: %u bytes, %u files, %u directories\n",
		dest, img.size, fs.n_files, fs.n_dirs );

 abort:
    FREE(fat);
    FREE(banner);
    FREE(ovl7);
    FREE(ovl9);
    FREE(arm9_footer);
    FREE(arm7);
    FREE(arm9);
    FREE(head);
    FREE(fnt.data);
    FREE(img.data);
    reset_fs(&fs);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
