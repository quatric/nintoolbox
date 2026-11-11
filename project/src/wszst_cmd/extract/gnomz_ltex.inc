#include "lib-lz10.h"
// Qubic Games' "LTEX" textures (WiiWare "2 Fast 4 Gnomz" and the other Qubic Wii games, ".ltex"): "LTEX", u32 flags, then
// u16 format / width / height (little endian) and 8 bytes of flags, the pixel data starting at 0x18. When byte 6 is 0x10 the
// data is a u32 header (low 24 bits = compressed length) followed by an LZ11 stream. Formats: 14 and 15 = RGB565 and
// 17 = RGBA8888, both little endian and row major. Written as "<name>.png" beside the source.
static enumError extract_gnomz_ltex_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ltex") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	u8 *dec = 0, *rgba = 0;
	if (sz > 0x18 && !memcmp (raw, "LTEX", 4))
	{
		const uint fmt = raw[8] | raw[9] << 8, w = raw[10] | raw[11] << 8, h = raw[12] | raw[13] << 8;
		const uint bpp = fmt == 17 ? 4 : fmt == 14 || fmt == 15 ? 2 : 0;
		if (bpp && w && h && w <= 8192 && h <= 8192)
		{
			const u8 *pix = raw + 0x18;
			size_t pix_n = sz - 0x18;
			if (raw[6] == 0x10 && pix_n > 8)
			{
				uint dn = 0;
				if (DecodeLZ10LZ11 (&dec, &dn, pix + 4, (uint)pix_n - 4) == ERR_OK && dec)
				{
					pix = dec;
					pix_n = dn;
				}
				else
					pix_n = 0;
			}
			if (pix_n >= (size_t)w * h * bpp && (rgba = MALLOC ((size_t)w * h * 4)))
			{
				for (size_t i = 0; i < (size_t)w * h; i++)
				{
					u8 *o = rgba + i * 4;
					if (bpp == 4)
						memcpy (o, pix + i * 4, 4);
					else
					{
						const uint v = pix[i * 2] | pix[i * 2 + 1] << 8;
						o[0] = ((v >> 11) & 31) * 255 / 31;
						o[1] = ((v >> 5) & 63) * 255 / 63;
						o[2] = (v & 31) * 255 / 31;
						o[3] = 255;
					}
				}
				char name[PATH_MAX];
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) *dot = 0;
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE LTEX:%s (%ux%u fmt %u) -> PNG\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt);
				err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
			}
		}
	}
	FREE (dec);
	FREE (rgba);
	FREE (raw);
	return err;
}
