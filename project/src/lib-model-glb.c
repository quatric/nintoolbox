#include "lib-model-glb.h"
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
#include "cgltf.h"
#include "cgltf_write.h"

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
		// Border/noise/mask/envmap layers are normally TEV/reflection details, not the base
		// color map a single-texture profile_COMMON effect should display.
		if (strstr (texture, "noise") || strstr (texture, "mask") || strstr (texture, "brd")
			|| strstr (texture, "envmap") || strstr (texture, "env_map") || strstr (texture, "cenvmap"))
			score -= 100;
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


// ---------------------------------------------------------------------------
// GLB (glTF 2.0 Binary) Exporter
// ---------------------------------------------------------------------------


int ExportModelToGLB (const model_t *model, const char *out_glb_file)
{
	if (!model || !out_glb_file)
		return -1;

	cgltf_options options = {0};
	options.type = cgltf_file_type_glb;
	cgltf_data data = {0};
	data.asset.generator = (char*)"wiimms-szs-tools-plus";
	data.asset.version = (char*)"2.0";

	uint8_t *bin_data = NULL;
	size_t bin_size = 0, bin_cap = 0;

	size_t max_bvs = model->num_materials * 8 + 1;
	size_t max_morphs = 0;
	for (size_t i = 0; i < model->num_meshes; i++)
		max_morphs += model->meshes[i].num_morph_targets;
	max_bvs += model->num_meshes * 16 + max_morphs;
	size_t max_anim_channels = 0;
	for (size_t i = 0; i < model->num_animations; i++)
		max_anim_channels += model->animations[i].num_channels;
	max_bvs += max_anim_channels * 2;

	data.buffers_count = 1;
	data.buffers = calloc (1, sizeof (cgltf_buffer));
	data.buffer_views = calloc (max_bvs, sizeof (cgltf_buffer_view));
	data.accessors = calloc (max_bvs, sizeof (cgltf_accessor));
	data.images = calloc (model->num_materials * 8 + 1, sizeof (cgltf_image));
	data.textures = calloc (model->num_materials * 8 + 1, sizeof (cgltf_texture));
	data.samplers = calloc (model->num_materials * 8 + 1, sizeof (cgltf_sampler));
	data.materials = calloc (model->num_materials > 0 ? model->num_materials : 1, sizeof (cgltf_material));
	data.meshes = calloc (model->num_meshes > 0 ? model->num_meshes : 1, sizeof (cgltf_mesh));
	size_t max_nodes = model->num_joints + model->num_meshes + model->num_instances + model->num_cameras + model->num_lights;
	data.nodes = calloc (max_nodes > 0 ? max_nodes : 1, sizeof (cgltf_node));
	data.scenes_count = 1;
	data.scenes = calloc (1, sizeof (cgltf_scene));
	data.scene = data.scenes;
	data.cameras = calloc (model->num_cameras > 0 ? model->num_cameras : 1, sizeof (cgltf_camera));
	data.cameras_count = model->num_cameras;
	data.lights = calloc (model->num_lights > 0 ? model->num_lights : 1, sizeof (cgltf_light));
	data.lights_count = model->num_lights;
	data.animations = calloc (model->num_animations > 0 ? model->num_animations : 1, sizeof (cgltf_animation));
	data.skins = calloc (1, sizeof (cgltf_skin));

	// Pre-allocate extensions arrays if needed. KHR_lights_punctual and KHR_texture_transform
	data.extensions_used = calloc(2, sizeof(char*));

	// Node children array for scenes and joints
	size_t *scene_nodes_idx = calloc(max_nodes, sizeof(size_t));
	size_t num_scene_nodes = 0;

	// Keep track of texture indices
	int mat_tex_idx[model->num_materials > 0 ? model->num_materials : 1][8];
	memset (mat_tex_idx, -1, sizeof (mat_tex_idx));

	for (size_t m = 0; m < model->num_materials; m++)
	{
		const material_t *mat = &model->materials[m];
		cgltf_material *gmat = &data.materials[data.materials_count++];
		gmat->name = (char*)mat->name;
		gmat->has_pbr_metallic_roughness = 1;
		gmat->pbr_metallic_roughness.metallic_factor = 0.0f;
		gmat->pbr_metallic_roughness.roughness_factor = mat->shininess > 0 ? (1.0f - 1.0f / (1.0f + mat->shininess * 0.01f)) * 0.9f : 0.9f;
		gmat->double_sided = 1;

		float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.0f;
		if (mat->diffuse[0] || mat->diffuse[1] || mat->diffuse[2] || mat->diffuse[3])
		{
			r = mat->diffuse[0]; g = mat->diffuse[1]; b = mat->diffuse[2]; a = mat->diffuse[3];
		}
		if (mat->num_textures > 0)
		{
			// When textures are present, retain explicit diffuse tint if provided, otherwise default to white
			if (!mat->diffuse[0] && !mat->diffuse[1] && !mat->diffuse[2])
			{
				r = 1.0f; g = 1.0f; b = 1.0f;
			}
			if (mat->diffuse[3] > 0.0f) a = mat->diffuse[3];
		}
		gmat->pbr_metallic_roughness.base_color_factor[0] = r;
		gmat->pbr_metallic_roughness.base_color_factor[1] = g;
		gmat->pbr_metallic_roughness.base_color_factor[2] = b;
		gmat->pbr_metallic_roughness.base_color_factor[3] = a;

		// Note: GameCube/Wii GX ambient is for fixed-function lighting, NOT glTF emissive
		gmat->emissive_factor[0] = 0.0f;
		gmat->emissive_factor[1] = 0.0f;
		gmat->emissive_factor[2] = 0.0f;
		gmat->pbr_metallic_roughness.metallic_factor = 0.0f;
		gmat->pbr_metallic_roughness.roughness_factor = 0.8f;
		gmat->alpha_mode = (mat->has_alpha || (mat->diffuse[3] > 0 && mat->diffuse[3] < 1.0f)) ? cgltf_alpha_mode_blend : cgltf_alpha_mode_opaque;

		int primary = dae_primary_texture(mat, out_glb_file);

		for (int t = 0; t < mat->num_textures; t++)
		{
			if (!mat->textures[t][0]) continue;
			char tex_path[PATH_MAX];
			if (!dae_texture_path (tex_path, sizeof (tex_path), out_glb_file, mat->textures[t])) continue;
			// dae_localize_texture (tex_path, sizeof (tex_path), out_glb_file);

			char full_png_path[PATH_MAX];
			if (tex_path[0] == '/') snprintf (full_png_path, sizeof (full_png_path), "%s", tex_path);
			else {
				const char *slash = strrchr (out_glb_file, '/');
				if (slash) snprintf (full_png_path, sizeof (full_png_path), "%.*s/%s", (int)(slash - out_glb_file), out_glb_file, tex_path);
				else snprintf (full_png_path, sizeof (full_png_path), "%s", tex_path);
			}

			FILE *fp = fopen (full_png_path, "rb");
			if (!fp) fp = fopen (tex_path, "rb");
			
			cgltf_image *gimg = &data.images[data.images_count++];
			gimg->name = (char*)mat->textures[t];
			gimg->mime_type = (char*)"image/png";

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
						while (bin_size % 4 != 0) {
							if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
							bin_data[bin_size++] = 0;
						}
						if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
						
						cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
						bv->buffer = &data.buffers[0];
						bv->offset = bin_size;
						bv->size = fsz;
						gimg->buffer_view = bv;

						memcpy (bin_data + bin_size, img_data, fsz);
						bin_size += fsz;
					}
					free (img_data);
				}
				fclose (fp);
			} else {
				gimg->uri = strdup(tex_path);
			}

			cgltf_sampler *gsmp = &data.samplers[data.samplers_count++];
			gsmp->wrap_s = mat->wrap_s[t] == 0 ? 33071 : (mat->wrap_s[t] == 2 ? 33648 : 10497);
			gsmp->wrap_t = mat->wrap_t[t] == 0 ? 33071 : (mat->wrap_t[t] == 2 ? 33648 : 10497);
			gsmp->min_filter = mat->min_filter[t] ? 9729 : 9728;
			gsmp->mag_filter = mat->mag_filter[t] ? 9729 : 9728;

			cgltf_texture *gtex = &data.textures[data.textures_count++];
			gtex->image = gimg;
			gtex->sampler = gsmp;

			mat_tex_idx[m][t] = data.textures_count - 1;

			if (t == primary) {
				gmat->pbr_metallic_roughness.base_color_texture.texture = gtex;
				if (mat->has_tex_transform[primary]) {
					gmat->pbr_metallic_roughness.base_color_texture.has_transform = 1;
					gmat->pbr_metallic_roughness.base_color_texture.transform.offset[0] = mat->tex_translate_s[primary];
					gmat->pbr_metallic_roughness.base_color_texture.transform.offset[1] = mat->tex_translate_t[primary];
					gmat->pbr_metallic_roughness.base_color_texture.transform.rotation = mat->tex_rotate[primary];
					gmat->pbr_metallic_roughness.base_color_texture.transform.scale[0] = mat->tex_scale_s[primary];
					gmat->pbr_metallic_roughness.base_color_texture.transform.scale[1] = mat->tex_scale_t[primary];
					
					int ext_found = 0;
					for(size_t e=0; e<data.extensions_used_count; e++) if(!strcmp(data.extensions_used[e], "KHR_texture_transform")) ext_found = 1;
					if (!ext_found) data.extensions_used[data.extensions_used_count++] = (char*)"KHR_texture_transform";
				}
			}
		}
	}

	int acc_ibm = -1;
	int any_skin = 0;
	for (size_t i = 0; i < model->num_meshes; i++)
		if (dae_mesh_is_skinned (model, &model->meshes[i]))
			any_skin = 1;

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
					m[0] = inv[0]; m[1] = inv[4]; m[2] = inv[8]; m[3] = 0.0f;
					m[4] = inv[1]; m[5] = inv[5]; m[6] = inv[9]; m[7] = 0.0f;
					m[8] = inv[2]; m[9] = inv[6]; m[10] = inv[10]; m[11] = 0.0f;
					m[12] = inv[3]; m[13] = inv[7]; m[14] = inv[11]; m[15] = 1.0f;
				}
				else
				{
					memset (m, 0, 16 * sizeof (float));
					m[0] = m[5] = m[10] = m[15] = 1.0f;
				}
			}

			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			size_t fsz = model->num_joints * 16 * sizeof(float);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			
			cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
			bv->buffer = &data.buffers[0];
			bv->offset = bin_size;
			bv->size = fsz;

			cgltf_accessor *acc = &data.accessors[data.accessors_count++];
			acc->buffer_view = bv;
			acc->component_type = cgltf_component_type_r_32f;
			acc->type = cgltf_type_mat4;
			acc->count = model->num_joints;
			acc_ibm = data.accessors_count - 1;

			memcpy (bin_data + bin_size, ibm, fsz);
			bin_size += fsz;
			free(ibm);
		}
	}

	for (size_t m = 0; m < model->num_meshes; m++)
	{
		const mesh_t *mesh = &model->meshes[m];
		cgltf_mesh *gmesh = &data.meshes[data.meshes_count++];
		gmesh->name = (char*)mesh->name;
		gmesh->primitives = calloc(1, sizeof(cgltf_primitive));
		gmesh->primitives_count = 1;
		cgltf_primitive *prim = &gmesh->primitives[0];
		prim->type = cgltf_primitive_type_triangles;
		if (mesh->material_idx >= 0 && (size_t)mesh->material_idx < model->num_materials) {
			prim->material = &data.materials[mesh->material_idx];
		}

		prim->attributes = calloc(16 + mesh->num_morph_targets, sizeof(cgltf_attribute));
		size_t attr_idx = 0;

		const size_t N = mesh->num_vertices;
		if (!N) continue;
		const int skinned = dae_mesh_is_skinned (model, mesh);

		// POSITION
		vec3_t *v_pos = malloc (N * sizeof (vec3_t));
		float min_p[4] = { 1e30f, 1e30f, 1e30f, 0.0f };
		float max_p[4] = { -1e30f, -1e30f, -1e30f, 0.0f };
		for (size_t v = 0; v < N; v++)
		{
			int pi = mesh->vertices[v].position_idx;
			vec3_t p = (pi >= 0 && (size_t)pi < mesh->num_positions) ? mesh->positions[pi] : (vec3_t){0,0,0};
			v_pos[v] = p;
			if (p.x < min_p[0]) min_p[0] = p.x;
			if (p.y < min_p[1]) min_p[1] = p.y;
			if (p.z < min_p[2]) min_p[2] = p.z;
			if (p.x > max_p[0]) max_p[0] = p.x;
			if (p.y > max_p[1]) max_p[1] = p.y;
			if (p.z > max_p[2]) max_p[2] = p.z;
		}
		while (bin_size % 4 != 0) {
			if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
			bin_data[bin_size++] = 0;
		}
		size_t fsz = N * sizeof(vec3_t);
		if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
		cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
		bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
		cgltf_accessor *acc = &data.accessors[data.accessors_count++];
		acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec3; acc->count = N;
		acc->has_min = 1; acc->has_max = 1;
		acc->min[0] = min_p[0]; acc->min[1] = min_p[1]; acc->min[2] = min_p[2];
		acc->max[0] = max_p[0]; acc->max[1] = max_p[1]; acc->max[2] = max_p[2];
		memcpy (bin_data + bin_size, v_pos, fsz); bin_size += fsz; free(v_pos);

		prim->attributes[attr_idx].name = (char*)"POSITION";
		prim->attributes[attr_idx].type = cgltf_attribute_type_position;
		prim->attributes[attr_idx].data = acc;
		attr_idx++;

		// MORPH TARGETS
		if (mesh->num_morph_targets > 0) {
			prim->targets = calloc(mesh->num_morph_targets, sizeof(cgltf_morph_target));
			prim->targets_count = mesh->num_morph_targets;
			gmesh->weights = calloc(mesh->num_morph_targets, sizeof(cgltf_float));
			gmesh->weights_count = mesh->num_morph_targets;
			gmesh->target_names = calloc(mesh->num_morph_targets, sizeof(char*));
			gmesh->target_names_count = mesh->num_morph_targets;

			for (size_t t = 0; t < mesh->num_morph_targets; t++) {
				const morph_target_t *mt = mesh->morph_targets + t;
				gmesh->weights[t] = mesh->morph_weights ? mesh->morph_weights[t] : 0.0f;
				gmesh->target_names[t] = (char*)mt->name;
				prim->targets[t].attributes = calloc(1, sizeof(cgltf_attribute));
				
				vec3_t *delta = calloc (N, sizeof (*delta));
				if (delta && mt->position_deltas && mt->num_positions) {
					for (size_t v = 0; v < N; v++) {
						int pi = mesh->vertices[v].position_idx;
						if (pi >= 0 && (size_t)pi < mt->num_positions) delta[v] = mt->position_deltas[pi];
					}
				}
				while (bin_size % 4 != 0) {
					if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
					bin_data[bin_size++] = 0;
				}
				fsz = N * sizeof(vec3_t);
				if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
				bv = &data.buffer_views[data.buffer_views_count++];
				bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
				acc = &data.accessors[data.accessors_count++];
				acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec3; acc->count = N;
				if (delta) { memcpy (bin_data + bin_size, delta, fsz); free(delta); }
				bin_size += fsz;

				prim->targets[t].attributes[0].name = (char*)"POSITION";
				prim->targets[t].attributes[0].type = cgltf_attribute_type_position;
				prim->targets[t].attributes[0].data = acc;
				prim->targets[t].attributes_count = 1;
			}
		}

		// NORMAL
		if (mesh->num_normals > 0)
		{
			vec3_t *v_nrm = malloc (N * sizeof (vec3_t));
			for (size_t v = 0; v < N; v++) {
				int ni = mesh->vertices[v].normal_idx;
				v_nrm[v] = (ni >= 0 && (size_t)ni < mesh->num_normals) ? mesh->normals[ni] : (vec3_t){0,1,0};
			}
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			fsz = N * sizeof(vec3_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			bv = &data.buffer_views[data.buffer_views_count++];
			bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
			acc = &data.accessors[data.accessors_count++];
			acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec3; acc->count = N;
			memcpy (bin_data + bin_size, v_nrm, fsz); bin_size += fsz; free(v_nrm);
			
			prim->attributes[attr_idx].name = (char*)"NORMAL";
			prim->attributes[attr_idx].type = cgltf_attribute_type_normal;
			prim->attributes[attr_idx].data = acc;
			attr_idx++;
		}

		// TANGENT
		if (mesh->num_tangents > 0)
		{
			vec3_t *v_tan = malloc (N * sizeof (vec3_t));
			for (size_t v = 0; v < N; v++) {
				int ti = mesh->vertices[v].tangent_idx;
				v_tan[v] = (ti >= 0 && (size_t)ti < mesh->num_tangents) ? mesh->tangents[ti] : (vec3_t){1,0,0};
			}
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			size_t fsz = N * sizeof(vec3_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
			bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
			cgltf_accessor *acc = &data.accessors[data.accessors_count++];
			acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec3; acc->count = N;
			memcpy (bin_data + bin_size, v_tan, fsz); bin_size += fsz; free(v_tan);
			
			prim->attributes[attr_idx].name = (char*)"TANGENT";
			prim->attributes[attr_idx].type = cgltf_attribute_type_tangent;
			prim->attributes[attr_idx].data = acc;
			attr_idx++;
		}

		// TEXCOORD 0
		if (mesh->num_texcoords > 0)
		{
			vec2_t *v_uv = malloc (N * sizeof (vec2_t));
			for (size_t v = 0; v < N; v++) {
				int ti = mesh->vertices[v].texcoord_idx;
				v_uv[v] = (ti >= 0 && (size_t)ti < mesh->num_texcoords) ? mesh->texcoords[ti] : (vec2_t){0,0};
			}
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			size_t fsz = N * sizeof(vec2_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
			bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
			cgltf_accessor *acc = &data.accessors[data.accessors_count++];
			acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec2; acc->count = N;
			memcpy (bin_data + bin_size, v_uv, fsz); bin_size += fsz; free(v_uv);
			
			prim->attributes[attr_idx].name = (char*)"TEXCOORD_0";
			prim->attributes[attr_idx].type = cgltf_attribute_type_texcoord;
			prim->attributes[attr_idx].index = 0;
			prim->attributes[attr_idx].data = acc;
			attr_idx++;
		}

		for (int set = 1; set < 8; set++)
		{
			if (mesh->num_extra_texcoords[set - 1] > 0) {
				const size_t num_ex = mesh->num_extra_texcoords[set - 1];
				const vec2_t *ex_uv = mesh->extra_texcoords[set - 1];
				vec2_t *v_uv = malloc (N * sizeof (vec2_t));
				for (size_t v = 0; v < N; v++) {
					int ti = mesh->vertices[v].extra_texcoord_idx[set - 1];
					v_uv[v] = (ti >= 0 && (size_t)ti < num_ex) ? ex_uv[ti] : (vec2_t){0,0};
				}
				while (bin_size % 4 != 0) {
					if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
					bin_data[bin_size++] = 0;
				}
				size_t fsz = N * sizeof(vec2_t);
				if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
				cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
				bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
				cgltf_accessor *acc = &data.accessors[data.accessors_count++];
				acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec2; acc->count = N;
				memcpy (bin_data + bin_size, v_uv, fsz); bin_size += fsz; free(v_uv);
				
				char name[32]; snprintf(name, sizeof(name), "TEXCOORD_%d", set);
				prim->attributes[attr_idx].name = strdup(name);
				prim->attributes[attr_idx].type = cgltf_attribute_type_texcoord;
				prim->attributes[attr_idx].index = set;
				prim->attributes[attr_idx].data = acc;
				attr_idx++;
			}
		}

		// COLORS
		if (mesh->num_colors[0] > 0)
		{
			color4_t *v_col = malloc (N * sizeof (color4_t));
			for (size_t v = 0; v < N; v++) {
				int ci = mesh->vertices[v].color_idx[0];
				v_col[v] = (ci >= 0 && (size_t)ci < mesh->num_colors[0]) ? mesh->colors[0][ci] : (color4_t){1,1,1,1};
			}
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			size_t fsz = N * sizeof(color4_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv = &data.buffer_views[data.buffer_views_count++];
			bv->buffer = &data.buffers[0]; bv->offset = bin_size; bv->size = fsz;
			cgltf_accessor *acc = &data.accessors[data.accessors_count++];
			acc->buffer_view = bv; acc->component_type = cgltf_component_type_r_32f; acc->type = cgltf_type_vec4; acc->count = N;
			memcpy (bin_data + bin_size, v_col, fsz); bin_size += fsz; free(v_col);
			
			prim->attributes[attr_idx].name = (char*)"COLOR_0";
			prim->attributes[attr_idx].type = cgltf_attribute_type_color;
			prim->attributes[attr_idx].index = 0;
			prim->attributes[attr_idx].data = acc;
			attr_idx++;
		}

		// JOINTS & WEIGHTS
		if (skinned && model->num_joints > 0)
		{
			uint16_t *v_jnt = calloc (N * 4, sizeof (uint16_t));
			float *v_wt = calloc (N * 4, sizeof (float));
			for (size_t v = 0; v < N; v++) {
				int pi = mesh->vertices[v].position_idx;
				int node = mesh->position_node ? mesh->position_node[pi] : -1;
				if (node >= 0 && (size_t)node < model->num_node_influences && model->node_influences[node].num_weights > 0) {
					const node_influence_t *inf = &model->node_influences[node];
					float total_w = 0.0f;
					for (size_t w = 0; w < 4 && w < inf->num_weights; w++) {
						v_jnt[v * 4 + w] = (uint16_t)inf->weights[w].bone_idx;
						v_wt[v * 4 + w] = inf->weights[w].weight;
						total_w += inf->weights[w].weight;
					}
					if (total_w > 0.0f) {
						for (size_t w = 0; w < 4; w++) v_wt[v * 4 + w] /= total_w;
					}
				} else {
					v_jnt[v * 4 + 0] = 0; v_wt[v * 4 + 0] = 1.0f;
				}
			}
			
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			size_t fsz = N * 4 * sizeof(uint16_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv_jnt = &data.buffer_views[data.buffer_views_count++];
			bv_jnt->buffer = &data.buffers[0]; bv_jnt->offset = bin_size; bv_jnt->size = fsz;
			cgltf_accessor *acc_jnt = &data.accessors[data.accessors_count++];
			acc_jnt->buffer_view = bv_jnt; acc_jnt->component_type = cgltf_component_type_r_16u; acc_jnt->type = cgltf_type_vec4; acc_jnt->count = N;
			memcpy (bin_data + bin_size, v_jnt, fsz); bin_size += fsz; free(v_jnt);

			prim->attributes[attr_idx].name = (char*)"JOINTS_0";
			prim->attributes[attr_idx].type = cgltf_attribute_type_joints;
			prim->attributes[attr_idx].index = 0;
			prim->attributes[attr_idx].data = acc_jnt;
			attr_idx++;

			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			fsz = N * 4 * sizeof(float);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv_wt = &data.buffer_views[data.buffer_views_count++];
			bv_wt->buffer = &data.buffers[0]; bv_wt->offset = bin_size; bv_wt->size = fsz;
			cgltf_accessor *acc_wt = &data.accessors[data.accessors_count++];
			acc_wt->buffer_view = bv_wt; acc_wt->component_type = cgltf_component_type_r_32f; acc_wt->type = cgltf_type_vec4; acc_wt->count = N;
			memcpy (bin_data + bin_size, v_wt, fsz); bin_size += fsz; free(v_wt);

			prim->attributes[attr_idx].name = (char*)"WEIGHTS_0";
			prim->attributes[attr_idx].type = cgltf_attribute_type_weights;
			prim->attributes[attr_idx].index = 0;
			prim->attributes[attr_idx].data = acc_wt;
			attr_idx++;
		}

		// INDICES
		while (bin_size % 4 != 0) {
			if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
			bin_data[bin_size++] = 0;
		}
		cgltf_buffer_view *bv_idx = &data.buffer_views[data.buffer_views_count++];
		bv_idx->buffer = &data.buffers[0]; bv_idx->offset = bin_size;
		cgltf_accessor *acc_idx = &data.accessors[data.accessors_count++];
		acc_idx->buffer_view = bv_idx; acc_idx->type = cgltf_type_scalar; acc_idx->count = N;
		if (N < 65536) {
			uint16_t *v_idx = malloc (N * sizeof(uint16_t));
			for (size_t v=0; v<N; v++) v_idx[v] = (uint16_t)v;
			size_t fsz = N * sizeof(uint16_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			bv_idx->size = fsz; acc_idx->component_type = cgltf_component_type_r_16u;
			memcpy(bin_data + bin_size, v_idx, fsz); bin_size += fsz; free(v_idx);
		} else {
			uint32_t *v_idx = malloc (N * sizeof(uint32_t));
			for (size_t v=0; v<N; v++) v_idx[v] = (uint32_t)v;
			size_t fsz = N * sizeof(uint32_t);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			bv_idx->size = fsz; acc_idx->component_type = cgltf_component_type_r_32u;
			memcpy(bin_data + bin_size, v_idx, fsz); bin_size += fsz; free(v_idx);
		}
		prim->indices = acc_idx;
		
		prim->attributes_count = attr_idx;
	}

	for (size_t a = 0; a < model->num_animations; a++)
	{
		cgltf_animation *ganim = &data.animations[data.animations_count++];
		ganim->name = (char*)model->animations[a].name;
		size_t nc = model->animations[a].num_channels;
		ganim->samplers = calloc(nc, sizeof(cgltf_animation_sampler));
		ganim->channels = calloc(nc, sizeof(cgltf_animation_channel));
		ganim->samplers_count = nc;
		ganim->channels_count = nc;
		
		for (size_t c = 0; c < nc; c++)
		{
			const model_anim_channel_t *ch = model->animations[a].channels + c;
			if (!ch->count || !ch->times || !ch->values) continue;
			
			cgltf_animation_sampler *gsmp = &ganim->samplers[c];
			cgltf_animation_channel *gch = &ganim->channels[c];
			
			gsmp->interpolation = cgltf_interpolation_type_linear;
			
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			size_t fsz = ch->count * sizeof(float);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv_in = &data.buffer_views[data.buffer_views_count++];
			bv_in->buffer = &data.buffers[0]; bv_in->offset = bin_size; bv_in->size = fsz;
			cgltf_accessor *acc_in = &data.accessors[data.accessors_count++];
			acc_in->buffer_view = bv_in; acc_in->component_type = cgltf_component_type_r_32f; acc_in->type = cgltf_type_scalar; acc_in->count = ch->count;
			acc_in->has_min = 1; acc_in->has_max = 1;
			acc_in->min[0] = ch->times[0]; acc_in->max[0] = ch->times[ch->count - 1];
			memcpy(bin_data + bin_size, ch->times, fsz); bin_size += fsz;
			gsmp->input = acc_in;
			
			while (bin_size % 4 != 0) {
				if (bin_size >= bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 1024; bin_data = realloc(bin_data, bin_cap); }
				bin_data[bin_size++] = 0;
			}
			fsz = ch->count * ch->components * sizeof(float);
			if (bin_size + fsz > bin_cap) { bin_cap = (bin_size + fsz + 4096) * 2; bin_data = realloc(bin_data, bin_cap); }
			cgltf_buffer_view *bv_out = &data.buffer_views[data.buffer_views_count++];
			bv_out->buffer = &data.buffers[0]; bv_out->offset = bin_size; bv_out->size = fsz;
			cgltf_accessor *acc_out = &data.accessors[data.accessors_count++];
			acc_out->buffer_view = bv_out; acc_out->component_type = cgltf_component_type_r_32f; 
			acc_out->type = ch->path == MODEL_ANIM_WEIGHTS ? cgltf_type_scalar : (ch->components == 4 ? cgltf_type_vec4 : (ch->components == 3 ? cgltf_type_vec3 : cgltf_type_scalar)); 
			acc_out->count = ch->path == MODEL_ANIM_WEIGHTS ? ch->count * ch->components : ch->count;
			memcpy(bin_data + bin_size, ch->values, fsz); bin_size += fsz;
			gsmp->output = acc_out;
			
			gch->sampler = gsmp;
			gch->target_node = &data.nodes[ch->node_idx];
			gch->target_path = ch->path == MODEL_ANIM_TRANSLATION ? cgltf_animation_path_type_translation : (ch->path == MODEL_ANIM_ROTATION ? cgltf_animation_path_type_rotation : (ch->path == MODEL_ANIM_SCALE ? cgltf_animation_path_type_scale : cgltf_animation_path_type_weights));
		}
	}

	for (size_t j = 0; j < model->num_joints; j++) {
		if (model->joints[j].parent_idx == -1) scene_nodes_idx[num_scene_nodes++] = j;
	}
	for (size_t m = 0; m < model->num_meshes; m++) {
		bool has_inst = false;
		for (size_t i = 0; i < model->num_instances; i++) {
			if (model->instances[i].mesh_idx == m) {
				has_inst = true;
				break;
			}
		}
		if (!has_inst) {
			scene_nodes_idx[num_scene_nodes++] = model->num_joints + m;
		}
	}
	for (size_t i = 0; i < model->num_instances; i++) {
		if (model->instances[i].parent_idx == -1) scene_nodes_idx[num_scene_nodes++] = model->num_joints + model->num_meshes + i;
	}
	const size_t scene_object_base = model->num_joints + model->num_meshes + model->num_instances;
	for (size_t i = 0; i < model->num_cameras + model->num_lights; i++) {
		scene_nodes_idx[num_scene_nodes++] = scene_object_base + i;
	}

	data.scenes[0].nodes = calloc(num_scene_nodes > 0 ? num_scene_nodes : 1, sizeof(cgltf_node*));
	data.scenes[0].nodes_count = num_scene_nodes;
	for (size_t i=0; i<num_scene_nodes; i++) data.scenes[0].nodes[i] = &data.nodes[scene_nodes_idx[i]];

	for (size_t j = 0; j < model->num_joints; j++)
	{
		const joint_t *joint = &model->joints[j];
		cgltf_node *gnode = &data.nodes[j];
		gnode->name = (char*)joint->name;
		
		const double hx = joint->rotate.x * M_PI / 360.0, hy = joint->rotate.y * M_PI / 360.0, hz = joint->rotate.z * M_PI / 360.0;
		const double cx = cos (hx), sx = sin (hx), cy = cos (hy), sy = sin (hy), cz = cos (hz), sz = sin (hz);
		const double qx = sx * cy * cz - cx * sy * sz, qy = cx * sy * cz + sx * cy * sz, qz = cx * cy * sz - sx * sy * cz, qw = cx * cy * cz + sx * sy * sz;
		
		gnode->has_translation = 1; gnode->translation[0] = joint->translate.x; gnode->translation[1] = joint->translate.y; gnode->translation[2] = joint->translate.z;
		gnode->has_rotation = 1; gnode->rotation[0] = qx; gnode->rotation[1] = qy; gnode->rotation[2] = qz; gnode->rotation[3] = qw;
		gnode->has_scale = 1; gnode->scale[0] = joint->scale.x; gnode->scale[1] = joint->scale.y; gnode->scale[2] = joint->scale.z;

		size_t num_children = 0;
		for (size_t c = 0; c < model->num_joints; c++) if (model->joints[c].parent_idx == (int)j) num_children++;
		for (size_t i = 0; i < model->num_instances; i++) if (model->instances[i].parent_idx == (int)j) num_children++;
		
		if (num_children > 0) {
			gnode->children = calloc(num_children, sizeof(cgltf_node*));
			gnode->children_count = num_children;
			size_t c_idx = 0;
			for (size_t c = 0; c < model->num_joints; c++) if (model->joints[c].parent_idx == (int)j) gnode->children[c_idx++] = &data.nodes[c];
			for (size_t i = 0; i < model->num_instances; i++) if (model->instances[i].parent_idx == (int)j) gnode->children[c_idx++] = &data.nodes[model->num_joints + model->num_meshes + i];
		}
	}

	for (size_t m = 0; m < model->num_meshes; m++)
	{
		const mesh_t *mesh = &model->meshes[m];
		cgltf_node *gnode = &data.nodes[model->num_joints + m];
		gnode->name = (char*)mesh->name;
		gnode->mesh = &data.meshes[m];
		if (dae_mesh_is_skinned (model, mesh) && model->num_joints > 0 && acc_ibm >= 0) {
			gnode->skin = &data.skins[0];
		}
	}

	for (size_t i = 0; i < model->num_instances; i++)
	{
		const model_instance_t *in = model->instances + i;
		cgltf_node *gnode = &data.nodes[model->num_joints + model->num_meshes + i];
		gnode->name = (char*)in->name;
		gnode->mesh = &data.meshes[in->mesh_idx];
		gnode->has_matrix = 1;
		
		if (in->has_matrix) {
			for (int k = 0; k < 16; k++) gnode->matrix[k] = in->matrix[k];
		} else {
			const double rx = in->rotate.x * M_PI / 180.0, ry = in->rotate.y * M_PI / 180.0, rz = in->rotate.z * M_PI / 180.0;
			const double cx = cos (rx), sx = sin (rx), cy = cos (ry), sy = sin (ry), cz = cos (rz), sz = sin (rz);
			const double r00 = cy * cz, r01 = cz * sx * sy - cx * sz, r02 = sx * sz + cx * cz * sy;
			const double r10 = cy * sz, r11 = cx * cz + sx * sy * sz, r12 = cx * sy * sz - cz * sx;
			const double r20 = -sy, r21 = cy * sx, r22 = cx * cy;
			gnode->matrix[0] = r00 * in->scale.x; gnode->matrix[1] = r10 * in->scale.x; gnode->matrix[2] = r20 * in->scale.x; gnode->matrix[3] = 0;
			gnode->matrix[4] = r01 * in->scale.y; gnode->matrix[5] = r11 * in->scale.y; gnode->matrix[6] = r21 * in->scale.y; gnode->matrix[7] = 0;
			gnode->matrix[8] = r02 * in->scale.z; gnode->matrix[9] = r12 * in->scale.z; gnode->matrix[10] = r22 * in->scale.z; gnode->matrix[11] = 0;
			gnode->matrix[12] = in->translate.x; gnode->matrix[13] = in->translate.y; gnode->matrix[14] = in->translate.z; gnode->matrix[15] = 1;
		}
	}

	for (size_t i = 0; i < model->num_cameras; i++)
	{
		const model_camera_t *c = model->cameras + i;
		cgltf_node *gnode = &data.nodes[scene_object_base + i];
		gnode->name = (char*)c->name;
		gnode->camera = &data.cameras[i];
		gnode->has_matrix = 1;
		for (int k = 0; k < 16; k++) gnode->matrix[k] = c->matrix[k];
		
		cgltf_camera *gcam = &data.cameras[i];
		gcam->name = (char*)c->name;
		gcam->type = cgltf_camera_type_perspective;
		gcam->data.perspective.yfov = c->yfov;
		gcam->data.perspective.znear = c->znear;
		gcam->data.perspective.has_zfar = c->zfar > c->znear;
		gcam->data.perspective.zfar = c->zfar;
	}

	for (size_t i = 0; i < model->num_lights; i++)
	{
		const model_light_t *l = model->lights + i;
		cgltf_node *gnode = &data.nodes[scene_object_base + model->num_cameras + i];
		gnode->name = (char*)l->name;
		gnode->light = &data.lights[i];
		gnode->has_matrix = 1;
		for (int k = 0; k < 16; k++) gnode->matrix[k] = l->matrix[k];

		cgltf_light *glight = &data.lights[i];
		glight->name = (char*)l->name;
		glight->type = l->kind == MODEL_LIGHT_DIRECTIONAL ? cgltf_light_type_directional : (l->kind == MODEL_LIGHT_SPOT ? cgltf_light_type_spot : cgltf_light_type_point);
		glight->color[0] = l->color[0]; glight->color[1] = l->color[1]; glight->color[2] = l->color[2];
		glight->intensity = l->intensity > 0 ? l->intensity : 1;
		glight->range = l->range > 0 ? l->range : 0;
		glight->spot_inner_cone_angle = l->inner_cone;
		glight->spot_outer_cone_angle = l->outer_cone;
	}

	int has_lights = (model->num_lights > 0);
	if (has_lights) {
		int ext_found = 0;
		for(size_t e=0; e<data.extensions_used_count; e++) if(!strcmp(data.extensions_used[e], "KHR_lights_punctual")) ext_found = 1;
		if (!ext_found) data.extensions_used[data.extensions_used_count++] = (char*)"KHR_lights_punctual";
	}

	if (any_skin && model->num_joints > 0 && acc_ibm >= 0)
	{
		data.skins_count = 1;
		data.skins[0].inverse_bind_matrices = &data.accessors[acc_ibm];
		data.skins[0].joints = calloc(model->num_joints, sizeof(cgltf_node*));
		data.skins[0].joints_count = model->num_joints;
		for (size_t j = 0; j < model->num_joints; j++) data.skins[0].joints[j] = &data.nodes[j];
	}

	data.buffers[0].size = bin_size;
	data.bin = bin_data;
	data.bin_size = bin_size;
	
	data.nodes_count = scene_object_base + model->num_cameras + model->num_lights;

	cgltf_result res = cgltf_write_file(&options, out_glb_file, &data);

	// Cleanup
	for(size_t i=0; i<data.meshes_count; i++) {
		for(size_t p=0; p<data.meshes[i].primitives_count; p++) {
			for(size_t a=0; a<data.meshes[i].primitives[p].attributes_count; a++) {
				if (strncmp(data.meshes[i].primitives[p].attributes[a].name, "TEXCOORD_", 9) == 0 && data.meshes[i].primitives[p].attributes[a].index > 0)
					free((void*)data.meshes[i].primitives[p].attributes[a].name);
			}
			free(data.meshes[i].primitives[p].attributes);
			if (data.meshes[i].primitives[p].targets) {
				for(size_t t=0; t<data.meshes[i].primitives[p].targets_count; t++) free(data.meshes[i].primitives[p].targets[t].attributes);
				free(data.meshes[i].primitives[p].targets);
			}
		}
		free(data.meshes[i].primitives);
		if (data.meshes[i].weights) free(data.meshes[i].weights);
		if (data.meshes[i].target_names) free(data.meshes[i].target_names);
	}
	for(size_t a=0; a<data.animations_count; a++) {
		if (data.animations[a].samplers) free(data.animations[a].samplers);
		if (data.animations[a].channels) free(data.animations[a].channels);
	}
	for(size_t i=0; i<data.images_count; i++) {
		if (data.images[i].uri) free((void*)data.images[i].uri);
	}
	for(size_t i=0; i<data.nodes_count; i++) if (data.nodes[i].children) free(data.nodes[i].children);
	if (data.skins[0].joints) free(data.skins[0].joints);
	
	free(data.buffers);
	free(data.buffer_views);
	free(data.accessors);
	free(data.images);
	free(data.textures);
	free(data.samplers);
	free(data.materials);
	free(data.meshes);
	free(data.nodes);
	free(data.scenes[0].nodes);
	free(data.scenes);
	free(data.cameras);
	free(data.lights);
	free(data.animations);
	free(data.skins);
	free(data.extensions_used);
	free(scene_nodes_idx);
	if (bin_data) free(bin_data);

	return (res == cgltf_result_success) ? 0 : -1;
}

static void convert_materials(cgltf_data *data, model_t *model) {
    if (!data->materials_count) return;
    model->materials = calloc(data->materials_count, sizeof(material_t));
    model->num_materials = data->materials_count;
    for (size_t i = 0; i < data->materials_count; i++) {
        cgltf_material *m = &data->materials[i];
        material_t *dst = &model->materials[i];
        if (m->name) snprintf(dst->name, sizeof(dst->name), "%s", m->name);
        else snprintf(dst->name, sizeof(dst->name), "mat_%zu", i);
        
        dst->diffuse[0] = dst->diffuse[1] = dst->diffuse[2] = dst->diffuse[3] = 1.0f;
        
        if (m->has_pbr_metallic_roughness) {
            memcpy(dst->diffuse, m->pbr_metallic_roughness.base_color_factor, sizeof(float)*4);
            if (m->pbr_metallic_roughness.base_color_texture.texture) {
                cgltf_texture *tex = m->pbr_metallic_roughness.base_color_texture.texture;
                if (tex->image && tex->image->name) {
                    snprintf(dst->textures[0], sizeof(dst->textures[0]), "%s", tex->image->name);
                } else if (tex->image && tex->image->uri) {
                    snprintf(dst->textures[0], sizeof(dst->textures[0]), "%s", tex->image->uri);
                }
                dst->num_textures = 1;
            }
        }
    }
}

static void convert_meshes_and_skin(cgltf_data *data, model_t *model) {
    // First, count total primitives
    size_t num_prims = 0;
    for (size_t i = 0; i < data->meshes_count; i++) {
        num_prims += data->meshes[i].primitives_count;
    }
    if (!num_prims) return;
    
    model->meshes = calloc(num_prims, sizeof(mesh_t));
    model->num_meshes = 0;
    
    // We will accumulate node influences across all meshes.
    // In lib-model-dae, node_influences is indexed by position_node.
    // We'll just append each vertex's influence to model->node_influences.
    size_t cap_influences = 1024;
    model->node_influences = calloc(cap_influences, sizeof(node_influence_t));
    model->num_node_influences = 0;
    
    for (size_t i = 0; i < data->meshes_count; i++) {
        cgltf_mesh *m = &data->meshes[i];
        for (size_t j = 0; j < m->primitives_count; j++) {
            cgltf_primitive *p = &m->primitives[j];
            mesh_t *dst = &model->meshes[model->num_meshes++];
            // As with instances below: only disambiguate with a "_<j>"
            // suffix when the glTF mesh actually has multiple primitives.
            // Appending it unconditionally mutates a single-primitive
            // mesh's name on every decode->encode cycle, breaking the
            // canonical fixed point.
            if (m->primitives_count > 1) {
                if (m->name) snprintf(dst->name, sizeof(dst->name), "%s_%zu", m->name, j);
                else snprintf(dst->name, sizeof(dst->name), "mesh_%zu_%zu", i, j);
            } else {
                if (m->name) snprintf(dst->name, sizeof(dst->name), "%s", m->name);
                else snprintf(dst->name, sizeof(dst->name), "mesh_%zu", i);
            }
            
            if (p->material) {
                dst->material_idx = p->material - data->materials;
            } else {
                dst->material_idx = -1;
            }
            
            size_t vertex_count = 0;
            // find position count
            for (size_t k = 0; k < p->attributes_count; k++) {
                if (p->attributes[k].type == cgltf_attribute_type_position) {
                    vertex_count = p->attributes[k].data->count;
                    break;
                }
            }
            
            if (!vertex_count) continue;
            
            cgltf_accessor *acc_pos = NULL, *acc_norm = NULL, *acc_tex[8] = {NULL}, *acc_col[2] = {NULL};
            cgltf_accessor *acc_joints = NULL, *acc_weights = NULL;
            
            for (size_t k = 0; k < p->attributes_count; k++) {
                cgltf_attribute *attr = &p->attributes[k];
                if (attr->type == cgltf_attribute_type_position) acc_pos = attr->data;
                else if (attr->type == cgltf_attribute_type_normal) acc_norm = attr->data;
                else if (attr->type == cgltf_attribute_type_texcoord) {
                    if (attr->index < 8) acc_tex[attr->index] = attr->data;
                }
                else if (attr->type == cgltf_attribute_type_color) {
                    if (attr->index < 2) acc_col[attr->index] = attr->data;
                }
                else if (attr->type == cgltf_attribute_type_joints) acc_joints = attr->data;
                else if (attr->type == cgltf_attribute_type_weights) acc_weights = attr->data;
            }
            
            if (acc_pos) {
                dst->num_positions = acc_pos->count;
                dst->positions = calloc(dst->num_positions, sizeof(vec3_t));
                for (size_t v = 0; v < dst->num_positions; v++) cgltf_accessor_read_float(acc_pos, v, (float*)&dst->positions[v], 3);
            }
            if (acc_norm) {
                dst->num_normals = acc_norm->count;
                dst->normals = calloc(dst->num_normals, sizeof(vec3_t));
                for (size_t v = 0; v < dst->num_normals; v++) cgltf_accessor_read_float(acc_norm, v, (float*)&dst->normals[v], 3);
            }
            if (acc_tex[0]) {
                dst->num_texcoords = acc_tex[0]->count;
                dst->texcoords = calloc(dst->num_texcoords, sizeof(vec2_t));
                for (size_t v = 0; v < dst->num_texcoords; v++) cgltf_accessor_read_float(acc_tex[0], v, (float*)&dst->texcoords[v], 2);
            }
            for (int t = 1; t < 8; t++) {
                if (acc_tex[t]) {
                    dst->num_extra_texcoords[t-1] = acc_tex[t]->count;
                    dst->extra_texcoords[t-1] = calloc(acc_tex[t]->count, sizeof(vec2_t));
                    for (size_t v = 0; v < acc_tex[t]->count; v++) cgltf_accessor_read_float(acc_tex[t], v, (float*)&dst->extra_texcoords[t-1][v], 2);
                }
            }
            for (int c = 0; c < 2; c++) {
                if (acc_col[c]) {
                    dst->num_colors[c] = acc_col[c]->count;
                    dst->colors[c] = calloc(acc_col[c]->count, sizeof(color4_t));
                    for (size_t v = 0; v < acc_col[c]->count; v++) {
                        float col[4] = {1,1,1,1};
                        cgltf_accessor_read_float(acc_col[c], v, col, 4);
                        dst->colors[c][v].r = col[0]; dst->colors[c][v].g = col[1];
                        dst->colors[c][v].b = col[2]; dst->colors[c][v].a = col[3];
                    }
                }
            }
            
            // skinning
            if (acc_joints && acc_weights) {
                dst->position_node = calloc(dst->num_positions, sizeof(int));
                for (size_t v = 0; v < dst->num_positions; v++) {
                    cgltf_uint joints[4] = {0,0,0,0};
                    float weights[4] = {0,0,0,0};
                    cgltf_accessor_read_uint(acc_joints, v, joints, 4);
                    cgltf_accessor_read_float(acc_weights, v, weights, 4);
                    
                    if (model->num_node_influences >= cap_influences) {
                        cap_influences *= 2;
                        model->node_influences = realloc(model->node_influences, cap_influences * sizeof(node_influence_t));
                    }
                    
                    int inf_idx = model->num_node_influences++;
                    node_influence_t *inf = &model->node_influences[inf_idx];
                    inf->weights = calloc(4, sizeof(influence_t));
                    inf->num_weights = 0;
                    for (int w = 0; w < 4; w++) {
                        if (weights[w] > 0.0f) {
                            inf->weights[inf->num_weights].bone_idx = joints[w];
                            inf->weights[inf->num_weights].weight = weights[w];
                            inf->num_weights++;
                        }
                    }
                    dst->position_node[v] = inf_idx;
                }
            } else {
                dst->position_node = calloc(dst->num_positions, sizeof(int));
                for (size_t v = 0; v < dst->num_positions; v++) {
                    dst->position_node[v] = -1;
                }
            }
            
            // Build indices
            if (p->indices) {
                dst->num_vertices = p->indices->count;
                dst->vertices = calloc(dst->num_vertices, sizeof(vertex_t));
                for (size_t v = 0; v < dst->num_vertices; v++) {
                    int idx = cgltf_accessor_read_index(p->indices, v);
                    dst->vertices[v].position_idx = dst->num_positions ? idx : 0;
                    dst->vertices[v].normal_idx = dst->num_normals ? idx : 0;
                    dst->vertices[v].texcoord_idx = dst->num_texcoords ? idx : 0;
                    dst->vertices[v].color_idx[0] = dst->num_colors[0] ? idx : 0;
                    dst->vertices[v].color_idx[1] = dst->num_colors[1] ? idx : 0;
                    for (int e = 0; e < 7; e++) dst->vertices[v].extra_texcoord_idx[e] = dst->num_extra_texcoords[e] ? idx : 0;
                }
            } else {
                dst->num_vertices = vertex_count;
                dst->vertices = calloc(dst->num_vertices, sizeof(vertex_t));
                for (size_t v = 0; v < dst->num_vertices; v++) {
                    dst->vertices[v].position_idx = dst->num_positions ? v : 0;
                    dst->vertices[v].normal_idx = dst->num_normals ? v : 0;
                    dst->vertices[v].texcoord_idx = dst->num_texcoords ? v : 0;
                    dst->vertices[v].color_idx[0] = dst->num_colors[0] ? v : 0;
                    dst->vertices[v].color_idx[1] = dst->num_colors[1] ? v : 0;
                    for (int e = 0; e < 7; e++) dst->vertices[v].extra_texcoord_idx[e] = dst->num_extra_texcoords[e] ? v : 0;
                }
            }
        }
    }
}

static void convert_nodes(cgltf_data *data, model_t *model) {
    if (!data->nodes_count) return;
    
    if (data->skins_count > 0) {
        cgltf_skin *skin = &data->skins[0];
        model->num_joints = skin->joints_count;
        model->joints = calloc(model->num_joints, sizeof(joint_t));
        for (size_t i = 0; i < model->num_joints; i++) {
            cgltf_node *jn = skin->joints[i];
            joint_t *dst = &model->joints[i];
            if (jn->name) snprintf(dst->name, sizeof(dst->name), "%s", jn->name);
            else snprintf(dst->name, sizeof(dst->name), "joint_%zu", i);
            
            if (jn->has_translation) memcpy(&dst->translate, jn->translation, sizeof(vec3_t));
            if (jn->has_rotation) memcpy(&dst->rotate, jn->rotation, sizeof(vec3_t));
            if (jn->has_scale) memcpy(&dst->scale, jn->scale, sizeof(vec3_t));
            else { dst->scale.x = 1.0f; dst->scale.y = 1.0f; dst->scale.z = 1.0f; }
            
            if (skin->inverse_bind_matrices) {
                float ibm[16];
                cgltf_accessor_read_float(skin->inverse_bind_matrices, i, ibm, 16);
                dst->inverse_bind[0] = ibm[0]; dst->inverse_bind[1] = ibm[4]; dst->inverse_bind[2] = ibm[8]; dst->inverse_bind[3] = ibm[12];
                dst->inverse_bind[4] = ibm[1]; dst->inverse_bind[5] = ibm[5]; dst->inverse_bind[6] = ibm[9]; dst->inverse_bind[7] = ibm[13];
                dst->inverse_bind[8] = ibm[2]; dst->inverse_bind[9] = ibm[6]; dst->inverse_bind[10] = ibm[10]; dst->inverse_bind[11] = ibm[14];
                dst->has_inverse_bind = 1;
            }
            
            dst->parent_idx = -1;
            if (jn->parent) {
                for (size_t p = 0; p < model->num_joints; p++) {
                    if (skin->joints[p] == jn->parent) {
                        dst->parent_idx = p;
                        break;
                    }
                }
            }
        }
    }
    
    // Our own GLB export writes an orphan node holding the raw mesh
    // definition (glTF node index model->num_joints+m) whenever no instance
    // references that mesh directly, purely so the mesh has a node slot to
    // point instances at (see the exporter around scene_object_base). That
    // node is never reachable from the scene graph (not a scene root, not
    // anyone's child). Re-importing it as its own instance -- on top of the
    // real instance nodes that already reference the same mesh -- duplicates
    // the placement on every decode->encode cycle, breaking the canonical
    // fixed point. Only mesh-bearing nodes actually reachable from the scene
    // graph become instances.
    cgltf_bool *is_root = data->nodes_count ? calloc(data->nodes_count, sizeof(cgltf_bool)) : 0;
    if (data->scene) {
        for (size_t s = 0; s < data->scene->nodes_count; s++) {
            cgltf_node *rn = data->scene->nodes[s];
            size_t idx = (size_t)(rn - data->nodes);
            if (idx < data->nodes_count) is_root[idx] = 1;
        }
    }
    #define NODE_REACHABLE(n) ((n)->parent != NULL || is_root[(size_t)((n) - data->nodes)])

    size_t num_inst = 0;
    for (size_t i = 0; i < data->nodes_count; i++) {
        if (data->nodes[i].mesh && NODE_REACHABLE(&data->nodes[i])) {
            num_inst += data->nodes[i].mesh->primitives_count;
        }
    }

    if (num_inst) {
        model->instances = calloc(num_inst, sizeof(model_instance_t));
        model->num_instances = 0;

        for (size_t i = 0; i < data->nodes_count; i++) {
            cgltf_node *n = &data->nodes[i];
            if (n->mesh && NODE_REACHABLE(n)) {
                int base_mesh_idx = -1;
                for (size_t mi = 0; mi < data->meshes_count; mi++) {
                    if (&data->meshes[mi] == n->mesh) {
                        size_t prim_offset = 0;
                        for (size_t prev = 0; prev < mi; prev++) prim_offset += data->meshes[prev].primitives_count;
                        base_mesh_idx = prim_offset;
                        break;
                    }
                }
                
                for (size_t p = 0; p < n->mesh->primitives_count; p++) {
                    model_instance_t *dst = &model->instances[model->num_instances++];
                    // Only disambiguate with a "_<p>" suffix when the node's
                    // mesh actually has multiple primitives -- appending it
                    // unconditionally mutates a single-primitive node's name
                    // on every decode->encode cycle (e.g. "x" -> "x_0" ->
                    // "x_0_0" ...), breaking the canonical fixed point.
                    if (n->mesh->primitives_count > 1) {
                        if (n->name) snprintf(dst->name, sizeof(dst->name), "%s_%zu", n->name, p);
                        else snprintf(dst->name, sizeof(dst->name), "inst_%zu_%zu", i, p);
                    } else {
                        if (n->name) snprintf(dst->name, sizeof(dst->name), "%s", n->name);
                        else snprintf(dst->name, sizeof(dst->name), "inst_%zu", i);
                    }
                    
                    dst->mesh_idx = base_mesh_idx + p;
                    dst->parent_idx = -1;
                    
                    if (n->has_translation) memcpy(&dst->translate, n->translation, sizeof(vec3_t));
                    if (n->has_rotation) memcpy(&dst->rotate, n->rotation, sizeof(vec3_t));
                    if (n->has_scale) memcpy(&dst->scale, n->scale, sizeof(vec3_t));
                    else { dst->scale.x = 1.0f; dst->scale.y = 1.0f; dst->scale.z = 1.0f; }
                    
                    if (n->has_matrix) {
                        memcpy(dst->matrix, n->matrix, sizeof(float)*16);
                        dst->has_matrix = 1;
                    }
                }
            }
        }
    }
    #undef NODE_REACHABLE
    free(is_root);
}

static void convert_animations(cgltf_data *data, model_t *model) {
    if (!data->animations_count) return;
    model->animations = calloc(data->animations_count, sizeof(model_animation_t));
    model->num_animations = data->animations_count;
    
    for (size_t i = 0; i < data->animations_count; i++) {
        cgltf_animation *a = &data->animations[i];
        model_animation_t *dst = &model->animations[i];
        if (a->name) snprintf(dst->name, sizeof(dst->name), "%s", a->name);
        else snprintf(dst->name, sizeof(dst->name), "anim_%zu", i);
        
        dst->num_channels = a->channels_count;
        dst->channels = calloc(dst->num_channels, sizeof(model_anim_channel_t));
        
        for (size_t c = 0; c < a->channels_count; c++) {
            cgltf_animation_channel *ch = &a->channels[c];
            model_anim_channel_t *dst_ch = &dst->channels[c];
            
            dst_ch->node_idx = -1;
            if (data->skins_count > 0 && ch->target_node) {
                cgltf_skin *skin = &data->skins[0];
                for (size_t j = 0; j < skin->joints_count; j++) {
                    if (skin->joints[j] == ch->target_node) {
                        dst_ch->node_idx = j;
                        break;
                    }
                }
            }
            
            if (ch->target_path == cgltf_animation_path_type_translation) dst_ch->path = MODEL_ANIM_TRANSLATION;
            else if (ch->target_path == cgltf_animation_path_type_rotation) dst_ch->path = MODEL_ANIM_ROTATION;
            else if (ch->target_path == cgltf_animation_path_type_scale) dst_ch->path = MODEL_ANIM_SCALE;
            else if (ch->target_path == cgltf_animation_path_type_weights) dst_ch->path = MODEL_ANIM_WEIGHTS;
            
            cgltf_animation_sampler *samp = ch->sampler;
            if (samp) {
                dst_ch->count = samp->input->count;
                dst_ch->times = calloc(dst_ch->count, sizeof(float));
                for (size_t t = 0; t < dst_ch->count; t++) cgltf_accessor_read_float(samp->input, t, &dst_ch->times[t], 1);
                
                size_t comp = 1;
                if (samp->output->type == cgltf_type_vec2) comp = 2;
                else if (samp->output->type == cgltf_type_vec3) comp = 3;
                else if (samp->output->type == cgltf_type_vec4) comp = 4;
                
                dst_ch->components = comp;
                dst_ch->values = calloc(dst_ch->count * comp, sizeof(float));
                for (size_t t = 0; t < dst_ch->count; t++) {
                    cgltf_accessor_read_float(samp->output, t, &dst_ch->values[t * comp], comp);
                }
            }
        }
    }
}

static model_t *BuildModelFromCgltf(cgltf_data *data) {
    model_t *model = calloc(1, sizeof(model_t));
    if (!model) return NULL;
    convert_materials(data, model);
    convert_meshes_and_skin(data, model);
    convert_nodes(data, model);
    convert_animations(data, model);
    return model;
}

model_t *ParseGLBFile (const char *filename) {
    cgltf_options options = {0};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, filename, &data);
    if (result != cgltf_result_success) return NULL;
    
    result = cgltf_load_buffers(&options, data, filename);
    if (result != cgltf_result_success) {
        cgltf_free(data);
        return NULL;
    }
    
    model_t *m = BuildModelFromCgltf(data);
    cgltf_free(data);
    return m;
}

model_t *ParseGLB (const uint8_t *in_data, size_t size) {
    cgltf_options options = {0};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse(&options, in_data, size, &data);
    if (result != cgltf_result_success) return NULL;
    
    result = cgltf_load_buffers(&options, data, NULL);
    if (result != cgltf_result_success) {
        cgltf_free(data);
        return NULL;
    }
    
    model_t *m = BuildModelFromCgltf(data);
    cgltf_free(data);
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


