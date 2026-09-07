// Nintendo VFF ("VFF ") -- a PrFILE2 (eSOL) Virtual FAT volume. Wii channels
// and save data keep a small filesystem inside one of these rather than a
// directory tree, so the contents are invisible until the volume is walked.
//
// The wrapper is a 0x20-byte big-endian header; everything below it is an
// ordinary little-endian FAT12/FAT16 volume with the boot sector left out,
// which is why a normal FAT tool cannot open one:
//
//   0x00  char[4]  "VFF "
//   0x04  u16      byte-order mark: 0xfeff big, 0xfffe little
//   0x06  u16      unknown
//   0x08  u32      volume size in bytes
//   0x0c  u16      cluster size in units of 16 bytes
//   0x0e..0x1f     padding
//
// Then two copies of the FAT, each padded up to a cluster boundary, then a
// fixed 0x1000-byte root directory, then the clusters themselves. Cluster
// numbering starts at 2, so cluster N begins at data_offset + (N-2)*cluster.
// FAT12 is chosen at or below 0xff5 clusters and FAT16 above it; there is no
// FAT32 here, and a volume claiming more than 0xfff5 clusters is refused.
//
// Layout follows marcan's vffdump.py and the wii-tools/vffmod header
// definition. Verified against a volume whose filesystem was built by mtools
// rather than by this code: a subdirectory and a 5000-byte file spanning ten
// clusters come back with the same SHA-1s they went in with.

#include "lib-vff.h"
#include "lib-std.h"
#include <string.h>
#include <stdio.h>

#define VFF_HEADER_SIZE 0x20
#define VFF_ROOT_SIZE 0x1000
#define VFF_DIR_ENTRY 32
#define VFF_MAX_CLUSTERS 0xfff5
#define VFF_FAT12_LIMIT 0xff5
#define VFF_MAX_DEPTH 16

typedef struct vff_t
{
	const u8 *data;
	uint size;
	uint cluster_size;
	uint cluster_count;
	uint fat_bits; // 12 or 16
	const u8 *fat;
	uint fat_size;
	const u8 *root;
	uint data_off;

} vff_t;

static u16 vff_rd_be16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}

static u32 vff_rd_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u16 vff_rd_le16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}

static u32 vff_rd_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static bool vff_open (vff_t *v, const u8 *data, uint size)
{
	if (!data || size <= VFF_HEADER_SIZE || memcmp (data, "VFF ", 4))
		return false;

	const u16 bom = vff_rd_be16 (data + 4);
	if (bom != 0xfeff && bom != 0xfffe)
		return false;

	const u32 volume_size = vff_rd_be32 (data + 8);
	const uint cluster_size = (uint)vff_rd_be16 (data + 12) * 16;
	if (!cluster_size || (cluster_size & (cluster_size - 1)) || cluster_size > 0x10000)
		return false;
	if (!volume_size || volume_size % cluster_size)
		return false;

	const uint cluster_count = volume_size / cluster_size;
	if (cluster_count < 2 || cluster_count > VFF_MAX_CLUSTERS)
		return false;

	const uint fat_bits = cluster_count > VFF_FAT12_LIMIT ? 16 : 12;
	const uint raw_fat = fat_bits == 16 ? cluster_count * 2 : ((cluster_count + 1) / 2) * 3;
	const uint fat_size = (raw_fat + cluster_size - 1) & ~(cluster_size - 1);

	// Header, both FAT copies and the root directory must fit, and there has
	// to be at least one cluster of data after them.
	const u64 head = (u64)VFF_HEADER_SIZE + 2 * (u64)fat_size + VFF_ROOT_SIZE;
	if (head + cluster_size > size)
		return false;

	v->data = data;
	v->size = size;
	v->cluster_size = cluster_size;
	v->cluster_count = cluster_count;
	v->fat_bits = fat_bits;
	v->fat = data + VFF_HEADER_SIZE;
	v->fat_size = fat_size;
	v->root = data + VFF_HEADER_SIZE + 2 * fat_size;
	v->data_off = (uint)head;
	return true;
}

// FAT entries are little-endian regardless of the header's byte-order mark:
// the volume below the wrapper is an ordinary FAT image.
static uint vff_fat (const vff_t *v, uint idx)
{
	if (v->fat_bits == 16)
	{
		if ((idx + 1) * 2 > v->fat_size)
			return 0xffff;
		return vff_rd_le16 (v->fat + idx * 2);
	}

	const uint off = (idx / 2) * 3;
	if (off + 3 > v->fat_size)
		return 0xfff;
	return (idx & 1) ? (v->fat[off + 1] >> 4) | ((uint)v->fat[off + 2] << 4)
					 : v->fat[off] | (((uint)v->fat[off + 1] & 0xf) << 8);
}

static uint vff_eoc (const vff_t *v)
{
	return v->fat_bits == 16 ? 0xfff0 : 0xff0;
}

// Walk a cluster chain into a fresh buffer. Returns NULL on a chain that
// loops, leaves the volume, or never terminates -- a corrupt FAT must not be
// allowed to spin or to read outside the file.
static u8 *vff_read_chain (const vff_t *v, uint start, uint want, uint *out_size)
{
	const uint limit = vff_eoc (v);
	uint count = 0;
	for (uint c = start; c >= 2 && c < limit; c = vff_fat (v, c))
	{
		if (c >= v->cluster_count || ++count > v->cluster_count)
			return 0;
	}

	if (!count)
		return 0;

	const u64 total = (u64)count * v->cluster_size;
	if (total > v->size)
		return 0;

	u8 *out = MALLOC ((uint)total);
	if (!out)
		return 0;

	uint pos = 0;
	for (uint c = start; c >= 2 && c < limit && pos < total; c = vff_fat (v, c))
	{
		const u64 off = (u64)v->data_off + (u64)(c - 2) * v->cluster_size;
		if (off + v->cluster_size > v->size)
		{
			FREE (out);
			return 0;
		}
		memcpy (out + pos, v->data + off, v->cluster_size);
		pos += v->cluster_size;
	}

	*out_size = want && want <= pos ? want : pos;
	return out;
}

// 8.3 name from a directory entry, lowercased the way the rest of these tools
// present FAT names, with anything that could escape the destination removed.
static void vff_entry_name (const u8 *e, char *out, uint out_size)
{
	char base[9], ext[4];
	uint n = 0;
	for (uint i = 0; i < 8 && e[i] != ' '; i++)
		base[n++] = e[i];
	base[n] = 0;
	n = 0;
	for (uint i = 8; i < 11 && e[i] != ' '; i++)
		ext[n++] = e[i];
	ext[n] = 0;

	if (*ext)
		snprintf (out, out_size, "%s.%s", base, ext);
	else
		snprintf (out, out_size, "%s", base);

	for (char *p = out; *p; p++)
		if (*p == '/' || *p == '\\' || *p < 0x20)
			*p = '_';
}

static enumError vff_dump_dir (
	const vff_t *v, const u8 *dir, uint dir_size, ccp dest, uint depth, uint *count)
{
	if (depth > VFF_MAX_DEPTH)
		return ERR_OK;

	for (uint i = 0; i + VFF_DIR_ENTRY <= dir_size; i += VFF_DIR_ENTRY)
	{
		const u8 *e = dir + i;
		if (!e[0] || e[0] == 0xe5) // free or deleted
			continue;
		const u8 attr = e[11];
		if ((attr & 0x0f) == 0x0f) // long-name fragment
			continue;
		if (attr & 0x08) // volume label
			continue;

		char name[16];
		vff_entry_name (e, name, sizeof (name));
		if (!*name || !strcmp (name, ".") || !strcmp (name, ".."))
			continue;

		const uint start = vff_rd_le16 (e + 26);
		const u32 fsize = vff_rd_le32 (e + 28);

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dest, name);

		if (attr & 0x10) // directory
		{
			uint sub_size = 0;
			u8 *sub = vff_read_chain (v, start, 0, &sub_size);
			if (!sub)
				continue;
			if (!testmode)
				CreatePath (path, true);
			vff_dump_dir (v, sub, sub_size, path, depth + 1, count);
			FREE (sub);
			continue;
		}

		if (!fsize)
		{
			if (!testmode)
			{
				CreatePath (path, false);
				SaveFile (path, 0, 0, (const u8 *)"", 0, 0);
			}
			(*count)++;
			continue;
		}

		uint got = 0;
		u8 *body = vff_read_chain (v, start, fsize, &got);
		if (!body)
			continue;
		if (verbose > 0)
			fprintf (stdlog, "  %s [%u bytes]\n", path, got);
		if (!testmode)
		{
			CreatePath (path, false);
			SaveFile (path, 0, 0, body, got, 0);
		}
		FREE (body);
		(*count)++;
	}

	return ERR_OK;
}

bool IsVFF (const u8 *data, uint size)
{
	vff_t v;
	return vff_open (&v, data, size);
}

enumError ExtractVFFArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	vff_t v;
	if (raw_size > UINT_MAX || !vff_open (&v, raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest && *opt_dest)
		snprintf (dest, sizeof (dest), "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, sizeof (dest), "%s", basedir);
	else
		snprintf (dest, sizeof (dest), "%s.d", arg);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT VFF:%s (FAT%u, %u clusters of %u bytes) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, v.fat_bits, v.cluster_count,
			v.cluster_size, dest);

	uint count = 0;
	vff_dump_dir (&v, v.root, VFF_ROOT_SIZE, dest, 0, &count);
	FREE (raw);

	if (!count)
		return ERR_INVALID_DATA;
	return ERR_OK;
}
