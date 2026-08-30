#include "lib-model-dae.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <stdarg.h>

typedef struct
{
	char *name;
	char *path;
} dae_texture_entry_t;

static dae_texture_entry_t *dae_texture_index;
static size_t dae_texture_index_used;
static size_t dae_texture_index_size;
static int dae_texture_search_enabled;
static char dae_texture_root[PATH_MAX];

static void clear_dae_texture_index (void)
{
	for (size_t i = 0; i < dae_texture_index_used; i++)
	{
		free (dae_texture_index[i].name);
		free (dae_texture_index[i].path);
	}
	free (dae_texture_index);
	dae_texture_index = NULL;
	dae_texture_index_used = dae_texture_index_size = 0;
}

static void index_dae_textures (const char *root, unsigned depth)
{
	if (depth > 48)
		return;
	DIR *dir = opendir (root);
	if (!dir)
		return;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (size_t)len >= sizeof (path))
			continue;
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
		{
			index_dae_textures (path, depth + 1);
			continue;
		}
		const size_t n = strlen (de->d_name);
		if (!S_ISREG (st.st_mode) || n < 5 || strcasecmp (de->d_name + n - 4, ".png"))
			continue;
		if (dae_texture_index_used == dae_texture_index_size)
		{
			const size_t next = dae_texture_index_size ? dae_texture_index_size * 2 : 256;
			void *mem = realloc (dae_texture_index, next * sizeof (*dae_texture_index));
			if (!mem)
				continue;
			dae_texture_index = mem;
			dae_texture_index_size = next;
		}
		dae_texture_entry_t *entry = dae_texture_index + dae_texture_index_used;
		entry->name = strdup (de->d_name);
		entry->path = strdup (path);
		if (entry->name && entry->path)
			dae_texture_index_used++;
		else
		{
			free (entry->name);
			free (entry->path);
		}
	}
	closedir (dir);
}

void SetDAETextureSearchRoot (const char *root)
{
	clear_dae_texture_index ();
	dae_texture_root[0] = 0;
	dae_texture_search_enabled = root && *root;
	if (!dae_texture_search_enabled)
		return;
	char absolute[PATH_MAX];
	const char *resolved = realpath (root, absolute) ? absolute : root;
	snprintf (dae_texture_root, sizeof (dae_texture_root), "%s", resolved);
	index_dae_textures (resolved, 0);
}

// Return the length of the common directory-component prefix. FROM is a
// directory and TO is a file path; both are canonical absolute paths.
static size_t dae_common_path (const char *from, const char *to)
{
	size_t i = 0;
	while (from[i] && to[i] && from[i] == to[i])
		i++;
	if (!from[i] && to[i] == '/')
		return i + 1;
	while (i && from[i - 1] != '/')
		i--;
	return i;
}

static void dae_relative_path (char *out, size_t out_size, const char *dae_path, const char *target)
{
	char from[PATH_MAX], to[PATH_MAX];
	if (!realpath (dae_path, from) || !realpath (target, to))
	{
		snprintf (out, out_size, "%s", target);
		return;
	}
	char *slash = strrchr (from, '/');
	if (!slash)
	{
		snprintf (out, out_size, "%s", to);
		return;
	}
	*slash = 0;
	const size_t common = dae_common_path (from, to);
	const size_t from_len = strlen (from);
	const char *remain = from + (common > from_len ? from_len : common);
	while (*remain == '/')
		remain++;
	size_t used = 0;
	if (*remain)
	{
		if (used + 3 < out_size)
		{
			memcpy (out + used, "../", 3);
			used += 3;
		}
		for (const char *p = remain; *p; p++)
			if (*p == '/' && used + 3 < out_size)
			{
				memcpy (out + used, "../", 3);
				used += 3;
			}
	}
	const char *suffix = to + common;
	while (*suffix == '/')
		suffix++;
	snprintf (out + used, out_size - used, "%s", suffix);
}

// A basename alone is not enough to link two different BRRES archives. Names
// such as e.0, m.0, skin and eye are reused by hundreds of unrelated assets.
// Accept a global match only when the model and texture share at least one
// meaningful directory below the configured extraction root (ignoring the
// generic disc staging components DATA/files/content/romfs). This preserves
// intentional links such as BgData/BgModel -> BgData/Pack while preventing an
// Item/Excap model from stealing an Npc/Special eye or mouth texture.
static int dae_shared_texture_scope (const char *dae_dir, const char *target)
{
	const size_t root_len = strlen (dae_texture_root);
	if (!root_len || strncmp (dae_dir, dae_texture_root, root_len)
		|| strncmp (target, dae_texture_root, root_len)
		|| dae_dir[root_len] && dae_dir[root_len] != '/'
		|| target[root_len] && target[root_len] != '/')
		return 0;

	// The model and its texture sitting in the literal same directory (no
	// subdirectory below the root at all -- the common case for a loose
	// .bfres and its FTEX-decoded sibling PNGs, unlike BRRES's split
	// 3DModels(NW4R)/Textures(NW4R) layout) is obviously in scope, but the
	// walk below only ever compares subdirectory *components*: with zero
	// components on either side its loop body never runs and it falls
	// through to the reject at the bottom. Check the trivial case first.
	// 'target' is a full FILE path (this function's caller always passes
	// one), so compare against its directory, not the file path itself.
	{
		const char *tslash = strrchr (target, '/');
		const size_t tdir_len = tslash ? (size_t)(tslash - target) : 0;
		if (tdir_len == strlen (dae_dir) && !strncmp (dae_dir, target, tdir_len))
			return 1;
	}

	const char *a = dae_dir + root_len, *b = target + root_len;
	while (*a == '/')
		a++;
	while (*b == '/')
		b++;
	while (*a && *b)
	{
		const char *ae = strchr (a, '/'), *be = strchr (b, '/');
		const size_t an = ae ? (size_t)(ae - a) : strlen (a);
		const size_t bn = be ? (size_t)(be - b) : strlen (b);
		if (an != bn || strncasecmp (a, b, an))
			break;
		if (strncasecmp (a, "data", an) || an != 4)
			if (strncasecmp (a, "files", an) || an != 5)
				if (strncasecmp (a, "content", an) || an != 7)
					if (strncasecmp (a, "romfs", an) || an != 5)
						return 1;
		if (!ae || !be)
			break;
		a = ae + 1;
		b = be + 1;
	}
	return 0;
}

// BRRES extraction writes models to 3DModels(NW4R) and decoded TEX0 images
// to the sibling Textures(NW4R) directory.  COLLADA resolves init_from paths
// relative to the .dae, so use that real location when it exists; keep the
// old local-name fallback for standalone model conversion.
//
// A texture *name* is normally a bare, extension-less identifier (BFRES/
// BCRES/MDL0 etc. all store it that way), but HSF is a real exception: its
// own texture table already bakes the ".png" suffix in at parse time (see
// lib-hsf.c's tex_names[i], "%s.png") and stores that complete filename
// straight into material_t.textures[]. Unconditionally appending ".png"
// here produced "foo.png.png" for every HSF model -- never matching the
// real file on disk regardless of how well the search index itself was
// built. dae_texture_filename() appends the extension only when it isn't
// already there, so both conventions resolve to the same real path.
static void dae_texture_filename (char *out, size_t out_size, const char *texture)
{
	const size_t len = strlen (texture);
	if (len >= 4 && !strcasecmp (texture + len - 4, ".png"))
		snprintf (out, out_size, "%s", texture);
	else
		snprintf (out, out_size, "%s.png", texture);
}

static int dae_texture_path (char *out, size_t out_size, const char *dae_path, const char *texture)
{
	if (!texture || !*texture)
		return 0;
	const char *slash = strrchr (dae_path, '/');
	if (!slash)
	{
		dae_texture_filename (out, out_size, texture);
		return !dae_texture_search_enabled;
	}

	const size_t dir_len = slash - dae_path;
	char tex_filename[512];
	dae_texture_filename (tex_filename, sizeof (tex_filename), texture);
	char candidate[4096];
	const int len = snprintf (candidate, sizeof (candidate), "%.*s/../Textures(NW4R)/%s",
		(int)dir_len, dae_path, tex_filename);
	struct stat st;
	if (len >= 0 && (size_t)len < sizeof (candidate) && !stat (candidate, &st)
		&& S_ISREG (st.st_mode))
	{
		snprintf (out, out_size, "../Textures(NW4R)/%s", tex_filename);
		return 1;
	}
	else
	{
		const char *wanted = tex_filename;
		const char *best = NULL;
		size_t best_common = 0;
		// realpath(3) requires every component of its argument to exist,
		// including the last -- but dae_path/out_glb_file is the model file
		// this very call is preparing to write, so it never exists yet.
		// Resolve the *containing directory* (which extraction has already
		// created) instead of the not-yet-written file itself.
		char dae_absolute[PATH_MAX];
		char *dae_dir = NULL;
		{
			char dir_only[PATH_MAX];
			snprintf (dir_only, sizeof (dir_only), "%.*s", (int)dir_len, dae_path);
			dae_dir = realpath (dir_only, dae_absolute) ? dae_absolute : NULL;
		}
		for (size_t i = 0; i < dae_texture_index_used; i++)
		{
			const dae_texture_entry_t *entry = dae_texture_index + i;
			if (strcasecmp (entry->name, wanted))
				continue;
			const size_t common = dae_dir ? dae_common_path (dae_dir, entry->path) : 0;
			if (!best || common > best_common
				|| (common == best_common && strcmp (entry->path, best) < 0))
			{
				best = entry->path;
				best_common = common;
			}
		}
		if (best && dae_dir && dae_shared_texture_scope (dae_dir, best))
		{
			dae_relative_path (out, out_size, dae_path, best);
			return 1;
		}
		snprintf (out, out_size, "%s", tex_filename);
		return !dae_texture_search_enabled;
	}
}

// Place a texture beside the .dae that references it and return its bare
// file name.
//
// A relative <init_from> that walks out of the model directory is legal
// COLLADA and correct resolvers (assimp, Foundation's URL machinery) follow
// it, but a large family of importers -- including the one behind macOS
// Preview/Quick Look -- only ever look for the *base name* in the document's
// own directory, so "../Textures(NW4R)/x.png" silently resolves to nothing
// and the model renders untextured. BrawlCrate sidesteps this by writing
// bare names next to the model, so match that layout.
//
// The payload is hard-linked when the filesystem allows it, so the sibling
// copy costs an inode rather than a second copy of every texture; a plain
// copy is the fallback across devices. A name that is already local, or that
// is taken by unrelated content, is left alone -- an existing file is never
// overwritten.
static int dae_same_content (const char *a, const char *b)
{
	struct stat sa, sb;
	if (stat (a, &sa) || stat (b, &sb))
		return 0;
	if (sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino)
		return 1;
	if (sa.st_size != sb.st_size)
		return 0;

	FILE *fa = fopen (a, "rb");
	if (!fa)
		return 0;
	FILE *fb = fopen (b, "rb");
	if (!fb)
	{
		fclose (fa);
		return 0;
	}

	int equal = 1;
	char ba[8192], bb[8192];
	size_t na;
	while (equal && (na = fread (ba, 1, sizeof (ba), fa)) > 0)
	{
		const size_t nb = fread (bb, 1, sizeof (bb), fb);
		if (na != nb || memcmp (ba, bb, na))
			equal = 0;
	}
	if (equal && fread (bb, 1, 1, fb) > 0)
		equal = 0;
	fclose (fa);
	fclose (fb);
	return equal;
}

static int dae_copy_file (const char *src, const char *dest)
{
	FILE *in = fopen (src, "rb");
	if (!in)
		return 0;
	FILE *out = fopen (dest, "wb");
	if (!out)
	{
		fclose (in);
		return 0;
	}

	int ok = 1;
	char buf[8192];
	size_t n;
	while ((n = fread (buf, 1, sizeof (buf), in)) > 0)
		if (fwrite (buf, 1, n, out) != n)
		{
			ok = 0;
			break;
		}
	if (ferror (in))
		ok = 0;
	if (fclose (out))
		ok = 0;
	fclose (in);
	if (!ok)
		unlink (dest);
	return ok;
}

static int dae_localize_texture (char *path, size_t path_size, const char *dae_path)
{
	const char *base = strrchr (path, '/');
	if (!base)
		return 1; // already a bare name beside the model
	base++;
	if (!*base)
		return 0;

	const char *dae_slash = strrchr (dae_path, '/');
	if (!dae_slash)
		return 0;
	const size_t dir_len = dae_slash - dae_path;

	char src[PATH_MAX], dest[PATH_MAX];
	if (snprintf (src, sizeof (src), "%.*s/%s", (int)dir_len, dae_path, path) >= (int)sizeof (src)
		|| snprintf (dest, sizeof (dest), "%.*s/%s", (int)dir_len, dae_path, base)
			>= (int)sizeof (dest))
		return 0;

	struct stat st;
	if (!stat (dest, &st))
		// Only reuse a sibling that really is this texture; a same-named file
		// belonging to something else must not be silently repurposed.
		return S_ISREG (st.st_mode) && dae_same_content (src, dest)
			? (snprintf (path, path_size, "%s", base), 1)
			: 0;

	if (link (src, dest) && !dae_copy_file (src, dest))
		return 0;

	snprintf (path, path_size, "%s", base);
	return 1;
}

static void dae_normalize_resource_name (char *out, size_t out_size, const char *name, int material)
{
	// Nintendo's common prefixes describe the resource type rather than its
	// identity (m_grass and tex_grass are a pair). Strip them before scoring.
	static const char *mat_prefix[] = { "material_", "mat_", "mt_", "m_" };
	static const char *tex_prefix[] = { "texture_", "tex_" };
	const char **prefix = material ? mat_prefix : tex_prefix;
	const unsigned count = material ? sizeof (mat_prefix) / sizeof (*mat_prefix)
									: sizeof (tex_prefix) / sizeof (*tex_prefix);
	for (unsigned i = 0; i < count; i++)
	{
		const size_t n = strlen (prefix[i]);
		if (!strncasecmp (name, prefix[i], n))
		{
			name += n;
			break;
		}
	}
	size_t used = 0;
	while (*name && used + 1 < out_size)
	{
		const unsigned char ch = *name++;
		if (isalnum (ch))
			out[used++] = (char)tolower (ch);
	}
	out[used] = 0;
}

static int dae_primary_texture (const material_t *mat, const char *dae_path)
{
	if (!mat->num_textures)
		return -1;
	char material[128];
	dae_normalize_resource_name (material, sizeof (material), mat->name, 1);
	int best = -1, best_score = -1;
	for (int t = 0; t < mat->num_textures; t++)
	{
		char path[PATH_MAX];
		if (!dae_texture_path (path, sizeof (path), dae_path, mat->textures[t]))
			continue;
		char texture[128];
		dae_normalize_resource_name (texture, sizeof (texture), mat->textures[t], 0);
		int score = mat->num_textures - t;
		if (material[0] && !strcmp (material, texture))
			score += 1000;
		else if (material[0]
			&& (strstr (texture, material) == texture || strstr (material, texture) == material))
			score += 200;
		// Border/noise/mask layers are normally TEV details, not the base
		// color map a single-texture profile_COMMON effect should display.
		if (strstr (texture, "noise") || strstr (texture, "mask") || strstr (texture, "brd"))
			score -= 50;
		if (score > best_score)
		{
			best = t;
			best_score = score;
		}
	}
	return best;
}

// ---------------------------------------------------------------------------
// XML/COLLADA identifier helpers
//
// MDL0 resource names are arbitrary bytes chosen by Nintendo's exporter. They
// reach COLLADA in two very different roles and both were previously written
// raw: as attribute *text* (name="...") where &, <, > and " must be escaped
// or the document stops being well-formed XML, and as *ids* (id=, url="#..",
// and Name_array entries) which must additionally be unique XML NCNames.
// ---------------------------------------------------------------------------

static void dae_escape (char *out, size_t out_size, const char *in)
{
	size_t used = 0;
	for (; in && *in; in++)
	{
		const char *rep;
		switch (*in)
		{
			case '&':
				rep = "&amp;";
				break;
			case '<':
				rep = "&lt;";
				break;
			case '>':
				rep = "&gt;";
				break;
			case '"':
				rep = "&quot;";
				break;
			case '\'':
				rep = "&apos;";
				break;
			default:
				// Control bytes are not representable in XML 1.0 at all.
				if ((unsigned char)*in < 0x20)
				{
					rep = "";
					break;
				}
				if (used + 1 < out_size)
					out[used++] = *in;
				continue;
		}
		const size_t n = strlen (rep);
		if (used + n < out_size)
		{
			memcpy (out + used, rep, n);
			used += n;
		}
	}
	out[used < out_size ? used : out_size - 1] = 0;
}

typedef char dae_id_t[96];

static void dae_make_id (dae_id_t out, const char *name, const char *fallback, size_t index,
	const dae_id_t *taken, size_t num_taken)
{
	char base[sizeof (dae_id_t)];
	size_t used = 0;
	for (const char *p = name; p && *p && used + 1 < sizeof (base) - 8; p++)
	{
		const unsigned char ch = (unsigned char)*p;
		base[used++] = isalnum (ch) || ch == '_' || ch == '-' || ch == '.' ? (char)ch : '_';
	}
	base[used] = 0;
	// NCNames may not start with a digit, '-' or '.'.
	if (!used || !(isalpha ((unsigned char)base[0]) || base[0] == '_'))
	{
		char prefixed[sizeof (base)];
		snprintf (prefixed, sizeof (prefixed), "%s_%s", fallback, base);
		snprintf (base, sizeof (base), "%s", prefixed);
	}
	snprintf (out, sizeof (dae_id_t), "%s", base);
	for (unsigned attempt = 1;; attempt++)
	{
		int clash = 0;
		for (size_t i = 0; i < num_taken && !clash; i++)
			if (!strcmp (taken[i], out))
				clash = 1;
		if (!clash)
			return;
		snprintf (out, sizeof (dae_id_t), "%.80s_%u", base, attempt);
		if (attempt > 64)
		{
			snprintf (out, sizeof (dae_id_t), "%s_%zu", fallback, index);
			return;
		}
	}
}

// Does any mesh bound to this material actually carry UV set `set`?
static int dae_material_has_uv_set (const model_t *model, int material_idx, int set)
{
	for (size_t i = 0; i < model->num_meshes; i++)
	{
		const mesh_t *mesh = &model->meshes[i];
		if (mesh->material_idx != material_idx)
			continue;
		if (!set && mesh->num_texcoords)
			return 1;
		if (set > 0 && set < 8 && mesh->num_extra_texcoords[set - 1])
			return 1;
	}
	return 0;
}

// A mesh may be skinned only if every one of its positions resolved to at
// least one bone influence; a partially bound controller silently drops
// geometry in importers, so those meshes stay plain instance_geometry.
static int dae_mesh_is_skinned (const model_t *model, const mesh_t *mesh)
{
	if (!model->num_joints || !mesh->position_node || !mesh->num_positions)
		return 0;
	for (size_t v = 0; v < mesh->num_positions; v++)
	{
		const int node = mesh->position_node[v];
		if (node < 0 || (size_t)node >= model->num_node_influences
			|| !model->node_influences[node].num_weights)
			return 0;
	}
	return 1;
}

// Writes a joint and, recursively, every joint whose parent_idx points
// back to it -- a real nested <node> tree, not just the flat root list
// this used to emit regardless of what hierarchy data a parser (e.g.
// NSBMD's RenderCommandList-derived parent_idx) actually supplied.
// out = a * b, for the 3x4 row-major affine matrices MDL0 stores.
// COLLADA <init_from> holds an xs:anyURI. Path separators and sub-delims
// such as '(' and ')' (NW4R's "Textures(NW4R)" directory) are legal in a URI
// path and are left alone so importers that do not percent-decode still
// resolve them; space, '#', '%' and friends are not legal and would silently
// truncate or misdirect the reference, so those are escaped.
static void dae_uri_escape (char *out, size_t out_size, const char *in)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t used = 0;
	for (; in && *in; in++)
	{
		const unsigned char ch = (unsigned char)*in;
		const int safe = isalnum (ch) || strchr ("-._~!$&'()*+,;=:@/", ch) != NULL;
		if (safe)
		{
			if (used + 1 < out_size)
				out[used++] = (char)ch;
		}
		else if (used + 3 < out_size)
		{
			out[used++] = '%';
			out[used++] = hex[ch >> 4];
			out[used++] = hex[ch & 15];
		}
	}
	out[used < out_size ? used : out_size - 1] = 0;
}

static const char *dae_wrap_mode (unsigned mode)
{
	// GX: 0 = clamp, 1 = repeat, 2 = mirror.
	return mode == 0 ? "CLAMP" : mode == 2 ? "MIRROR" : "WRAP";
}

static const char *dae_filter_mode (unsigned mode, int is_min)
{
	if (!is_min)
		return mode ? "LINEAR" : "NEAREST";
	switch (mode)
	{
		case 0:
			return "NEAREST";
		case 1:
			return "LINEAR";
		case 2:
			return "NEAREST_MIPMAP_NEAREST";
		case 3:
			return "LINEAR_MIPMAP_NEAREST";
		case 4:
			return "NEAREST_MIPMAP_LINEAR";
		default:
			return "LINEAR_MIPMAP_LINEAR";
	}
}

static void dae_mul43 (float out[12], const float a[12], const float b[12])
{
	for (unsigned r = 0; r < 3; r++)
	{
		for (unsigned c = 0; c < 3; c++)
			out[r * 4 + c]
				= a[r * 4 + 0] * b[c + 0] + a[r * 4 + 1] * b[c + 4] + a[r * 4 + 2] * b[c + 8];
		out[r * 4 + 3]
			= a[r * 4 + 0] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
}

// The joint's local matrix as COLLADA would compose it from the TRS
// components: T * Rz * Ry * Rx * S.
static void dae_joint_trs (float out[12], const joint_t *joint)
{
	const double dx = joint->rotate.x * (M_PI / 180.0);
	const double dy = joint->rotate.y * (M_PI / 180.0);
	const double dz = joint->rotate.z * (M_PI / 180.0);
	const float cx = (float)cos (dx), sx = (float)sin (dx);
	const float cy = (float)cos (dy), sy = (float)sin (dy);
	const float cz = (float)cos (dz), sz = (float)sin (dz);
	const float rot[12] = { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, 0.0f, sz * cy,
		sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, 0.0f, -sy, cy * sx, cy * cx, 0.0f };
	for (unsigned r = 0; r < 3; r++)
	{
		out[r * 4 + 0] = rot[r * 4 + 0] * joint->scale.x;
		out[r * 4 + 1] = rot[r * 4 + 1] * joint->scale.y;
		out[r * 4 + 2] = rot[r * 4 + 2] * joint->scale.z;
	}
	out[3] = joint->translate.x;
	out[7] = joint->translate.y;
	out[11] = joint->translate.z;
}

static int dae_invert43 (float out[12], const float m[12])
{
	const double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9])
		- (double)m[1] * (m[4] * m[10] - m[6] * m[8]) + (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
	if (fabs (det) < 1e-20)
		return 0;
	const float d = (float)(1.0 / det);
	out[0] = (m[5] * m[10] - m[6] * m[9]) * d;
	out[1] = (m[2] * m[9] - m[1] * m[10]) * d;
	out[2] = (m[1] * m[6] - m[2] * m[5]) * d;
	out[4] = (m[6] * m[8] - m[4] * m[10]) * d;
	out[5] = (m[0] * m[10] - m[2] * m[8]) * d;
	out[6] = (m[2] * m[4] - m[0] * m[6]) * d;
	out[8] = (m[4] * m[9] - m[5] * m[8]) * d;
	out[9] = (m[1] * m[8] - m[0] * m[9]) * d;
	out[10] = (m[0] * m[5] - m[1] * m[4]) * d;
	out[3] = -(out[0] * m[3] + out[1] * m[7] + out[2] * m[11]);
	out[7] = -(out[4] * m[3] + out[5] * m[7] + out[6] * m[11]);
	out[11] = -(out[8] * m[3] + out[9] * m[7] + out[10] * m[11]);
	return 1;
}

static int dae_compute_bind (model_t *model, size_t i, uint8_t *state)
{
	if (state[i] == 2)
		return 1;
	if (state[i] == 1)
		return 0;
	state[i] = 1;
	float local[12];
	dae_joint_trs (local, model->joints + i);
	const int p = model->joints[i].parent_idx;
	if (p >= 0 && (size_t)p < model->num_joints)
	{
		if (!dae_compute_bind (model, p, state))
			return 0;
		dae_mul43 (model->joints[i].bind, model->joints[p].bind, local);
	}
	else
		memcpy (model->joints[i].bind, local, sizeof (local));
	if (!dae_invert43 (model->joints[i].inverse_bind, model->joints[i].bind))
		return 0;
	model->joints[i].has_inverse_bind = 1;
	state[i] = 2;
	return 1;
}

int ComputeModelTRSBinds (model_t *model)
{
	if (!model || (!model->joints && model->num_joints))
		return 0;
	uint8_t *state = calloc (model->num_joints ? model->num_joints : 1, 1);
	if (!state)
		return 0;
	int ok = 1;
	for (size_t i = 0; i < model->num_joints && ok; i++)
		ok = dae_compute_bind (model, i, state);
	free (state);
	return ok;
}

// NW4R bones may carry "segment scale compensate", which cancels the parent's
// scale instead of inheriting it. COLLADA nodes have no such rule, so for
// those bones the stored absolute matrix (which the skin's inverse binds are
// derived from) simply is not reproducible from an inherited TRS chain --
// the skeleton and the inverse binds then disagree and importers deform the
// mesh. Recover the true local matrix from the stored absolutes and report
// whether the TRS form matches it.
static int dae_joint_local_matrix (const model_t *model, size_t idx, float out[12])
{
	const joint_t *joint = &model->joints[idx];
	if (!joint->has_inverse_bind)
	{
		dae_joint_trs (out, joint);
		return 1;
	}
	const int parent = joint->parent_idx;
	if (parent >= 0 && (size_t)parent < model->num_joints)
	{
		if (!model->joints[parent].has_inverse_bind)
		{
			dae_joint_trs (out, joint);
			return 1;
		}
		dae_mul43 (out, model->joints[parent].inverse_bind, joint->bind);
	}
	else
		memcpy (out, joint->bind, 12 * sizeof (*out));

	float trs[12];
	dae_joint_trs (trs, joint);
	// Judge the linear part and the translation against their own magnitudes.
	// A single shared scale let a bone with a large offset accept a TRS that
	// was off by ~0.04, which then broke the world(joint)*invBind==identity
	// invariant the skin relies on; such bones fall back to <matrix>, which
	// reproduces the stored bind matrix exactly.
	float linear = 1.0f, translation = 1.0f;
	for (unsigned r = 0; r < 3; r++)
	{
		for (unsigned c = 0; c < 3; c++)
			if (fabsf (out[r * 4 + c]) > linear)
				linear = fabsf (out[r * 4 + c]);
		if (fabsf (out[r * 4 + 3]) > translation)
			translation = fabsf (out[r * 4 + 3]);
	}
	for (unsigned r = 0; r < 3; r++)
	{
		for (unsigned c = 0; c < 3; c++)
			if (fabsf (trs[r * 4 + c] - out[r * 4 + c]) > 1e-4f * linear)
				return 0;
		if (fabsf (trs[r * 4 + 3] - out[r * 4 + 3]) > 1e-4f * translation)
			return 0;
	}
	return 1;
}

static void write_joint_node (
	FILE *f, const model_t *model, const dae_id_t *ids, size_t idx, int indent)
{
	if (indent > 200)
		return; // guard against a malformed/cyclic parent_idx chain
	const joint_t *joint = &model->joints[idx];
	char name[256];
	dae_escape (name, sizeof (name), joint->name);
	fprintf (f, "%*s<node id=\"%s\" name=\"%s\" sid=\"%s\" type=\"JOINT\">\n", indent, "", ids[idx],
		name, ids[idx]);
	float local[12];
	if (!dae_joint_local_matrix (model, idx, local))
	{
		fprintf (f, "%*s  <matrix sid=\"transform\">", indent, "");
		for (unsigned n = 0; n < 12; n++)
			fprintf (f, "%f ", local[n]);
		fprintf (f, "0 0 0 1</matrix>\n");
		for (size_t i = 0; i < model->num_joints; i++)
			if (model->joints[i].parent_idx == (int)idx)
				write_joint_node (f, model, ids, i, indent + 2);
		fprintf (f, "%*s</node>\n", indent, "");
		return;
	}
	fprintf (f, "%*s  <translate sid=\"translate\">%f %f %f</translate>\n", indent, "",
		joint->translate.x, joint->translate.y, joint->translate.z);
	// NW4R composes a bone's local matrix as T * Rz * Ry * Rx * S. COLLADA
	// applies transform elements in document order, so the rotations must be
	// written Z, Y, X -- writing X, Y, Z (as this did) silently transposes
	// the rotation order and misplaces every bone that turns about more than
	// one axis. Verified against BrawlLib's ColladaExporter.WriteBone().
	fprintf (f, "%*s  <rotate sid=\"rotateZ\">0 0 1 %f</rotate>\n", indent, "", joint->rotate.z);
	fprintf (f, "%*s  <rotate sid=\"rotateY\">0 1 0 %f</rotate>\n", indent, "", joint->rotate.y);
	fprintf (f, "%*s  <rotate sid=\"rotateX\">1 0 0 %f</rotate>\n", indent, "", joint->rotate.x);
	fprintf (f, "%*s  <scale sid=\"scale\">%f %f %f</scale>\n", indent, "", joint->scale.x,
		joint->scale.y, joint->scale.z);
	for (size_t i = 0; i < model->num_joints; i++)
		if (model->joints[i].parent_idx == (int)idx)
			write_joint_node (f, model, ids, i, indent + 2);
	fprintf (f, "%*s</node>\n", indent, "");
}

int ExportModelToDAE (const model_t *model, const char *out_xml_file)
{
	if (!model || !out_xml_file)
		return -1;

	// Stable, unique NCName ids derived from the real MDL0 resource names.
	dae_id_t *joint_ids
		= model->num_joints ? calloc (model->num_joints, sizeof (*joint_ids)) : NULL;
	dae_id_t *mesh_ids = model->num_meshes ? calloc (model->num_meshes, sizeof (*mesh_ids)) : NULL;
	dae_id_t *material_ids
		= model->num_materials ? calloc (model->num_materials, sizeof (*material_ids)) : NULL;
	if ((model->num_joints && !joint_ids) || (model->num_meshes && !mesh_ids)
		|| (model->num_materials && !material_ids))
	{
		free (joint_ids);
		free (mesh_ids);
		free (material_ids);
		return -1;
	}
	for (size_t i = 0; i < model->num_joints; i++)
		dae_make_id (joint_ids[i], model->joints[i].name, "joint", i, joint_ids, i);
	for (size_t i = 0; i < model->num_meshes; i++)
		dae_make_id (mesh_ids[i], model->meshes[i].name, "mesh", i, mesh_ids, i);
	for (size_t i = 0; i < model->num_materials; i++)
		dae_make_id (material_ids[i], model->materials[i].name, "material", i, material_ids, i);

	FILE *f = fopen (out_xml_file, "w");
	if (!f)
	{
		free (joint_ids);
		free (mesh_ids);
		free (material_ids);
		return -1;
	}

	char timestamp[32] = "1970-01-01T00:00:00Z";
	{
		const time_t now = time (NULL);
		struct tm utc;
		if (gmtime_r (&now, &utc))
			strftime (timestamp, sizeof (timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
	}

	fprintf (f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	fprintf (
		f, "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n");
	fprintf (f, "  <asset>\n");
	fprintf (f, "    <contributor>\n");
	fprintf (f, "      <authoring_tool>wiimms-szs-tools-nintendo exporter</authoring_tool>\n");
	fprintf (f, "    </contributor>\n");
	fprintf (f, "    <created>%s</created>\n", timestamp);
	fprintf (f, "    <modified>%s</modified>\n", timestamp);
	// NW4R model coordinates are centimeters. Match BrawlCrate's COLLADA
	// exporter so importers don't interpret a 37-unit character as 37 m.
	fprintf (f, "    <unit name=\"centimeter\" meter=\"0.01\"/>\n");
	fprintf (f, "    <up_axis>Y_UP</up_axis>\n");
	fprintf (f, "  </asset>\n");

	// Images/effects/materials: only meaningful for materials that actually
	// resolved at least one texture layer name during MDL0 parsing.
	fprintf (f, "  <library_images>\n");
	for (size_t i = 0; i < model->num_materials; i++)
	{
		const material_t *mat = &model->materials[i];
		for (int t = 0; t < mat->num_textures; t++)
		{
			char texture_path[4096];
			if (!dae_texture_path (
					texture_path, sizeof (texture_path), out_xml_file, mat->textures[t]))
				continue;
			dae_localize_texture (texture_path, sizeof (texture_path), out_xml_file);
			char image_name[256], image_uri[4096], image_href[4096];
			dae_escape (image_name, sizeof (image_name), mat->textures[t]);
			dae_uri_escape (image_uri, sizeof (image_uri), texture_path);
			dae_escape (image_href, sizeof (image_href), image_uri);
			fprintf (f, "    <image id=\"img_%zu_%d\" name=\"%s\">\n", i, t, image_name);
			fprintf (f, "      <init_from>%s</init_from>\n", image_href);
			fprintf (f, "    </image>\n");
		}
	}
	fprintf (f, "  </library_images>\n");

	fprintf (f, "  <library_effects>\n");
	for (size_t i = 0; i < model->num_materials; i++)
	{
		const material_t *mat = &model->materials[i];
		const int primary = dae_primary_texture (mat, out_xml_file);
		fprintf (f, "    <effect id=\"fx_%zu\">\n", i);
		fprintf (f, "      <profile_COMMON>\n");
		for (int t = 0; t < mat->num_textures; t++)
		{
			char texture_path[PATH_MAX];
			if (!dae_texture_path (
					texture_path, sizeof (texture_path), out_xml_file, mat->textures[t]))
				continue;
			fprintf (f, "        <newparam sid=\"surface_%zu_%d\">\n", i, t);
			fprintf (f,
				"          <surface type=\"2D\"><init_from>img_%zu_%d</init_from></surface>\n", i,
				t);
			fprintf (f, "        </newparam>\n");
			fprintf (f, "        <newparam sid=\"sampler_%zu_%d\">\n", i, t);
			fprintf (f, "          <sampler2D>\n");
			fprintf (f, "            <source>surface_%zu_%d</source>\n", i, t);
			fprintf (f, "            <wrap_s>%s</wrap_s>\n", dae_wrap_mode (mat->wrap_s[t]));
			fprintf (f, "            <wrap_t>%s</wrap_t>\n", dae_wrap_mode (mat->wrap_t[t]));
			fprintf (f, "            <minfilter>%s</minfilter>\n",
				dae_filter_mode (mat->min_filter[t], 1));
			fprintf (f, "            <magfilter>%s</magfilter>\n",
				dae_filter_mode (mat->mag_filter[t], 0));
			fprintf (f, "          </sampler2D>\n");
			fprintf (f, "        </newparam>\n");
		}
		fprintf (f, "        <technique sid=\"COMMON\">\n");
		fprintf (f, mat->shininess > 0 ? "          <phong>\n" : "          <lambert>\n");
		// The texgen source row only names a UV set for TexCoord-sourced
		// layers; environment/normal-sourced ones used to be reported as set
		// 0 regardless, producing an effect that references a UV set the mesh
		// never binds (57 models in a retail corpus). Fall back to a set the
		// geometry really has, or to a plain colour when it has none.
		int coord = primary >= 0 ? mat->texture_coord[primary] : 0;
		if (primary >= 0 && !dae_material_has_uv_set (model, (int)i, coord))
			coord = dae_material_has_uv_set (model, (int)i, 0) ? 0 : -1;
		if (primary >= 0 && coord >= 0)
			fprintf (f,
				"            <diffuse><texture texture=\"sampler_%zu_%d\" "
				"texcoord=\"TEXCOORD%d\"/></diffuse>\n",
				i, primary, coord);
		else
		{
			float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.0f;
			if (mat->diffuse[0] || mat->diffuse[1] || mat->diffuse[2] || mat->diffuse[3])
			{
				r = mat->diffuse[0];
				g = mat->diffuse[1];
				b = mat->diffuse[2];
				a = mat->diffuse[3];
			}
			fprintf (f, "            <diffuse><color>%g %g %g %g</color></diffuse>\n", r, g, b, a);
		}
		if (mat->ambient[0] > 0 || mat->ambient[1] > 0 || mat->ambient[2] > 0)
			fprintf (f, "            <emission><color>%g %g %g</color></emission>\n",
				mat->ambient[0], mat->ambient[1], mat->ambient[2]);
		if (mat->diffuse[3] > 0 && mat->diffuse[3] < 1.0f)
			fprintf (
				f, "            <transparency><float>%g</float></transparency>\n", mat->diffuse[3]);
		if (mat->shininess > 0)
			fprintf (f, "            <shininess><float>%g</float></shininess>\n", mat->shininess);
		fprintf (f, mat->shininess > 0 ? "          </phong>\n" : "          </lambert>\n");
		fprintf (f, "        </technique>\n");
		fprintf (f, "      </profile_COMMON>\n");
		fprintf (f, "    </effect>\n");
	}
	fprintf (f, "  </library_effects>\n");

	fprintf (f, "  <library_materials>\n");
	for (size_t i = 0; i < model->num_materials; i++)
	{
		const material_t *mat = &model->materials[i];
		char material_name[256];
		dae_escape (material_name, sizeof (material_name), mat->name);
		fprintf (f, "    <material id=\"%s\" name=\"%s\">\n", material_ids[i], material_name);
		fprintf (f, "      <instance_effect url=\"#fx_%zu\"/>\n", i);
		fprintf (f, "    </material>\n");
	}
	fprintf (f, "  </library_materials>\n");

	fprintf (f, "  <library_geometries>\n");
	for (size_t i = 0; i < model->num_meshes; i++)
	{
		const mesh_t *mesh = &model->meshes[i];
		const char *mid = mesh_ids[i];
		char mesh_name[256];
		dae_escape (mesh_name, sizeof (mesh_name), mesh->name);
		fprintf (f, "    <geometry id=\"%s\" name=\"%s\">\n", mid, mesh_name);
		fprintf (f, "      <mesh>\n");

		// Positions
		fprintf (f, "        <source id=\"%s-positions\">\n", mid);
		fprintf (f, "          <float_array id=\"%s-positions-array\" count=\"%zu\">", mid,
			mesh->num_positions * 3);
		for (size_t j = 0; j < mesh->num_positions; j++)
		{
			fprintf (
				f, "%f %f %f ", mesh->positions[j].x, mesh->positions[j].y, mesh->positions[j].z);
		}
		fprintf (f, "</float_array>\n");
		fprintf (f, "          <technique_common>\n");
		fprintf (f,
			"            <accessor source=\"#%s-positions-array\" count=\"%zu\" stride=\"3\">\n",
			mid, mesh->num_positions);
		fprintf (f, "              <param name=\"X\" type=\"float\"/>\n");
		fprintf (f, "              <param name=\"Y\" type=\"float\"/>\n");
		fprintf (f, "              <param name=\"Z\" type=\"float\"/>\n");
		fprintf (f, "            </accessor>\n");
		fprintf (f, "          </technique_common>\n");
		fprintf (f, "        </source>\n");

		// Normals are optional in GX. Do not declare an empty source/input:
		// COLLADA importers correctly reject index 0 into a zero-length array.
		if (mesh->num_normals)
		{
			fprintf (f, "        <source id=\"%s-normals\">\n", mid);
			fprintf (f, "          <float_array id=\"%s-normals-array\" count=\"%zu\">", mid,
				mesh->num_normals * 3);
			for (size_t j = 0; j < mesh->num_normals; j++)
				fprintf (
					f, "%f %f %f ", mesh->normals[j].x, mesh->normals[j].y, mesh->normals[j].z);
			fprintf (f, "</float_array>\n");
			fprintf (f, "          <technique_common>\n");
			fprintf (f,
				"            <accessor source=\"#%s-normals-array\" count=\"%zu\" stride=\"3\">\n",
				mid, mesh->num_normals);
			fprintf (f, "              <param name=\"X\" type=\"float\"/>\n");
			fprintf (f, "              <param name=\"Y\" type=\"float\"/>\n");
			fprintf (f, "              <param name=\"Z\" type=\"float\"/>\n");
			fprintf (f, "            </accessor>\n");
			fprintf (f, "          </technique_common>\n");
			fprintf (f, "        </source>\n");
		}

		// Texture coordinates are optional too.
		if (mesh->num_texcoords)
		{
			fprintf (f, "        <source id=\"%s-texcoords\">\n", mid);
			fprintf (f, "          <float_array id=\"%s-texcoords-array\" count=\"%zu\">", mid,
				mesh->num_texcoords * 2);
			for (size_t j = 0; j < mesh->num_texcoords; j++)
			{
				// NW4R uses a top-down T axis; COLLADA's conventional texture
				// coordinate origin is bottom-left. BrawlCrate performs the
				// same conversion when exporting MDL0 UV sets.
				fprintf (f, "%f %f ", mesh->texcoords[j].u, 1.0f - mesh->texcoords[j].v);
			}
			fprintf (f, "</float_array>\n");
			fprintf (f, "          <technique_common>\n");
			fprintf (f,
				"            <accessor source=\"#%s-texcoords-array\" count=\"%zu\" "
				"stride=\"2\">\n",
				mid, mesh->num_texcoords);
			fprintf (f, "              <param name=\"S\" type=\"float\"/>\n");
			fprintf (f, "              <param name=\"T\" type=\"float\"/>\n");
			fprintf (f, "            </accessor>\n");
			fprintf (f, "          </technique_common>\n");
			fprintf (f, "        </source>\n");
		}

		// GX supports two indexed vertex-color sets.
		for (unsigned set = 0; set < 2; set++)
			if (mesh->num_colors[set])
			{
				fprintf (f, "        <source id=\"%s-colors-%u\">\n", mid, set);
				fprintf (f, "          <float_array id=\"%s-colors-%u-array\" count=\"%zu\">", mid,
					set, mesh->num_colors[set] * 4);
				for (size_t j = 0; j < mesh->num_colors[set]; j++)
				{
					const color4_t c = mesh->colors[set][j];
					fprintf (f, "%f %f %f %f ", c.r, c.g, c.b, c.a);
				}
				fprintf (f, "</float_array>\n");
				fprintf (f, "          <technique_common>\n");
				fprintf (f,
					"            <accessor source=\"#%s-colors-%u-array\" count=\"%zu\" "
					"stride=\"4\">\n",
					mid, set, mesh->num_colors[set]);
				fprintf (f, "              <param name=\"R\" type=\"float\"/>\n");
				fprintf (f, "              <param name=\"G\" type=\"float\"/>\n");
				fprintf (f, "              <param name=\"B\" type=\"float\"/>\n");
				fprintf (f, "              <param name=\"A\" type=\"float\"/>\n");
				fprintf (f, "            </accessor>\n");
				fprintf (f, "          </technique_common>\n");
				fprintf (f, "        </source>\n");
			}

		// UV1..UV7 are independent GX arrays, not aliases of UV0.
		for (unsigned set = 1; set < 8; set++)
		{
			const size_t count = mesh->num_extra_texcoords[set - 1];
			if (!count)
				continue;
			const vec2_t *uv = mesh->extra_texcoords[set - 1];
			fprintf (f, "        <source id=\"%s-texcoords-%u\">\n", mid, set);
			fprintf (f, "          <float_array id=\"%s-texcoords-%u-array\" count=\"%zu\">", mid,
				set, count * 2);
			for (size_t j = 0; j < count; j++)
				fprintf (f, "%f %f ", uv[j].u, 1.0f - uv[j].v);
			fprintf (f, "</float_array>\n");
			fprintf (f, "          <technique_common>\n");
			fprintf (f,
				"            <accessor source=\"#%s-texcoords-%u-array\" count=\"%zu\" "
				"stride=\"2\">\n",
				mid, set, count);
			fprintf (f, "              <param name=\"S\" type=\"float\"/>\n");
			fprintf (f, "              <param name=\"T\" type=\"float\"/>\n");
			fprintf (f, "            </accessor>\n");
			fprintf (f, "          </technique_common>\n");
			fprintf (f, "        </source>\n");
		}

		// Vertices
		fprintf (f, "        <vertices id=\"%s-vertices\">\n", mid);
		fprintf (f, "          <input semantic=\"POSITION\" source=\"#%s-positions\"/>\n", mid);
		fprintf (f, "        </vertices>\n");

		// Triangles
		int has_mat = mesh->material_idx >= 0 && (size_t)mesh->material_idx < model->num_materials;
		if (has_mat)
			fprintf (f, "        <triangles count=\"%zu\" material=\"%s\">\n",
				mesh->num_vertices / 3, material_ids[mesh->material_idx]);
		else
			fprintf (f, "        <triangles count=\"%zu\">\n", mesh->num_vertices / 3);
		fprintf (f, "          <input semantic=\"VERTEX\" source=\"#%s-vertices\" offset=\"0\"/>\n",
			mid);
		unsigned input_offset = 1;
		if (mesh->num_normals)
			fprintf (f,
				"          <input semantic=\"NORMAL\" source=\"#%s-normals\" offset=\"%u\"/>\n",
				mid, input_offset++);
		for (unsigned set = 0; set < 2; set++)
			if (mesh->num_colors[set])
				fprintf (f,
					"          <input semantic=\"COLOR\" source=\"#%s-colors-%u\" offset=\"%u\" "
					"set=\"%u\"/>\n",
					mid, set, input_offset++, set);
		if (mesh->num_texcoords)
			fprintf (f,
				"          <input semantic=\"TEXCOORD\" source=\"#%s-texcoords\" offset=\"%u\" "
				"set=\"0\"/>\n",
				mid, input_offset++);
		for (unsigned set = 1; set < 8; set++)
			if (mesh->num_extra_texcoords[set - 1])
				fprintf (f,
					"          <input semantic=\"TEXCOORD\" source=\"#%s-texcoords-%u\" "
					"offset=\"%u\" set=\"%u\"/>\n",
					mid, set, input_offset++, set);
		fprintf (f, "          <p>");
		for (size_t j = 0; j < mesh->num_vertices; j++)
		{
			fprintf (f, "%d ", mesh->vertices[j].position_idx);
			if (mesh->num_normals)
				fprintf (f, "%d ", mesh->vertices[j].normal_idx);
			for (unsigned set = 0; set < 2; set++)
				if (mesh->num_colors[set])
					fprintf (f, "%d ", mesh->vertices[j].color_idx[set]);
			if (mesh->num_texcoords)
				fprintf (f, "%d ", mesh->vertices[j].texcoord_idx);
			for (unsigned set = 1; set < 8; set++)
				if (mesh->num_extra_texcoords[set - 1])
					fprintf (f, "%d ", mesh->vertices[j].extra_texcoord_idx[set - 1]);
		}
		fprintf (f, "</p>\n");
		fprintf (f, "        </triangles>\n");

		fprintf (f, "      </mesh>\n");
		fprintf (f, "    </geometry>\n");
	}
	fprintf (f, "  </library_geometries>\n");

	// ---------------------------------------------------------------------
	// Skin controllers
	//
	// MDL0 positions are stored per matrix-node and baked to world space
	// above (BrawlCrate's Vertex3.WeightedPosition does the same), so the
	// bind shape matrix is identity and every joint's inverse bind matrix
	// cancels its own bind matrix at rest. Without this library the whole
	// rig was dropped and rigged models exported as frozen static meshes.
	// ---------------------------------------------------------------------
	int any_skin = 0;
	for (size_t i = 0; i < model->num_meshes; i++)
		if (dae_mesh_is_skinned (model, &model->meshes[i]))
		{
			any_skin = 1;
			break;
		}

	if (any_skin)
	{
		fprintf (f, "  <library_controllers>\n");
		for (size_t i = 0; i < model->num_meshes; i++)
		{
			const mesh_t *mesh = &model->meshes[i];
			if (!dae_mesh_is_skinned (model, mesh))
				continue;
			const char *mid = mesh_ids[i];

			size_t total_weights = 0;
			for (size_t v = 0; v < mesh->num_positions; v++)
				total_weights += model->node_influences[mesh->position_node[v]].num_weights;

			fprintf (f, "    <controller id=\"%s-skin\" name=\"%s-skin\">\n", mid, mid);
			fprintf (f, "      <skin source=\"#%s\">\n", mid);
			fprintf (f,
				"        <bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>\n");

			fprintf (f, "        <source id=\"%s-skin-joints\">\n", mid);
			fprintf (f, "          <Name_array id=\"%s-skin-joints-array\" count=\"%zu\">", mid,
				model->num_joints);
			for (size_t j = 0; j < model->num_joints; j++)
				fprintf (f, "%s%s", j ? " " : "", joint_ids[j]);
			fprintf (f, "</Name_array>\n");
			fprintf (f, "          <technique_common>\n");
			fprintf (f,
				"            <accessor source=\"#%s-skin-joints-array\" count=\"%zu\" "
				"stride=\"1\">\n",
				mid, model->num_joints);
			fprintf (f, "              <param name=\"JOINT\" type=\"name\"/>\n");
			fprintf (f, "            </accessor>\n");
			fprintf (f, "          </technique_common>\n");
			fprintf (f, "        </source>\n");

			fprintf (f, "        <source id=\"%s-skin-binds\">\n", mid);
			fprintf (f, "          <float_array id=\"%s-skin-binds-array\" count=\"%zu\">", mid,
				model->num_joints * 16);
			for (size_t j = 0; j < model->num_joints; j++)
			{
				const joint_t *joint = &model->joints[j];
				if (joint->has_inverse_bind)
					for (unsigned n = 0; n < 12; n++)
						fprintf (f, "%f ", joint->inverse_bind[n]);
				else
					fprintf (f, "1 0 0 0 0 1 0 0 0 0 1 0 ");
				fprintf (f, "0 0 0 1 ");
			}
			fprintf (f, "</float_array>\n");
			fprintf (f, "          <technique_common>\n");
			fprintf (f,
				"            <accessor source=\"#%s-skin-binds-array\" count=\"%zu\" "
				"stride=\"16\">\n",
				mid, model->num_joints);
			fprintf (f, "              <param name=\"TRANSFORM\" type=\"float4x4\"/>\n");
			fprintf (f, "            </accessor>\n");
			fprintf (f, "          </technique_common>\n");
			fprintf (f, "        </source>\n");

			fprintf (f, "        <source id=\"%s-skin-weights\">\n", mid);
			fprintf (f, "          <float_array id=\"%s-skin-weights-array\" count=\"%zu\">", mid,
				total_weights);
			for (size_t v = 0; v < mesh->num_positions; v++)
			{
				const node_influence_t *inf = &model->node_influences[mesh->position_node[v]];
				for (size_t w = 0; w < inf->num_weights; w++)
					fprintf (f, "%f ", inf->weights[w].weight);
			}
			fprintf (f, "</float_array>\n");
			fprintf (f, "          <technique_common>\n");
			fprintf (f,
				"            <accessor source=\"#%s-skin-weights-array\" count=\"%zu\" "
				"stride=\"1\">\n",
				mid, total_weights);
			fprintf (f, "              <param name=\"WEIGHT\" type=\"float\"/>\n");
			fprintf (f, "            </accessor>\n");
			fprintf (f, "          </technique_common>\n");
			fprintf (f, "        </source>\n");

			fprintf (f, "        <joints>\n");
			fprintf (f, "          <input semantic=\"JOINT\" source=\"#%s-skin-joints\"/>\n", mid);
			fprintf (f,
				"          <input semantic=\"INV_BIND_MATRIX\" source=\"#%s-skin-binds\"/>\n", mid);
			fprintf (f, "        </joints>\n");

			fprintf (f, "        <vertex_weights count=\"%zu\">\n", mesh->num_positions);
			fprintf (f,
				"          <input semantic=\"JOINT\" source=\"#%s-skin-joints\" offset=\"0\"/>\n",
				mid);
			fprintf (f,
				"          <input semantic=\"WEIGHT\" source=\"#%s-skin-weights\" offset=\"1\"/>\n",
				mid);
			fprintf (f, "          <vcount>");
			for (size_t v = 0; v < mesh->num_positions; v++)
				fprintf (f, "%zu ", model->node_influences[mesh->position_node[v]].num_weights);
			fprintf (f, "</vcount>\n");
			fprintf (f, "          <v>");
			for (size_t v = 0, running = 0; v < mesh->num_positions; v++)
			{
				const node_influence_t *inf = &model->node_influences[mesh->position_node[v]];
				for (size_t w = 0; w < inf->num_weights; w++)
					fprintf (f, "%d %zu ", inf->weights[w].bone_idx, running++);
			}
			fprintf (f, "</v>\n");
			fprintf (f, "        </vertex_weights>\n");
			fprintf (f, "      </skin>\n");
			fprintf (f, "    </controller>\n");
		}
		fprintf (f, "  </library_controllers>\n");
	}

	fprintf (f, "  <library_visual_scenes>\n");
	fprintf (f, "    <visual_scene id=\"Scene\" name=\"Scene\">\n");

	// Nested joint tree: every root (parent_idx == -1) recursively pulls
	// in its own children, and their children, etc.
	const char *skeleton_root = NULL;
	for (size_t i = 0; i < model->num_joints; i++)
		if (model->joints[i].name[0] && model->joints[i].parent_idx == -1)
		{
			if (!skeleton_root)
				skeleton_root = joint_ids[i];
			write_joint_node (f, model, joint_ids, i, 6);
		}

	for (size_t i = 0; i < model->num_meshes; i++)
	{
		const mesh_t *mesh = &model->meshes[i];
		const char *mid = mesh_ids[i];
		const int has_mat
			= mesh->material_idx >= 0 && (size_t)mesh->material_idx < model->num_materials;
		const int skinned = skeleton_root && dae_mesh_is_skinned (model, mesh);
		char mesh_name[256];
		dae_escape (mesh_name, sizeof (mesh_name), mesh->name);
		fprintf (f, "      <node id=\"%s-node\" name=\"%s\" type=\"NODE\">\n", mid, mesh_name);
		if (skinned)
		{
			fprintf (f, "        <instance_controller url=\"#%s-skin\">\n", mid);
			fprintf (f, "          <skeleton>#%s</skeleton>\n", skeleton_root);
		}
		else if (has_mat)
		{
			fprintf (f, "        <instance_geometry url=\"#%s\">\n", mid);
		}
		else
		{
			fprintf (f, "        <instance_geometry url=\"#%s\"/>\n", mid);
		}
		if (skinned || has_mat)
		{
			if (has_mat)
			{
				fprintf (f, "          <bind_material>\n");
				fprintf (f, "            <technique_common>\n");
				fprintf (f, "              <instance_material symbol=\"%s\" target=\"#%s\">\n",
					material_ids[mesh->material_idx], material_ids[mesh->material_idx]);
				// BrawlCrate names the bound sets TEXCOORD<n>; the effect's
				// texcoord attribute above uses the same spelling, so the
				// two always resolve against each other.
				if (mesh->num_texcoords)
					fprintf (f,
						"                <bind_vertex_input semantic=\"TEXCOORD0\" "
						"input_semantic=\"TEXCOORD\" input_set=\"0\"/>\n");
				for (unsigned set = 1; set < 8; set++)
					if (mesh->num_extra_texcoords[set - 1])
						fprintf (f,
							"                <bind_vertex_input semantic=\"TEXCOORD%u\" "
							"input_semantic=\"TEXCOORD\" input_set=\"%u\"/>\n",
							set, set);
				fprintf (f, "              </instance_material>\n");
				fprintf (f, "            </technique_common>\n");
				fprintf (f, "          </bind_material>\n");
			}
			fprintf (f, "        </%s>\n", skinned ? "instance_controller" : "instance_geometry");
		}
		fprintf (f, "      </node>\n");
	}

	fprintf (f, "    </visual_scene>\n");
	fprintf (f, "  </library_visual_scenes>\n");

	fprintf (f, "  <scene>\n");
	fprintf (f, "    <instance_visual_scene url=\"#Scene\"/>\n");
	fprintf (f, "  </scene>\n");

	fprintf (f, "</COLLADA>\n");

	const int failed = ferror (f);
	fclose (f);
	free (joint_ids);
	free (mesh_ids);
	free (material_ids);
	return failed ? -1 : 0;
}

// ---------------------------------------------------------------------------
// GLB (glTF 2.0 Binary) Exporter
// ---------------------------------------------------------------------------

typedef struct
{
	uint8_t *data;
	size_t size;
	size_t cap;
} glb_buffer_t;

typedef struct
{
	char *data;
	size_t size;
	size_t cap;
} glb_str_t;

static void glb_buf_align4 (glb_buffer_t *buf, uint8_t pad_byte)
{
	while (buf->size % 4 != 0)
	{
		if (buf->size >= buf->cap)
		{
			buf->cap = buf->cap ? buf->cap * 2 : 1024;
			buf->data = realloc (buf->data, buf->cap);
		}
		buf->data[buf->size++] = pad_byte;
	}
}

static size_t glb_buf_append (glb_buffer_t *buf, const void *src, size_t len)
{
	glb_buf_align4 (buf, 0);
	size_t offset = buf->size;
	if (buf->size + len > buf->cap)
	{
		buf->cap = (buf->size + len + 4096) * 2;
		buf->data = realloc (buf->data, buf->cap);
	}
	memcpy (buf->data + offset, src, len);
	buf->size += len;
	return offset;
}

static void glb_str_append (glb_str_t *str, const char *s)
{
	size_t len = strlen (s);
	if (str->size + len + 1 > str->cap)
	{
		str->cap = (str->size + len + 4096) * 2;
		str->data = realloc (str->data, str->cap);
	}
	memcpy (str->data + str->size, s, len);
	str->size += len;
	str->data[str->size] = '\0';
}

static void glb_str_printf (glb_str_t *str, const char *fmt, ...)
{
	char tmp[4096];
	va_list args;
	va_start (args, fmt);
	int n = vsnprintf (tmp, sizeof (tmp), fmt, args);
	va_end (args);
	if (n > 0)
	{
		glb_str_append (str, tmp);
	}
}

static void glb_json_escape_str (glb_str_t *str, const char *in)
{
	if (!in)
	{
		glb_str_append (str, "\"\"");
		return;
	}
	glb_str_append (str, "\"");
	for (const char *p = in; *p; p++)
	{
		if (*p == '"')
			glb_str_append (str, "\\\"");
		else if (*p == '\\')
			glb_str_append (str, "\\\\");
		else if (*p == '\n')
			glb_str_append (str, "\\n");
		else if (*p == '\r')
			glb_str_append (str, "\\r");
		else if (*p == '\t')
			glb_str_append (str, "\\t");
		else if ((unsigned char)*p < 0x20)
		{
			char hex[8];
			snprintf (hex, sizeof (hex), "\\u%04x", (unsigned char)*p);
			glb_str_append (str, hex);
		}
		else
		{
			char ch[2] = { *p, 0 };
			glb_str_append (str, ch);
		}
	}
	glb_str_append (str, "\"");
}

typedef struct
{
	size_t byteOffset;
	size_t byteLength;
	unsigned target; // 0, 34962, 34963
} glb_bv_entry_t;

typedef struct
{
	int bufferView;
	size_t byteOffset;
	unsigned componentType;
	size_t count;
	char type[16];
	float min_vals[4];
	float max_vals[4];
	int has_bounds;
} glb_acc_entry_t;

typedef struct
{
	char name[64];
	int bv_idx;
	int has_bv;
	char uri[PATH_MAX];
} glb_tex_image_t;

typedef struct
{
	unsigned wrapS;
	unsigned wrapT;
	unsigned minFilter;
	unsigned magFilter;
} glb_tex_sampler_t;

typedef struct
{
	int sampler;
	int source;
} glb_tex_entry_t;

typedef struct
{
	int acc_position;
	int acc_normal;
	int acc_tangent;
	int acc_texcoord[8];
	int num_texcoords;
	int acc_color[2];
	int num_colors;
	int acc_joints;
	int acc_weights;
	int acc_indices;
	int material_idx;
	int *acc_morph;
	size_t num_morph;
} glb_prim_info_t;

int ExportModelToGLB (const model_t *model, const char *out_glb_file)
{
	if (!model || !out_glb_file)
		return -1;

	glb_buffer_t bin = { 0 };
	glb_str_t json = { 0 };

	// Allocate dynamic tracking arrays
	glb_bv_entry_t *bvs = NULL;
	size_t num_bvs = 0, cap_bvs = 0;
	glb_acc_entry_t *accs = NULL;
	size_t num_accs = 0, cap_accs = 0;

#define ADD_BV(offset, len, tgt)                                                                   \
	do                                                                                             \
	{                                                                                              \
		if (num_bvs >= cap_bvs)                                                                    \
		{                                                                                          \
			cap_bvs = cap_bvs ? cap_bvs * 2 : 64;                                                  \
			bvs = realloc (bvs, cap_bvs * sizeof (*bvs));                                          \
		}                                                                                          \
		bvs[num_bvs].byteOffset = (offset);                                                        \
		bvs[num_bvs].byteLength = (len);                                                           \
		bvs[num_bvs].target = (tgt);                                                               \
		num_bvs++;                                                                                 \
	} while (0)

#define ADD_ACC(bv, off, comp, cnt, tstr, bounds, minv, maxv)                                      \
	do                                                                                             \
	{                                                                                              \
		if (num_accs >= cap_accs)                                                                  \
		{                                                                                          \
			cap_accs = cap_accs ? cap_accs * 2 : 64;                                               \
			accs = realloc (accs, cap_accs * sizeof (*accs));                                      \
		}                                                                                          \
		accs[num_accs].bufferView = (bv);                                                          \
		accs[num_accs].byteOffset = (off);                                                         \
		accs[num_accs].componentType = (comp);                                                     \
		accs[num_accs].count = (cnt);                                                              \
		snprintf (accs[num_accs].type, sizeof (accs[num_accs].type), "%s", (tstr));                \
		accs[num_accs].has_bounds = (bounds);                                                      \
		if ((bounds) && (minv) && (maxv))                                                          \
		{                                                                                          \
			memcpy (accs[num_accs].min_vals, minv, 4 * sizeof (float));                            \
			memcpy (accs[num_accs].max_vals, maxv, 4 * sizeof (float));                            \
		}                                                                                          \
		num_accs++;                                                                                \
	} while (0)

	// 1. Process Textures / Images / Samplers
	glb_tex_image_t *images = NULL;
	size_t num_images = 0, cap_images = 0;
	glb_tex_sampler_t *samplers = NULL;
	size_t num_samplers = 0, cap_samplers = 0;
	glb_tex_entry_t *textures = NULL;
	size_t num_textures = 0, cap_textures = 0;

	// Track texture index for material layer: [mat_idx][layer_idx]
	int mat_tex_idx[model->num_materials > 0 ? model->num_materials : 1][8];
	memset (mat_tex_idx, -1, sizeof (mat_tex_idx));

	for (size_t m = 0; m < model->num_materials; m++)
	{
		const material_t *mat = &model->materials[m];
		for (int t = 0; t < mat->num_textures; t++)
		{
			if (!mat->textures[t][0])
				continue;
			char tex_path[PATH_MAX];
			if (!dae_texture_path (tex_path, sizeof (tex_path), out_glb_file, mat->textures[t]))
				continue;
			dae_localize_texture (tex_path, sizeof (tex_path), out_glb_file);

			// Read image data from disk if accessible to embed into GLB
			char full_png_path[PATH_MAX];
			if (tex_path[0] == '/')
			{
				snprintf (full_png_path, sizeof (full_png_path), "%s", tex_path);
			}
			else
			{
				const char *slash = strrchr (out_glb_file, '/');
				if (slash)
					snprintf (full_png_path, sizeof (full_png_path), "%.*s/%s",
						(int)(slash - out_glb_file), out_glb_file, tex_path);
				else
					snprintf (full_png_path, sizeof (full_png_path), "%s", tex_path);
			}

			int img_bv = -1;
			FILE *fp = fopen (full_png_path, "rb");
			if (!fp)
				fp = fopen (tex_path, "rb");
			if (fp)
			{
				fseek (fp, 0, SEEK_END);
				long fsz = ftell (fp);
				fseek (fp, 0, SEEK_SET);
				if (fsz > 0)
				{
					uint8_t *img_data = malloc (fsz);
					if (img_data && fread (img_data, 1, fsz, fp) == (size_t)fsz)
					{
						size_t off = glb_buf_append (&bin, img_data, fsz);
						img_bv = (int)num_bvs;
						ADD_BV (off, fsz, 0);
					}
					free (img_data);
				}
				fclose (fp);
			}

			// Record image
			if (num_images >= cap_images)
			{
				cap_images = cap_images ? cap_images * 2 : 16;
				images = realloc (images, cap_images * sizeof (*images));
			}
			int img_idx = (int)num_images;
			snprintf (images[img_idx].name, sizeof (images[img_idx].name), "%s", mat->textures[t]);
			images[img_idx].bv_idx = img_bv;
			images[img_idx].has_bv = (img_bv >= 0);
			snprintf (images[img_idx].uri, sizeof (images[img_idx].uri), "%s", tex_path);
			num_images++;

			// Record sampler
			if (num_samplers >= cap_samplers)
			{
				cap_samplers = cap_samplers ? cap_samplers * 2 : 16;
				samplers = realloc (samplers, cap_samplers * sizeof (*samplers));
			}
			int smp_idx = (int)num_samplers;
			samplers[smp_idx].wrapS
				= mat->wrap_s[t] == 0 ? 33071 : (mat->wrap_s[t] == 2 ? 33648 : 10497);
			samplers[smp_idx].wrapT
				= mat->wrap_t[t] == 0 ? 33071 : (mat->wrap_t[t] == 2 ? 33648 : 10497);
			samplers[smp_idx].minFilter = mat->min_filter[t] ? 9729 : 9728;
			samplers[smp_idx].magFilter = mat->mag_filter[t] ? 9729 : 9728;
			num_samplers++;

			// Record texture
			if (num_textures >= cap_textures)
			{
				cap_textures = cap_textures ? cap_textures * 2 : 16;
				textures = realloc (textures, cap_textures * sizeof (*textures));
			}
			int tex_entry_idx = (int)num_textures;
			textures[tex_entry_idx].sampler = smp_idx;
			textures[tex_entry_idx].source = img_idx;
			num_textures++;

			mat_tex_idx[m][t] = tex_entry_idx;
		}
	}

	// 2. Process Inverse Bind Matrices (if joints exist)
	int acc_ibm = -1;
	int any_skin = 0;
	for (size_t i = 0; i < model->num_meshes; i++)
		if (dae_mesh_is_skinned (model, &model->meshes[i]))
		{
			any_skin = 1;
			break;
		}

	if (any_skin && model->num_joints > 0)
	{
		float *ibm = malloc (model->num_joints * 16 * sizeof (float));
		if (ibm)
		{
			for (size_t j = 0; j < model->num_joints; j++)
			{
				const joint_t *joint = &model->joints[j];
				float *m = ibm + j * 16;
				if (joint->has_inverse_bind)
				{
					const float *inv = joint->inverse_bind;
					m[0] = inv[0];
					m[1] = inv[4];
					m[2] = inv[8];
					m[3] = 0.0f;
					m[4] = inv[1];
					m[5] = inv[5];
					m[6] = inv[9];
					m[7] = 0.0f;
					m[8] = inv[2];
					m[9] = inv[6];
					m[10] = inv[10];
					m[11] = 0.0f;
					m[12] = inv[3];
					m[13] = inv[7];
					m[14] = inv[11];
					m[15] = 1.0f;
				}
				else
				{
					memset (m, 0, 16 * sizeof (float));
					m[0] = m[5] = m[10] = m[15] = 1.0f;
				}
			}
			size_t off = glb_buf_append (&bin, ibm, model->num_joints * 16 * sizeof (float));
			int bv = (int)num_bvs;
			ADD_BV (off, model->num_joints * 16 * sizeof (float), 0);
			acc_ibm = (int)num_accs;
			ADD_ACC (bv, 0, 5126, model->num_joints, "MAT4", 0, NULL, NULL);
			free (ibm);
		}
	}

	// 3. Process Meshes / Primitives
	glb_prim_info_t *prims
		= calloc (model->num_meshes > 0 ? model->num_meshes : 1, sizeof (*prims));
	for (size_t m = 0; m < model->num_meshes; m++)
	{
		const mesh_t *mesh = &model->meshes[m];
		glb_prim_info_t *prim = &prims[m];
		prim->acc_position = -1;
		prim->acc_normal = -1;
		prim->acc_tangent = -1;
		for (int i = 0; i < 8; i++)
			prim->acc_texcoord[i] = -1;
		prim->acc_color[0] = prim->acc_color[1] = -1;
		prim->acc_joints = -1;
		prim->acc_weights = -1;
		prim->acc_indices = -1;
		prim->material_idx = mesh->material_idx;
		prim->num_morph = mesh->num_morph_targets;
		prim->acc_morph = mesh->num_morph_targets
			? malloc (mesh->num_morph_targets * sizeof (*prim->acc_morph))
			: NULL;
		for (size_t t = 0; t < mesh->num_morph_targets; t++)
			prim->acc_morph[t] = -1;

		const size_t N = mesh->num_vertices;
		if (!N)
			continue;

		const int skinned = dae_mesh_is_skinned (model, mesh);

		// Unified vertex arrays
		vec3_t *v_pos = malloc (N * sizeof (vec3_t));
		float min_p[4] = { 1e30f, 1e30f, 1e30f, 0.0f };
		float max_p[4] = { -1e30f, -1e30f, -1e30f, 0.0f };
		for (size_t v = 0; v < N; v++)
		{
			int pi = mesh->vertices[v].position_idx;
			vec3_t p = (pi >= 0 && (size_t)pi < mesh->num_positions) ? mesh->positions[pi]
																	 : (vec3_t) { 0, 0, 0 };
			v_pos[v] = p;
			if (p.x < min_p[0])
				min_p[0] = p.x;
			if (p.y < min_p[1])
				min_p[1] = p.y;
			if (p.z < min_p[2])
				min_p[2] = p.z;
			if (p.x > max_p[0])
				max_p[0] = p.x;
			if (p.y > max_p[1])
				max_p[1] = p.y;
			if (p.z > max_p[2])
				max_p[2] = p.z;
		}

		size_t off_pos = glb_buf_append (&bin, v_pos, N * sizeof (vec3_t));
		int bv_pos = (int)num_bvs;
		ADD_BV (off_pos, N * sizeof (vec3_t), 34962);
		prim->acc_position = (int)num_accs;
		ADD_ACC (bv_pos, 0, 5126, N, "VEC3", 1, min_p, max_p);
		free (v_pos);

		// Morph POSITION values are deltas, not alternate absolute positions.
		for (size_t t = 0; t < mesh->num_morph_targets; t++)
		{
			const morph_target_t *mt = mesh->morph_targets + t;
			if (!mt->position_deltas || !mt->num_positions)
				continue;
			vec3_t *delta = calloc (N, sizeof (*delta));
			if (!delta)
				continue;
			for (size_t v = 0; v < N; v++)
			{
				int pi = mesh->vertices[v].position_idx;
				if (pi >= 0 && (size_t)pi < mt->num_positions)
					delta[v] = mt->position_deltas[pi];
			}
			size_t moff = glb_buf_append (&bin, delta, N * sizeof (*delta));
			int mbv = (int)num_bvs;
			ADD_BV (moff, N * sizeof (*delta), 34962);
			prim->acc_morph[t] = (int)num_accs;
			ADD_ACC (mbv, 0, 5126, N, "VEC3", 0, NULL, NULL);
			free (delta);
		}

		// Normals
		if (mesh->num_normals > 0)
		{
			vec3_t *v_nrm = malloc (N * sizeof (vec3_t));
			for (size_t v = 0; v < N; v++)
			{
				int ni = mesh->vertices[v].normal_idx;
				v_nrm[v] = (ni >= 0 && (size_t)ni < mesh->num_normals) ? mesh->normals[ni]
																	   : (vec3_t) { 0, 1, 0 };
			}
			size_t off_nrm = glb_buf_append (&bin, v_nrm, N * sizeof (vec3_t));
			int bv_nrm = (int)num_bvs;
			ADD_BV (off_nrm, N * sizeof (vec3_t), 34962);
			prim->acc_normal = (int)num_accs;
			ADD_ACC (bv_nrm, 0, 5126, N, "VEC3", 0, NULL, NULL);
			free (v_nrm);
		}

		// Tangents
		prim->acc_tangent = -1;
		if (mesh->num_tangents > 0)
		{
			vec3_t *v_tan = malloc (N * sizeof (vec3_t));
			for (size_t v = 0; v < N; v++)
			{
				int ti = mesh->vertices[v].tangent_idx;
				v_tan[v] = (ti >= 0 && (size_t)ti < mesh->num_tangents) ? mesh->tangents[ti]
																		: (vec3_t) { 1, 0, 0 };
			}
			size_t off_tan = glb_buf_append (&bin, v_tan, N * sizeof (vec3_t));
			int bv_tan = (int)num_bvs;
			ADD_BV (off_tan, N * sizeof (vec3_t), 34962);
			prim->acc_tangent = (int)num_accs;
			ADD_ACC (bv_tan, 0, 5126, N, "VEC3", 0, NULL, NULL);
			free (v_tan);
		}

		// UV 0
		if (mesh->num_texcoords > 0)
		{
			vec2_t *v_uv = malloc (N * sizeof (vec2_t));
			for (size_t v = 0; v < N; v++)
			{
				int ti = mesh->vertices[v].texcoord_idx;
				v_uv[v] = (ti >= 0 && (size_t)ti < mesh->num_texcoords) ? mesh->texcoords[ti]
																		: (vec2_t) { 0, 0 };
			}
			size_t off_uv = glb_buf_append (&bin, v_uv, N * sizeof (vec2_t));
			int bv_uv = (int)num_bvs;
			ADD_BV (off_uv, N * sizeof (vec2_t), 34962);
			prim->acc_texcoord[0] = (int)num_accs;
			prim->num_texcoords = 1;
			ADD_ACC (bv_uv, 0, 5126, N, "VEC2", 0, NULL, NULL);
			free (v_uv);
		}

		// Extra UVs
		for (int set = 1; set < 8; set++)
		{
			if (mesh->num_extra_texcoords[set - 1] > 0)
			{
				const size_t num_ex = mesh->num_extra_texcoords[set - 1];
				const vec2_t *ex_uv = mesh->extra_texcoords[set - 1];
				vec2_t *v_uv = malloc (N * sizeof (vec2_t));
				for (size_t v = 0; v < N; v++)
				{
					int ti = mesh->vertices[v].extra_texcoord_idx[set - 1];
					v_uv[v] = (ti >= 0 && (size_t)ti < num_ex) ? ex_uv[ti] : (vec2_t) { 0, 0 };
				}
				size_t off_uv = glb_buf_append (&bin, v_uv, N * sizeof (vec2_t));
				int bv_uv = (int)num_bvs;
				ADD_BV (off_uv, N * sizeof (vec2_t), 34962);
				prim->acc_texcoord[set] = (int)num_accs;
				prim->num_texcoords = set + 1;
				ADD_ACC (bv_uv, 0, 5126, N, "VEC2", 0, NULL, NULL);
				free (v_uv);
			}
		}

		// Colors
		if (mesh->num_colors[0] > 0)
		{
			color4_t *v_col = malloc (N * sizeof (color4_t));
			for (size_t v = 0; v < N; v++)
			{
				int ci = mesh->vertices[v].color_idx[0];
				v_col[v] = (ci >= 0 && (size_t)ci < mesh->num_colors[0])
					? mesh->colors[0][ci]
					: (color4_t) { 1, 1, 1, 1 };
			}
			size_t off_col = glb_buf_append (&bin, v_col, N * sizeof (color4_t));
			int bv_col = (int)num_bvs;
			ADD_BV (off_col, N * sizeof (color4_t), 34962);
			prim->acc_color[0] = (int)num_accs;
			prim->num_colors = 1;
			ADD_ACC (bv_col, 0, 5126, N, "VEC4", 0, NULL, NULL);
			free (v_col);
		}

		// Joints and Weights (Skinning)
		if (skinned && model->num_joints > 0)
		{
			uint16_t *v_jnt = calloc (N * 4, sizeof (uint16_t));
			float *v_wt = calloc (N * 4, sizeof (float));
			for (size_t v = 0; v < N; v++)
			{
				int pi = mesh->vertices[v].position_idx;
				int node = mesh->position_node ? mesh->position_node[pi] : -1;
				if (node >= 0 && (size_t)node < model->num_node_influences
					&& model->node_influences[node].num_weights > 0)
				{
					const node_influence_t *inf = &model->node_influences[node];
					float total_w = 0.0f;
					for (size_t w = 0; w < 4 && w < inf->num_weights; w++)
					{
						v_jnt[v * 4 + w] = (uint16_t)inf->weights[w].bone_idx;
						v_wt[v * 4 + w] = inf->weights[w].weight;
						total_w += inf->weights[w].weight;
					}
					if (total_w > 0.0f)
					{
						for (size_t w = 0; w < 4; w++)
							v_wt[v * 4 + w] /= total_w;
					}
				}
				else
				{
					v_jnt[v * 4 + 0] = 0;
					v_wt[v * 4 + 0] = 1.0f;
				}
			}

			size_t off_jnt = glb_buf_append (&bin, v_jnt, N * 4 * sizeof (uint16_t));
			int bv_jnt = (int)num_bvs;
			ADD_BV (off_jnt, N * 4 * sizeof (uint16_t), 34962);
			prim->acc_joints = (int)num_accs;
			ADD_ACC (bv_jnt, 0, 5123, N, "VEC4", 0, NULL, NULL);
			free (v_jnt);

			size_t off_wt = glb_buf_append (&bin, v_wt, N * 4 * sizeof (float));
			int bv_wt = (int)num_bvs;
			ADD_BV (off_wt, N * 4 * sizeof (float), 34962);
			prim->acc_weights = (int)num_accs;
			ADD_ACC (bv_wt, 0, 5126, N, "VEC4", 0, NULL, NULL);
			free (v_wt);
		}

		// Indices
		if (N < 65536)
		{
			uint16_t *v_idx = malloc (N * sizeof (uint16_t));
			for (size_t v = 0; v < N; v++)
				v_idx[v] = (uint16_t)v;
			size_t off_idx = glb_buf_append (&bin, v_idx, N * sizeof (uint16_t));
			int bv_idx = (int)num_bvs;
			ADD_BV (off_idx, N * sizeof (uint16_t), 34963);
			prim->acc_indices = (int)num_accs;
			ADD_ACC (bv_idx, 0, 5123, N, "SCALAR", 0, NULL, NULL);
			free (v_idx);
		}
		else
		{
			uint32_t *v_idx = malloc (N * sizeof (uint32_t));
			for (size_t v = 0; v < N; v++)
				v_idx[v] = (uint32_t)v;
			size_t off_idx = glb_buf_append (&bin, v_idx, N * sizeof (uint32_t));
			int bv_idx = (int)num_bvs;
			ADD_BV (off_idx, N * sizeof (uint32_t), 34963);
			prim->acc_indices = (int)num_accs;
			ADD_ACC (bv_idx, 0, 5125, N, "SCALAR", 0, NULL, NULL);
			free (v_idx);
		}
	}

	int **anim_in
		= model->num_animations ? calloc (model->num_animations, sizeof (*anim_in)) : NULL;
	int **anim_out
		= model->num_animations ? calloc (model->num_animations, sizeof (*anim_out)) : NULL;
	for (size_t a = 0; a < model->num_animations; a++)
	{
		size_t nc = model->animations[a].num_channels;
		anim_in[a] = malloc (nc * sizeof (int));
		anim_out[a] = malloc (nc * sizeof (int));
		for (size_t c = 0; c < nc; c++)
		{
			const model_anim_channel_t *ch = model->animations[a].channels + c;
			anim_in[a][c] = anim_out[a][c] = -1;
			if (!ch->count || !ch->times || !ch->values)
				continue;
			float bounds_min[4] = { ch->times[0], 0, 0, 0 },
				  bounds_max[4] = { ch->times[ch->count - 1], 0, 0, 0 };
			size_t x = glb_buf_append (&bin, ch->times, ch->count * sizeof (float));
			int bv = (int)num_bvs;
			ADD_BV (x, ch->count * sizeof (float), 0);
			anim_in[a][c] = (int)num_accs;
			ADD_ACC (bv, 0, 5126, ch->count, "SCALAR", 1, bounds_min, bounds_max);
			x = glb_buf_append (&bin, ch->values, ch->count * ch->components * sizeof (float));
			bv = (int)num_bvs;
			ADD_BV (x, ch->count * ch->components * sizeof (float), 0);
			anim_out[a][c] = (int)num_accs;
			ADD_ACC (bv, 0, 5126,
				ch->path == MODEL_ANIM_WEIGHTS ? ch->count * ch->components : ch->count,
				ch->path == MODEL_ANIM_WEIGHTS ? "SCALAR"
					: ch->components == 4	   ? "VEC4"
					: ch->components == 3	   ? "VEC3"
											   : "SCALAR",
				0, NULL, NULL);
		}
	}

	// 4. Construct JSON Document
	glb_str_append (&json,
		"{\"asset\":{\"generator\":\"wiimms-szs-tools-plus\",\"version\":\"2.0\"},\"scene\":0,"
		"\"scenes\":[{\"nodes\":[");
	int first_scene_node = 1;
	for (size_t j = 0; j < model->num_joints; j++)
	{
		if (model->joints[j].parent_idx == -1)
		{
			if (!first_scene_node)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "%zu", j);
			first_scene_node = 0;
		}
	}
	for (size_t m = 0; m < model->num_meshes; m++)
	{
		if (!first_scene_node)
			glb_str_append (&json, ",");
		glb_str_printf (&json, "%zu", model->num_joints + m);
		first_scene_node = 0;
	}
	for (size_t i = 0; i < model->num_instances; i++)
	{
		if (model->instances[i].parent_idx >= 0)
			continue;
		if (!first_scene_node)
			glb_str_append (&json, ",");
		glb_str_printf (&json, "%zu", model->num_joints + model->num_meshes + i);
		first_scene_node = 0;
	}
	const size_t scene_object_base = model->num_joints + model->num_meshes + model->num_instances;
	for (size_t i = 0; i < model->num_cameras + model->num_lights; i++)
	{
		if (!first_scene_node)
			glb_str_append (&json, ",");
		glb_str_printf (&json, "%zu", scene_object_base + i);
		first_scene_node = 0;
	}
	glb_str_append (&json, "]}],\"nodes\":[");

	// Joint Nodes
	for (size_t j = 0; j < model->num_joints; j++)
	{
		if (j > 0)
			glb_str_append (&json, ",");
		const joint_t *joint = &model->joints[j];
		glb_str_append (&json, "{\"name\":");
		glb_json_escape_str (&json, joint->name);
		const double hx = joint->rotate.x * M_PI / 360.0, hy = joint->rotate.y * M_PI / 360.0,
					 hz = joint->rotate.z * M_PI / 360.0;
		const double cx = cos (hx), sx = sin (hx), cy = cos (hy), sy = sin (hy), cz = cos (hz),
					 sz = sin (hz);
		const double qx = sx * cy * cz - cx * sy * sz, qy = cx * sy * cz + sx * cy * sz,
					 qz = cx * cy * sz - sx * sy * cz, qw = cx * cy * cz + sx * sy * sz;
		glb_str_printf (&json,
			",\"translation\":[%g,%g,%g],\"rotation\":[%g,%g,%g,%g],\"scale\":[%g,%g,%g]",
			joint->translate.x, joint->translate.y, joint->translate.z, qx, qy, qz, qw,
			joint->scale.x, joint->scale.y, joint->scale.z);

		// Children
		int has_children = 0;
		for (size_t c = 0; c < model->num_joints; c++)
		{
			if (model->joints[c].parent_idx == (int)j)
			{
				if (!has_children)
				{
					glb_str_append (&json, ",\"children\":[");
					has_children = 1;
				}
				else
				{
					glb_str_append (&json, ",");
				}
				glb_str_printf (&json, "%zu", c);
			}
		}
		for (size_t i = 0; i < model->num_instances; i++)
		{
			if (model->instances[i].parent_idx != (int)j)
				continue;
			if (!has_children)
			{
				glb_str_append (&json, ",\"children\":[");
				has_children = 1;
			}
			else
				glb_str_append (&json, ",");
			glb_str_printf (&json, "%zu", model->num_joints + model->num_meshes + i);
		}
		if (has_children)
			glb_str_append (&json, "]");
		glb_str_append (&json, "}");
	}

	// Mesh Nodes
	for (size_t m = 0; m < model->num_meshes; m++)
	{
		if (model->num_joints > 0 || m > 0)
			glb_str_append (&json, ",");
		const mesh_t *mesh = &model->meshes[m];
		const int skinned = dae_mesh_is_skinned (model, mesh);
		glb_str_append (&json, "{\"name\":");
		glb_json_escape_str (&json, mesh->name);
		glb_str_printf (&json, ",\"mesh\":%zu", m);
		if (skinned && model->num_joints > 0 && acc_ibm >= 0)
		{
			glb_str_append (&json, ",\"skin\":0");
		}
		glb_str_append (&json, "}");
	}
	// Reusable mesh-instance nodes (HSF replicas). HSF Euler angles are in
	// degrees and use the same XYZ order as joints; a matrix avoids imposing
	// quaternion conventions on the shared representation.
	for (size_t i = 0; i < model->num_instances; i++)
	{
		const model_instance_t *in = model->instances + i;
		glb_str_append (&json, ",{\"name\":");
		glb_json_escape_str (&json, in->name);
		glb_str_printf (&json, ",\"mesh\":%d", in->mesh_idx);
		if (in->has_matrix)
		{
			glb_str_append (&json, ",\"matrix\":[");
			for (int k = 0; k < 16; k++)
			{
				if (k)
					glb_str_append (&json, ",");
				glb_str_printf (&json, "%g", in->matrix[k]);
			}
			glb_str_append (&json, "]}");
			continue;
		}
		const double rx = in->rotate.x * M_PI / 180.0, ry = in->rotate.y * M_PI / 180.0,
					 rz = in->rotate.z * M_PI / 180.0;
		const double cx = cos (rx), sx = sin (rx), cy = cos (ry), sy = sin (ry), cz = cos (rz),
					 sz = sin (rz);
		const double r00 = cy * cz, r01 = cz * sx * sy - cx * sz, r02 = sx * sz + cx * cz * sy;
		const double r10 = cy * sz, r11 = cx * cz + sx * sy * sz, r12 = cx * sy * sz - cz * sx;
		const double r20 = -sy, r21 = cy * sx, r22 = cx * cy;
		glb_str_printf (&json, ",\"matrix\":[%g,%g,%g,0,%g,%g,%g,0,%g,%g,%g,0,%g,%g,%g,1]",
			r00 * in->scale.x, r10 * in->scale.x, r20 * in->scale.x, r01 * in->scale.y,
			r11 * in->scale.y, r21 * in->scale.y, r02 * in->scale.z, r12 * in->scale.z,
			r22 * in->scale.z, in->translate.x, in->translate.y, in->translate.z);
		glb_str_append (&json, "}");
	}
	for (size_t i = 0; i < model->num_cameras; i++)
	{
		const model_camera_t *c = model->cameras + i;
		if (scene_object_base + i)
			glb_str_append (&json, ",");
		glb_str_append (&json, "{\"name\":");
		glb_json_escape_str (&json, c->name);
		glb_str_printf (&json, ",\"camera\":%zu,\"matrix\":[", i);
		for (int k = 0; k < 16; k++)
		{
			if (k)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "%g", c->matrix[k]);
		}
		glb_str_append (&json, "]}");
	}
	for (size_t i = 0; i < model->num_lights; i++)
	{
		const model_light_t *l = model->lights + i;
		if (scene_object_base + model->num_cameras + i)
			glb_str_append (&json, ",");
		glb_str_append (&json, "{\"name\":");
		glb_json_escape_str (&json, l->name);
		glb_str_append (&json, ",\"matrix\":[");
		for (int k = 0; k < 16; k++)
		{
			if (k)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "%g", l->matrix[k]);
		}
		glb_str_printf (&json, "],\"extensions\":{\"KHR_lights_punctual\":{\"light\":%zu}}}", i);
	}
	glb_str_append (&json, "]");

	if (model->num_cameras)
	{
		glb_str_append (&json, ",\"cameras\":[");
		for (size_t i = 0; i < model->num_cameras; i++)
		{
			if (i)
				glb_str_append (&json, ",");
			const model_camera_t *c = model->cameras + i;
			glb_str_append (&json, "{\"name\":");
			glb_json_escape_str (&json, c->name);
			glb_str_printf (&json,
				",\"type\":\"perspective\",\"perspective\":{\"yfov\":%g,\"znear\":%g", c->yfov,
				c->znear);
			if (c->zfar > c->znear)
				glb_str_printf (&json, ",\"zfar\":%g", c->zfar);
			glb_str_append (&json, "}}");
		}
		glb_str_append (&json, "]");
	}

	// Detect extensions needed
	int has_lights = (model->num_lights > 0);
	int has_tex_transform = 0;
	for (size_t m = 0; m < model->num_materials && !has_tex_transform; m++)
	{
		const material_t *mat = &model->materials[m];
		int primary = dae_primary_texture (mat, out_glb_file);
		if (primary >= 0 && primary < 8 && mat->has_tex_transform[primary])
			has_tex_transform = 1;
	}
	if (has_lights || has_tex_transform)
	{
		glb_str_append (&json, ",\"extensionsUsed\":[");
		int first_ext = 1;
		if (has_lights)
		{
			glb_str_append (&json, "\"KHR_lights_punctual\"");
			first_ext = 0;
		}
		if (has_tex_transform)
		{
			if (!first_ext)
				glb_str_append (&json, ",");
			glb_str_append (&json, "\"KHR_texture_transform\"");
		}
		glb_str_append (&json, "]");
	}
	if (has_lights)
	{
		glb_str_append (&json, ",\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[");
		for (size_t i = 0; i < model->num_lights; i++)
		{
			if (i)
				glb_str_append (&json, ",");
			const model_light_t *l = model->lights + i;
			const char *type = l->kind == MODEL_LIGHT_DIRECTIONAL ? "directional"
				: l->kind == MODEL_LIGHT_SPOT					  ? "spot"
																  : "point";
			glb_str_append (&json, "{\"name\":");
			glb_json_escape_str (&json, l->name);
			glb_str_printf (&json, ",\"type\":\"%s\",\"color\":[%g,%g,%g],\"intensity\":%g", type,
				l->color[0], l->color[1], l->color[2], l->intensity > 0 ? l->intensity : 1);
			if (l->range > 0)
				glb_str_printf (&json, ",\"range\":%g", l->range);
			if (l->kind == MODEL_LIGHT_SPOT)
				glb_str_printf (&json, ",\"spot\":{\"innerConeAngle\":%g,\"outerConeAngle\":%g}",
					l->inner_cone, l->outer_cone);
			glb_str_append (&json, "}");
		}
		glb_str_append (&json, "]}}");
	}

	// Materials
	if (model->num_materials > 0)
	{
		glb_str_append (&json, ",\"materials\":[");
		for (size_t m = 0; m < model->num_materials; m++)
		{
			if (m > 0)
				glb_str_append (&json, ",");
			const material_t *mat = &model->materials[m];
			int primary = dae_primary_texture (mat, out_glb_file);
			int tex_idx = (primary >= 0 && primary < 8) ? mat_tex_idx[m][primary] : -1;
			glb_str_append (&json, "{\"name\":");
			glb_json_escape_str (&json, mat->name);
			glb_str_append (&json, ",\"pbrMetallicRoughness\":{");
			if (tex_idx >= 0)
			{
				glb_str_append (&json, "\"baseColorTexture\":{\"index\":");
				glb_str_printf (&json, "%d", tex_idx);
				// KHR_texture_transform on the primary texture layer
				if (primary >= 0 && primary < 8 && mat->has_tex_transform[primary])
				{
					glb_str_append (&json, ",\"extensions\":{\"KHR_texture_transform\":{");
					glb_str_printf (&json, "\"offset\":[%g,%g]", mat->tex_translate_s[primary],
						mat->tex_translate_t[primary]);
					glb_str_printf (&json, ",\"rotation\":%g", mat->tex_rotate[primary]);
					glb_str_printf (&json, ",\"scale\":[%g,%g]", mat->tex_scale_s[primary],
						mat->tex_scale_t[primary]);
					glb_str_append (&json, "}}");
				}
				glb_str_printf (&json, "},\"metallicFactor\":0.0,\"roughnessFactor\":%g",
					mat->shininess > 0 ? (1.0f - 1.0f / (1.0f + mat->shininess * 0.01f)) * 0.9f
									   : 0.9f);
			}
			else
			{
				float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.0f;
				if (mat->diffuse[0] || mat->diffuse[1] || mat->diffuse[2] || mat->diffuse[3])
				{
					r = mat->diffuse[0];
					g = mat->diffuse[1];
					b = mat->diffuse[2];
					a = mat->diffuse[3];
				}
				glb_str_printf (&json,
					"\"baseColorFactor\":[%g,%g,%g,%g],\"metallicFactor\":0.0,\"roughnessFactor\":%"
					"g",
					r, g, b, a,
					mat->shininess > 0 ? (1.0f - 1.0f / (1.0f + mat->shininess * 0.01f)) * 0.9f
									   : 0.9f);
			}
			glb_str_append (&json, "},\"doubleSided\":true");
			if (mat->ambient[0] > 0 || mat->ambient[1] > 0 || mat->ambient[2] > 0)
				glb_str_printf (&json, ",\"emissiveFactor\":[%g,%g,%g]", mat->ambient[0],
					mat->ambient[1], mat->ambient[2]);
			if (mat->diffuse[3] > 0 && mat->diffuse[3] < 1.0f)
				glb_str_append (&json, ",\"alphaMode\":\"BLEND\"");
			else
				glb_str_append (&json, ",\"alphaMode\":\"OPAQUE\"");
			glb_str_append (&json, "}");
		}
		glb_str_append (&json, "]");
	}

	// Textures / Images / Samplers
	if (num_textures > 0)
	{
		glb_str_append (&json, ",\"textures\":[");
		for (size_t t = 0; t < num_textures; t++)
		{
			if (t > 0)
				glb_str_append (&json, ",");
			glb_str_printf (
				&json, "{\"sampler\":%d,\"source\":%d}", textures[t].sampler, textures[t].source);
		}
		glb_str_append (&json, "]");
	}

	if (num_images > 0)
	{
		glb_str_append (&json, ",\"images\":[");
		for (size_t i = 0; i < num_images; i++)
		{
			if (i > 0)
				glb_str_append (&json, ",");
			glb_str_append (&json, "{\"name\":");
			glb_json_escape_str (&json, images[i].name);
			if (images[i].has_bv)
			{
				glb_str_printf (
					&json, ",\"mimeType\":\"image/png\",\"bufferView\":%d}", images[i].bv_idx);
			}
			else
			{
				glb_str_append (&json, ",\"uri\":");
				glb_json_escape_str (&json, images[i].uri);
				glb_str_append (&json, "}");
			}
		}
		glb_str_append (&json, "]");
	}

	if (num_samplers > 0)
	{
		glb_str_append (&json, ",\"samplers\":[");
		for (size_t s = 0; s < num_samplers; s++)
		{
			if (s > 0)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "{\"magFilter\":%u,\"minFilter\":%u,\"wrapS\":%u,\"wrapT\":%u}",
				samplers[s].magFilter, samplers[s].minFilter, samplers[s].wrapS, samplers[s].wrapT);
		}
		glb_str_append (&json, "]");
	}

	// Meshes
	glb_str_append (&json, ",\"meshes\":[");
	for (size_t m = 0; m < model->num_meshes; m++)
	{
		if (m > 0)
			glb_str_append (&json, ",");
		const mesh_t *mesh = &model->meshes[m];
		const glb_prim_info_t *prim = &prims[m];
		glb_str_append (&json, "{\"name\":");
		glb_json_escape_str (&json, mesh->name);
		glb_str_append (&json, ",\"primitives\":[{\"attributes\":{");
		int first_attr = 1;
		if (prim->acc_position >= 0)
		{
			glb_str_printf (&json, "\"POSITION\":%d", prim->acc_position);
			first_attr = 0;
		}
		if (prim->acc_normal >= 0)
		{
			if (!first_attr)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "\"NORMAL\":%d", prim->acc_normal);
			first_attr = 0;
		}
		if (prim->acc_tangent >= 0)
		{
			if (!first_attr)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "\"TANGENT\":%d", prim->acc_tangent);
			first_attr = 0;
		}
		for (int set = 0; set < prim->num_texcoords; set++)
		{
			if (prim->acc_texcoord[set] >= 0)
			{
				if (!first_attr)
					glb_str_append (&json, ",");
				glb_str_printf (&json, "\"TEXCOORD_%d\":%d", set, prim->acc_texcoord[set]);
				first_attr = 0;
			}
		}
		for (int set = 0; set < prim->num_colors; set++)
		{
			if (prim->acc_color[set] >= 0)
			{
				if (!first_attr)
					glb_str_append (&json, ",");
				glb_str_printf (&json, "\"COLOR_%d\":%d", set, prim->acc_color[set]);
				first_attr = 0;
			}
		}
		if (prim->acc_joints >= 0)
		{
			if (!first_attr)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "\"JOINTS_0\":%d", prim->acc_joints);
			first_attr = 0;
		}
		if (prim->acc_weights >= 0)
		{
			if (!first_attr)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "\"WEIGHTS_0\":%d", prim->acc_weights);
			first_attr = 0;
		}
		glb_str_append (&json, "}");
		if (prim->acc_indices >= 0)
		{
			glb_str_printf (&json, ",\"indices\":%d", prim->acc_indices);
		}
		if (prim->material_idx >= 0 && (size_t)prim->material_idx < model->num_materials)
		{
			glb_str_printf (&json, ",\"material\":%d", prim->material_idx);
		}
		if (prim->num_morph)
		{
			glb_str_append (&json, ",\"targets\":[");
			for (size_t t = 0; t < prim->num_morph; t++)
			{
				if (t)
					glb_str_append (&json, ",");
				glb_str_printf (&json, "{\"POSITION\":%d}", prim->acc_morph[t]);
			}
			glb_str_append (&json, "]");
		}
		glb_str_append (&json, "}]");
		if (mesh->num_morph_targets)
		{
			glb_str_append (&json, ",\"weights\":[");
			for (size_t t = 0; t < mesh->num_morph_targets; t++)
			{
				if (t)
					glb_str_append (&json, ",");
				glb_str_printf (&json, "%g", mesh->morph_weights ? mesh->morph_weights[t] : 0.0f);
			}
			glb_str_append (&json, "],\"extras\":{\"targetNames\":[");
			for (size_t t = 0; t < mesh->num_morph_targets; t++)
			{
				if (t)
					glb_str_append (&json, ",");
				glb_json_escape_str (&json, mesh->morph_targets[t].name);
			}
			glb_str_append (&json, "]}");
		}
		glb_str_append (&json, "}");
	}
	glb_str_append (&json, "]");

	// Skins
	if (any_skin && model->num_joints > 0 && acc_ibm >= 0)
	{
		glb_str_printf (&json, ",\"skins\":[{\"inverseBindMatrices\":%d,\"joints\":[", acc_ibm);
		for (size_t j = 0; j < model->num_joints; j++)
		{
			if (j > 0)
				glb_str_append (&json, ",");
			glb_str_printf (&json, "%zu", j);
		}
		glb_str_append (&json, "]}]");
	}

	if (model->num_animations)
	{
		glb_str_append (&json, ",\"animations\":[");
		for (size_t a = 0; a < model->num_animations; a++)
		{
			if (a)
				glb_str_append (&json, ",");
			glb_str_append (&json, "{\"name\":");
			glb_json_escape_str (&json, model->animations[a].name);
			glb_str_append (&json, ",\"samplers\":[");
			for (size_t c = 0; c < model->animations[a].num_channels; c++)
			{
				if (c)
					glb_str_append (&json, ",");
				glb_str_printf (&json, "{\"input\":%d,\"output\":%d,\"interpolation\":\"LINEAR\"}",
					anim_in[a][c], anim_out[a][c]);
			}
			glb_str_append (&json, "],\"channels\":[");
			for (size_t c = 0; c < model->animations[a].num_channels; c++)
			{
				if (c)
					glb_str_append (&json, ",");
				const model_anim_channel_t *ch = model->animations[a].channels + c;
				const char *path = ch->path == MODEL_ANIM_TRANSLATION ? "translation"
					: ch->path == MODEL_ANIM_ROTATION				  ? "rotation"
					: ch->path == MODEL_ANIM_SCALE					  ? "scale"
																	  : "weights";
				glb_str_printf (&json, "{\"sampler\":%zu,\"target\":{\"node\":%d,\"path\":\"%s\"}}",
					c, ch->node_idx, path);
			}
			glb_str_append (&json, "]}");
		}
		glb_str_append (&json, "]");
	}

	// Accessors
	if (num_accs > 0)
	{
		glb_str_append (&json, ",\"accessors\":[");
		for (size_t a = 0; a < num_accs; a++)
		{
			if (a > 0)
				glb_str_append (&json, ",");
			const glb_acc_entry_t *acc = &accs[a];
			glb_str_printf (&json,
				"{\"bufferView\":%d,\"componentType\":%u,\"count\":%zu,\"type\":\"%s\"",
				acc->bufferView, acc->componentType, acc->count, acc->type);
			if (acc->has_bounds)
			{
				if (!strcmp (acc->type, "VEC3"))
				{
					glb_str_printf (&json, ",\"max\":[%g,%g,%g],\"min\":[%g,%g,%g]",
						acc->max_vals[0], acc->max_vals[1], acc->max_vals[2], acc->min_vals[0],
						acc->min_vals[1], acc->min_vals[2]);
				}
				else if (!strcmp (acc->type, "SCALAR"))
				{
					glb_str_printf (
						&json, ",\"max\":[%g],\"min\":[%g]", acc->max_vals[0], acc->min_vals[0]);
				}
			}
			glb_str_append (&json, "}");
		}
		glb_str_append (&json, "]");
	}

	// BufferViews
	if (num_bvs > 0)
	{
		glb_str_append (&json, ",\"bufferViews\":[");
		for (size_t b = 0; b < num_bvs; b++)
		{
			if (b > 0)
				glb_str_append (&json, ",");
			const glb_bv_entry_t *bv = &bvs[b];
			glb_str_printf (&json, "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu",
				bv->byteOffset, bv->byteLength);
			if (bv->target > 0)
			{
				glb_str_printf (&json, ",\"target\":%u", bv->target);
			}
			glb_str_append (&json, "}");
		}
		glb_str_append (&json, "]");
	}

	// Buffers
	glb_buf_align4 (&bin, 0);
	glb_str_printf (&json, ",\"buffers\":[{\"byteLength\":%zu}]}", bin.size);

	// Align JSON string with space characters ' ' to 4 bytes
	while (json.size % 4 != 0)
	{
		glb_str_append (&json, " ");
	}

	// 5. Write GLB Output File
	FILE *f = fopen (out_glb_file, "wb");
	int ok = 0;
	if (f)
	{
		uint32_t header[3];
		header[0] = 0x46546C67; // "glTF"
		header[1] = 2; // version 2
		header[2] = 12 + (8 + (uint32_t)json.size) + (8 + (uint32_t)bin.size);

		uint32_t json_chunk[2];
		json_chunk[0] = (uint32_t)json.size;
		json_chunk[1] = 0x4E4F534A; // "JSON"

		uint32_t bin_chunk[2];
		bin_chunk[0] = (uint32_t)bin.size;
		bin_chunk[1] = 0x004E4942; // "BIN\0"

		if (fwrite (header, 1, sizeof (header), f) == sizeof (header)
			&& fwrite (json_chunk, 1, sizeof (json_chunk), f) == sizeof (json_chunk)
			&& fwrite (json.data, 1, json.size, f) == json.size
			&& fwrite (bin_chunk, 1, sizeof (bin_chunk), f) == sizeof (bin_chunk)
			&& fwrite (bin.data, 1, bin.size, f) == bin.size)
		{
			ok = 1;
		}
		fclose (f);
	}

	// Cleanup
	free (bin.data);
	free (json.data);
	free (bvs);
	free (accs);
	free (images);
	free (samplers);
	free (textures);
	for (size_t m = 0; m < model->num_meshes; m++)
		free (prims[m].acc_morph);
	free (prims);
	for (size_t a = 0; a < model->num_animations; a++)
	{
		free (anim_in[a]);
		free (anim_out[a]);
	}
	free (anim_in);
	free (anim_out);

	return ok ? 0 : -1;
}

model_t *ParseGLBFile (const char *filename)
{
	if (!filename || !*filename)
		return NULL;
	char tmp_dae[PATH_MAX];
	snprintf (tmp_dae, sizeof (tmp_dae), "/tmp/szs_glb_%d_%ld.dae", getpid (), (long)time (NULL));
	char cmd[PATH_MAX * 2 + 64];
	snprintf (cmd, sizeof (cmd), "assimp export \"%s\" \"%s\" >/dev/null 2>&1", filename, tmp_dae);
	if (system (cmd) == 0 && access (tmp_dae, F_OK) == 0)
	{
		model_t *m = ParseDAEFile (tmp_dae);
		unlink (tmp_dae);
		if (m)
			return m;
	}
	return NULL;
}

model_t *ParseGLB (const uint8_t *data, size_t size)
{
	if (!data || size < 20)
		return NULL;
	char tmp_glb[PATH_MAX];
	snprintf (
		tmp_glb, sizeof (tmp_glb), "/tmp/szs_glb_in_%d_%ld.glb", getpid (), (long)time (NULL));
	FILE *f = fopen (tmp_glb, "wb");
	if (!f)
		return NULL;
	fwrite (data, 1, size, f);
	fclose (f);
	model_t *m = ParseGLBFile (tmp_glb);
	unlink (tmp_glb);
	return m;
}

// ---------------------------------------------------------------------------
// COLLADA (.dae) XML Parser
// ---------------------------------------------------------------------------

typedef struct
{
	char id[64];
	float *data;
	size_t count;
	unsigned stride;
} dae_source_t;

static const char *dae_find_tag (
	const char *src, const char *end, const char *tag, const char **out_tag_end)
{
	size_t tlen = strlen (tag);
	const char *p = src;
	while (p && p < end)
	{
		p = strchr (p, '<');
		if (!p || p >= end)
			return NULL;
		if (p + 1 + tlen <= end && !memcmp (p + 1, tag, tlen))
		{
			char next = p[1 + tlen];
			if (next == '>' || next == '/' || isspace ((unsigned char)next))
			{
				const char *close = strchr (p, '>');
				if (close && close < end)
				{
					if (out_tag_end)
						*out_tag_end = close + 1;
					return p;
				}
			}
		}
		p++;
	}
	return NULL;
}

static const char *dae_find_close_tag (const char *src, const char *end, const char *tag)
{
	char close_str[70];
	snprintf (close_str, sizeof (close_str), "</%s>", tag);
	size_t clen = strlen (close_str);
	const char *p = src;
	while (p && p + clen <= end)
	{
		if (!memcmp (p, close_str, clen))
			return p;
		p++;
	}
	return NULL;
}

static int dae_get_attr (
	const char *tag_start, const char *tag_end, const char *attr, char *out_val, size_t out_max)
{
	size_t alen = strlen (attr);
	const char *p = tag_start;
	while (p && p + alen + 2 < tag_end)
	{
		if (!memcmp (p, attr, alen) && p[alen] == '=')
		{
			char quote = p[alen + 1];
			if (quote == '"' || quote == '\'')
			{
				const char *val_start = p + alen + 2;
				const char *val_end = strchr (val_start, quote);
				if (val_end && val_end < tag_end)
				{
					size_t vlen = val_end - val_start;
					if (vlen >= out_max)
						vlen = out_max - 1;
					memcpy (out_val, val_start, vlen);
					out_val[vlen] = 0;
					return 1;
				}
			}
		}
		p++;
	}
	out_val[0] = 0;
	return 0;
}

static float *dae_parse_floats (const char *start, const char *end, size_t *out_count)
{
	size_t cap = 256, count = 0;
	float *arr = malloc (cap * sizeof (float));
	if (!arr)
		return NULL;

	const char *p = start;
	while (p < end)
	{
		while (p < end && isspace ((unsigned char)*p))
			p++;
		if (p >= end || *p == '<')
			break;
		char *next = NULL;
		float val = strtof (p, &next);
		if (next == p)
			break;
		p = next;
		if (count == cap)
		{
			cap *= 2;
			float *resized = realloc (arr, cap * sizeof (float));
			if (!resized)
			{
				free (arr);
				return NULL;
			}
			arr = resized;
		}
		arr[count++] = val;
	}
	*out_count = count;
	return arr;
}

static int *dae_parse_ints (const char *start, const char *end, size_t *out_count)
{
	size_t cap = 512, count = 0;
	int *arr = malloc (cap * sizeof (int));
	if (!arr)
		return NULL;

	const char *p = start;
	while (p < end)
	{
		while (p < end && isspace ((unsigned char)*p))
			p++;
		if (p >= end || *p == '<')
			break;
		char *next = NULL;
		long val = strtol (p, &next, 10);
		if (next == p)
			break;
		p = next;
		if (count == cap)
		{
			cap *= 2;
			int *resized = realloc (arr, cap * sizeof (int));
			if (!resized)
			{
				free (arr);
				return NULL;
			}
			arr = resized;
		}
		arr[count++] = (int)val;
	}
	*out_count = count;
	return arr;
}

model_t *ParseDAE (const char *xml_data, size_t xml_size)
{
	if (!xml_data || !xml_size)
		return NULL;
	const char *end = xml_data + xml_size;

	model_t *model = calloc (1, sizeof (*model));
	if (!model)
		return NULL;

	// 1. Parse materials
	const char *mat_lib_start = dae_find_tag (xml_data, end, "library_materials", NULL);
	const char *mat_lib_end
		= mat_lib_start ? dae_find_close_tag (mat_lib_start, end, "library_materials") : NULL;
	if (mat_lib_start && mat_lib_end)
	{
		const char *mp = mat_lib_start;
		while (mp < mat_lib_end)
		{
			const char *mat_tag_end = NULL;
			const char *mat_tag = dae_find_tag (mp, mat_lib_end, "material", &mat_tag_end);
			if (!mat_tag)
				break;
			char mat_id[64] = "", mat_name[64] = "";
			dae_get_attr (mat_tag, mat_tag_end, "id", mat_id, sizeof (mat_id));
			dae_get_attr (mat_tag, mat_tag_end, "name", mat_name, sizeof (mat_name));
			if (!*mat_name)
				snprintf (mat_name, sizeof (mat_name), "%s", mat_id);

			material_t *resized
				= realloc (model->materials, (model->num_materials + 1) * sizeof (material_t));
			if (resized)
			{
				model->materials = resized;
				material_t *m = &model->materials[model->num_materials++];
				memset (m, 0, sizeof (*m));
				snprintf (m->name, sizeof (m->name), "%s", mat_id); // use ID for lookup matching
			}
			mp = mat_tag_end;
		}
	}

	// Resolve COLLADA profile_COMMON image chains for material textures.
	// This also makes files emitted by ExportModelToDAE self-importing:
	// fx_N -> surface -> image -> <image name>.  The display name is used
	// instead of init_from because older exports could append ".png" twice
	// there while the sibling image name remained correct.
	const char *fx_lib = dae_find_tag (xml_data, end, "library_effects", NULL),
			   *fx_end = fx_lib ? dae_find_close_tag (fx_lib, end, "library_effects") : NULL;
	const char *im_lib = dae_find_tag (xml_data, end, "library_images", NULL),
			   *im_end = im_lib ? dae_find_close_tag (im_lib, end, "library_images") : NULL;
	if (fx_lib && fx_end && im_lib && im_end)
		for (size_t mi = 0; mi < model->num_materials; mi++)
		{
			char want[32];
			snprintf (want, sizeof (want), "fx_%zu", mi);
			const char *p = fx_lib, *close = 0, *tag_end = 0, *fx = 0;
			while ((p = dae_find_tag (p, fx_end, "effect", &tag_end)))
			{
				char id[64];
				dae_get_attr (p, tag_end, "id", id, sizeof (id));
				close = dae_find_close_tag (tag_end, fx_end, "effect");
				if (!strcmp (id, want))
				{
					fx = p;
					break;
				}
				p = tag_end;
			}
			if (!fx || !close)
				continue;
			const char *it_end = 0, *it = dae_find_tag (fx, close, "init_from", &it_end);
			if (!it || !it_end)
				continue;
			const char *it_close = dae_find_close_tag (it_end, close, "init_from");
			if (!it_close)
				continue;
			char image_id[64];
			size_t il = it_close - it_end;
			while (il && isspace ((unsigned char)*it_end))
			{
				it_end++;
				il--;
			}
			while (il && isspace ((unsigned char)it_end[il - 1]))
				il--;
			if (il >= sizeof (image_id))
				il = sizeof (image_id) - 1;
			memcpy (image_id, it_end, il);
			image_id[il] = 0;
			p = im_lib;
			while ((p = dae_find_tag (p, im_end, "image", &tag_end)))
			{
				char id[64], name[64];
				dae_get_attr (p, tag_end, "id", id, sizeof (id));
				dae_get_attr (p, tag_end, "name", name, sizeof (name));
				if (!strcmp (id, image_id))
				{
					snprintf (model->materials[mi].textures[0], 64, "%s", name);
					model->materials[mi].num_textures = 1;
					break;
				}
				p = tag_end;
			}
		}

	// 2. Parse geometries
	const char *geom_lib_start = dae_find_tag (xml_data, end, "library_geometries", NULL);
	const char *geom_lib_end
		= geom_lib_start ? dae_find_close_tag (geom_lib_start, end, "library_geometries") : NULL;
	if (!geom_lib_start || !geom_lib_end)
	{
		FreeModel (model);
		return NULL;
	}

	const char *gp = geom_lib_start;
	while (gp < geom_lib_end)
	{
		const char *geom_tag_end = NULL;
		const char *geom_tag = dae_find_tag (gp, geom_lib_end, "geometry", &geom_tag_end);
		if (!geom_tag)
			break;
		const char *geom_close = dae_find_close_tag (geom_tag, geom_lib_end, "geometry");
		if (!geom_close)
			break;

		char geom_id[64] = "", geom_name[64] = "";
		dae_get_attr (geom_tag, geom_tag_end, "id", geom_id, sizeof (geom_id));
		dae_get_attr (geom_tag, geom_tag_end, "name", geom_name, sizeof (geom_name));
		if (!*geom_name)
			snprintf (geom_name, sizeof (geom_name), "%s", geom_id);

		const char *mesh_tag = dae_find_tag (geom_tag_end, geom_close, "mesh", NULL);
		if (mesh_tag)
		{
			// Parse sources inside mesh
			dae_source_t sources[32];
			size_t num_sources = 0;

			const char *sp = mesh_tag;
			while (sp < geom_close && num_sources < 32)
			{
				const char *src_tag_end = NULL;
				const char *src_tag = dae_find_tag (sp, geom_close, "source", &src_tag_end);
				if (!src_tag)
					break;
				const char *src_close = dae_find_close_tag (src_tag, geom_close, "source");
				if (!src_close)
					break;

				dae_source_t *s = &sources[num_sources];
				memset (s, 0, sizeof (*s));
				dae_get_attr (src_tag, src_tag_end, "id", s->id, sizeof (s->id));
				s->stride = 3;

				// Check accessor stride
				const char *acc_tag = dae_find_tag (src_tag_end, src_close, "accessor", NULL);
				if (acc_tag)
				{
					char stride_str[16] = "";
					const char *acc_end = strchr (acc_tag, '>');
					if (acc_end)
					{
						dae_get_attr (acc_tag, acc_end, "stride", stride_str, sizeof (stride_str));
						if (*stride_str)
							s->stride = (unsigned)atoi (stride_str);
					}
				}

				// Check float array
				const char *fa_tag_end = NULL;
				const char *fa_tag
					= dae_find_tag (src_tag_end, src_close, "float_array", &fa_tag_end);
				if (fa_tag && fa_tag_end)
				{
					const char *fa_close = dae_find_close_tag (fa_tag, src_close, "float_array");
					if (fa_close)
					{
						s->data = dae_parse_floats (fa_tag_end, fa_close, &s->count);
					}
				}

				if (s->data && s->count)
					num_sources++;
				sp = src_close + 9;
			}

			// Parse <vertices> to find position source ID
			char pos_source_id[64] = "";
			char vertices_id[64] = "";
			const char *vert_tag_end = NULL;
			const char *vert_tag = dae_find_tag (mesh_tag, geom_close, "vertices", &vert_tag_end);
			if (vert_tag)
			{
				const char *vert_close = dae_find_close_tag (vert_tag, geom_close, "vertices");
				dae_get_attr (vert_tag, vert_tag_end, "id", vertices_id, sizeof (vertices_id));
				if (vert_close)
				{
					const char *inp_tag_end = NULL;
					const char *inp_tag
						= dae_find_tag (vert_tag_end, vert_close, "input", &inp_tag_end);
					while (inp_tag)
					{
						char sem[32] = "", src_ref[64] = "";
						dae_get_attr (inp_tag, inp_tag_end, "semantic", sem, sizeof (sem));
						dae_get_attr (inp_tag, inp_tag_end, "source", src_ref, sizeof (src_ref));
						const char *clean_ref = src_ref[0] == '#' ? src_ref + 1 : src_ref;
						if (!strcmp (sem, "POSITION"))
							snprintf (pos_source_id, sizeof (pos_source_id), "%s", clean_ref);
						inp_tag = dae_find_tag (inp_tag_end, vert_close, "input", &inp_tag_end);
					}
				}
			}

			// Allocate a mesh_t
			mesh_t *resized_meshes
				= realloc (model->meshes, (model->num_meshes + 1) * sizeof (mesh_t));
			if (resized_meshes)
			{
				model->meshes = resized_meshes;
				mesh_t *mesh = &model->meshes[model->num_meshes++];
				memset (mesh, 0, sizeof (*mesh));
				snprintf (mesh->name, sizeof (mesh->name), "%s", geom_name);
				mesh->material_idx = -1;

				// Extract positions, normals, and UVs arrays into mesh
				for (size_t s = 0; s < num_sources; s++)
				{
					if (*pos_source_id && !strcmp (sources[s].id, pos_source_id))
					{
						mesh->num_positions = sources[s].count / 3;
						mesh->positions = malloc (mesh->num_positions * sizeof (vec3_t));
						for (size_t i = 0; i < mesh->num_positions; i++)
						{
							mesh->positions[i].x = sources[s].data[i * 3 + 0];
							mesh->positions[i].y = sources[s].data[i * 3 + 1];
							mesh->positions[i].z = sources[s].data[i * 3 + 2];
						}
					}
					else if (strstr (sources[s].id, "normal") || strstr (sources[s].id, "Normal"))
					{
						if (!mesh->normals)
						{
							mesh->num_normals = sources[s].count / 3;
							mesh->normals = malloc (mesh->num_normals * sizeof (vec3_t));
							for (size_t i = 0; i < mesh->num_normals; i++)
							{
								mesh->normals[i].x = sources[s].data[i * 3 + 0];
								mesh->normals[i].y = sources[s].data[i * 3 + 1];
								mesh->normals[i].z = sources[s].data[i * 3 + 2];
							}
						}
					}
					else if (strstr (sources[s].id, "map") || strstr (sources[s].id, "uv")
						|| strstr (sources[s].id, "UV") || strstr (sources[s].id, "texcoord"))
					{
						if (!mesh->texcoords)
						{
							mesh->num_texcoords
								= sources[s].count / (sources[s].stride ? sources[s].stride : 2);
							mesh->texcoords = malloc (mesh->num_texcoords * sizeof (vec2_t));
							unsigned stride = sources[s].stride ? sources[s].stride : 2;
							for (size_t i = 0; i < mesh->num_texcoords; i++)
							{
								mesh->texcoords[i].u = sources[s].data[i * stride + 0];
								mesh->texcoords[i].v = sources[s].data[i * stride + 1];
							}
						}
					}
				}

				// If pos_source_id was not matched, fallback to first source
				if (!mesh->positions && num_sources > 0)
				{
					mesh->num_positions = sources[0].count / 3;
					mesh->positions = malloc (mesh->num_positions * sizeof (vec3_t));
					for (size_t i = 0; i < mesh->num_positions; i++)
					{
						mesh->positions[i].x = sources[0].data[i * 3 + 0];
						mesh->positions[i].y = sources[0].data[i * 3 + 1];
						mesh->positions[i].z = sources[0].data[i * 3 + 2];
					}
				}

				// Parse primitive elements: <triangles> or <polylist>
				const char *prim_types[] = { "triangles", "polylist", NULL };
				for (int pt = 0; prim_types[pt]; pt++)
				{
					const char *pname = prim_types[pt];
					const char *pp = mesh_tag;
					while (pp < geom_close)
					{
						const char *ptag_end = NULL;
						const char *ptag = dae_find_tag (pp, geom_close, pname, &ptag_end);
						if (!ptag)
							break;
						const char *pclose = dae_find_close_tag (ptag, geom_close, pname);
						if (!pclose)
							break;

						char mat_sym[64] = "";
						dae_get_attr (ptag, ptag_end, "material", mat_sym, sizeof (mat_sym));
						if (*mat_sym && mesh->material_idx < 0)
						{
							for (size_t m = 0; m < model->num_materials; m++)
							{
								if (!strcmp (model->materials[m].name, mat_sym))
								{
									mesh->material_idx = (int)m;
									break;
								}
							}
						}

						// Parse inputs
						int pos_offset = -1, norm_offset = -1, uv_offset = -1, col_offset = -1;
						int max_offset = 0;
						const char *inp_tag_end = NULL;
						const char *inp_tag
							= dae_find_tag (ptag_end, pclose, "input", &inp_tag_end);
						while (inp_tag)
						{
							char sem[32] = "", off_str[16] = "";
							dae_get_attr (inp_tag, inp_tag_end, "semantic", sem, sizeof (sem));
							dae_get_attr (
								inp_tag, inp_tag_end, "offset", off_str, sizeof (off_str));
							int off = atoi (off_str);
							if (off > max_offset)
								max_offset = off;

							if (!strcmp (sem, "VERTEX") || !strcmp (sem, "POSITION"))
								pos_offset = off;
							else if (!strcmp (sem, "NORMAL"))
								norm_offset = off;
							else if (!strcmp (sem, "TEXCOORD"))
							{
								if (uv_offset < 0)
									uv_offset = off;
							}
							else if (!strcmp (sem, "COLOR"))
							{
								if (col_offset < 0)
									col_offset = off;
							}
							inp_tag = dae_find_tag (inp_tag_end, pclose, "input", &inp_tag_end);
						}

						int stride = max_offset + 1;
						if (pos_offset < 0)
							pos_offset = 0;

						// Parse vcount if polylist
						size_t num_vcounts = 0;
						int *vcounts = NULL;
						if (!strcmp (pname, "polylist"))
						{
							const char *vc_tag_end = NULL;
							const char *vc_tag
								= dae_find_tag (ptag_end, pclose, "vcount", &vc_tag_end);
							if (vc_tag && vc_tag_end)
							{
								const char *vc_close
									= dae_find_close_tag (vc_tag, pclose, "vcount");
								if (vc_close)
								{
									vcounts = dae_parse_ints (vc_tag_end, vc_close, &num_vcounts);
								}
							}
						}

						// Parse index stream <p>
						const char *p_tag_end = NULL;
						const char *p_tag = dae_find_tag (ptag_end, pclose, "p", &p_tag_end);
						if (p_tag && p_tag_end)
						{
							const char *p_close = dae_find_close_tag (p_tag, pclose, "p");
							if (p_close)
							{
								size_t num_indices = 0;
								int *indices = dae_parse_ints (p_tag_end, p_close, &num_indices);
								if (indices)
								{
									size_t cur_idx = 0;
									if (vcounts)
									{
										for (size_t poly = 0; poly < num_vcounts; poly++)
										{
											int vc = vcounts[poly];
											for (int i = 1; i < vc - 1; i++)
											{
												// Triangle: 0, i, i+1
												int v_indices[3] = { 0, i, i + 1 };
												for (int t = 0; t < 3; t++)
												{
													size_t vi
														= cur_idx + (size_t)v_indices[t] * stride;
													vertex_t vtx;
													memset (&vtx, 0, sizeof (vtx));
													vtx.matrix_idx = -1;
													vtx.position_idx
														= (vi + pos_offset < num_indices)
														? indices[vi + pos_offset]
														: 0;
													vtx.normal_idx
														= (norm_offset >= 0
															  && vi + norm_offset < num_indices)
														? indices[vi + norm_offset]
														: 0;
													vtx.texcoord_idx
														= (uv_offset >= 0
															  && vi + uv_offset < num_indices)
														? indices[vi + uv_offset]
														: 0;
													vtx.color_idx[0]
														= (col_offset >= 0
															  && vi + col_offset < num_indices)
														? indices[vi + col_offset]
														: 0;

													vertex_t *r = realloc (mesh->vertices,
														(mesh->num_vertices + 1)
															* sizeof (vertex_t));
													if (r)
													{
														mesh->vertices = r;
														mesh->vertices[mesh->num_vertices++] = vtx;
													}
												}
											}
											cur_idx += (size_t)vc * stride;
										}
										free (vcounts);
									}
									else
									{
										// Plain triangles
										size_t total_verts = num_indices / stride;
										for (size_t v = 0; v < total_verts; v++)
										{
											size_t vi = v * stride;
											vertex_t vtx;
											memset (&vtx, 0, sizeof (vtx));
											vtx.matrix_idx = -1;
											vtx.position_idx = (vi + pos_offset < num_indices)
												? indices[vi + pos_offset]
												: 0;
											vtx.normal_idx = (norm_offset >= 0
																 && vi + norm_offset < num_indices)
												? indices[vi + norm_offset]
												: 0;
											vtx.texcoord_idx
												= (uv_offset >= 0 && vi + uv_offset < num_indices)
												? indices[vi + uv_offset]
												: 0;
											vtx.color_idx[0]
												= (col_offset >= 0 && vi + col_offset < num_indices)
												? indices[vi + col_offset]
												: 0;

											vertex_t *r = realloc (mesh->vertices,
												(mesh->num_vertices + 1) * sizeof (vertex_t));
											if (r)
											{
												mesh->vertices = r;
												mesh->vertices[mesh->num_vertices++] = vtx;
											}
										}
									}
									free (indices);
								}
							}
						}

						pp = pclose + strlen (pname) + 3;
					}
				}
			}

			// Cleanup sources
			for (size_t s = 0; s < num_sources; s++)
				if (sources[s].data)
					free (sources[s].data);
		}

		gp = geom_close + 11;
	}

	if (!model->num_meshes)
	{
		FreeModel (model);
		return NULL;
	}
	return model;
}

model_t *ParseDAEFile (const char *filename)
{
	if (!filename || !*filename)
		return NULL;
	FILE *f = fopen (filename, "rb");
	if (!f)
		return NULL;
	fseek (f, 0, SEEK_END);
	long size = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (size <= 0 || size > 256 * 1024 * 1024)
	{
		fclose (f);
		return NULL;
	}

	char *buf = malloc ((size_t)size + 1);
	if (!buf)
	{
		fclose (f);
		return NULL;
	}
	size_t got = fread (buf, 1, (size_t)size, f);
	fclose (f);
	buf[got] = 0;

	model_t *m = ParseDAE (buf, got);
	free (buf);
	return m;
}
