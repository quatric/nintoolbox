
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
#include <sys/stat.h>

#include "lib-xfile.h"
#include "lib-wux.h"
#include "x-formats.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   format table			///////////////
///////////////////////////////////////////////////////////////////////////////

const xformat_info_t xformat_info[XF__N] =
{
    { XF_UNKNOWN, "-",	 "",	 "unknown container",		0,0,0 },

    { XF_WUD,	"WUD",	".wud", "Wii U disc image",		1,0,1 },
    { XF_WUX,	"WUX",	".wux", "Wii U disc image, sparse",	1,0,1 },
    { XF_NDS,	"NDS",	".nds", "Nintendo DS/DSi cartridge",	1,1,0 },
    { XF_WAD,	"WAD",	".wad", "installable Wii title",	1,1,0 },
    { XF_CCI,	"CCI",	".3ds", "3DS cartridge image",		1,0,0 },
    { XF_CIA,	"CIA",	".cia", "3DS installable title",	1,0,0 },
    { XF_XCI,	"XCI",	".xci", "Switch cartridge image",	1,0,0 },
    { XF_NSP,	"NSP",	".nsp", "Switch package",		1,0,0 },
    { XF_NKIT_GC, "NKIT", ".iso", "NKit-compressed GC image (restore only)", 0,0,1 },
    { XF_NKIT_WII,"NKIT", ".iso", "NKit-compressed Wii image (restore only)", 0,0,1 },
};

///////////////////////////////////////////////////////////////////////////////

xformat_t ScanXFormat ( ccp name )
{
    if (name)
	for ( int i = 1; i < XF__N; i++ )
	    if (!strcasecmp(name,xformat_info[i].name))
		return xformat_info[i].format;
    return XF_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////

static xformat_t format_by_ext ( ccp fname )
{
    ccp dot = fname ? strrchr(fname,'.') : 0;
    if (dot)
	for ( int i = 1; i < XF__N; i++ )
	    if (!strcasecmp(dot,xformat_info[i].ext))
		return xformat_info[i].format;
    return XF_UNKNOWN;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   detection			///////////////
///////////////////////////////////////////////////////////////////////////////

xformat_t AnalyzeXFormat
(
    const void		*data,		// valid pointer to the file start
    uint		data_size,	// number of valid bytes in 'data'
    u64			file_size	// 0 or the real file size
)
{
    if ( !data || data_size < 0x200 )
	return XF_UNKNOWN;
    const u8 *d = data;

    //--- Wii U

    if (IsValidWUX(d,data_size,0))
	return XF_WUX;

    // A Wii U image starts with the plain text disc identifier; retail dumps
    // whose first sector is still encrypted start with the disc magic
    // instead.
    if ( !memcmp(d,"WUP-",4) || be32(d) == 0xcc549eb9 )
	return XF_WUD;

    //--- Switch

    // XCI: the gamecard header 'HEAD' sits behind the 0x100 byte signature.
    if ( data_size >= 0x104 && !memcmp(d+0x100,"HEAD",4) )
	return XF_XCI;

    // NSP is a bare PFS0 archive.
    if (!memcmp(d,"PFS0",4))
	return XF_NSP;

    //--- 3DS

    // CCI: NCSD magic behind the 0x100 byte signature.  NCSD is also used by
    // NAND images, which are distinguished by a zero media id at 0x108.
    if ( data_size >= 0x110 && !memcmp(d+0x100,"NCSD",4) && le64(d+0x108) )
	return XF_CCI;

    // CIA has no magic at all: it is identified by its fixed header size and
    // a type/version pair that is always zero.
    if ( le32(d) == 0x2020 && !le16(d+4) && !le16(d+6) )
	return XF_CIA;

    //--- Wii WAD
    // Same trick as CIA: header size 0x20 plus a known two byte type.

    if ( be32(d) == 0x20 )
    {
	const u32 type = be32(d+4);
	if ( type == 0x49730000 || type == 0x69620000 )	// 'Is\0\0' / 'ib\0\0'
	    return XF_WAD;
    }

    //--- Nintendo DS
    // No magic either.  The header holds a CRC16 over the 156 byte Nintendo
    // logo, and that value is a constant for every genuine cartridge.

    if ( data_size >= 0x160 && le16(d+0x15c) == 0xcf56 )
	return XF_NDS;

    //--- NKit
    // "NKIT v01" tag stashed in the disc header's own unused 0x200 region --
    // see x-nkit.c (GC) and x-nkit-wii.c (Wii).
    //
    // GC vs Wii is decided by the disc magic the NKit header still carries
    // verbatim: NKit only ever overwrites 0x200..0x218 (its own tag/CRC/size
    // fields) plus 0x60/0x61, so WII_MAGIC at WII_MAGIC_OFF (0x18) and
    // GC_MAGIC at GC_MAGIC_OFF (0x1c) survive untouched and are exactly the
    // discriminator every other GC/Wii reader in this codebase already uses.
    // The Wii partition table at 0x40000 says the same thing, but lies well
    // past this function's XFILE_HEAD_SIZE (0x1000) sniff buffer, whereas
    // both magics are comfortably inside it.

    if ( data_size >= 0x208 && !memcmp(d+0x200,"NKIT v01",8) )
	return data_size >= WII_MAGIC_OFF+WII_MAGIC_LEN
		&& be32(d+WII_MAGIC_OFF) == WII_MAGIC
		? XF_NKIT_WII : XF_NKIT_GC;

    (void)file_size;
    return XF_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////

xformat_t AnalyzeXFile
(
    ccp			fname,		// file to analyze
    u64			*file_size	// not NULL: store the file size here
)
{
    if (file_size)
	*file_size = 0;

    struct stat st;
    if ( stat(fname,&st) || !S_ISREG(st.st_mode) )
    {
	ERROR1(ERR_CANT_OPEN,"Can't open file: %s\n",fname);
	return XF_UNKNOWN;
    }

    FILE *f = fopen(fname,"rb");
    if (!f)
    {
	ERROR1(ERR_CANT_OPEN,"Can't open file: %s\n",fname);
	return XF_UNKNOWN;
    }

    u8 head[XFILE_HEAD_SIZE];
    memset(head,0,sizeof(head));
    const size_t read_size = fread(head,1,sizeof(head),f);
    fclose(f);

    const xformat_t fform
	= AnalyzeXFormat(head,(uint)read_size,(u64)st.st_size);
    if ( fform != XF_UNKNOWN && file_size )
	*file_size = st.st_size;
    return fform;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     XINFO			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XInfo ( ccp source )
{
    u64 file_size = 0;
    const xformat_t fform = AnalyzeXFile(source,&file_size);
    if ( fform == XF_UNKNOWN )
    {
	printf("%-5s %s\n","-",source);
	return ERR_WRONG_FILE_TYPE;
    }

    const xformat_info_t *info = xformat_info + fform;
    printf("%-5s %10llu MiB  %s\n",info->name,file_size/MiB,source);

    switch (fform)
    {
	case XF_WUX:
	case XF_WUD:
	    return XInfoWiiU(source,fform);

	case XF_NDS:
	    return XInfoNDS(source);

	case XF_WAD:
	    return XInfoWAD(source);

	case XF_CCI:
	    return XInfoCCI(source);

	case XF_CIA:
	    return XInfoCIA(source);

	case XF_XCI:
	    return XInfoXCI(source);

	case XF_NSP:
	    return XInfoNSP(source);

	default:
	    // Detected, but nothing beyond the one line above is known yet.
	    printf("      %s\n",info->info);
	    return ERR_OK;
    }
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    XEXTRACT			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XExtract ( ccp source, ccp dest )
{
    const xformat_t fform = AnalyzeXFile(source,0);
    if ( fform == XF_UNKNOWN )
	return ERROR0(ERR_WRONG_FILE_TYPE,
		"Not a supported container: %s\n",source);

    if (!xformat_info[fform].can_extract)
	return ERROR0(ERR_NOT_IMPLEMENTED,
		"%s images can be detected, but not extracted yet%s: %s\n",
		xformat_info[fform].name,
		xformat_info[fform].can_convert ? " (XCONVERT works)" : "",
		source );

    switch (fform)
    {
	case XF_WUD:
	case XF_WUX:	return XExtractWiiU(source,fform,dest);
	case XF_NDS:	return XExtractNDS(source,dest);
	case XF_WAD:	return XExtractWAD(source,dest);
	case XF_CCI:	return XExtractCCI(source,dest);
	case XF_CIA:	return XExtractCIA(source,dest);
	case XF_XCI:	return XExtractXCI(source,dest);
	case XF_NSP:	return XExtractNSP(source,dest);
	default:	return ERROR0(ERR_INTERNAL,0);
    }
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    XCREATE			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XCreate ( ccp source, ccp dest, xformat_t format )
{
    if ( format == XF_UNKNOWN )
	format = format_by_ext(dest);
    if ( format == XF_UNKNOWN )
	return ERROR0(ERR_SYNTAX,
		"Can't derive the output format from the file name."
		" Use --xformat: %s\n", dest );

    if (!xformat_info[format].can_create)
	return ERROR0(ERR_NOT_IMPLEMENTED,
		"Creating %s images is not implemented yet.\n",
		xformat_info[format].name );

    switch (format)
    {
	case XF_NDS:	return XCreateNDS(source,dest);
	case XF_WAD:	return XCreateWAD(source,dest);
	default:	return ERROR0(ERR_INTERNAL,0);
    }
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XCONVERT			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XConvert ( ccp source, ccp dest, xformat_t format )
{
    const xformat_t src_format = AnalyzeXFile(source,0);
    if ( src_format == XF_UNKNOWN )
	return ERROR0(ERR_WRONG_FILE_TYPE,
		"Not a supported container: %s\n",source);

    // NKit restoration is one-directional (compressed -> plain disc image)
    // and its output is a normal GC/Wii ISO, which -- unlike every other
    // XCONVERT target -- has no xformat_info entry of its own (SuperFile_t
    // owns plain disc images; see the file header comment in lib-xfile.h).
    // So this bypasses the format_by_ext()/can_convert table lookup below
    // entirely rather than trying to force a destination format that
    // doesn't exist in this subsystem.
    if ( src_format == XF_NKIT_GC )
	return XExtractNKitGC(source,dest);
    if ( src_format == XF_NKIT_WII )
	return XExtractNKitWii(source,dest);

    if ( format == XF_UNKNOWN )
	format = format_by_ext(dest);
    if ( format == XF_UNKNOWN )
	return ERROR0(ERR_SYNTAX,
		"Can't derive the output format from the file name."
		" Use --xformat: %s\n", dest );

    if ( !xformat_info[src_format].can_convert
	|| !xformat_info[format].can_convert )
	return ERROR0(ERR_NOT_IMPLEMENTED,
		"Can't convert %s to %s.\n",
		xformat_info[src_format].name, xformat_info[format].name );

    // Both families are closed under conversion, so a cross family request
    // is a user error rather than a missing feature.
    const bool src_wiiu = src_format == XF_WUD || src_format == XF_WUX;
    const bool dst_wiiu = format == XF_WUD || format == XF_WUX;
    if ( src_wiiu != dst_wiiu )
	return ERROR0(ERR_SYNTAX,
		"%s and %s are unrelated formats.\n",
		xformat_info[src_format].name, xformat_info[format].name );

    return XConvertWiiU(source,src_format,dest,format);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
