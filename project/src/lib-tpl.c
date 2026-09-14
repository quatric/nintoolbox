// Nintendo GameCube/Wii TPL/TPLX texture palette library -- Image_t save
// glue and container-inspection helpers.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"

void ResetRawTPL (tpl_raw_t *raw)
{
	if (raw)
	{
		for (int i = 0; i < raw->n_image; i++)
			ResetMMI (raw->mmi + i);
		FREE (raw->mmi);
		FreeString (raw->data.ptr);
		memset (raw, 0, sizeof (*raw));
	}
}

//-----------------------------------------------------------------------------

const tpl_signature_t TPLSignature0 = { "LE-CODE\0", 0, 0, 0, 0, 0, "Cup Icon" };

//-----------------------------------------------------------------------------

enumError CreateRawTPL (tpl_raw_t *raw, // results with alloced data
	Image_t *src_img, // pointer to valid source img
	file_format_t fform // FF_TPL or FF_TPLX
)
{
	DASSERT (src_img);
	DASSERT (raw);
	memset (raw, 0, sizeof (*raw));

	//--- special handling for cup icons

	bool create_tplx = fform == FF_TPLX;
	if (fform == FF_CUPICON || src_img->is_cup_icon && !n_transform) // no forced transformation
	{
		ConvertIMG (src_img, false, 0, IMG_CMPR, PAL_INVALID);
		create_tplx = true;
	}
	else
	{
		Transform2InternIMG (src_img);
		const enumError err = ExecTransformIMG (src_img);
		if (err)
			return err;
	}

	PRINT ("is_cup_icon=%d, test_mode=%d, ff=%s\n", src_img->is_cup_icon, src_img->test_mode,
		GetImageFormatName (src_img->iform, "?"));

	if (create_tplx)
		FreeMipmapsIMG (src_img);

	//--- TPLx calculations

	const ImageGeometry_t *geo = GetImageGeometry (src_img->iform);
	if (!geo)
		return ERROR0 (ERR_INTERNAL, 0);

	int ex_width, ex_height, ex_fill;
	if (create_tplx)
	{
		// add 1 for special line
		uint max_lines = MAX_IMAGE_HEIGHT / geo->block_height;
		const uint n_lines = (src_img->height + 2 * geo->block_height - 1) / geo->block_height;
		if (src_img->test_mode && max_lines >= n_lines)
			max_lines = (n_lines - 1) / geo->block_height * geo->block_height;
		const uint n_cols = (n_lines + max_lines - 1) / max_lines;

		ex_width = src_img->width * n_cols;
		ex_height = (n_lines + n_cols - 1) / n_cols * geo->block_height;
		ex_fill
			= (ex_width * ex_height - src_img->width * src_img->height) * geo->bits_per_pixel / 8;

		PRINT ("TPLx: %u*%u, cols=%d, lines=%d/%d, geo=%d*%d => %d*%d, fill:%x\n", src_img->width,
			src_img->height, n_cols, n_lines, max_lines, geo->block_width, geo->block_height,
			ex_width, ex_height, ex_fill);
	}
	else
	{
		ex_width = src_img->width < MAX_IMAGE_WIDTH ? src_img->width : MAX_IMAGE_WIDTH;
		ex_height = src_img->height < MAX_IMAGE_HEIGHT ? src_img->height : MAX_IMAGE_HEIGHT;
		ex_fill = 0;
	}

	//--- setup images

	u8 *data = 0;
	const int n_image = CountMipmapsIMG (src_img) + 1;
	mipmap_info_t *mmi = CALLOC (n_image, sizeof (*mmi));
	raw->n_image = n_image;
	raw->mmi = mmi;
	enumError err = ERR_OK;

	const uint align = 0x20;
	// [[tpl-ex+]]
	const uint tab_off = create_tplx ? sizeof (tpl_header_ex_t) : sizeof (tpl_header_t);
	uint data_off = tab_off + n_image * sizeof (tpl_imgtab_t);

	mipmap_info_t *m = mmi;
	const Image_t *img = src_img;

	//--- first loop: setup header offsets

	int i;
	for (i = 0; i < n_image; i++, m++, img = img->mipmap)
	{
		DASSERT (img);
		err = PrepareImagesTPL (m, img);
		if (err)
			return err;

		m->pal_head_off = 0;
		m->img_head_off = data_off;

		if (m->img.n_pal)
		{
			m->pal_head_off = data_off;
			m->pal_data_off = m->pal_head_off + sizeof (tpl_pal_header_t);
			m->pal_data_size = 2 * m->img.n_pal;
			m->img_head_off = ALIGN32 (m->pal_data_off + m->pal_data_size, 4);
		}
		data_off = ALIGN32 (m->img_head_off + sizeof (tpl_img_header_t), 4);
	}
	data_off = ALIGN32 (data_off, align);

	//--- second loop: setup data offsets

	for (i = 0, m = mmi; i < n_image; i++, m++)
	{
		m->img_data_off = data_off;
		uint data_size = m->image_size;
		m->img_data_size = data_size;
		if (!i)
			data_size += ex_fill;
		data_off = ALIGN32 (m->img_data_off + data_size, align);

		PRINT0 ("%6x %6x %6x | %6x %6x %6x | %6x = %6u\n", m->pal_head_off, m->pal_data_off,
			m->pal_data_size, m->img_head_off, m->img_data_off, m->img_data_size, data_off,
			data_off);
	}

	//--- alloc data & setup file header

	data = CALLOC (1, data_off);
	raw->data.ptr = (ccp)data;
	raw->data.len = data_off;
	const endian_func_t *endian = src_img->endian;
	raw->endian = endian;

	// [[tpl-ex+]]
	tpl_header_ex_t *tpl = (tpl_header_ex_t *)data;
	endian->wr32 (tpl->magic, TPL_MAGIC_NUM);
	endian->wr32 (&tpl->n_image, n_image);
	endian->wr32 (&tpl->imgtab_off, tab_off);

	// [[tpl-ex+]]
	if (create_tplx)
	{
		endian->wr32 (&tpl->ex_magic, TPL_EX_MAGIC_NUM);
		endian->wr32 (&tpl->ex_width, src_img->width);
		endian->wr32 (&tpl->ex_height, src_img->height);
		endian->wr32 (&tpl->ex_n_icon, src_img->height / src_img->width);
	}

	tpl_imgtab_t *tab = (tpl_imgtab_t *)(data + tab_off);

	//--- third loop: copy data

	for (i = 0, m = mmi; i < n_image; i++, m++)
	{
		endian->wr32 (&tab[i].image_off, m->img_head_off);
		endian->wr32 (&tab[i].palette_off, m->pal_head_off);

		if (m->img.n_pal)
		{
			tpl_pal_header_t *tp = (tpl_pal_header_t *)(data + m->pal_head_off);
			endian->wr16 (&tp->n_entry, m->img.n_pal);
			endian->wr32 (&tp->pform, m->img.pform);
			endian->wr32 (&tp->data_off, m->pal_data_off);
			memcpy (data + m->pal_data_off, m->img.pal, 2 * m->img.n_pal);
		}

		// [[tpl-ex+]]
		tpl_img_header_t *ti = (tpl_img_header_t *)(data + m->img_head_off);
		endian->wr16 (&ti->width, ex_width);
		endian->wr16 (&ti->height, ex_height);
		endian->wr32 (&ti->iform, m->img.iform);
		endian->wr32 (&ti->data_off, m->img_data_off);
		endian->wr32 (&ti->min_filter, 1);
		endian->wr32 (&ti->mag_filter, 1);

		u8 *dest;
		err = WriteImageData (m, data + m->img_data_off, &dest);
		if (err)
			return err;
		PRINT0 (
			"DEST: %p - %p = %zx\n", dest, data + m->img_data_off, dest - (data + m->img_data_off));

		if (!i && ex_fill)
		{
			memset (dest, 0, ex_fill);
			if (ex_fill >= sizeof (tpl_signature_t))
			{
				tpl_signature_t *sig = (tpl_signature_t *)(dest + ex_fill) - 1;
				*sig = TPLSignature0;
				endian->wr32 (&sig->width, src_img->width);
				endian->wr32 (&sig->height, src_img->height);
				endian->wr32 (&sig->n_icon, src_img->height / src_img->width);
				endian->wr16 (&sig->iform, src_img->iform);
				endian->wr16 (&sig->pform, src_img->pform);
			}
		}
	}

	raw->valid = true;
	return ERR_OK;
}

//-----------------------------------------------------------------------------

enumError SaveRawTPL (tpl_raw_t *raw, // raw data created by CreateRawTPL()
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: allow overwriting
)
{
	DASSERT (raw);
	DASSERT (fname);
	PRINT ("SaveRawTPL(o=%d) %s\n", overwrite, fname);

	return raw->valid ? SaveFILE2 (f, fname, 0, overwrite, raw->data.ptr, raw->data.len, 0)
					  : ERR_ERROR;
}

//-----------------------------------------------------------------------------

enumError SaveTPL (Image_t *src_img, // pointer to valid source img
	file_format_t fform, // FF_TPL or FF_TPLX
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: allow overwriting
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT ("SaveTPL(ff=%s,o=%d) %s\n", GetNameFF (fform, fform), overwrite, fname);

	tpl_raw_t raw;
	enumError err = CreateRawTPL (&raw, src_img, fform);
	if (raw.valid)
		err = SaveRawTPL (&raw, f, fname, overwrite);
	//	err = SaveFILE2(f,fname,0,overwrite,raw.data.ptr,raw.data.len,0);
	ResetRawTPL (&raw);
	return err;
}


uint GetNImagesTPL (const u8 *data, // TPL data
	uint data_size, // size of tpl data
	const endian_func_t *endian // endian functions
)
{
	DASSERT (data);
	DASSERT (endian);

	// [[tpl-ex+]]
	return data_size >= sizeof (tpl_header_t) && endian->rd32 (data) == TPL_MAGIC_NUM
		? endian->rd32 (data + 4)
		: 0;
}

///////////////////////////////////////////////////////////////////////////////

bool SetupPointerTPL (const u8 *data, // TPL data
	uint data_size, // size of tpl data
	uint img_index, // index of image to extract
	const tpl_header_t **tpl_head, // not NULL: store pointer here
	const tpl_imgtab_t **tpl_tab, // not NULL: store pointer here
	const tpl_pal_header_t **tpl_pal, // not NULL: store pointer here
	const tpl_img_header_t **tpl_img, // not NULL: store pointer here
	const u8 **pal_data, // not NULL: store pointer to pal data
	const u8 **img_data, // not NULL: store pointer to img data
	const endian_func_t *endian // endian functions
)
{
	DASSERT (data);
	DASSERT (endian);

	// [[tpl-ex+]]
	if (data_size >= sizeof (tpl_header_t) && endian->rd32 (data) == TPL_MAGIC_NUM)
	{
		const tpl_header_t *tpl = (tpl_header_t *)data;
		const uint n_img = endian->rd32 (&tpl->n_image);
		u32 tab_off = endian->rd32 (&tpl->imgtab_off);
		if (tab_off < sizeof (tpl_header_t) || tab_off + n_img * sizeof (tpl_imgtab_t) > data_size)
		{
			// News Channel TPL fix: fallback to default offset
			tab_off = sizeof (tpl_header_t);
		}
		if (img_index < n_img && tab_off + n_img * sizeof (tpl_imgtab_t) <= data_size)
		{
			const tpl_imgtab_t *tab = (tpl_imgtab_t *)(data + tab_off) + img_index;
			const u32 img_off = endian->rd32 (&tab->image_off);
			if (img_off && img_off + sizeof (tpl_img_header_t) <= data_size)
			{
				const tpl_img_header_t *img = (tpl_img_header_t *)(data + img_off);
				const u32 img_data_off = endian->rd32 (&img->data_off);
				if (!img_data_off || img_data_off >= data_size)
					goto abort;

				if (tpl_head)
					*tpl_head = tpl;
				if (tpl_tab)
					*tpl_tab = tab;
				if (tpl_pal)
					*tpl_pal = 0;
				if (tpl_img)
					*tpl_img = img;
				if (pal_data)
					*pal_data = 0;
				if (img_data)
					*img_data = data + img_data_off;

				const u32 pal_off = endian->rd32 (&tab->palette_off);
				if (pal_off)
				{
					if (pal_off > sizeof (tpl_pal_header_t) >= data_size)
						goto abort;

					const tpl_pal_header_t *pal = (tpl_pal_header_t *)(data + pal_off);
					const u32 pal_data_off = endian->rd32 (&pal->data_off);
					if (!pal_data_off || pal_data_off >= data_size)
						goto abort;

					if (tpl_pal)
						*tpl_pal = pal;
					if (pal_data)
						*pal_data = data + pal_data_off;
				}
				return true;
			}
		}
	}

abort:
	if (tpl_head)
		*tpl_head = 0;
	if (tpl_tab)
		*tpl_tab = 0;
	if (tpl_pal)
		*tpl_pal = 0;
	if (tpl_img)
		*tpl_img = 0;
	if (pal_data)
		*pal_data = 0;
	if (img_data)
		*img_data = 0;
	return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    BTI support			///////////////

