// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Metroid Prime Remastered CMDL models (Switch) -> model_t.
//
// See lib-mpr-cmdl.h for the format summary. Structure reference:
// PrimeDecomp/retrotool lib/src/format/cmdl.rs (MIT/Apache-2.0,
// re-implemented here); GPU buffer compression is the same Retro LZSS
// modes 0-3 this tree already decodes for MPR PACK/TXTR
// (DecodeMPR_LZSS, same u32-LE mode word and exact-size mode 0).
//
// Validation philosophy matches the MPR PACK scanner: ScanMPRCMDL
// bounds-checks every table without decompressing anything, so big
// models probe fast; ParseMPRCMDL decompresses and additionally
// requires all positions finite and inside the HEAD AABB (a wrong
// layout drifts into garbage almost immediately), skipping meshes
// that fail rather than emitting wrong geometry.
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "types.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-mpr-pak.h"
#include "lib-model-glb.h"
#include "lib-mpr-cmdl.h"
#ifdef __cplusplus
}
#endif

#define MPR_CMDL_MAX_MESHES 65536
#define MPR_CMDL_MAX_VERTS (16u << 20)
#define MPR_CMDL_MAX_INDICES (64u << 20)
#define MPR_CMDL_MAX_BUFFERS 4096
#define MPR_CMDL_MAX_OUTPUT (512u << 20)

// Retrotool EVertexDataFormat ids -> byte size (0 = unsupported here).
static uint mpr_cmdl_format_size (uint fmt)
{
	switch (fmt)
	{
		case 0:
		case 1:
		case 2:
		case 3:
			return 1;
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
			return 2;
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
		case 22:
		case 23:
		case 24:
		case 25:
		case 26:
			return 4;
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:
		case 32:
		case 33:
		case 34:
			return 8;
		case 35:
		case 36:
		case 37:
			return 12;
		case 38:
		case 39:
		case 40:
			return 16;
		default:
			return 0;
	}
}

// Vertex component ids (retrotool EVertexComponent).
enum
{
	MC_POSITION = 0,
	MC_NORMAL = 1,
	MC_TEXCOORD0 = 5,
	MC_TEXCOORD1 = 6,
	MC_TEXCOORD2 = 7,
	MC_TEXCOORD3 = 8,
	MC_COLOR = 9,
	MC_BONE_INDICES = 10,
	MC_BONE_WEIGHTS = 11,
};

// Parsed-file view shared by Scan and Parse (all pointers into src).
typedef struct mpr_cmdl_t
{
	const u8 *src;
	uint size;
	bool skinned; // SMDL form (SKHD present); CMDL otherwise
	// chunk bodies
	const u8 *head;
	uint head_size;
	const u8 *mtrl;
	uint mtrl_size;
	uint mtrl_count;
	const u8 *mesh;
	uint mesh_size;
	uint mesh_count;
	const u8 *vbuf;
	uint vbuf_size;
	const u8 *ibuf;
	uint ibuf_size;
	const u8 *meta;
	uint meta_size;
	// SKHD bone count (SMDL only; 0 for CMDL). Verified corpus-wide:
	// every BoneIndices value is below it and every BoneWeights
	// row sums to ~1 (half or float quads).
	uint sk_bones;
	// HEAD AABB
	float bb_min[3];
	float bb_max[3];
} mpr_cmdl_t;

static bool mpr_cmdl_form (const u8 *d, uint size, uint off, char id[4], u32 *rver, u32 *wver,
	u64 *body_size, uint *body_off)
{
	if (!d || (u64)off + 0x20 > size || memcmp (d + off, "RFRM", 4))
		return false;
	const u64 fsize = rd_le64 (d + off + 4);
	if (fsize > size || (u64)off + 0x20 + fsize > size)
		return false;
	memcpy (id, d + off + 0x14, 4);
	if (rver)
		*rver = rd_le32 (d + off + 0x18);
	if (wver)
		*wver = rd_le32 (d + off + 0x1c);
	if (body_size)
		*body_size = fsize;
	if (body_off)
		*body_off = off + 0x20;
	return true;
}

static bool mpr_cmdl_chunk (const u8 *d, uint size, uint off, char id[4], u64 *body_size,
	uint *body_off)
{
	if (!d || (u64)off + 24 > size)
		return false;
	memcpy (id, d + off, 4);
	const u64 csize = rd_le64 (d + off + 4);
	const u64 skip = rd_le64 (d + off + 16);
	if (csize > size || skip > size || (u64)off + 24 + skip + csize > size)
		return false;
	if (body_size)
		*body_size = csize;
	if (body_off)
		*body_off = off + 24 + (uint)skip;
	return true;
}

static float mpr_cmdl_f32 (const u8 *p)
{
	float v;
	memcpy (&v, p, 4);
	return v;
}

static float mpr_cmdl_half (u16 h)
{
	const uint sign = h >> 15, exp = (h >> 10) & 31, mant = h & 1023;
	float v;
	if (!exp)
		v = mant / 16777216.0f;
	else if (exp == 31)
		v = mant ? 0.0f : 65504.0f;
	else
	{
		v = 1.0f + mant / 1024.0f;
		int e = (int)exp - 15;
		while (e > 0)
		{
			v *= 2;
			e--;
		}
		while (e < 0)
		{
			v *= .5f;
			e++;
		}
	}
	return sign ? -v : v;
}

// Walk the MESH chunk exactly (reference SMeshLoadInformation). Returns
// false on any bound violation or trailing garbage.
static bool mpr_cmdl_scan_mesh (const u8 *b, uint size, uint *mesh_count)
{
	if (size < 4)
		return false;
	const uint n = rd_le32 (b);
	if (n > MPR_CMDL_MAX_MESHES)
		return false;
	u64 pos = 4 + (u64)n * 16;
	if (pos > size)
		return false;
	pos += (n + 3) / 4 + (n + 7) / 8;
	if (pos + 4 > size)
		return false;
	const uint nshort = rd_le32 (b + pos);
	pos += 4;
	if (nshort > MPR_CMDL_MAX_MESHES * 4 || pos + (u64)nshort * 2 > size)
		return false;
	pos += (u64)nshort * 2;
	if (pos + 1 > size)
		return false;
	const uint nlods = b[pos];
	pos += 1;
	if (nlods > 64 || pos + (u64)nlods * 40 + 4 > size)
		return false;
	pos += (u64)nlods * 40;
	const uint has_rules = rd_le32 (b + pos);
	pos += 4;
	// Corpus values are 0 (ends here), 1 (one f32 rule per LOD follows)
	// and 2 (ends here, 3 files). The reference only reads rules for
	// exactly 1 and ignores anything else, so mirror that leniency.
	if (has_rules > 2)
		return false;
	if (has_rules == 1)
	{
		if (pos + (u64)nlods * 4 > size)
			return false;
		pos += (u64)nlods * 4;
	}
	if (pos != size)
		return false;
	if (mesh_count)
		*mesh_count = n;
	return true;
}

// Walk one VBUF entry; on success *next is the offset past it
// (num_buffers u8 included) and *nbufs is its buffer count.
static bool mpr_cmdl_scan_vbuf_entry (const u8 *b, uint size, uint off, uint *next, uint *nbufs,
	uint *nverts)
{
	if ((u64)off + 8 > size)
		return false;
	const uint vc = rd_le32 (b + off), cc = rd_le32 (b + off + 4);
	if (!vc || vc > MPR_CMDL_MAX_VERTS || cc > 64)
		return false;
	u64 p = (u64)off + 8;
	for (uint c = 0; c < cc; c++)
	{
		if (p + 20 > size)
			return false;
		const uint stride = rd_le32 (b + p + 8);
		const uint fmt = rd_le32 (b + p + 12);
		const uint comp = rd_le32 (b + p + 16);
		const uint fsz = mpr_cmdl_format_size (fmt);
		if (!fsz || comp > 28 || !stride || stride > 4096)
			return false;
		const uint at = rd_le32 (b + p + 4);
		if ((u64)at + fsz > stride)
			return false;
		p += 20;
	}
	if (p + 1 > size)
		return false;
	const uint nb = b[p];
	if (!nb || nb > 64)
		return false;
	if (next)
		*next = (uint)p + 1;
	if (nbufs)
		*nbufs = nb;
	if (nverts)
		*nverts = vc;
	return true;
}

static enumError mpr_cmdl_scan (mpr_cmdl_t *m, const u8 *data, uint size)
{
	if (!m || !data || size < 0x20)
		return EINVAL;
	memset (m, 0, sizeof (*m));
	m->src = data;
	m->size = size;

	char fid[4];
	u32 rver, wver;
	u64 fsize;
	uint fbody;
	if (!mpr_cmdl_form (data, size, 0, fid, &rver, &wver, &fsize, &fbody))
		return EINVAL;
	// Static CMDL (114/125) and skinned SMDL (127/133) share every
	// chunk but SKHD; bone transforms live outside either file (no
	// skeleton asset is known), so SMDL decodes unskinned here.
	if (!memcmp (fid, "CMDL", 4))
	{
		if (rver != 114 || wver != 125)
			return EINVAL;
		m->skinned = false;
	}
	else if (!memcmp (fid, "SMDL", 4))
	{
		if (rver != 127 || wver != 133)
			return EINVAL;
		m->skinned = true;
	}
	else
		return EINVAL;
	const uint fend = fbody + (uint)fsize;

	uint pos = fbody;
	bool have_skhd = false, have_head = false, have_mtrl = false, have_mesh = false,
		 have_vbuf = false, have_ibuf = false, have_gpu = false;
	while (pos < fend)
	{
		char cid[4];
		u64 csize;
		uint cbody;
		if (!mpr_cmdl_chunk (data, size, pos, cid, &csize, &cbody) || csize > UINT_MAX
			|| (u64)cbody + csize > fend)
			return EINVAL;
		if (!memcmp (cid, "HEAD", 4))
		{
			if (have_head || csize < 28 || csize > (16u << 20))
				return EINVAL;
			m->head = data + cbody;
			m->head_size = (uint)csize;
			have_head = true;
		}
		else if (!memcmp (cid, "SKHD", 4))
		{
			// Skeleton header: (0, bone_count, hash, ?, 1, 0, 0) u32s
			// plus a few trailing bytes on some files. Bone transforms
			// are not stored here, so only size and singularity gate;
			// the count itself is validated against the skinning
			// components at decode time.
			if (have_skhd || csize < 20 || csize > 4096)
				return EINVAL;
			m->sk_bones = rd_le32 (data + cbody + 4);
			if (m->sk_bones > 1000000)
				return EINVAL;
			have_skhd = true;
		}
		else if (!memcmp (cid, "MTRL", 4))
		{
			if (have_mtrl || csize < 8)
				return EINVAL;
			m->mtrl = data + cbody;
			m->mtrl_size = (uint)csize;
			m->mtrl_count = rd_le32 (data + cbody + 4);
			if (!m->mtrl_count || m->mtrl_count > 4096)
				return EINVAL;
			have_mtrl = true;
		}
		else if (!memcmp (cid, "MESH", 4))
		{
			if (have_mesh)
				return EINVAL;
			if (!mpr_cmdl_scan_mesh (data + cbody, (uint)csize, &m->mesh_count) || !m->mesh_count)
				return EINVAL;
			m->mesh = data + cbody;
			m->mesh_size = (uint)csize;
			have_mesh = true;
		}
		else if (!memcmp (cid, "VBUF", 4))
		{
			if (have_vbuf || csize < 4 || csize > (64u << 20))
				return EINVAL;
			m->vbuf = data + cbody;
			m->vbuf_size = (uint)csize;
			have_vbuf = true;
		}
		else if (!memcmp (cid, "IBUF", 4))
		{
			if (have_ibuf || csize < 4 || csize > (64u << 20))
				return EINVAL;
			const uint nic = rd_le32 (data + cbody);
			if (!nic || nic > 1024 || 4 + (u64)nic * 4 != csize)
				return EINVAL;
			for (uint i = 0; i < nic; i++)
				if (rd_le32 (data + cbody + 4 + i * 4) > 2)
					return EINVAL;
			m->ibuf = data + cbody;
			m->ibuf_size = (uint)csize;
			have_ibuf = true;
		}
		else if (!memcmp (cid, "GPU ", 4))
		{
			if (have_gpu || !csize)
				return EINVAL;
			have_gpu = true;
		}
		else
			return EINVAL;
		pos = cbody + (uint)csize;
	}
	if (!have_head || !have_mtrl || !have_mesh || !have_vbuf || !have_ibuf || !have_gpu)
		return EINVAL;
	if (m->skinned != have_skhd)
		return EINVAL;

	// VBUF entries must walk exactly; count their buffers.
	uint voff = 4;
	const uint nvent = rd_le32 (m->vbuf);
	if (!nvent || nvent > 4096)
		return EINVAL;
	u64 total_bufs = 0;
	for (uint e = 0; e < nvent; e++)
	{
		uint next = 0, nb = 0;
		if (!mpr_cmdl_scan_vbuf_entry (m->vbuf, m->vbuf_size, voff, &next, &nb, 0))
			return EINVAL;
		voff = next;
		total_bufs += nb;
	}
	if (voff != m->vbuf_size || !total_bufs || total_bufs > MPR_CMDL_MAX_BUFFERS)
		return EINVAL;

	// Trailing FOOT form holds AINF + META.
	uint fpos = fend;
	char ffid[4];
	u64 footsize;
	uint footbody;
	if (!mpr_cmdl_form (data, size, fpos, ffid, 0, 0, &footsize, &footbody)
		|| memcmp (ffid, "FOOT", 4))
		return EINVAL;
	const uint footend = footbody + (uint)footsize;
	uint cpos = footbody;
	bool have_meta = false;
	while (cpos < footend)
	{
		char cid[4];
		u64 csize;
		uint cbody;
		if (!mpr_cmdl_chunk (data, size, cpos, cid, &csize, &cbody) || csize > UINT_MAX
			|| (u64)cbody + csize > footend)
			return EINVAL;
		if (!memcmp (cid, "META", 4))
		{
			if (have_meta)
				return EINVAL;
			m->meta = data + cbody;
			m->meta_size = (uint)csize;
			have_meta = true;
		}
		cpos = cbody + (uint)csize;
	}
	if (!have_meta || m->meta_size < 16)
		return EINVAL;

	// META: unk, gpu_off, nread, reads[{size,off}8B], nvtx,
	// vbufs[{read,off,size,dest}16B], nidx, ibufs[same].
	const u8 *meta = m->meta;
	const uint msize = m->meta_size;
	const uint nread = rd_le32 (meta + 8);
	if (!nread || nread > MPR_CMDL_MAX_BUFFERS || 12 + (u64)nread * 8 + 4 > msize)
		return EINVAL;
	uint mp = 12 + nread * 8;
	for (uint i = 0; i < nread; i++)
	{
		const uint rsize = rd_le32 (meta + 12 + i * 8);
		const uint roff = rd_le32 (meta + 12 + i * 8 + 4);
		if (!rsize || (u64)roff + rsize > size)
			return EINVAL;
	}
	const uint nvtx = rd_le32 (meta + mp);
	mp += 4;
	if (!nvtx || nvtx > MPR_CMDL_MAX_BUFFERS || (u64)nvtx != total_bufs)
		return EINVAL;
	if ((u64)mp + (u64)nvtx * 16 + 4 > msize)
		return EINVAL;
	for (uint i = 0; i < nvtx; i++)
	{
		const uint ri = rd_le32 (meta + mp + i * 16);
		const uint boff = rd_le32 (meta + mp + i * 16 + 4);
		const uint bsize = rd_le32 (meta + mp + i * 16 + 8);
		const uint bdst = rd_le32 (meta + mp + i * 16 + 12);
		if (ri >= nread || !bsize || !bdst || bdst > MPR_CMDL_MAX_OUTPUT)
			return EINVAL;
		const uint rsize = rd_le32 (meta + 12 + ri * 8);
		if ((u64)boff + bsize > rsize)
			return EINVAL;
	}
	mp += nvtx * 16;
	const uint nidx = rd_le32 (meta + mp);
	mp += 4;
	if (!nidx || nidx > MPR_CMDL_MAX_BUFFERS || (u64)mp + (u64)nidx * 16 > msize)
		return EINVAL;
	for (uint i = 0; i < nidx; i++)
	{
		const uint ri = rd_le32 (meta + mp + i * 16);
		const uint boff = rd_le32 (meta + mp + i * 16 + 4);
		const uint bsize = rd_le32 (meta + mp + i * 16 + 8);
		const uint bdst = rd_le32 (meta + mp + i * 16 + 12);
		if (ri >= nread || !bsize || !bdst || bdst > MPR_CMDL_MAX_OUTPUT)
			return EINVAL;
		const uint rsize = rd_le32 (meta + 12 + ri * 8);
		if ((u64)boff + bsize > rsize)
			return EINVAL;
	}
	mp += nidx * 16;
	if (mp != msize)
		return EINVAL;

	// HEAD AABB for the decode plausibility check.
	for (uint c = 0; c < 3; c++)
	{
		m->bb_min[c] = mpr_cmdl_f32 (m->head + 4 + c * 4);
		m->bb_max[c] = mpr_cmdl_f32 (m->head + 16 + c * 4);
		if (!(m->bb_min[c] <= m->bb_max[c]))
			return EINVAL;
	}
	return ERR_OK;
}

bool IsMPRCMDL (const u8 *data, uint size)
{
	if (!data || size < 0x20 + 24 + 28)
		return false;
	mpr_cmdl_t m;
	return mpr_cmdl_scan (&m, data, size) == ERR_OK;
}

// One META buffer entry -> freshly decompressed bytes. Mode word u32 LE:
// 0 = stored, 1-3 = Retro LZSS (same dispatch as DecodeMPR_LZSS, but a
// short stored span is also accepted: the reference borrows the slice).
static enumError mpr_cmdl_decomp (u8 **dest, uint *dest_size, const u8 *src, uint src_size,
	uint want)
{
	if (!dest || !dest_size || !src || !want || want > MPR_CMDL_MAX_OUTPUT)
		return EINVAL;
	if (src_size < 4)
		return EINVAL;
	if (!rd_le32 (src))
	{
		if ((u64)src_size - 4 < want)
			return EINVAL;
		u8 *out = MALLOC (want);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, src + 4, want);
		*dest = out;
		*dest_size = want;
		return ERR_OK;
	}
	return DecodeMPR_LZSS (dest, dest_size, src, src_size, want);
}

typedef struct mpr_cmdl_attr_t
{
	const u8 *data; // interleaved vertex data for this attribute's buffer
	uint stride;
	uint format;
} mpr_cmdl_attr_t;

// Decode one attribute element to out[4] floats; returns false when the
// (format, component) combination is not supported here.
static bool mpr_cmdl_read_attr (float out[4], const u8 *p, uint fmt, uint comp)
{
	out[0] = out[1] = out[2] = out[3] = 0.0f;
	switch (comp)
	{
		case MC_POSITION:
			if (fmt == 37 || fmt == 40)
			{
				out[0] = mpr_cmdl_f32 (p);
				out[1] = mpr_cmdl_f32 (p + 4);
				out[2] = mpr_cmdl_f32 (p + 8);
				return true;
			}
			if (fmt == 34 || fmt == 20)
			{
				// Half floats (xyzw or xy); official meshes quantize
				// positions this way, z=0 for the 2-component form.
				out[0] = mpr_cmdl_half (rd_le16 (p));
				out[1] = mpr_cmdl_half (rd_le16 (p + 2));
				out[2] = fmt == 34 ? mpr_cmdl_half (rd_le16 (p + 4)) : 0.0f;
				return true;
			}
			return false;
		case MC_NORMAL:
			if (fmt == 37)
			{
				out[0] = mpr_cmdl_f32 (p);
				out[1] = mpr_cmdl_f32 (p + 4);
				out[2] = mpr_cmdl_f32 (p + 8);
				return true;
			}
			if (fmt == 34)
			{
				out[0] = mpr_cmdl_half (rd_le16 (p));
				out[1] = mpr_cmdl_half (rd_le16 (p + 2));
				out[2] = mpr_cmdl_half (rd_le16 (p + 4));
				return true;
			}
			return false;
		case MC_TEXCOORD0:
		case MC_TEXCOORD1:
		case MC_TEXCOORD2:
		case MC_TEXCOORD3:
			if (fmt == 34 || fmt == 20)
			{
				out[0] = mpr_cmdl_half (rd_le16 (p));
				out[1] = mpr_cmdl_half (rd_le16 (p + 2));
				return true;
			}
			if (fmt == 37 || fmt == 40 || fmt == 29)
			{
				out[0] = mpr_cmdl_f32 (p);
				out[1] = mpr_cmdl_f32 (p + 4);
				return true;
			}
			return false;
		case MC_COLOR:
			if (fmt == 21)
			{
				out[0] = p[0] / 255.0f;
				out[1] = p[1] / 255.0f;
				out[2] = p[2] / 255.0f;
				out[3] = p[3] / 255.0f;
				return true;
			}
			if (fmt == 34)
			{
				for (uint c = 0; c < 4; c++)
				{
					float v = mpr_cmdl_half (rd_le16 (p + 2 * c));
					out[c] = v < 0 ? 0 : v > 1 ? 1 : v;
				}
				return true;
			}
			return false;
		default:
			return false;
	}
}

static bool mpr_cmdl_finite3 (const float v[4])
{
	for (uint c = 0; c < 3; c++)
		if (!(v[c] > -1e9f && v[c] < 1e9f))
			return false;
	return true;
}

// MTRL data-item inner sizes by FourCC type. Returns 0 for unknown.
// TXTR/CPLX-layered texture tokens carry a LE uuid plus a 20-byte usage
// record unless the uuid is nil; CPLX is only ever the layered form
// (BCRL/MTLL/NRML) in the verified corpus.
static bool mpr_cmdl_data_skip (const u8 *d, uint size, uint *pos, const u8 ty[4])
{
	uint p = *pos;
	if (!memcmp (ty, "TXTR", 4))
	{
		if ((u64)p + 16 > size)
			return false;
		bool nil = true;
		for (uint i = 0; i < 16; i++)
			if (d[p + i])
			{
				nil = false;
				break;
			}
		p += nil ? 16 : 36;
	}
	else if (!memcmp (ty, "COLR", 4) || !memcmp (ty, "INT4", 4))
		p += 16;
	else if (!memcmp (ty, "SCLR", 4) || !memcmp (ty, "INT1", 4))
		p += 4;
	else if (!memcmp (ty, "MAT4", 4))
		p += 64;
	else if (!memcmp (ty, "CPLX", 4))
	{
		if ((u64)p + 53 > size)
			return false;
		p += 53;
		for (uint k = 0; k < 3; k++)
		{
			if ((u64)p + 16 > size)
				return false;
			bool nil = true;
			for (uint i = 0; i < 16; i++)
				if (d[p + i])
				{
					nil = false;
					break;
				}
			p += nil ? 16 : 36;
		}
	}
	else
		return false;
	if (p > size)
		return false;
	*pos = p;
	return true;
}

// Extract MTRL material names (CMaterialCache walk per retrotool's
// cmdl.rs: name, shader/guid, type FourCCs, render types, (id,type)
// pairs, then (id,type,inner) triples). Any anomaly aborts with
// false; the caller then exports unnamed materials instead of
// failing the file.
static bool mpr_cmdl_materials (const mpr_cmdl_t *m, model_t *model)
{
	const u8 *d = m->mtrl;
	const uint size = m->mtrl_size;
	uint p = 8;
	model->materials = CALLOC (m->mtrl_count, sizeof (*model->materials));
	if (!model->materials)
		return false;
	for (uint i = 0; i < m->mtrl_count; i++)
	{
		if ((u64)p + 4 > size)
			goto bad;
		const uint nl = rd_le32 (d + p);
		p += 4;
		if (!nl || nl > 256 || (u64)p + nl + 40 > size)
			goto bad;
		for (uint k = 0; k < nl; k++)
			if (d[p + k] < 32 || d[p + k] >= 127)
				goto bad;
		snprintf (model->materials[i].name, sizeof (model->materials[i].name), "%.*s", nl,
			d + p);
		p += nl + 16 + 16 + 4 + 4;
		if ((u64)p + 4 > size)
			goto bad;
		const uint nt = rd_le32 (d + p);
		p += 4;
		if (nt > 1024 || (u64)p + (u64)nt * 4 > size)
			goto bad;
		p += nt * 4;
		if ((u64)p + 4 > size)
			goto bad;
		const uint nrt = rd_le32 (d + p);
		p += 4;
		if (nrt > 1024 || (u64)p + (u64)nrt * 10 > size)
			goto bad;
		p += nrt * 10;
		if ((u64)p + 4 > size)
			goto bad;
		const uint nd = rd_le32 (d + p);
		p += 4;
		if (nd > 4096 || (u64)p + (u64)nd * 8 > size)
			goto bad;
		// Triples repeat their (id,type) header verbatim from the
		// pairs array; each header is verified as it is walked.
		// The first diffuse (DIFT) texture uuid becomes the material's
		// texture name: the deferred PNG index resolves "<uuid>.TXTR.png"
		// to embedded image bytes when the texture was extracted beside
		// the model (PACK cascade), else it stays an external URI.
		const u8 *pairs = d + p;
		p += nd * 8;
		bool have_dift = false;
		for (uint t = 0; t < nd; t++)
		{
			u8 ty[4];
			if ((u64)p + 8 > size || memcmp (d + p, pairs + (size_t)t * 8, 8))
				goto bad;
			memcpy (ty, d + p + 4, 4);
			if (!have_dift && !memcmp (d + p, "DIFT", 4) && !memcmp (ty, "TXTR", 4)
				&& (u64)p + 8 + 16 <= size)
			{
				bool nil = true;
				for (uint k = 0; k < 16; k++)
					if (d[p + 8 + k])
					{
						nil = false;
						break;
					}
				if (!nil)
				{
					char guid[37];
					FormatMPRGUID (guid, d + p + 8);
					snprintf (model->materials[i].textures[0],
						sizeof (model->materials[i].textures[0]), "%s.TXTR.png", guid);
					model->materials[i].num_textures = 1;
					model->materials[i].texture_coord[0] = 0;
					model->materials[i].wrap_s[0] = model->materials[i].wrap_t[0] = 1;
					model->materials[i].min_filter[0] = model->materials[i].mag_filter[0] = 1;
					have_dift = true;
				}
			}
			p += 8;
			if (!mpr_cmdl_data_skip (d, size, &p, ty))
				goto bad;
		}
	}
	if (p != size)
		goto bad;
	model->num_materials = m->mtrl_count;
	return true;

bad:
	FREE (model->materials);
	model->materials = 0;
	return false;
}

model_t *ParseMPRCMDL (const u8 *data, size_t size)
{
	if (!data || !size || size > UINT_MAX)
		return 0;
	mpr_cmdl_t m;
	if (mpr_cmdl_scan (&m, data, (uint)size))
		return 0;

	const u8 *meta = m.meta;
	const uint nread = rd_le32 (meta + 8);
	uint mp = 12 + nread * 8;
	const uint nvtx = rd_le32 (meta + mp);
	mp += 4;
	const u8 *vbuf_tab = meta + mp;
	mp += nvtx * 16;
	const uint nidx = rd_le32 (meta + mp);
	mp += 4;
	const u8 *ibuf_tab = meta + mp;

	// Decompress all META buffers. All pointers stay NULL until
	// owned, so every error path below can share fail_meshes.
	u8 **vbufs = CALLOC (nvtx, sizeof (*vbufs));
	u8 **ibufs = CALLOC (nidx, sizeof (*ibufs));
	uint *vbuf_sz = CALLOC (nvtx, sizeof (*vbuf_sz));
	uint *ibuf_sz = CALLOC (nidx, sizeof (*ibuf_sz));
	uint *ibuf_elsz = 0;
	uint *entry_first = 0, *entry_nbuf = 0;
	model_t *model = 0;
	if (!vbufs || !ibufs || !vbuf_sz || !ibuf_sz)
		goto fail_meshes;
	for (uint i = 0; i < nvtx; i++)
	{
		const uint ri = rd_le32 (vbuf_tab + i * 16);
		const uint boff = rd_le32 (vbuf_tab + i * 16 + 4);
		const uint bsize = rd_le32 (vbuf_tab + i * 16 + 8);
		const uint bdst = rd_le32 (vbuf_tab + i * 16 + 12);
		const uint roff = rd_le32 (meta + 12 + ri * 8 + 4);
		uint got = 0;
		if ((u64)roff + boff + bsize > size
			|| mpr_cmdl_decomp (&vbufs[i], &got, data + roff + boff, bsize, bdst)
			|| got != bdst)
		{
			FREE (vbufs[i]);
			vbufs[i] = 0;
			goto fail_meshes;
		}
		vbuf_sz[i] = got;
	}
	for (uint i = 0; i < nidx; i++)
	{
		const uint ri = rd_le32 (ibuf_tab + i * 16);
		const uint boff = rd_le32 (ibuf_tab + i * 16 + 4);
		const uint bsize = rd_le32 (ibuf_tab + i * 16 + 8);
		const uint bdst = rd_le32 (ibuf_tab + i * 16 + 12);
		const uint roff = rd_le32 (meta + 12 + ri * 8 + 4);
		uint got = 0;
		if ((u64)roff + boff + bsize > size
			|| mpr_cmdl_decomp (&ibufs[i], &got, data + roff + boff, bsize, bdst)
			|| got != bdst)
		{
			FREE (ibufs[i]);
			ibufs[i] = 0;
			goto fail_meshes;
		}
		ibuf_sz[i] = got;
	}

	// Index element sizes from the IBUF section.
	const uint nic = rd_le32 (m.ibuf);
	ibuf_elsz = CALLOC (nic > nidx ? nic : nidx, sizeof (*ibuf_elsz));
	if (!ibuf_elsz)
		goto fail_meshes;
	for (uint i = 0, n = nic > nidx ? nic : nidx; i < n; i++)
	{
		uint t = 1;
		if (i < nic)
			t = rd_le32 (m.ibuf + 4 + i * 4);
		ibuf_elsz[i] = t == 0 ? 1 : t == 1 ? 2 : 4;
	}

	model = CALLOC (1, sizeof (*model));
	if (!model)
		goto fail_meshes;
	model->meshes = CALLOC (m.mesh_count, sizeof (*model->meshes));
	if (!model->meshes)
	{
		FREE (model);
		model = 0;
		goto fail_meshes;
	}

	// Material names (best effort: any walk anomaly exports unnamed
	// materials instead of failing the file).
	const bool have_materials = mpr_cmdl_materials (&m, model);

	// AABB with a small tolerance for quantization noise.
	float tol = 1.0f;
	{
		float diag = 0;
		for (uint c = 0; c < 3; c++)
		{
			float d = m.bb_max[c] - m.bb_min[c];
			diag += d * d;
		}
		if (diag > 1.0f)
			tol = diag / 100.0f + 1.0f;
	}

	// META vbuf cursor: VBUF entries own consecutive buffers.
	uint nvent = rd_le32 (m.vbuf);
	if (!nvent || nvent > 4096)
		goto fail_meshes;
	entry_first = CALLOC (nvent, sizeof (*entry_first));
	entry_nbuf = CALLOC (nvent, sizeof (*entry_nbuf));
	if (!entry_first || !entry_nbuf)
		goto fail_meshes;
	{
		uint voff = 4, cursor = 0;
		for (uint e = 0; e < nvent; e++)
		{
			uint next = 0, nb = 0;
			if (!mpr_cmdl_scan_vbuf_entry (m.vbuf, m.vbuf_size, voff, &next, &nb, 0)
				|| cursor + nb > nvtx)
				goto fail_meshes;
			entry_first[e] = cursor;
			entry_nbuf[e] = nb;
			cursor += nb;
			voff = next;
		}
	}

	for (uint mi = 0; mi < m.mesh_count; mi++)
	{
		const u8 *me = m.mesh + 4 + mi * 16;
		const uint mat = rd_le16 (me);
		const uint vei = me[2], iei = me[3];
		const uint istart = rd_le32 (me + 4), icount = rd_le32 (me + 8);
		if (mat >= m.mtrl_count || vei >= nvent || iei >= nic || iei >= nidx)
			continue;
		if (!icount || icount % 3 || icount > MPR_CMDL_MAX_INDICES)
			continue;
		const uint elsz = ibuf_elsz[iei];
		const uint nidx_elem = ibuf_sz[iei] / elsz;
		if ((u64)istart + icount > nidx_elem)
			continue;

		// Locate this VBUF entry's components.
		uint voff = 4;
		for (uint e = 0; e < vei; e++)
		{
			uint next = 0;
			mpr_cmdl_scan_vbuf_entry (m.vbuf, m.vbuf_size, voff, &next, 0, 0);
			voff = next;
		}
		const uint vc = rd_le32 (m.vbuf + voff), cc = rd_le32 (m.vbuf + voff + 4);
		if (!vc || vc > MPR_CMDL_MAX_VERTS || cc > 64)
			continue;
		const uint first = entry_first[vei];
		mpr_cmdl_attr_t pos = { 0, 0, 0 }, nor = { 0, 0, 0 }, uv = { 0, 0, 0 },
						  col = { 0, 0, 0 }, skb = { 0, 0, 0 }, skw = { 0, 0, 0 };
		uint uv_comp = 0;
		bool bad = false;
		for (uint c = 0; c < cc && !bad; c++)
		{
			const u8 *cp = m.vbuf + voff + 8 + c * 20;
			const uint bi = rd_le32 (cp), at = rd_le32 (cp + 4), stride = rd_le32 (cp + 8),
					   fmt = rd_le32 (cp + 12), comp = rd_le32 (cp + 16);
			const uint fsz = mpr_cmdl_format_size (fmt);
			if (bi >= entry_nbuf[vei] || !fsz || (u64)at + fsz > stride)
			{
				bad = true;
				break;
			}
			const uint gi = first + bi;
			if ((u64)at + (u64)(vc - 1) * stride + fsz > vbuf_sz[gi])
			{
				bad = true;
				break;
			}
			mpr_cmdl_attr_t a = { vbufs[gi] + at, stride, fmt };
			if (comp == MC_POSITION && !pos.data)
				pos = a;
			else if (comp == MC_NORMAL && !nor.data)
				nor = a;
			else if (comp >= MC_TEXCOORD0 && comp <= MC_TEXCOORD3 && !uv.data)
			{
				uv = a;
				uv_comp = comp;
			}
			else if (comp == MC_COLOR && !col.data)
				col = a;
			else if (comp == MC_BONE_INDICES && !skb.data)
			{
				// u8x4 bone indices (the only encoding seen corpus-wide).
				if (fmt != 22)
				{
					bad = true;
					break;
				}
				skb = a;
			}
			else if (comp == MC_BONE_WEIGHTS && !skw.data)
			{
				// Half-x4 or float-x4 weights (both seen corpus-wide).
				if (fmt != 34 && fmt != 40)
				{
					bad = true;
					break;
				}
				skw = a;
			}
		}
		if (bad || !pos.data)
			continue;
		if (!!skb.data != !!skw.data)
			continue;
		float probe[4];
		if (!mpr_cmdl_read_attr (probe, pos.data, pos.format, MC_POSITION))
			continue;
		// Skinning validates but does not export (no skeleton oracle):
		// every index must address a real SKHD bone and every weight
		// row must be finite and sum to ~=1 (zero rows = unbound).
		if (skb.data)
		{
			bool skin_ok = true;
			for (uint v = 0; v < vc && skin_ok; v++)
			{
				const u8 *bp = skb.data + (size_t)v * skb.stride;
				const u8 *wp = skw.data + (size_t)v * skw.stride;
				float sum = 0;
				for (uint k = 0; k < 4; k++)
				{
					if (bp[k] >= m.sk_bones)
					{
						skin_ok = false;
						break;
					}
					float w = skw.format == 40 ? mpr_cmdl_f32 (wp + 4 * k)
											   : mpr_cmdl_half (rd_le16 (wp + 2 * k));
					if (!(w >= 0.0f && w <= 1.05f))
					{
						skin_ok = false;
						break;
					}
					sum += w;
				}
				if (sum > 1.1f)
					skin_ok = false;
			}
			if (!skin_ok)
				continue;
		}

		mesh_t *mesh = model->meshes + model->num_meshes;
		snprintf (mesh->name, sizeof (mesh->name), "mesh%u_mat%u", mi, mat);
		mesh->material_idx = have_materials ? (int)mat : -1;
		mesh->positions = CALLOC (vc, sizeof (*mesh->positions));
		if (!mesh->positions)
			continue;
		bool have_nor = nor.data != 0, have_uv = uv.data != 0, have_col = col.data != 0;
		if (have_nor)
		{
			mesh->normals = CALLOC (vc, sizeof (*mesh->normals));
			if (!mesh->normals)
				have_nor = false;
		}
		if (have_uv)
		{
			mesh->texcoords = CALLOC (vc, sizeof (*mesh->texcoords));
			if (!mesh->texcoords)
				have_uv = false;
		}
		if (have_col)
		{
			mesh->colors[0] = CALLOC (vc, sizeof (*mesh->colors[0]));
			mesh->num_colors[0] = mesh->colors[0] ? vc : 0;
			if (!mesh->colors[0])
				have_col = false;
		}
		bool ok = true;
		for (uint v = 0; v < vc && ok; v++)
		{
			float p[4];
			if (!mpr_cmdl_read_attr (p, pos.data + (size_t)v * pos.stride, pos.format,
					MC_POSITION)
				|| !mpr_cmdl_finite3 (p))
			{
				ok = false;
				break;
			}
			for (uint c = 0; c < 3; c++)
				if (p[c] < m.bb_min[c] - tol || p[c] > m.bb_max[c] + tol)
				{
					ok = false;
					break;
				}
			if (!ok)
				break;
			mesh->positions[v].x = p[0];
			mesh->positions[v].y = p[1];
			mesh->positions[v].z = p[2];
			if (have_nor)
			{
				float q[4];
				if (!mpr_cmdl_read_attr (q, nor.data + (size_t)v * nor.stride, nor.format,
						MC_NORMAL))
					have_nor = false;
				else
				{
					mesh->normals[v].x = q[0];
					mesh->normals[v].y = q[1];
					mesh->normals[v].z = q[2];
				}
			}
			if (have_uv)
			{
				float q[4];
				if (!mpr_cmdl_read_attr (q, uv.data + (size_t)v * uv.stride, uv.format,
						uv_comp))
					have_uv = false;
				else
				{
					mesh->texcoords[v].u = q[0];
					mesh->texcoords[v].v = q[1];
				}
			}
			if (have_col)
			{
				float q[4];
				if (!mpr_cmdl_read_attr (q, col.data + (size_t)v * col.stride, col.format,
						MC_COLOR))
					have_col = false;
				else
				{
					mesh->colors[0][v].r = q[0];
					mesh->colors[0][v].g = q[1];
					mesh->colors[0][v].b = q[2];
					mesh->colors[0][v].a = q[3];
				}
			}
		}
		if (!ok)
		{
			FREE (mesh->positions);
			mesh->positions = 0;
			FREE (mesh->normals);
			mesh->normals = 0;
			FREE (mesh->texcoords);
			mesh->texcoords = 0;
			FREE (mesh->colors[0]);
			mesh->colors[0] = 0;
			mesh->num_colors[0] = 0;
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->num_positions = vc;
		if (mesh->normals)
			mesh->num_normals = vc;
		else
			have_nor = false;
		if (mesh->texcoords)
			mesh->num_texcoords = vc;
		else
			have_uv = false;

		vertex_t *verts = CALLOC (icount, sizeof (*verts));
		if (!verts)
		{
			FREE (mesh->positions);
			mesh->positions = 0;
			FREE (mesh->normals);
			mesh->normals = 0;
			FREE (mesh->texcoords);
			mesh->texcoords = 0;
			FREE (mesh->colors[0]);
			mesh->colors[0] = 0;
			mesh->num_colors[0] = 0;
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		size_t nv = 0;
		const u8 *idx = ibufs[iei] + (size_t)istart * elsz;
		for (uint k = 0; k < icount; k++)
		{
			uint vi = elsz == 1 ? idx[k] : elsz == 2 ? rd_le16 (idx + (size_t)k * 2)
													 : rd_le32 (idx + (size_t)k * 4);
			if (vi >= vc)
			{
				nv = 0;
				break;
			}
			verts[nv].position_idx = (int)vi;
			verts[nv].normal_idx = mesh->normals ? (int)vi : -1;
			verts[nv].tangent_idx = -1;
			verts[nv].texcoord_idx = mesh->texcoords ? (int)vi : -1;
			verts[nv].matrix_idx = -1;
			verts[nv].color_idx[0] = mesh->colors[0] ? (int)vi : -1;
			verts[nv].color_idx[1] = -1;
			nv++;
		}
		if (!nv)
		{
			FREE (verts);
			FREE (mesh->positions);
			mesh->positions = 0;
			FREE (mesh->normals);
			mesh->normals = 0;
			FREE (mesh->texcoords);
			mesh->texcoords = 0;
			FREE (mesh->colors[0]);
			mesh->colors[0] = 0;
			mesh->num_colors[0] = 0;
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->vertices = verts;
		mesh->num_vertices = nv;
		model->num_meshes++;
	}

	// Scratch is freed on exactly one of the two paths below.
	if (!model || !model->num_meshes)
		goto fail_meshes;
	FREE (entry_first);
	FREE (entry_nbuf);
	FREE (ibuf_elsz);
	for (uint i = 0; i < nvtx; i++)
		FREE (vbufs[i]);
	for (uint i = 0; i < nidx; i++)
		FREE (ibufs[i]);
	FREE (vbufs);
	FREE (ibufs);
	FREE (vbuf_sz);
	FREE (ibuf_sz);
	return model;

fail_meshes:
	FREE (entry_first);
	FREE (entry_nbuf);
	FreeModel (model);
	for (uint i = 0; i < nvtx; i++)
		FREE (vbufs[i]);
	for (uint i = 0; i < nidx; i++)
		FREE (ibufs[i]);
	FREE (vbufs);
	FREE (ibufs);
	FREE (vbuf_sz);
	FREE (ibuf_sz);
	FREE (ibuf_elsz);
	return 0;
}
