#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "lib-iqipack.h"
#include "lib-szs.h"

// XXTEA parameters
#define XXTEA_DELTA 0x9E3779B9U

static const u32 s_fixed_key[4] = {
	0xA0D0FFB0U, 0x81230089U, 0x12159842U, 0xFF78F3C7U
};

#define XXTEA_MIX(p_idx) \
	(((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) \
	^ ((sum ^ y) + (key[((p_idx) & 3) ^ e] ^ z)))

static u32 HashString (ccp str, size_t len)
{
	u32 hash = 0x1505U;
	for (size_t i = 0; i < len; i++)
	{
		hash *= 33U;
		hash ^= (u8)str[i];
	}
	return hash;
}

static void GenerateKey (u32 key[4], ccp str, size_t str_len, u32 length, u32 offset)
{
	const u32 base = (length ^ offset) ^ HashString (str, str_len);
	for (int i = 0; i < 4; i++)
		key[i] = base & s_fixed_key[i];
}

static void DecryptXXTEA (u32 *data, u32 n, const u32 key[4])
{
	if (n <= 1)
		return;

	const u32 rounds = 6 + (52 / n);
	u32 sum = rounds * XXTEA_DELTA;
	u32 y, z;
	u32 r = rounds;

	y = data[0];

	do
	{
		const u32 e = (sum >> 2) & 3;
		for (u32 p = n - 1; p > 0; p--)
		{
			z = data[p - 1];
			y = data[p] -= XXTEA_MIX (p);
		}

		z = data[n - 1];
		y = data[0] -= XXTEA_MIX (0);

		sum -= XXTEA_DELTA;
	}
	while (--r);
}

static void DecryptAsset (ccp name, size_t name_len, u8 *data, u32 length)
{
	for (u32 i = 0; i < length; i += 0x2000)
	{
		const u32 remainder = (length - i < 0x2000) ? length - i : 0x2000;
		const u32 n_words = remainder / 4;
		if (n_words > 1)
		{
			u32 key[4];
			GenerateKey (key, name, name_len, length, i);
			DecryptXXTEA ((u32 *)(data + i), n_words, key);
		}
	}
}

bool IsIQIPack (const u8 *data, size_t size)
{
	if (!data || size < sizeof (iqipack_header_t))
		return false;

	const iqipack_header_t *hdr = (const iqipack_header_t *)data;
	if (hdr->magic != IQIPACK_MAGIC)
		return false;

	// In NVIDIA Shield iQiyi PAKs, header_size and size2 match
	if (hdr->header_size == 0 || hdr->header_size != hdr->size2)
		return false;

	if (sizeof (iqipack_header_t) + (size_t)hdr->header_size > size)
		return false;

	return true;
}

bool IsIQIPackFile (ccp filename)
{
	if (!filename)
		return false;

	FILE *f = fopen (filename, "rb");
	if (!f)
		return false;

	iqipack_header_t hdr;
	const size_t n = fread (&hdr, 1, sizeof (hdr), f);
	fclose (f);

	if (n != sizeof (hdr) || hdr.magic != IQIPACK_MAGIC)
		return false;

	return hdr.header_size > 0 && hdr.header_size == hdr.size2;
}

static void get_dest_dir (char *dest, size_t dest_size, ccp arg, ccp basedir)
{
	if (opt_dest && *opt_dest)
		snprintf (dest, dest_size, "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, dest_size, "%s/%s.d", basedir, FindFilename (arg, 0));
	else
		snprintf (dest, dest_size, "%s.d", arg);
}

enumError ExtractIQIPack (ccp arg, ccp basedir, uint depth)
{
	if (!arg)
		return ERR_NOTHING_TO_DO;

	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;

	iqipack_header_t header;
	if (fread (&header, 1, sizeof (header), in) != sizeof (header))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	if (header.magic != IQIPACK_MAGIC || header.header_size == 0 || header.header_size != header.size2)
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	const u32 header_size = header.header_size;
	u8 *hdr_buf = MALLOC (header_size);
	if (!hdr_buf)
	{
		fclose (in);
		return ERR_CANT_CREATE;
	}

	if (fread (hdr_buf, 1, header_size, in) != header_size)
	{
		FREE (hdr_buf);
		fclose (in);
		return ERR_READ_FAILED;
	}

	DecryptAsset ("header", 6, hdr_buf, header_size);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IQIPACK: %s -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	const u8 *ptr = hdr_buf;
	const u8 *hdr_end = hdr_buf + header_size;

	if (ptr + 4 > hdr_end)
	{
		FREE (hdr_buf);
		fclose (in);
		return ERR_INVALID_DATA;
	}

	u32 num_assets = rd_le32 (ptr);
	ptr += 4;

	const off_t asset_base_offset = (off_t)(sizeof (iqipack_header_t) + header_size);

	while (num_assets-- > 0 && ptr < hdr_end)
	{
		if (ptr + 4 > hdr_end)
			break;

		const u32 len_str = rd_le32 (ptr);
		ptr += 4;

		if (len_str == 0 || ptr + len_str > hdr_end)
			break;

		char rel_path[PATH_MAX];
		if (len_str >= sizeof (rel_path))
			break;

		memcpy (rel_path, ptr, len_str);
		rel_path[len_str] = '\0';
		ptr += len_str;

		if (ptr + 12 > hdr_end)
			break;

		const u32 asset_size = rd_le32 (ptr);
		ptr += 4;
		// size2 duplicate
		ptr += 4;
		const u32 asset_offset = rd_le32 (ptr);
		ptr += 4;

		// 0x10 bytes padding
		ptr += 0x10;

		char out_file[PATH_MAX];
		snprintf (out_file, sizeof (out_file), "%s/%s", dest, rel_path);

		char *slash = strrchr (out_file, '/');
		if (slash)
		{
			*slash = '\0';
			CreatePath (out_file, true);
			*slash = '/';
		}

		if (testmode)
			continue;

		if (fseeko (in, asset_base_offset + asset_offset, SEEK_SET) != 0)
			continue;

		u8 *asset_buf = MALLOC (asset_size);
		if (!asset_buf)
			continue;

		if (fread (asset_buf, 1, asset_size, in) == asset_size)
		{
			DecryptAsset (rel_path, len_str, asset_buf, asset_size);
			SaveFile (out_file, 0, 0, asset_buf, asset_size, 0);
		}
		FREE (asset_buf);
	}

	FREE (hdr_buf);
	fclose (in);

	return ERR_OK;
}
