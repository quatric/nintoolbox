// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-mpr-pak.h"
#include <string.h>

// ScanMPRPACK is defined below; IsMPRPACK needs it up here.
enumError ScanMPRPACK (mpr_pack_t *pak, const u8 *data, uint size);
void ResetMPRPACK (mpr_pack_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	if (pak->names)
	{
		for (uint i = 0; i < pak->n_names; i++)
			FREE (pak->names[i].name);
		FREE (pak->names);
	}
	if (pak->metas)
	{
		for (uint i = 0; i < pak->n_metas; i++)
			FREE (pak->metas[i].data);
		FREE (pak->metas);
	}
	memset (pak, 0, sizeof (*pak));
}

bool IsMPRPACK (const u8 *data, uint size)
{
	mpr_pack_t pak;
	const bool ok = ScanMPRPACK (&pak, data, size) == ERR_OK;
	if (ok)
		ResetMPRPACK (&pak);
	return ok;
}

void FormatMPRGUID (char out[37], const u8 guid[16])
{
	static const char hexd[] = "0123456789abcdef";
	uint o = 0;
	for (uint i = 0; i < 16; i++)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10)
			out[o++] = '-';
		// First three fields are LE integers (Uuid::from_bytes_le).
		uint v = guid[i];
		if (i < 4)
			v = guid[i ^ 3];
		else if (i < 6)
			v = guid[i ^ 1];
		else if (i < 8)
			v = guid[i ^ 1];
		out[o++] = hexd[v >> 4];
		out[o++] = hexd[v & 15];
	}
	out[o] = 0;
}

static bool guid_eq (const u8 a[16], const u8 b[16])
{
	return !memcmp (a, b, 16);
}

ccp FindMPRPACKName (const mpr_pack_t *pak, const u8 guid[16])
{
	if (!pak || !guid)
		return 0;
	for (uint i = 0; i < pak->n_names; i++)
		if (guid_eq (pak->names[i].guid, guid))
			return pak->names[i].name;
	return 0;
}

const u8 *FindMPRPACKMeta (const mpr_pack_t *pak, const u8 guid[16], uint *meta_size)
{
	if (meta_size)
		*meta_size = 0;
	if (!pak || !guid)
		return 0;
	for (uint i = 0; i < pak->n_metas; i++)
		if (guid_eq (pak->metas[i].guid, guid))
		{
			if (meta_size)
				*meta_size = pak->metas[i].size;
			return pak->metas[i].data;
		}
	return 0;
}

//--- LZSS ---------------------------------------------------------------
// Modes 1..3 copy 1/2/4-byte groups (group = 2^(mode-1)); a set header
// bit reads a back-reference of count = (b0>>4)+(4-mode) groups at
// distance length = (((b0&0xF)<<8)|b1)<<(mode-1) bytes. Same core as
// the Tropical Freeze stream (see lib-retro-txtr.c), only the mode
// word here is u32 LE. Fully bounds-checked: truncation or a wild
// back-reference fails instead of overrunning.

enumError DecodeMPR_LZSS (u8 **dest, uint *dest_size, const u8 *src, uint src_size,
	uint decomp_size)
{
	if (!dest || !dest_size)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	if (!src || src_size < 4 || !decomp_size || decomp_size > MPR_PACK_MAX_OUTPUT)
		return EINVAL;
	const uint mode = (uint)src[0] | (uint)src[1] << 8 | (uint)src[2] << 16 | (uint)src[3] << 24;
	if (mode > 3)
		return EINVAL;
	const u8 *in = src + 4;
	uint in_left = src_size - 4;
	u8 *out = MALLOC (decomp_size);
	if (!out)
		return ERR_CANT_CREATE;
	uint dp = 0;
	if (mode == 0)
	{
		// Stored: exact size match, like the reference shortcut.
		if (in_left != decomp_size)
		{
			FREE (out);
			return EINVAL;
		}
		memcpy (out, in, decomp_size);
		*dest = out;
		*dest_size = decomp_size;
		return ERR_OK;
	}
	const uint group = 1u << (mode - 1);
	uint header = 0, left = 0;
	while (dp < decomp_size)
	{
		if (!left)
		{
			if (!in_left)
			{
				FREE (out);
				return EINVAL;
			}
			header = *in++;
			in_left--;
			left = 8;
		}
		const bool ref = (header & 0x80) != 0;
		header = (header << 1) & 0xff;
		left--;
		if (!ref)
		{
			if (in_left < group || decomp_size - dp < group)
			{
				FREE (out);
				return EINVAL;
			}
			memcpy (out + dp, in, group);
			in += group;
			in_left -= group;
			dp += group;
		}
		else
		{
			if (in_left < 2)
			{
				FREE (out);
				return EINVAL;
			}
			const uint b0 = in[0], b1 = in[1];
			in += 2;
			in_left -= 2;
			const uint count = (b0 >> 4) + (4 - mode);
			const uint length = (((uint)(b0 & 0x0f) << 8) | b1) << (mode - 1);
			if (!length || length > dp)
			{
				FREE (out);
				return EINVAL;
			}
			const u64 need = (u64)count * group;
			if (need > decomp_size - dp)
			{
				FREE (out);
				return EINVAL;
			}
			uint seek = dp - length;
			for (uint c = 0; c < count; c++)
				for (uint k = 0; k < group; k++)
					out[dp++] = out[seek++];
		}
	}
	*dest = out;
	*dest_size = decomp_size;
	return ERR_OK;
}

//--- structural scan ----------------------------------------------------

static bool read_form (const u8 *data, uint size, uint off, char id[4], u32 *rver, u32 *wver,
	u64 *body_size, uint *body_off)
{
	if (!data || (u64)off + 0x20 > size || memcmp (data + off, "RFRM", 4))
		return false;
	const u64 fsize = rd_le64 (data + off + 4);
	if (fsize > size || (u64)off + 0x20 + fsize > size)
		return false;
	memcpy (id, data + off + 0x14, 4);
	if (rver)
		*rver = rd_le32 (data + off + 0x18);
	if (wver)
		*wver = rd_le32 (data + off + 0x1c);
	if (body_size)
		*body_size = fsize;
	if (body_off)
		*body_off = off + 0x20;
	return true;
}

static bool read_chunk (const u8 *data, uint size, uint off, char id[4], u64 *body_size,
	uint *body_off)
{
	if (!data || (u64)off + 0x18 > size)
		return false;
	memcpy (id, data + off, 4);
	const u64 csize = rd_le64 (data + off + 4);
	const u64 skip = rd_le64 (data + off + 0x10);
	if (csize > size || skip > size || (u64)off + 0x18 + skip + csize > size)
		return false;
	if (body_size)
		*body_size = csize;
	if (body_off)
		*body_off = off + 0x18 + (uint)skip;
	return true;
}

enumError ScanMPRPACK (mpr_pack_t *pak, const u8 *data, uint size)
{
	if (!pak || !data || size < 0x20)
		return EINVAL;
	memset (pak, 0, sizeof (*pak));

	char fid[4];
	u32 prver, pwver;
	u64 psize;
	uint pbody;
	if (!read_form (data, size, 0, fid, &prver, &pwver, &psize, &pbody)
		|| memcmp (fid, "PACK", 4) || prver != 1)
		return ERR_NOTHING_TO_DO;

	char tid[4];
	u32 trver, twver;
	u64 tsize;
	uint tbody;
	if (!read_form (data, size, pbody, tid, &trver, &twver, &tsize, &tbody)
		|| memcmp (tid, "TOCC", 4) || trver != 3)
		return ERR_NOTHING_TO_DO;

	mpr_pack_entry_t *entries = 0;
	uint n_entries = 0, cap_entries = 0;
	mpr_pack_name_t *names = 0;
	uint n_names = 0, cap_names = 0;
	mpr_pack_meta_t *metas = 0;
	uint n_metas = 0, cap_metas = 0;
	// ERR_OK while scanning: the while() guard treats any error as "stop".
	// A pack with zero usable entries still declines via !n_entries below.
	enumError err = ERR_OK;

	uint pos = tbody;
	const uint tend = tbody + (uint)tsize;
	while (!err && pos < tend)
	{
		char cid[4];
		u64 csize;
		uint cbody;
		if (!read_chunk (data, size, pos, cid, &csize, &cbody) || csize > UINT_MAX
			|| (u64)cbody + csize > (u64)tbody + tsize)
		{
			err = ERR_NOTHING_TO_DO;
			break;
		}
		if (!memcmp (cid, "ADIR", 4))		{
			if (csize < 4)
			{
				err = ERR_NOTHING_TO_DO;
				break;
			}
			const uint count = rd_le32 (data + cbody);
			if (!count || count > MPR_PACK_MAX_ENTRIES
				|| (u64)count * 52 > csize - 4)
			{
				err = ERR_NOTHING_TO_DO;
				break;
			}
			for (uint i = 0; i < count; i++)
			{
				const u8 *e = data + cbody + 4 + (uint)i * 52;
				mpr_pack_entry_t ent;
				memcpy (ent.type, e, 4);
				memcpy (ent.guid, e + 4, 16);
				ent.version = rd_le32 (e + 20);
				ent.other_version = rd_le32 (e + 24);
				ent.offset = rd_le64 (e + 28);
				ent.decomp_size = rd_le64 (e + 36);
				ent.size = rd_le64 (e + 44);
				if (ent.offset > size || ent.size > size
					|| ent.offset + ent.size > size || ent.decomp_size > MPR_PACK_MAX_OUTPUT
					|| ent.size < 0x20 || ent.offset + ent.size < ent.offset)
				{
					err = ERR_NOTHING_TO_DO;
					break;
				}
				// Inner RFRM must agree with the directory, exactly like
				// the reference validator: id, both versions, and
				// decompressed size == form size + 32. Stored entries
				// check against their raw bytes here; compressed ones
				// only carry a mode word yet (validating them means
				// decompressing, which GetMPRPACKEntry() does and then
				// re-validates before handing the bytes out). Scanning
				// must stay cheap: no decompression happens here, so
				// multi-GB paks still probe fast.
				if (ent.size == ent.decomp_size)
				{
					char iid[4];
					u32 irver, iwver;
					u64 isize;
					if (!read_form (data, size, (uint)ent.offset, iid, &irver, &iwver, &isize,
							0)
						|| memcmp (iid, ent.type, 4) || irver != ent.version
						|| iwver != ent.other_version || isize + 32 != ent.decomp_size)
					{
						err = ERR_NOTHING_TO_DO;
						break;
					}
				}
				else
				{
					if (ent.size < 4)
					{
						err = ERR_NOTHING_TO_DO;
						break;
					}
					const u32 mode = rd_le32 (data + (uint)ent.offset);
					if (mode > 3
						|| (mode == 0 && ent.size - 4 != ent.decomp_size))
					{
						err = ERR_NOTHING_TO_DO;
						break;
					}
				}
				// Consecutive duplicate guids collapse (reference
				// read_sparse); the first record wins for naming.
				if (n_entries
					&& !memcmp (entries[n_entries - 1].guid, ent.guid, 16))
					continue;
				if (n_entries >= cap_entries)
				{
					uint ncap = cap_entries ? cap_entries * 2 : 64;
					mpr_pack_entry_t *grown = REALLOC (entries, ncap * sizeof (*grown));
					if (!grown)
					{
						err = ERR_CANT_CREATE;
						break;
					}
					entries = grown;
					cap_entries = ncap;
				}
				entries[n_entries++] = ent;
			}
		}
		else if (!memcmp (cid, "META", 4))
		{
			if (csize < 4)
			{
				err = ERR_NOTHING_TO_DO;
				break;
			}
			const uint count = rd_le32 (data + cbody);
			uint mpos = cbody + 4;
			for (uint i = 0; i < count; i++)
			{
				if ((u64)mpos + 20 > (u64)cbody + csize)
				{
					err = ERR_NOTHING_TO_DO;
					break;
				}
				mpr_pack_meta_t m;
				memcpy (m.guid, data + mpos, 16);
				const uint moff = rd_le32 (data + mpos + 16);
				if ((u64)moff + 4 > csize)
				{
					err = ERR_NOTHING_TO_DO;
					break;
				}
				const uint msize = rd_le32 (data + cbody + moff);
				if ((u64)moff + 4 + msize > csize)
				{
					err = ERR_NOTHING_TO_DO;
					break;
				}
				m.data = MALLOC (msize ? msize : 1);
				if (!m.data)
				{
					err = ERR_CANT_CREATE;
					break;
				}
				m.size = msize;
				if (msize)
					memcpy (m.data, data + cbody + moff + 4, msize);
				if (n_metas >= cap_metas)
				{
					uint ncap = cap_metas ? cap_metas * 2 : 16;
					mpr_pack_meta_t *grown = REALLOC (metas, ncap * sizeof (*grown));
					if (!grown)
					{
						FREE (m.data);
						err = ERR_CANT_CREATE;
						break;
					}
					metas = grown;
					cap_metas = ncap;
				}
				metas[n_metas++] = m;
				mpos += 20;
			}
		}
		else if (!memcmp (cid, "STRG", 4))
		{
			if (csize < 4)
			{
				err = ERR_NOTHING_TO_DO;
				break;
			}
			const uint count = rd_le32 (data + cbody);
			uint spos = cbody + 4;
			for (uint i = 0; i < count; i++)
			{
				if ((u64)spos + 24 > (u64)cbody + csize)
				{
					err = ERR_NOTHING_TO_DO;
					break;
				}
				const uint nlen = rd_le32 (data + spos + 20);
				if (nlen > MPR_PACK_MAX_OUTPUT
					|| (u64)spos + 24 + nlen > (u64)cbody + csize)
				{
					err = ERR_NOTHING_TO_DO;
					break;
				}
				mpr_pack_name_t nm;
				memcpy (nm.guid, data + spos + 4, 16);
				nm.name = MALLOC (nlen + 1);
				if (!nm.name)
				{
					err = ERR_CANT_CREATE;
					break;
				}
				if (nlen)
					memcpy (nm.name, data + spos + 24, nlen);
				nm.name[nlen] = 0;
				// No encoding check here: a hostile or odd name must not
				// fail the whole pak scan. The extractor only uses names
				// that pass valid_sarc_path() and falls back to the guid
				// filename otherwise; every NAME still lands in the FOOT.
				if (n_names >= cap_names)
				{
					uint ncap = cap_names ? cap_names * 2 : 16;
					mpr_pack_name_t *grown = REALLOC (names, ncap * sizeof (*grown));
					if (!grown)
					{
						FREE (nm.name);
						err = ERR_CANT_CREATE;
						break;
					}
					names = grown;
					cap_names = ncap;
				}
				names[n_names++] = nm;
				spos += 24 + nlen;
			}
		}
		else
		{
			// Unknown TOCC chunk: decline rather than guess, like the
			// reference bail on unhandled chunks.
			err = ERR_NOTHING_TO_DO;
			break;
		}
		pos = cbody + (uint)csize;
	}

	if (err || !n_entries)
	{
		FREE (entries);
		if (names)
		{
			for (uint i = 0; i < n_names; i++)
				FREE (names[i].name);
			FREE (names);
		}
		if (metas)
		{
			for (uint i = 0; i < n_metas; i++)
				FREE (metas[i].data);
			FREE (metas);
		}
		return err ? err : ERR_NOTHING_TO_DO;
	}
	pak->data = data;
	pak->size = size;
	pak->entries = entries;
	pak->n_entries = n_entries;
	pak->names = names;
	pak->n_names = n_names;
	pak->metas = metas;
	pak->n_metas = n_metas;
	return ERR_OK;
}

enumError GetMPRPACKEntry (u8 **dest, uint *dest_size, const mpr_pack_t *pak, uint index)
{
	if (!dest || !dest_size || !pak || index >= pak->n_entries)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	const mpr_pack_entry_t *e = pak->entries + index;
	if (e->offset > pak->size || e->size > pak->size - e->offset
		|| e->size > UINT_MAX || e->decomp_size > MPR_PACK_MAX_OUTPUT)
		return EINVAL;
	const uint ssize = (uint)e->size;
	const uint dsize = (uint)e->decomp_size;
	if (!ssize || !dsize)
		return EINVAL;
	u8 *out = 0;
	uint out_size = 0;
	enumError derr;
	if (ssize == dsize)
	{
		out = MALLOC (dsize);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, pak->data + e->offset, dsize);
		out_size = dsize;
		derr = ERR_OK;
	}
	else
		derr = DecodeMPR_LZSS (&out, &out_size, pak->data + e->offset, ssize, dsize);
	if (derr)
		return derr;
	// The scan could only check the mode word for compressed entries;
	// the decompressed bytes must still be the advertised resource.
	char iid[4];
	u32 irver, iwver;
	u64 isize;
	if (!read_form (out, out_size, 0, iid, &irver, &iwver, &isize, 0)
		|| memcmp (iid, e->type, 4) || irver != e->version || iwver != e->other_version
		|| isize + 32 != e->decomp_size)
	{
		FREE (out);
		return EINVAL;
	}
	*dest = out;
	*dest_size = out_size;
	return ERR_OK;
}

//--- FOOT writer ------------------------------------------------------
// Form and chunk descriptors are LE here (MPR), with zero unk/skip on
// the FOOT chunks, matching retrotool's own writer byte-for-byte.
// wr_le32 comes from lib-nintendo.h; the u64 form is local.

static void mpr_wr_le64 (u8 *p, u64 v)
{
	wr_le32 (p, (u32)v);
	wr_le32 (p + 4, (u32)(v >> 32));
}

enumError BuildMPRPACKFoot (u8 **dest, uint *dest_size, const mpr_pack_t *pak, uint index,
	uint comp_mode, u64 orig_offset)
{
	if (!dest || !dest_size || !pak || index >= pak->n_entries)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	const mpr_pack_entry_t *e = pak->entries + index;

	uint meta_size = 0;
	const u8 *meta = FindMPRPACKMeta (pak, e->guid, &meta_size);
	uint name_len = 0, n_names = 0;
	for (uint i = 0; i < pak->n_names; i++)
		if (guid_eq (pak->names[i].guid, e->guid))
		{
			name_len += (uint)strlen (pak->names[i].name);
			n_names++;
		}
	const u64 foot_body = (0x18 + 28) + (meta ? 0x18 + meta_size : 0)
		+ (u64)n_names * 0x18 + name_len;
	if (foot_body > MPR_PACK_MAX_OUTPUT)
		return EINVAL;
	const uint total = 0x20 + (uint)foot_body;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "RFRM", 4);
	mpr_wr_le64 (out + 4, foot_body);
	memcpy (out + 0x14, "FOOT", 4);
	wr_le32 (out + 0x18, 1);
	wr_le32 (out + 0x1c, 1);
	uint pos = 0x20;
	memcpy (out + pos, "AINF", 4);
	mpr_wr_le64 (out + pos + 4, 28);
	pos += 0x18;
	memcpy (out + pos, e->guid, 16);
	wr_le32 (out + pos + 16, comp_mode);
	mpr_wr_le64 (out + pos + 20, orig_offset);
	pos += 28;
	if (meta)
	{
		memcpy (out + pos, "META", 4);
		mpr_wr_le64 (out + pos + 4, meta_size);
		pos += 0x18;
		if (meta_size)
			memcpy (out + pos, meta, meta_size);
		pos += meta_size;
	}
	for (uint i = 0; i < pak->n_names; i++)
		if (guid_eq (pak->names[i].guid, e->guid))
		{
			const uint nl = (uint)strlen (pak->names[i].name);
			memcpy (out + pos, "NAME", 4);
			mpr_wr_le64 (out + pos + 4, nl);
			pos += 0x18;
			if (nl)
				memcpy (out + pos, pak->names[i].name, nl);
			pos += nl;
		}
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}
