// NintendoWare NW4R BREFT particle-effect texture (Wii) -- Image_t save glue.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-breff.h"

enumError SaveBREFTIMG (Image_t *src_img, // pointer to valid img
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: force overwriting
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT ("SaveBREFTIMG(o=%d) mo=%s, %s\n", overwrite, InfoMipmapOptions (mmo), fname);

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
		ERROR0 (ERR_WARNING, "BREFT files don't support palettes, image converted to '%s'.",
			PrintFormat3 (0, mmi.img.iform, mmi.img.pform));
	}

	//--- calculate image size

	const uint img_off = 0x20;
	const uint total_size = img_off + mmi.image_size;

	PRINT ("TOTAL-SIZE: %x = %u\n", total_size, total_size);
	PRINT ("IMAGE-SIZE: %x = %u, %u*%u, N=1+%u\n", mmi.img.data_size, mmi.img.data_size,
		mmi.img.width, mmi.img.height, mmi.n_mipmap);

	//--- alloc and setup data

	data = CALLOC (1, total_size);
	const endian_func_t *endian = mmi.img.endian;

	breft_image_t *bi = (breft_image_t *)data;
	endian->wr16 (&bi->width, mmi.img.width);
	endian->wr16 (&bi->height, mmi.img.height);
	endian->wr32 (&bi->img_size, mmi.image_size);
	bi->iform = mmi.img.iform;
	bi->n_mipmap = mmi.n_mipmap;

	err = WriteImageData (&mmi, data + img_off, 0);
	if (!err)
		err = SaveFILE2 (f, fname, 0, overwrite, data, total_size, 0);

abort:
	FREE (data);
	ResetMMI (&mmi);
	return err;
}

//

///////////////////////////////////////////////////////////////////////////////
///////////////			    TPL support			///////////////
