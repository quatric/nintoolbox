// SPDX-License-Identifier: GPL-2.0+
// Nintendo 3DS Camera application resources; see lib-ctrres.h.
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-ctrres.h"
#include "lib-archive-util.h"
#include <string.h>

// open_memstream() buffers come from the libc allocator
#undef free

static u32 ctr_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}
static uint ctr_le16 (const u8 *p)
{
	return p[0] | p[1] << 8;
}

//-----------------------------------------------------------------------------
// PACK
//-----------------------------------------------------------------------------

#define PACK_REC 0x40
#define PACK_NAME 0x38

// Number of members, or 0 when DATA is not a camera PACK.
static uint pack_count (const u8 *d, size_t size)
{
	uint n = 0;
	while ((size_t)(n + 1) * PACK_REC <= size && d[n * PACK_REC])
		n++;
	if (!n || n > 4096 || (size_t)(n + 1) * PACK_REC > size)
		return 0;
	u64 first = ~0ull;
	for (uint i = 0; i < n; i++)
	{
		const u8 *r = d + (size_t)i * PACK_REC;
		size_t len = 0;
		while (len < PACK_NAME && r[len])
			len++;
		if (len == PACK_NAME)
			return 0;
		for (size_t k = 0; k < len; k++)
			if (r[k] < 0x20 || r[k] > 0x7e || r[k] == '/' || r[k] == '\\')
				return 0;
		for (size_t k = len; k < PACK_NAME; k++)
			if (r[k])
				return 0;
		const u32 off = ctr_le32 (r + PACK_NAME), sz = ctr_le32 (r + PACK_NAME + 4);
		if (!sz || (u64)off + sz > size || off < (u64)(n + 1) * PACK_REC)
			return 0;
		if (off < first)
			first = off;
	}
	// the record table must be followed by nothing but zero padding up to the first member
	for (size_t o = (size_t)n * PACK_REC; o < first && o < size; o++)
		if (d[o])
			return 0;
	return n;
}

bool IsCtrPack (const u8 *d, size_t size)
{
	return pack_count (d, size) != 0;
}

enumError ExtractCtrPack (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext_match (arg, ".pack"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	const uint n = pack_count (raw, raw_size);
	if (!n)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CTR-PACK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	if (!testmode)
	{
		CreatePath (dest, true);
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = raw + (size_t)i * PACK_REC;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, (ccp)r);
			SaveFile (path, 0, 0, raw + ctr_le32 (r + PACK_NAME), ctr_le32 (r + PACK_NAME + 4), 0);
		}
	}
	FREE (raw);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// GBIN
//-----------------------------------------------------------------------------

#define GBIN_HDR 8
#define GBIN_GUID (4 + 28 + 16)

bool IsCtrGbin (const u8 *d, size_t size)
{
	if (size < 12 || memcmp (d, "GBIN", 4))
		return false;
	const u32 n = ctr_le32 (d + 4);
	if (!n || n > 100000)
		return false;
	size_t p = GBIN_HDR;
	for (u32 i = 0; i < n; i++)
	{
		if (p + (GBIN_GUID) > size || memcmp (d + p, "GUID", 4))
			return false;
		const u32 parts = ctr_le32 (d + p + 4);
		if (parts > 4096)
			return false;
		p += GBIN_GUID + (size_t)parts * 20;
	}
	return p <= size;
}

static void yaml_name (FILE *f, const u8 *p, size_t max)
{
	size_t l = 0;
	while (l < max && p[l])
		l++;
	fprintf (f, "\"");
	for (size_t i = 0; i < l; i++)
		fputc (p[i] >= 0x20 && p[i] < 0x7f && p[i] != '"' && p[i] != '\\' ? p[i] : '?', f);
	fprintf (f, "\"");
}

char *DumpCtrGbin (const u8 *d, size_t size, ccp source)
{
	if (!IsCtrGbin (d, size))
		return 0;
	char *buf = 0;
	size_t len = 0;
	FILE *f = open_memstream (&buf, &len);
	if (!f)
		return 0;
	const u32 n = ctr_le32 (d + 4);
	fprintf (f,
		"#\n# Nintendo 3DS Camera guide table (GBIN), decoded from %s\n"
		"# flags = the 6 words after the part count of each GUID record (meaning unknown)\n#\n",
		source);
	fprintf (f, "guides: # %u entries\n", n);
	size_t p = GBIN_HDR;
	for (u32 i = 0; i < n; i++)
	{
		const u32 parts = ctr_le32 (d + p + 4);
		fprintf (f, "  - name: ");
		yaml_name (f, d + p + 32, 16);
		fprintf (f, "\n    flags: [");
		for (uint w = 1; w < 7; w++)
			fprintf (f, "%s%u", w > 1 ? ", " : " ", ctr_le32 (d + p + 4 + 4 * w));
		fprintf (f, " ]\n    parts:\n");
		for (u32 k = 0; k < parts; k++)
		{
			const u8 *q = d + p + GBIN_GUID + (size_t)k * 20;
			fprintf (f, "      - { name: ");
			yaml_name (f, q, 16);
			fprintf (f, ", value: %u }\n", ctr_le32 (q + 16));
		}
		p += GBIN_GUID + (size_t)parts * 20;
	}
	fclose (f);
	(void)len;
	return buf;
}

//-----------------------------------------------------------------------------
// CBNK / CWSD
//-----------------------------------------------------------------------------

bool IsCtrBankOrWsd (const u8 *d, size_t size)
{
	if (size < 0x40 || (memcmp (d, "CBNK", 4) && memcmp (d, "CWSD", 4)) || ctr_le16 (d + 4) != 0xfeff)
		return false;
	const u32 fsize = ctr_le32 (d + 12), blocks = ctr_le32 (d + 0x10);
	return fsize <= size && blocks == 1 && !memcmp (d + 0x20, "INFO", 4);
}

char *DumpCtrBankOrWsd (const u8 *d, size_t size, ccp source)
{
	if (!IsCtrBankOrWsd (d, size))
		return 0;
	const bool bank = !memcmp (d, "CBNK", 4);
	const u32 info_off = ctr_le32 (d + 0x18), info_size = ctr_le32 (d + 0x1c);
	if (info_off + 8ull > size || info_off + (u64)info_size > size || memcmp (d + info_off, "INFO", 4))
		return 0;
	const u8 *body = d + info_off + 8;
	const size_t blen = info_size >= 8 ? info_size - 8 : 0;

	char *buf = 0;
	size_t len = 0;
	FILE *f = open_memstream (&buf, &len);
	if (!f)
		return 0;
	fprintf (f, "#\n# Nintendo 3DS %s, decoded from %s\n#\n",
		bank ? "instrument bank (CBNK)" : "wave sound file (CWSD)", source);
	fprintf (f, "type: %s\nversion: 0x%08x\nfile-size: %u\n", bank ? "CBNK" : "CWSD", ctr_le32 (d + 8),
		ctr_le32 (d + 12));

	// wave id table (first reference of the body)
	const u32 wt = ctr_le32 (body + 4);
	if (blen >= 8 && wt + 4ull <= blen)
	{
		const u32 n = ctr_le32 (body + wt);
		if ((u64)wt + 4 + 8ull * n <= blen && n < 65536)
		{
			fprintf (f, "waves: # %u entries {archive-id, wave-index}\n", n);
			for (u32 i = 0; i < n; i++)
				fprintf (f, "  - { archive-id: 0x%08x, index: %u }\n", ctr_le32 (body + wt + 4 + 8 * i),
					ctr_le32 (body + wt + 8 + 8 * i));
		}
	}
	// second reference: the table of instruments / wave sounds
	if (blen >= 16)
	{
		const u32 tt = ctr_le32 (body + 0x0c);
		if (tt + 4ull <= blen)
		{
			const u32 n = ctr_le32 (body + tt);
			if ((u64)tt + 4 + 8ull * n <= blen && n < 65536)
			{
				fprintf (f, "%s: # %u slots {reference type, offset from the INFO body}\n",
					bank ? "instruments" : "wave-sounds", n);
				for (u32 i = 0; i < n; i++)
				{
					const u32 off = ctr_le32 (body + tt + 8 + 8 * i);
					if (off == 0xffffffffu)
						continue;
					fprintf (f, "  - { slot: %u, type: 0x%04x, offset: 0x%x }\n", i,
						ctr_le16 (body + tt + 4 + 8 * i), off);
				}
			}
		}
	}
	fclose (f);
	(void)len;
	return buf;
}

enumError ExtractCtrResource (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	const bool gbin = is_ext_match (arg, ".gbin");
	if (!gbin && !is_ext_match (arg, ".bcbnk") && !is_ext_match (arg, ".bcwsd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	char *text = gbin ? DumpCtrGbin (raw, raw_size, arg) : DumpCtrBankOrWsd (raw, raw_size, arg);
	FREE (raw);
	if (!text)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yaml", false);
	else
		snprintf (dest, sizeof (dest), "%s.yaml", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CTR-%s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", gbin ? "GBIN" : "BANK", arg, dest);
	enumError err = ERR_OK;
	if (!testmode)
		err = SaveFile (dest, 0, 0, (u8 *)text, strlen (text), 0);
	free (text);
	return err;
}
