#include <zlib.h>
#include "lib-std.h"
#include "lib-jarc.h"
#include <string.h>
#include <errno.h>


enumError DecodeJCMP (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;

	if (memcmp (src, "jCMP", 4) && memcmp (src, "JCMP", 4))
		return EINVAL;

	const u8 comp_type = src[8];
	const u8 *payload = src + 16;
	uint payload_size = src_size >= 16 ? src_size - 16 : 0;

	// Check if header is 20 bytes (e.g. 0x14)
	if (src_size >= 20 && (src[16] == 0x78 || !memcmp (src + 16, "jARC", 4) || !memcmp (src + 16, "JARC", 4)))
	{
		payload = src + 16;
		payload_size = src_size - 16;
	}
	else if (src_size >= 24 && (src[20] == 0x78 || !memcmp (src + 20, "jARC", 4) || !memcmp (src + 20, "JARC", 4)))
	{
		payload = src + 20;
		payload_size = src_size - 20;
	}

	if (comp_type == 6 || (payload_size > 0 && payload[0] == 0x78))
	{
		uint out_cap = src_size * 4 + 65536;
		if (out_cap < 256 * 1024)
			out_cap = 256 * 1024;
		u8 *out = MALLOC (out_cap);
		if (!out)
			return ERR_CANT_CREATE;

		z_stream zs;
		memset (&zs, 0, sizeof (zs));
		zs.next_in = (Bytef *)payload;
		zs.avail_in = payload_size;

		int zrc = inflateInit2 (&zs, 15 + 32);
		if (zrc != Z_OK)
			zrc = inflateInit2 (&zs, -15);
		if (zrc != Z_OK)
		{
			FREE (out);
			return EINVAL;
		}

		zs.next_out = (Bytef *)out;
		zs.avail_out = out_cap;

		while (1)
		{
			int ret = inflate (&zs, Z_NO_FLUSH);
			if (ret == Z_STREAM_END)
				break;
			if (ret != Z_OK)
			{
				inflateEnd (&zs);
				if (payload != src + 20 && src_size >= 24)
				{
					memset (&zs, 0, sizeof (zs));
					zs.next_in = (Bytef *)(src + 20);
					zs.avail_in = src_size - 20;
					if (inflateInit2 (&zs, -15) == Z_OK || inflateInit2 (&zs, 15 + 32) == Z_OK)
					{
						zs.next_out = (Bytef *)out;
						zs.avail_out = out_cap;
						ret = inflate (&zs, Z_FINISH);
						inflateEnd (&zs);
						if (ret == Z_STREAM_END || ret == Z_OK)
							break;
					}
				}
				FREE (out);
				return EINVAL;
			}
			if (zs.avail_out == 0)
			{
				uint old_size = out_cap;
				out_cap *= 2;
				if (out_cap > 512 * 1024 * 1024)
				{
					inflateEnd (&zs);
					FREE (out);
					return ERR_FILE_TOO_BIG;
				}
				u8 *nout = REALLOC (out, out_cap);
				if (!nout)
				{
					inflateEnd (&zs);
					FREE (out);
					return ERR_CANT_CREATE;
				}
				out = nout;
				zs.next_out = (Bytef *)(out + old_size);
				zs.avail_out = out_cap - old_size;
			}
		}

		uint total_out = (uint)zs.total_out;
		inflateEnd (&zs);
		*dest = out;
		*dest_size = total_out;
		return ERR_OK;
	}
	else if (comp_type == 1 || comp_type == 0)
	{
		u8 *out = MALLOC (payload_size ? payload_size : 1);
		if (!out)
			return ERR_CANT_CREATE;
		if (payload_size)
			memcpy (out, payload, payload_size);
		*dest = out;
		*dest_size = payload_size;
		return ERR_OK;
	}
	else if (comp_type == 2)
	{
		return DecodeLZ10LZ11 (dest, dest_size, payload, payload_size);
	}

	return EINVAL;
}

static const char *infer_jarc_ext (const u8 *data, uint size)
{
	if (!data || size < 4)
		return "bin";
	if (!memcmp (data, "jMDL", 4) || !memcmp (data, "JMDL", 4))
		return "jmdl";
	if (!memcmp (data, "jTEX", 4) || !memcmp (data, "JTEX", 4))
		return "jtex";
	if (!memcmp (data, "jIMG", 4) || !memcmp (data, "JIMG", 4))
		return "jimg";
	if (!memcmp (data, "jMOT", 4) || !memcmp (data, "JMOT", 4))
		return "jmot";
	if (!memcmp (data, "jMSG", 4) || !memcmp (data, "JMSG", 4))
		return "jmsg";
	if (!memcmp (data, "jCLT", 4) || !memcmp (data, "JCLT", 4))
		return "jclt";
	if (!memcmp (data, "jEFC", 4) || !memcmp (data, "JEFC", 4))
		return "jefc";
	if (!memcmp (data, "jSCN", 4) || !memcmp (data, "JSCN", 4))
		return "jscn";
	if (!memcmp (data, "jWAT", 4) || !memcmp (data, "JWAT", 4))
		return "jwat";
	if (!memcmp (data, "jSND", 4) || !memcmp (data, "JSND", 4))
		return "jsnd";
	if (!memcmp (data, "jCMP", 4) || !memcmp (data, "JCMP", 4))
		return "jcmp";
	if (!memcmp (data, "jARC", 4) || !memcmp (data, "JARC", 4))
		return "jarc";
	if (!memcmp (data, "FRES", 4))
		return "bfres";
	if (!memcmp (data, "SARC", 4))
		return "sarc";
	if (!memcmp (data, "Yaz0", 4))
		return "szs";
	return "bin";
}

enumError ScanJARC (jarc_t *jarc, const u8 *data, size_t size)
{
	if (!jarc || !data || size < 16)
		return EINVAL;

	memset (jarc, 0, sizeof (*jarc));
	jarc->raw = data;
	jarc->raw_size = size;

	const u8 *buf = data;
	uint bsize = (uint)size;

	if (!memcmp (buf, "jCMP", 4) || !memcmp (buf, "JCMP", 4))
	{
		u8 *decomp = 0;
		uint dsize = 0;
		if (DecodeJCMP (&decomp, &dsize, buf, bsize) == ERR_OK && decomp && dsize >= 4)
		{
			jarc->decomp_buffer = decomp;
			jarc->decomp_size = dsize;
			buf = decomp;
			bsize = dsize;
		}
	}

	bool is_be = true;
	if (memcmp (buf, "jARC", 4) && memcmp (buf, "JARC", 4))
	{
		if (jarc->decomp_buffer)
		{
			jarc->entries = CALLOC (1, sizeof (jarc_entry_t));
			if (!jarc->entries)
				return ERR_CANT_CREATE;
			jarc->n_entries = 1;
			jarc->entries[0].data = buf;
			jarc->entries[0].size = bsize;
			jarc->entries[0].offset = 0;
			const char *ext = infer_jarc_ext (buf, bsize);
			snprintf (jarc->entries[0].ext, sizeof (jarc->entries[0].ext), "%s", ext);
			snprintf (jarc->entries[0].name, sizeof (jarc->entries[0].name), "file_0000.%s", ext);
			return ERR_OK;
		}
		return EINVAL;
	}

	u32 fcount_be = bsize >= 12 ? rd_be32 (buf + 8) : 0;
	u32 fcount_le = bsize >= 12 ? rd_le32 (buf + 8) : 0;
	uint n_files = 0;
	if (fcount_be > 0 && fcount_be < 50000 && (fcount_be * 8 <= bsize))
	{
		is_be = true;
		n_files = fcount_be;
	}
	else if (fcount_le > 0 && fcount_le < 50000 && (fcount_le * 8 <= bsize))
	{
		is_be = false;
		n_files = fcount_le;
	}
	else
	{
		u32 f4_be = bsize >= 8 ? rd_be32 (buf + 4) : 0;
		u32 f4_le = bsize >= 8 ? rd_le32 (buf + 4) : 0;
		if (f4_be > 0 && f4_be < 50000 && (f4_be * 8 <= bsize))
		{
			is_be = true;
			n_files = f4_be;
		}
		else if (f4_le > 0 && f4_le < 50000 && (f4_le * 8 <= bsize))
		{
			is_be = false;
			n_files = f4_le;
		}
	}
	jarc->is_big_endian = is_be;

	uint toc_offset = 0x10;
	if (bsize >= 0x20)
	{
		u32 t_off = is_be ? rd_be32 (buf + 12) : rd_le32 (buf + 12);
		if (t_off >= 0x10 && t_off < bsize)
			toc_offset = t_off;
	}

	if (n_files > 0 && (u64)toc_offset + (u64)n_files * 8 <= bsize)
	{
		jarc->entries = CALLOC (n_files, sizeof (jarc_entry_t));
		if (!jarc->entries)
			return ERR_CANT_CREATE;
		jarc->n_entries = n_files;

		for (uint i = 0; i < n_files; i++)
		{
			const u8 *entry_ptr = buf + toc_offset + i * 16;
			u32 off = 0, len = 0;
			if (toc_offset + (i + 1) * 16 <= bsize)
			{
				off = is_be ? rd_be32 (entry_ptr) : rd_le32 (entry_ptr);
				len = is_be ? rd_be32 (entry_ptr + 4) : rd_le32 (entry_ptr + 4);
			}
			else
			{
				entry_ptr = buf + toc_offset + i * 8;
				off = is_be ? rd_be32 (entry_ptr) : rd_le32 (entry_ptr);
				len = is_be ? rd_be32 (entry_ptr + 4) : rd_le32 (entry_ptr + 4);
			}

			if (off < bsize && (u64)off + len <= bsize && len > 0)
			{
				jarc->entries[i].data = buf + off;
				jarc->entries[i].size = len;
				jarc->entries[i].offset = off;
				const char *ext = infer_jarc_ext (buf + off, len);
				snprintf (jarc->entries[i].ext, sizeof (jarc->entries[i].ext), "%s", ext);
				snprintf (jarc->entries[i].name, sizeof (jarc->entries[i].name), "file_%04u.%s", i, ext);
			}
			else
			{
				jarc->entries[i].offset = off;
				jarc->entries[i].size = 0;
				snprintf (jarc->entries[i].name, sizeof (jarc->entries[i].name), "file_%04u.bin", i);
			}
		}
		return ERR_OK;
	}

	uint max_chunks = 2048;
	jarc_entry_t *chunks = CALLOC (max_chunks, sizeof (jarc_entry_t));
	if (!chunks)
		return ERR_CANT_CREATE;
	uint n_chunks = 0;

	for (uint pos = 4; pos + 8 <= bsize; pos += 4)
	{
		const u8 *p = buf + pos;
		if (!memcmp (p, "jMDL", 4) || !memcmp (p, "JMDL", 4)
			|| !memcmp (p, "jTEX", 4) || !memcmp (p, "JTEX", 4)
			|| !memcmp (p, "jIMG", 4) || !memcmp (p, "JIMG", 4)
			|| !memcmp (p, "jMOT", 4) || !memcmp (p, "JMOT", 4)
			|| !memcmp (p, "jMSG", 4) || !memcmp (p, "JMSG", 4)
			|| !memcmp (p, "jCLT", 4) || !memcmp (p, "JCLT", 4)
			|| !memcmp (p, "jEFC", 4) || !memcmp (p, "JEFC", 4)
			|| !memcmp (p, "jSCN", 4) || !memcmp (p, "JSCN", 4)
			|| !memcmp (p, "jWAT", 4) || !memcmp (p, "JWAT", 4)
			|| !memcmp (p, "jSND", 4) || !memcmp (p, "JSND", 4)
			|| !memcmp (p, "jCMP", 4) || !memcmp (p, "JCMP", 4))
		{
			if (n_chunks > 0 && chunks[n_chunks - 1].size == 0)
				chunks[n_chunks - 1].size = pos - chunks[n_chunks - 1].offset;

			if (n_chunks < max_chunks)
			{
				chunks[n_chunks].offset = pos;
				chunks[n_chunks].data = p;
				chunks[n_chunks].size = 0;
				const char *ext = infer_jarc_ext (p, bsize - pos);
				snprintf (chunks[n_chunks].ext, sizeof (chunks[n_chunks].ext), "%s", ext);
				snprintf (chunks[n_chunks].name, sizeof (chunks[n_chunks].name), "file_%04u.%s", n_chunks, ext);
				n_chunks++;
			}
		}
	}

	if (n_chunks > 0)
	{
		if (chunks[n_chunks - 1].size == 0)
			chunks[n_chunks - 1].size = bsize - chunks[n_chunks - 1].offset;
		jarc->entries = chunks;
		jarc->n_entries = n_chunks;
		return ERR_OK;
	}

	FREE (chunks);
	return EINVAL;
}

//-----------------------------------------------------------------------------
///////////////	  QuickBMS-derived flat archive ports		///////////////
//-----------------------------------------------------------------------------
