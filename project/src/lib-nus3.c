#include "lib-nus3.h"
#include "lib-std.h"

bool IsNUS3 (const u8 *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "NUS3", 4) || !memcmp (data, "nus3", 4)
		|| !memcmp (data, "3SUN", 4) || !memcmp (data, "3sun", 4);
}

static ccp infer_audio_ext (const u8 *d, size_t sz)
{
	if (!d || sz < 4)
		return ".bin";
	if (!memcmp (d, "IDSP", 4))
		return ".idsp";
	if (!memcmp (d, "OPUS", 4) || !memcmp (d, "Opus", 4))
		return ".lopus";
	if (!memcmp (d, "RIFF", 4))
		return ".wav";
	if (!memcmp (d, "DSP ", 4) || !memcmp (d, "C_DSP", 5))
		return ".dsp";
	if (!memcmp (d, "BNS ", 4))
		return ".bns";
	if (!memcmp (d, "BSEQ", 4))
		return ".bseq";
	if (!memcmp (d, "CSEQ", 4))
		return ".cseq";
	if (!memcmp (d, "FSEQ", 4))
		return ".fseq";
	return ".bin";
}

enumError ScanNUS3 (nus3_t *nus, const u8 *data, size_t size)
{
	if (!nus || !data || size < 16 || !IsNUS3 (data, size))
		return ERR_INVALID_DATA;

	memset (nus, 0, sizeof (*nus));
	nus->data = data;
	nus->size = size;

	// Endianness check: if second byte is 'U', normal; check size field
	u32 sz_le = le32 (data + 4);
	u32 sz_be = be32 (data + 4);
	nus->is_big_endian = (sz_be <= size + 16 && sz_le > size + 16);
	nus->total_size = nus->is_big_endian ? sz_be : sz_le;

#define N16(p) (nus->is_big_endian ? be16 (p) : le16 (p))
#define N32(p) (nus->is_big_endian ? be32 (p) : le32 (p))

	const u8 *p = data + 8;
	const u8 *end = data + size;

	const u8 *audiindx_data = 0;
	size_t audiindx_sz = 0;
	const u8 *tnid_data = 0;
	size_t tnid_sz = 0;
	const u8 *nmof_data = 0;
	size_t nmof_sz = 0;
	const u8 *adof_data = 0;
	size_t adof_sz = 0;
	const u8 *tnnm_data = 0;
	size_t tnnm_sz = 0;
	const u8 *pack_data = 0;
	size_t pack_sz = 0;
	const u8 *banktoc_data = 0;
	size_t banktoc_sz = 0;

	// Scan chunks (each chunk has 8-byte tag + 4-byte size)
	while (p + 12 <= end)
	{
		char tag[9] = { 0 };
		memcpy (tag, p, 8);
		u32 chunk_len = N32 (p + 8);
		const u8 *payload = p + 12;

		if (payload + chunk_len > end)
			chunk_len = (u32)(end - payload);

		if (!strncasecmp (tag, "AUDIINDX", 8))
		{
			audiindx_data = payload;
			audiindx_sz = chunk_len;
		}
		else if (!strncasecmp (tag, "TNID", 4))
		{
			tnid_data = payload;
			tnid_sz = chunk_len;
		}
		else if (!strncasecmp (tag, "NMOF", 4))
		{
			nmof_data = payload;
			nmof_sz = chunk_len;
		}
		else if (!strncasecmp (tag, "ADOF", 4))
		{
			adof_data = payload;
			adof_sz = chunk_len;
		}
		else if (!strncasecmp (tag, "TNNM", 4))
		{
			tnnm_data = payload;
			tnnm_sz = chunk_len;
		}
		else if (!strncasecmp (tag, "PACK", 4))
		{
			pack_data = payload;
			pack_sz = chunk_len;
		}
		else if (!strncasecmp (tag, "BANKTOC", 7))
		{
			banktoc_data = payload;
			banktoc_sz = chunk_len;
		}

		p = payload + chunk_len;
	}

	nus->pack_data = pack_data;
	nus->pack_size = pack_sz;

	// Path 1: NUS3AUDIO (Ultimate)
	if (audiindx_data && adof_data && audiindx_sz >= 4)
	{
		uint count = N32 (audiindx_data);
		if (count > 0 && count < 10000)
		{
			nus->n_tracks = count;
			nus->tracks = CALLOC (count, sizeof (nus3_track_t));
			if (!nus->tracks)
				return ERR_OUT_OF_MEMORY;

			for (uint i = 0; i < count; i++)
			{
				nus3_track_t *t = &nus->tracks[i];
				t->track_id = i;

				if (tnid_data && (i + 1) * 4 <= tnid_sz)
					t->track_id = N32 (tnid_data + i * 4);

				// Resolve name
				if (nmof_data && tnnm_data && (i + 1) * 4 <= nmof_sz)
				{
					u32 str_off = N32 (nmof_data + i * 4);
					if (str_off < tnnm_sz)
					{
						const u8 *sp = tnnm_data + str_off;
						u8 slen = *sp;
						if (slen > 0 && str_off + 1 + slen <= tnnm_sz)
						{
							size_t copy_len = slen < sizeof (t->name) - 1 ? slen : sizeof (t->name) - 1;
							memcpy (t->name, sp + 1, copy_len);
							t->name[copy_len] = 0;
						}
						else
						{
							// Null-terminated string fallback
							snprintf (t->name, sizeof (t->name), "%s", (const char *)sp);
						}
					}
				}

				if (!*t->name)
					snprintf (t->name, sizeof (t->name), "track_%03u", t->track_id);

				// Resolve audio offset and size
				if ((i + 1) * 8 <= adof_sz)
				{
					t->offset = N32 (adof_data + i * 8);
					t->size = N32 (adof_data + i * 8 + 4);

					if (pack_data && t->offset + t->size <= pack_sz)
						t->data = pack_data + t->offset;
					else if (t->offset + t->size <= size)
						t->data = data + t->offset;
				}

				if (t->data && t->size > 0)
					snprintf (t->ext, sizeof (t->ext), "%s", infer_audio_ext (t->data, t->size));
				else
					snprintf (t->ext, sizeof (t->ext), ".bin");
			}
			return ERR_OK;
		}
	}

	// Path 2: NUS3BANK (Smash 4 3DS / Wii U)
	if (pack_data && pack_sz > 0)
	{
		// Scan subfiles within PACK chunk
		const u8 *sp = pack_data;
		const u8 *send = pack_data + pack_sz;
		uint count = 0;
		u32 offsets[512] = { 0 };
		u32 sizes[512] = { 0 };

		while (sp + 16 <= send && count < 512)
		{
			// Check if subfile starts with IDSP / BNS / RIFF / etc.
			offsets[count] = (u32)(sp - pack_data);
			u32 sub_sz = 0;
			if (!memcmp (sp, "IDSP", 4))
			{
				sub_sz = be32 (sp + 4); // IDSP total size is big-endian
				if (sub_sz == 0 || sub_sz > (size_t)(send - sp))
					sub_sz = (u32)(send - sp);
			}
			else if (!memcmp (sp, "RIFF", 4))
			{
				sub_sz = le32 (sp + 4) + 8;
			}
			else
			{
				sub_sz = (u32)(send - sp);
			}
			sizes[count] = sub_sz;
			count++;
			sp += (sub_sz + 15) & ~15u; // 16-byte align
		}

		if (count > 0)
		{
			nus->n_tracks = count;
			nus->tracks = CALLOC (count, sizeof (nus3_track_t));
			if (!nus->tracks)
				return ERR_OUT_OF_MEMORY;

			for (uint i = 0; i < count; i++)
			{
				nus3_track_t *t = &nus->tracks[i];
				t->track_id = i;
				t->offset = offsets[i];
				t->size = sizes[i];
				t->data = pack_data + t->offset;
				snprintf (t->ext, sizeof (t->ext), "%s", infer_audio_ext (t->data, t->size));
				snprintf (t->name, sizeof (t->name), "tone_%03u", i);
			}
			return ERR_OK;
		}
	}

	(void)banktoc_data;
	(void)banktoc_sz;
	return ERR_INVALID_DATA;
}

void ResetNUS3 (nus3_t *nus)
{
	if (!nus)
		return;
	if (nus->tracks)
		FREE (nus->tracks);
	memset (nus, 0, sizeof (*nus));
}

static inline void wr_le32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

enumError CreateNUS3Audio (u8 **dest, size_t *dest_size, uint n_tracks,
	const char *const *names, const u32 *track_ids, const u8 *const *audio_data, const size_t *audio_sizes)
{
	if (!dest || !dest_size || !n_tracks)
		return ERR_INVALID_DATA;

	// Calculate chunk sizes
	size_t audiindx_sz = 4;
	size_t tnid_sz = n_tracks * 4;
	size_t nmof_sz = n_tracks * 4;
	size_t adof_sz = n_tracks * 8;

	size_t tnnm_sz = 0;
	for (uint i = 0; i < n_tracks; i++)
	{
		const char *nm = names && names[i] ? names[i] : "track";
		tnnm_sz += 1 + strlen (nm) + 1; // len byte + string + null
	}

	size_t pack_sz = 0;
	for (uint i = 0; i < n_tracks; i++)
	{
		size_t asz = audio_sizes ? audio_sizes[i] : 0;
		pack_sz += (asz + 15) & ~15u;
	}

	size_t total_size = 8 // NUS3 header
		+ (12 + audiindx_sz)
		+ (12 + tnid_sz)
		+ (12 + nmof_sz)
		+ (12 + adof_sz)
		+ (12 + tnnm_sz)
		+ (12 + pack_sz);

	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	u8 *p = buf;

	// NUS3 Header
	memcpy (p, "NUS3", 4);
	p += 4;
	wr_le32 (p, (u32)(total_size - 8));
	p += 4;

	// AUDIINDX Chunk
	memcpy (p, "AUDIINDX", 8);
	p += 8;
	wr_le32 (p, (u32)audiindx_sz);
	p += 4;
	wr_le32 (p, n_tracks);
	p += 4;

	// TNID Chunk
	memcpy (p, "TNID\0\0\0\0", 8);
	p += 8;
	wr_le32 (p, (u32)tnid_sz);
	p += 4;
	for (uint i = 0; i < n_tracks; i++)
	{
		wr_le32 (p, track_ids ? track_ids[i] : i);
		p += 4;
	}

	// NMOF Chunk
	memcpy (p, "NMOF\0\0\0\0", 8);
	p += 8;
	wr_le32 (p, (u32)nmof_sz);
	p += 4;
	u8 *nmof_ptr = p;
	p += nmof_sz;

	// ADOF Chunk
	memcpy (p, "ADOF\0\0\0\0", 8);
	p += 8;
	wr_le32 (p, (u32)adof_sz);
	p += 4;
	u8 *adof_ptr = p;
	p += adof_sz;

	// TNNM Chunk
	memcpy (p, "TNNM\0\0\0\0", 8);
	p += 8;
	wr_le32 (p, (u32)tnnm_sz);
	p += 4;
	u8 *tnnm_base = p;

	for (uint i = 0; i < n_tracks; i++)
	{
		wr_le32 (nmof_ptr + i * 4, (u32)(p - tnnm_base));
		const char *nm = names && names[i] ? names[i] : "track";
		u8 slen = (u8)strlen (nm);
		*p++ = slen;
		memcpy (p, nm, slen);
		p += slen;
		*p++ = 0;
	}

	// PACK Chunk
	memcpy (p, "PACK\0\0\0\0", 8);
	p += 8;
	wr_le32 (p, (u32)pack_sz);
	p += 4;
	u8 *pack_base = p;

	for (uint i = 0; i < n_tracks; i++)
	{
		size_t asz = audio_sizes ? audio_sizes[i] : 0;
		u32 pack_off = (u32)(p - pack_base);
		wr_le32 (adof_ptr + i * 8, pack_off);
		wr_le32 (adof_ptr + i * 8 + 4, (u32)asz);

		if (asz > 0 && audio_data && audio_data[i])
			memcpy (p, audio_data[i], asz);
		p += (asz + 15) & ~15u;
	}

	*dest = buf;
	*dest_size = total_size;
	return ERR_OK;
}
