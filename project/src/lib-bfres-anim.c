// SPDX-License-Identifier: GPL-2.0+
// Binding of animation-only Switch BFRES archives to a skeleton; see lib-bfres.h.

#include "lib-bfres.h"
#include "lib-szs.h"
#include "lib-zstd.h"
#include <dirent.h>

static bool is_ext (ccp src, ccp ext)
{
	const size_t a = strlen (src), b = strlen (ext);
	return a >= b && !strcasecmp (src + a - b, ext);
}

// An animation-only Switch BFRES (FSKA list, no FMDL; e.g. Tomodachi Life: Living the Dream
// "Model/AnimationProduct.bfres") is bound to a skeleton-only BFRES next to it: the first file in the same
// directory whose name contains "Skeleton" (shortest name first; ".bfres" or ZSTD ".bfres.zs") that gets
// at least one animation. Returns the skeleton model with the animations, or NULL.
model_t *LoadBFRESSwitchAnimSkeleton (ccp arg, const u8 *data, size_t size)
{
	if (le16 (data + 0x0C) != 0xFEFF)
		return NULL;
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	DIR *dp = opendir (dir);
	if (!dp)
		return NULL;
	char cand[64][256];
	uint n_cand = 0;
	// Tears-of-the-Kingdom style romfs: "<Base>_Animation.anim.bfres" animates the model
	// "<Base>.<Base>.bfres" (".zs") in the same directory; try those before any "*Skeleton*" file.
	uint n_first = 0;
	{
		ccp fn = strrchr (arg, '/');
		fn = fn ? fn + 1 : arg;
		char base[256];
		snprintf (base, sizeof (base), "%s", fn);
		char *dot = strstr (base, "_Animation.anim.bfres");
		if (dot)
		{
			*dot = 0;
			static ccp const sfx[] = { ".bfres.zs", ".bfres" };
			for (uint k = 0; k < 2 && n_cand < 63; k++)
			{
				char path[PATH_MAX];
				snprintf (cand[n_cand], sizeof (cand[0]), "%s.%s%s", base, base, sfx[k]);
				snprintf (path, sizeof (path), "%s/%s", dir, cand[n_cand]);
				if (!access (path, F_OK))
					n_cand++, n_first++;
			}
		}
	}
	for (const struct dirent *de; n_cand < 64 && (de = readdir (dp));)
		if (strstr (de->d_name, "Skeleton") && (is_ext (de->d_name, ".bfres") || is_ext (de->d_name, ".bfres.zs"))
			&& strlen (de->d_name) < sizeof (cand[0]))
			snprintf (cand[n_cand++], sizeof (cand[0]), "%s", de->d_name);
	closedir (dp);
	for (uint i = n_first + 1; i < n_cand; i++) // shortest (then alphabetical) first
		for (uint j = i; j > n_first; j--)
		{
			const size_t a = strlen (cand[j - 1]), b = strlen (cand[j]);
			if (a < b || (a == b && strcmp (cand[j - 1], cand[j]) <= 0))
				break;
			char t[256];
			memcpy (t, cand[j], sizeof (t));
			memcpy (cand[j], cand[j - 1], sizeof (t));
			memcpy (cand[j - 1], t, sizeof (t));
		}
	for (uint i = 0; i < n_cand; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dir, cand[i]);
		u8 *skel = 0, *dec = 0;
		size_t skel_size = 0;
		if (LoadFileAlloc (path, 0, 0, &skel, &skel_size, 0, 0, 0, false) || !skel)
		{
			FREE (skel);
			continue;
		}
		const u8 *sp = skel;
		size_t sn = skel_size;
		uint dn = 0;
		if (skel_size >= 4 && !memcmp (skel, "\x28\xb5\x2f\xfd", 4) && skel_size < 0x40000000
			&& !DecodeZSTD (&dec, &dn, skel, (uint)skel_size) && dec)
			sp = dec, sn = dn;
		model_t *m = ParseBFRESSwitch (sp, sn);
		FREE (dec);
		FREE (skel);
		if (!m)
			continue;
		const int n = ImportBFRESSwitchAnims (m, data, size);
		if (n > 0)
		{
			if (verbose >= 0)
				fprintf (stdlog, "  - %d skeletal animations bound to skeleton %s (%zu joints)\n", n, path,
					(size_t)m->num_joints);
			return m;
		}
		FreeModel (m);
	}
	return NULL;
}
