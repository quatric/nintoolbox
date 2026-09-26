// NintendoWare NW4R TEX0 texture resource (embedded in BRRES, Wii) --
// Image_t save glue. Not to be confused with Monster Games' own unrelated
// .tex format (see lib-excite.c).

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-brres.h"
#include "lib-plt0.h"

// Write a TEX0 that keeps its palette, plus the sibling PLT0 that palette
// lives in. SaveTEX() below always calls Transform2NoPaletteIMG(), which
// rewrites C4/C8/C14X2 into a direct-colour format -- correct for a lone
// .tex0 on disk, which has nowhere to carry a palette, but wrong inside a
// BRRES, where the palette is a separate PLT0 resource and the overwhelming
// majority of shipped textures are indexed. Converting one of those to
// RGB565 changes the format the game expects and inflates the texture several
// times over, so a rewrite that must stay in the archive comes through here.
//
// The image must already be in an indexed format with its palette built (see
// create_C_palette() in lib-image-convert.c); this only packages what it holds.
enumError SaveTEXwithPLT0 (
	Image_t *src_img, const MipmapOptions_t *mmo, ccp tex_fname, ccp plt_fname, bool overwrite)
{
	DASSERT (src_img);
	DASSERT (tex_fname);
	DASSERT (plt_fname);

	if (src_img->iform != IMG_C4 && src_img->iform != IMG_C8 && src_img->iform != IMG_C14X2)
		return ERR_SEMANTIC;
	if (!src_img->pal || !src_img->n_pal)
		return ERR_SEMANTIC;

	u8 *data = 0;
	u8 *plt = 0;
	mipmap_info_t mmi;

	// Deliberately no Transform2NoPaletteIMG() here -- that is the whole point.
	enumError err = PrepareImages (&mmi, src_img, mmo);
	if (err)
		goto abort;

	ccp realfile = strrchr (tex_fname, '/');
	realfile = realfile ? realfile + 1 : tex_fname;
	const uint filelen = strlen (realfile);
	const uint grp_off = 0x40;
	const uint data_size = grp_off + mmi.image_size;
	const uint total_size = data_size + 4 + ALIGN32 (filelen + 1, 4);

	data = CALLOC (1, total_size);
	if (!data)
	{
		err = ERR_OUT_OF_MEMORY;
		goto abort;
	}
	const endian_func_t *endian = mmi.img.endian;

	brsub_header_t *bh = (brsub_header_t *)data;
	memcpy (bh->magic, TEX_MAGIC, sizeof (bh->magic));
	endian->wr32 (&bh->size, data_size);
	endian->wr32 (&bh->version, 3);
	endian->wr32 (&bh->grp_offset, grp_off);

	const uint n_grp = GetSectionNumBRSUB (data, data_size, endian);
	tex_info_t *ti = (tex_info_t *)(bh->grp_offset + n_grp);
	endian->wr32 (&ti->name_off, data_size + 4);
	endian->wr32 (data + data_size, filelen);
	memcpy (data + data_size + 4, realfile, filelen);
	endian->wr16 (&ti->width, mmi.img.width);
	endian->wr16 (&ti->height, mmi.img.height);
	endian->wr32 (&ti->iform, mmi.img.iform);
	endian->wr32 (&ti->n_image, mmi.n_mipmap + 1);
	endian->wrf4 (&ti->image_val, mmi.n_mipmap);

	err = WriteImageData (&mmi, data + grp_off, 0);
	if (err)
		goto abort;

	// Only commit the TEX0 once its palette is in hand: half a pair would
	// leave the archive with indexed pixels and no colours to read them with.
	uint plt_size = 0;
	err = EncodePLT0_Raw (&plt, &plt_size, mmi.img.pal ? mmi.img.pal : src_img->pal,
		mmi.img.pal ? mmi.img.n_pal : src_img->n_pal, mmi.img.pal ? mmi.img.pform : src_img->pform);
	if (err)
		goto abort;

	err = SaveFILE2 (0, tex_fname, 0, overwrite, data, total_size, 0);
	if (!err)
		err = SaveFILE2 (0, plt_fname, 0, overwrite, plt, plt_size, 0);

abort:
	FREE (plt);
	FREE (data);
	ResetMMI (&mmi);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveTEX (Image_t *src_img, // pointer to valid source img
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite, // true: force overwriting
	bool ctcode_support // true: include a CT-CODE file
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT ("SaveTEX(o=%d,ct=%d) mo=%s, %s\n", overwrite, ctcode_support, InfoMipmapOptions (mmo),
		fname);

	//--- setup images

	u8 *data = 0;
	const bool no_pal_stat = Transform2NoPaletteIMG (src_img);

	mipmap_info_t mmi;
	enumError err = PrepareImages (&mmi, src_img, mmo);
	if (err)
		goto abort;

	static int warn_count = 3;
	if (no_pal_stat && warn_count > 0)
	{
		warn_count--;
		ERROR0 (ERR_WARNING, "TEX0 files don't support palettes, image converted to '%s'.",
			PrintFormat3 (0, mmi.img.iform, mmi.img.pform));
	}

	//--- calculate image size

	ccp realfile = strrchr (fname, '/');
	realfile = realfile ? realfile + 1 : fname;
	const uint filelen = strlen (realfile);
	const uint grp_off = 0x40;
	const uint data_size = grp_off + mmi.image_size;
	const uint total_size = data_size + 4 + ALIGN32 (filelen + 1, 4);

	PRINT ("TOTAL-SIZE: %x = %u\n", total_size, total_size);
	PRINT ("IMAGE-SIZE: %x = %u, %u*%u, N=1+%u\n", mmi.img.data_size, mmi.img.data_size,
		mmi.img.width, mmi.img.height, mmi.n_mipmap);

	//--- alloc and setup data

	data = CALLOC (1, total_size);
	const endian_func_t *endian = mmi.img.endian;

	brsub_header_t *bh = (brsub_header_t *)data;
	memcpy (bh->magic, TEX_MAGIC, sizeof (bh->magic));
	endian->wr32 (&bh->size, data_size);
	endian->wr32 (&bh->version, 3);
	endian->wr32 (&bh->grp_offset, grp_off);

	const uint n_grp = GetSectionNumBRSUB (data, data_size, endian);
	tex_info_t *ti = (tex_info_t *)(bh->grp_offset + n_grp);
	endian->wr32 (&ti->name_off, data_size + 4);
	endian->wr32 (data + data_size, filelen);
	memcpy (data + data_size + 4, realfile, filelen);
	endian->wr16 (&ti->width, mmi.img.width);
	endian->wr16 (&ti->height, mmi.img.height);
	endian->wr32 (&ti->iform, mmi.img.iform);
	endian->wr32 (&ti->n_image, mmi.n_mipmap + 1);
	endian->wrf4 (&ti->image_val, mmi.n_mipmap);

	err = WriteImageData (&mmi, data + grp_off, 0);
	if (!err)
		err = SaveFILE2 (f, fname, 0, overwrite, data, total_size, 0);

abort:
	FREE (data);
	ResetMMI (&mmi);
	return err;
}
