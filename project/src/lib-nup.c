// SPDX-License-Identifier: GPL-2.0+
// NUP ("NX Package") container: decrypted Switch / Switch 2 base, update and
// add-on content trees in one file (spec: github.com/a55uka/nup, SPEC.md v1.0).
#include "lib-nup.h"
#include "lib-archive-util.h"
#include "lib-sha256.h"
#include "zstd.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NUP_PACKAGE_SIZE 0x40
#define NUP_DIR_SIZE 0x18
#define NUP_FILE_SIZE 0x50
#define NUP_NONE 0xFFFFFFFFu
#define NUP_MAX_DEPTH 64

bool IsNUP (const u8 *data, uint size)
{
	if (!data || size < NUP_SUPERBLOCK_SIZE || memcmp (data, "NUP\x01", 4))
		return false;
	const u16 header_size = rd_le16 (data + 4);
	const u64 toc_offset = rd_le64 (data + 8);
	const u64 blob_offset = rd_le64 (data + 0x38);
	return header_size >= NUP_SUPERBLOCK_SIZE && toc_offset == header_size
		&& !(blob_offset & 15) && rd_le32 (data + 0x48) >= 1;
}

typedef struct nup_ctx_t
{
	FILE *fp;
	u64 file_size;
	const u8 *toc;
	u64 toc_size, blob_off;
	u64 dir_count, file_count, slot_count;
	u64 dirs_off, files_off, slots_off, pool_off, pool_size;
	u8 *buf;
	size_t buf_size;
	u8 *zbuf;
	size_t zbuf_size;
	ZSTD_DStream *zds;
	uint written, failed;
} nup_ctx_t;

static bool nup_name (const nup_ctx_t *c, u32 off, u32 len, char *out, size_t out_size)
{
	if (!len || len >= out_size || (u64)off + len > c->pool_size)
		return false;
	memcpy (out, c->toc + c->pool_off + off, len);
	out[len] = 0;
	// one path component: no separators, no "." / ".." and nothing OwnedNameOk rejects
	if (strchr (out, '/') || !strcmp (out, ".") || !strcmp (out, "..") || !OwnedNameOk (out))
		return false;
	return true;
}

static bool nup_read_at (nup_ctx_t *c, u64 off, void *dst, size_t n)
{
	return fseeko (c->fp, (off_t)off, SEEK_SET) == 0 && fread (dst, 1, n, c->fp) == n;
}

// Copy one extent to 'path', decoding zstd on the fly so multi-GB members never sit in memory.
static enumError nup_write_file (nup_ctx_t *c, const u8 *rec, ccp path)
{
	const u64 size = rd_le64 (rec + 8);
	const u64 ext_off = rd_le64 (rec + 0x10);
	const u64 ext_size = rd_le64 (rec + 0x18);
	const u8 codec = rec[0x40];

	if (codec > 1 || (size && (ext_off < c->blob_off || ext_off > c->file_size
			|| ext_size > c->file_size - ext_off))
		|| (codec == 0 && ext_size != size))
		return ERR_INVALID_DATA;

	FILE *out = fopen (path, "wb");
	if (!out)
		return ERR_CANT_CREATE;
	enumError err = ERR_OK;
	if (size && fseeko (c->fp, (off_t)ext_off, SEEK_SET))
		err = ERR_READ_FAILED;

	u64 left = size ? ext_size : 0, produced = 0;
	if (codec == 1)
		ZSTD_DCtx_reset (c->zds, ZSTD_reset_session_only);
	while (!err && left)
	{
		const size_t n = left > c->buf_size ? c->buf_size : (size_t)left;
		if (fread (c->buf, 1, n, c->fp) != n)
		{
			err = ERR_READ_FAILED;
			break;
		}
		left -= n;
		if (codec == 0)
		{
			if (fwrite (c->buf, 1, n, out) != n)
				err = ERR_WRITE_FAILED;
			produced += n;
			continue;
		}
		ZSTD_inBuffer in = { c->buf, n, 0 };
		while (!err && in.pos < in.size)
		{
			ZSTD_outBuffer o = { c->zbuf, c->zbuf_size, 0 };
			const size_t r = ZSTD_decompressStream (c->zds, &o, &in);
			if (ZSTD_isError (r) || produced + o.pos > size)
				err = ERR_INVALID_DATA;
			else if (o.pos && fwrite (c->zbuf, 1, o.pos, out) != o.pos)
				err = ERR_WRITE_FAILED;
			produced += o.pos;
			if (!r && in.pos < in.size)
				err = ERR_INVALID_DATA; // trailing bytes after the frame
		}
	}
	// flush what the decoder still holds once all input is consumed
	while (!err && codec == 1 && produced < size)
	{
		ZSTD_inBuffer in = { 0, 0, 0 };
		ZSTD_outBuffer o = { c->zbuf, c->zbuf_size, 0 };
		const size_t r = ZSTD_decompressStream (c->zds, &o, &in);
		if (ZSTD_isError (r) || !o.pos || produced + o.pos > size)
			err = ERR_INVALID_DATA;
		else if (fwrite (c->zbuf, 1, o.pos, out) != o.pos)
			err = ERR_WRITE_FAILED;
		produced += o.pos;
	}
	if (!err && produced != size)
		err = ERR_INVALID_DATA;
	if (fclose (out) && !err)
		err = ERR_WRITE_FAILED;
	if (err)
		unlink (path); // never leave a truncated member behind
	return err;
}

static void nup_walk (nup_ctx_t *c, u32 dir, ccp path, uint depth, u8 *visited)
{
	if (dir >= c->dir_count || depth > NUP_MAX_DEPTH || visited[dir])
		return;
	visited[dir] = 1;
	CreatePath (path, true);

	const u8 *d = c->toc + c->dirs_off + (u64)dir * NUP_DIR_SIZE;
	const u64 first = rd_le32 (d + 8), count = rd_le32 (d + 12);
	if (first > c->slot_count || count > c->slot_count - first)
		return;

	for (u64 k = 0; k < count; k++)
	{
		const u32 node = rd_le32 (c->toc + c->slots_off + (first + k) * 4);
		char name[256], sub[PATH_MAX];
		if (node < c->dir_count)
		{
			const u8 *cd = c->toc + c->dirs_off + (u64)node * NUP_DIR_SIZE;
			if (!nup_name (c, rd_le32 (cd), rd_le32 (cd + 4), name, sizeof (name)))
				snprintf (name, sizeof (name), "dir_%u", node);
			snprintf (sub, sizeof (sub), "%s/%s", path, name);
			nup_walk (c, node, sub, depth + 1, visited);
		}
		else if (node - c->dir_count < c->file_count)
		{
			const u64 fi = node - c->dir_count;
			const u8 *f = c->toc + c->files_off + fi * NUP_FILE_SIZE;
			if (!nup_name (c, rd_le32 (f), rd_le32 (f + 4), name, sizeof (name)))
				snprintf (name, sizeof (name), "file_%llu.bin", (unsigned long long)fi);
			snprintf (sub, sizeof (sub), "%s/%s", path, name);
			if (testmode)
			{
				c->written++;
				continue;
			}
			const enumError e = nup_write_file (c, f, sub);
			if (e)
			{
				c->failed++;
				ERROR0 (e, "NUP: cannot extract %s\n", sub);
			}
			else
				c->written++;
		}
	}
}

static ccp nup_kind_name (u8 kind)
{
	switch (kind)
	{
	case 0: return "base";
	case 1: return "update";
	case 2: return "addon";
	case 3: return "addon_update";
	case 4: return "system";
	default: return "kind";
	}
}

static void nup_write_blob (ccp path, const u8 *data, size_t size)
{
	FILE *f = fopen (path, "wb");
	if (f)
	{
		fwrite (data, 1, size, f);
		fclose (f);
	}
}

// Save the package meta TLVs (NPDM, NACP, icon, names, versions) beside the tree.
static void nup_write_meta (nup_ctx_t *c, const u8 *pkg, ccp pkg_dir)
{
	const u64 off = rd_le64 (pkg + 0x20);
	const u32 size = rd_le32 (pkg + 0x28);
	if (!off || !size || size > 64u << 20 || off > c->file_size || size > c->file_size - off)
		return;
	u8 *m = MALLOC (size);
	if (!nup_read_at (c, off, m, size))
	{
		FREE (m);
		return;
	}
	char dir[PATH_MAX], path[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s/meta", pkg_dir);
	CreatePath (dir, true);

	snprintf (path, sizeof (path), "%s/meta.txt", dir);
	FILE *txt = fopen (path, "w");
	if (txt)
		fprintf (txt, "title_id = %016llx\nversion = %u\nkind = %s\n",
			(unsigned long long)rd_le64 (pkg), rd_le32 (pkg + 0x14), nup_kind_name (pkg[0x18]));

	for (u32 p = 0; p + 8 <= size;)
	{
		const u16 type = rd_le16 (m + p);
		const u32 len = rd_le32 (m + p + 4);
		if (len > size - p - 8)
			break;
		const u8 *v = m + p + 8;
		switch (type)
		{
		case 1:
			snprintf (path, sizeof (path), "%s/main.npdm", dir);
			nup_write_blob (path, v, len);
			break;
		case 2:
			snprintf (path, sizeof (path), "%s/control.nacp", dir);
			nup_write_blob (path, v, len);
			break;
		case 3:
			snprintf (path, sizeof (path), "%s/icon.%s", dir,
				len >= 4 && !memcmp (v, "\x89PNG", 4) ? "png" : "jpg");
			nup_write_blob (path, v, len);
			break;
		case 4:
		case 5:
		case 6:
		case 8:
			if (txt)
				fprintf (txt, "%s = %.*s\n",
					type == 4 ? "display_version" : type == 5 ? "title" : type == 6 ? "publisher" : "source",
					(int)len, (ccp)v);
			break;
		case 7:
			if (txt)
			{
				fputs ("build_id = ", txt);
				for (u32 i = 0; i < len && i < 32; i++)
					fprintf (txt, "%02x", v[i]);
				fputc ('\n', txt);
			}
			break;
		case 9:
		case 10:
			for (u32 q = 0; q < len;)
			{
				const u8 ll = v[q];
				if ((u64)q + 1 + ll + 4 > len)
					break;
				char lang[64];
				const uint lcopy = ll < sizeof (lang) ? ll : sizeof (lang) - 1;
				memcpy (lang, v + q + 1, lcopy);
				lang[lcopy] = 0;
				const u32 dl = rd_le32 (v + q + 1 + ll);
				const u8 *dv = v + q + 5 + ll;
				if (dl > len - q - 5 - ll)
					break;
				if (type == 9 && txt)
					fprintf (txt, "title.%s = %.*s\n", lang, (int)dl, (ccp)dv);
				else if (type == 10 && OwnedNameOk (lang) && !strchr (lang, '/'))
				{
					snprintf (path, sizeof (path), "%s/icon.%s.%s", dir, lang,
						dl >= 4 && !memcmp (dv, "\x89PNG", 4) ? "png" : "jpg");
					nup_write_blob (path, dv, dl);
				}
				q += 5 + ll + dl;
			}
			break;
		}
		p += 8 + len;
	}
	if (txt)
		fclose (txt);
	FREE (m);
}

enumError ExtractNUPArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_CANT_OPEN;

	u8 sb[NUP_SUPERBLOCK_SIZE];
	if (fread (sb, 1, sizeof (sb), fp) != sizeof (sb) || !IsNUP (sb, sizeof (sb)))
	{
		fclose (fp);
		return ERR_NOTHING_TO_DO;
	}

	struct stat st;
	const u64 real_size = fstat (fileno (fp), &st) ? 0 : (u64)st.st_size;
	const u64 toc_off = rd_le64 (sb + 8), toc_size = rd_le64 (sb + 0x10);
	const u64 blob_off = rd_le64 (sb + 0x38), file_size = rd_le64 (sb + 0x40);
	const u32 pkg_count = rd_le32 (sb + 0x48), flags = rd_le32 (sb + 0x4c);

	if (flags || file_size != real_size || toc_size < 0x30 || toc_size > (1ull << 31)
		|| toc_off + toc_size > blob_off || blob_off > file_size)
	{
		fclose (fp);
		return ERROR0 (ERR_INVALID_DATA, "NUP: bad superblock: %s\n", arg);
	}

	u8 *toc = MALLOC (toc_size);
	nup_ctx_t c = { .fp = fp, .file_size = file_size, .toc = toc, .toc_size = toc_size, .blob_off = blob_off };
	if (!nup_read_at (&c, toc_off, toc, toc_size))
	{
		FREE (toc);
		fclose (fp);
		return ERROR0 (ERR_READ_FAILED, "NUP: cannot read table of contents: %s\n", arg);
	}

	u8 digest[SHA256_DIGEST_SIZE];
	sha256_calc (toc, toc_size, digest);
	enumError err = ERR_OK;
	if (memcmp (digest, sb + 0x18, sizeof (digest)))
		err = ERROR0 (ERR_INVALID_DATA, "NUP: table of contents hash mismatch: %s\n", arg);
	else if (memcmp (toc, "TOC\x01", 4) || rd_le32 (toc + 4) != pkg_count)
		err = ERROR0 (ERR_INVALID_DATA, "NUP: bad table of contents: %s\n", arg);

	if (!err)
	{
		c.dir_count = rd_le64 (toc + 8);
		c.file_count = rd_le64 (toc + 0x10);
		c.pool_off = rd_le64 (toc + 0x18);
		c.pool_size = rd_le64 (toc + 0x20);
		c.dirs_off = 0x30 + (u64)pkg_count * NUP_PACKAGE_SIZE;
		c.files_off = c.dirs_off + c.dir_count * NUP_DIR_SIZE;
		c.slots_off = c.files_off + c.file_count * NUP_FILE_SIZE;
		if (c.dir_count > toc_size / NUP_DIR_SIZE || c.file_count > toc_size / NUP_FILE_SIZE
			|| c.slots_off > toc_size || c.pool_off > toc_size || c.pool_size > toc_size - c.pool_off
			|| c.pool_off < c.slots_off)
			err = ERROR0 (ERR_INVALID_DATA, "NUP: table of contents out of range: %s\n", arg);
		c.slot_count = err ? 0 : (c.pool_off - c.slots_off) / 4;
	}
	if (err)
	{
		FREE (toc);
		fclose (fp);
		return err;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUP:%s (%u packages, %llu files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, pkg_count,
			(unsigned long long)c.file_count, dest);
	CreatePath (dest, true);

	c.buf_size = 4 << 20;
	c.buf = MALLOC (c.buf_size);
	c.zbuf_size = ZSTD_DStreamOutSize () * 8;
	c.zbuf = MALLOC (c.zbuf_size);
	c.zds = ZSTD_createDStream ();
	u8 *visited = CALLOC (c.dir_count ? c.dir_count : 1, 1);

	for (u32 i = 0; i < pkg_count; i++)
	{
		const u8 *pkg = toc + 0x30 + (u64)i * NUP_PACKAGE_SIZE;
		const u64 tid = rd_le64 (pkg);
		const u32 version = rd_le32 (pkg + 0x14);
		char pkg_dir[PATH_MAX];
		if (version)
			snprintf (pkg_dir, sizeof (pkg_dir), "%s/%016llX_%s_v%u", dest, (unsigned long long)tid,
				nup_kind_name (pkg[0x18]), version);
		else
			snprintf (pkg_dir, sizeof (pkg_dir), "%s/%016llX_%s", dest, (unsigned long long)tid,
				nup_kind_name (pkg[0x18]));
		if (verbose > 0)
			fprintf (stdlog, "  - package %u: %016llX %s v%u\n", i, (unsigned long long)tid,
				nup_kind_name (pkg[0x18]), version);
		nup_walk (&c, rd_le32 (pkg + 8), pkg_dir, 0, visited);
		if (!testmode)
			nup_write_meta (&c, pkg, pkg_dir);
	}

	if (c.failed)
		err = ERROR0 (ERR_WARNING, "NUP: %u of %u files failed: %s\n", c.failed,
			c.failed + c.written, arg);
	ZSTD_freeDStream (c.zds);
	FREE (visited);
	FREE (c.zbuf);
	FREE (c.buf);
	FREE (toc);
	fclose (fp);
	return err;
}
