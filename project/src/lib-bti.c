// Nintendo GameCube/Wii BTI texture format -- Image_t save glue and
// container-inspection helpers.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"

enumError SaveBTI (Image_t *src_img, // pointer to valid source img
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: force overwriting
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT0 ("SaveBTI(o=%d) mo=%s, %s\n", overwrite, InfoMipmapOptions (mmo), fname);

	//--- setup images

	u8 *data = 0;
	// don't know how to store palettes [[2do]]
	const bool no_pal_stat = Transform2NoPaletteIMG (src_img);

	mipmap_info_t mmi;
	enumError err = PrepareImages (&mmi, src_img, mmo);
	if (err)
		goto abort;

	static int warn_count = 3;
	if (no_pal_stat && warn_count > 0)
	{
		warn_count--;
		ERROR0 (ERR_WARNING, "BTI files with palettes not supported yet. Image converted to '%s'.",
			PrintFormat3 (0, mmi.img.iform, mmi.img.pform));
	}

	//--- calculate image size

	const uint data_off = sizeof (bti_header_t);
	const uint total_size = data_off + mmi.image_size;
	PRINT ("TOTAL-SIZE: %x = %u\n", total_size, total_size);
	PRINT ("IMAGE-SIZE: %x = %u, %u*%u, N=1+%u\n", mmi.img.data_size, mmi.img.data_size,
		mmi.img.width, mmi.img.height, mmi.n_mipmap);

	//--- alloc and setup data

	data = CALLOC (1, total_size);
	const endian_func_t *endian = &be_func;

	bti_header_t *bti = (bti_header_t *)data;
	bti->iform = mmi.img.iform;
	bti->n_image = mmi.n_mipmap + 1;
	bti->unknown_14 = 1;
	bti->unknown_15 = 1;
	endian->wr16 (&bti->width, mmi.img.width);
	endian->wr16 (&bti->height, mmi.img.height);
	endian->wr32 (&bti->data_off, data_off);

	ccp point = strrchr (fname, '.');
	const bool is_special
		= point && (!strcasecmp (point, ".btiEnv") || !strcasecmp (point, ".btiMat"));
	if (!is_special)
	{
		bti->unknown_01 = 2;
		bti->wrap_s = 1;
		bti->wrap_t = 1;
	}

	err = WriteImageData (&mmi, data + data_off, 0);
	if (!err)
		err = SaveFILE2 (f, fname, 0, overwrite, data, total_size, 0);

abort:
	FREE (data);
	ResetMMI (&mmi);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

uint GetNImagesBTI (const u8 *data, // bti data
	uint data_size // size of bti data
)
{
	DASSERT (data);
	if (IsValidBTI (data, data_size, 0, 0) >= VALID_ERROR)
		return 0;

	const bti_header_t *bti = (bti_header_t *)data;
	return bti->n_image;
}

///////////////////////////////////////////////////////////////////////////////
#if 0

bool SetupPointerBTI
(
    const u8			* data,		// BTI data
    uint			data_size,	// size of bti data
    uint			img_index,	// index of image to extract
    const bti_header_t		** bti_head,	// not NULL: store pointer here
    const bti_imgtab_t		** bti_tab,	// not NULL: store pointer here
    const bti_pal_header_t	** bti_pal,	// not NULL: store pointer here
    const bti_img_header_t	** bti_img,	// not NULL: store pointer here
    const u8			** pal_data,	// not NULL: store pointer to pal data
    const u8			** img_data,	// not NULL: store pointer to img data
    const endian_func_t		* endian	// endian functions
)
{
    DASSERT(data);
    DASSERT(endian);

    if ( data_size >= sizeof(bti_header_t) && endian->rd32(data) == BTI_MAGIC_NUM )
    {
      const bti_header_t *bti = (bti_header_t*)data;
      const uint n_img = endian->rd32(&bti->n_image);
      const u32 tab_off = endian->rd32(&bti->imgtab_off);
      if ( img_index < n_img && tab_off + n_img*sizeof(bti_imgtab_t) <= data_size )
      {
	const bti_imgtab_t *tab = (bti_imgtab_t*)( data + tab_off ) + img_index;
	const u32 img_off = endian->rd32(&tab->image_off);
	if ( img_off && img_off + sizeof(bti_img_header_t) <= data_size )
	{
	    const bti_img_header_t *img = (bti_img_header_t*)(data+img_off);
	    const u32 img_data_off = endian->rd32(&img->data_off);
	    if ( !img_data_off || img_data_off >= data_size )
		goto abort;

	    if (bti_head) *bti_head = bti;
	    if (bti_tab)  *bti_tab  = tab;
	    if (bti_pal)  *bti_pal  = 0;
	    if (bti_img)  *bti_img  = img;
	    if (pal_data) *pal_data = 0;
	    if (img_data) *img_data = data + img_data_off;

	    const u32 pal_off = endian->rd32(&tab->palette_off);
	    if (pal_off)
	    {
		if ( pal_off > sizeof(bti_pal_header_t) >= data_size )
		    goto abort;

		const bti_pal_header_t *pal = (bti_pal_header_t*)(data+pal_off);
		const u32 pal_data_off = endian->rd32(&pal->data_off);
		if ( !pal_data_off || pal_data_off >= data_size )
		    goto abort;

		if (bti_pal)  *bti_pal = pal;
		if (pal_data) *pal_data = data + pal_data_off;
	    }
	    return true;
	}
      }
    }

 abort:
    if (bti_head) *bti_head = 0;
    if (bti_tab)  *bti_tab  = 0;
    if (bti_pal)  *bti_pal  = 0;
    if (bti_img)  *bti_img  = 0;
    if (pal_data) *pal_data = 0;
    if (img_data) *img_data = 0;
    return false;
}

#endif

