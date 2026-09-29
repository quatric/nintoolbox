// True only while extract_tree_complete()'s own first pass (raw extraction,
// export_count temporarily zeroed) is running. HSF embeds and decodes its
// own textures as a side effect of the very same call that writes the model
// (see lib-hsf.c's DecodeHSF()), so converting a .hsf during this first pass
// -- before SetDAETextureSearchRoot() has indexed anything -- means every
// texture lookup misses and the model comes out untextured, even though the
// referenced PNG gets written to disk moments later by that same call.
// extract_hsf_file() checks this flag to defer to the second, properly-
// indexed pass (export_models_tree(), which calls it again once this flag
// is back off) instead of converting immediately. A plain single-file
// `wszst EXTRACT foo.hsf`/`XDECODE foo.hsf` never sets this flag at all
// (extract_one_file() is called directly, never through
// extract_tree_complete()), so that already-verified-working standalone
// path is unaffected.
static bool g_in_model_deferred_pass = false;

// SubstDest() with a NULL/empty 'dest' param just echoes 'arg' back
// unchanged -- it returns before ever touching the "\1P/\1N" pattern (see
// its own early-return). extract_tree()'s recursion into pass-through
// staged files deliberately passes opt_dest=0 ("each decoded file belongs
// beside its own source"), so every one of SARC/PAC/GFA's "\1P/\1N"
// SubstDest() calls degenerated to "extract into a directory with the
// exact same path as the source file itself" -- a guaranteed "Not a
// directory" write failure the moment the first member is written. Only
// caught because a real Kirby's Epic Yarn WBFS exercises this exact path
// (2329 of 2342 on-disc .gfa archives go through here); the opt_dest-set
// case this was originally tested with never triggers SubstDest()'s
// early-return, so it looked fine in isolation.
static void beside_source_dest (char *dest, uint dest_size, ccp arg)
{
	if (opt_dest)
		SubstDest (dest, dest_size, arg, opt_dest, "\1P/\1N", 0, false);
	else
		snprintf (dest, dest_size, "%s.d", arg);
}

// Like beside_source_dest(), but for formats that decode to a single sibling
// file (e.g. "foo.tex" -> "foo.png") rather than a member-extraction
// directory: swaps the source's own extension for 'new_ext' instead of
// appending ".d".
static void beside_source_dest_ext (char *dest, uint dest_size, ccp arg, ccp new_ext)
{
	if (opt_dest)
	{
		SubstDest (dest, dest_size, arg, opt_dest, "\1P/\1N", 0, false);
		return;
	}
	char base[PATH_MAX];
	StringCopyS (base, sizeof (base), arg);
	char *dot = strrchr (base, '.');
	char *slash = strrchr (base, '/');
	if (dot && (!slash || dot > slash))
		*dot = 0;
	snprintf (dest, dest_size, "%s%s", base, new_ext);
}

// Two different sources in one archive can share a stem -- Excite Truck ships
// betatex2.art beside betatex2.tex -- and both decode to an image, so swapping
// the extension alone points them at the same PNG and the second one is lost
// with a "file already exists". Keep the plain name when it is free and fall
// back to carrying the source extension through when it is not, so the common
// case still reads <stem>.png.
static void beside_source_image_dest (char *dest, uint dest_size, ccp arg)
{
	beside_source_dest_ext (dest, dest_size, arg, ".png");
	if (access (dest, F_OK))
		return;

	// Taken: reproduce the source's own name and add the suffix.
	char taken[PATH_MAX];
	StringCopyS (taken, sizeof (taken), dest);

	if (opt_dest)
	{
		ccp slash = strrchr (arg, '/');
		snprintf (dest, dest_size, "%s/%s.png", opt_dest, slash ? slash + 1 : arg);
	}
	else
		snprintf (dest, dest_size, "%s.png", arg);

	// Nothing gained if that is the same path; leave the original in place.
	if (!strcmp (dest, taken))
		StringCopyS (dest, dest_size, taken);
}

// Sonic and the Secret Rings / Sonic and the Black Knight use a small
// big-endian ONE archive around PRS-compressed assets.  It has no magic:
// header[1] is always the 0x10 entry-table offset and header[3] is 0 (Secret
// Rings) or -1 (Black Knight).  Keep detection deliberately structural so a
// random file named .one is not opened as an archive.
static bool decode_storybook_prs (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	if (!out || !out_size || !in || !in_size)
		return false;

	uint ip = 0, op = 0;
	int n_bits = 9;
	u8 control = in[ip++];
	for (;;)
	{
#define PRS_BIT()                                                                                  \
	(--n_bits == 0 ? (ip >= in_size ? -1 : (control = in[ip++], n_bits = 8, control & 1))          \
				   : control & 1)
		int bit = PRS_BIT ();
		if (bit < 0)
			return false;
		control >>= 1;
		if (bit)
		{
			if (ip >= in_size || op >= out_size)
				return false;
			out[op++] = in[ip++];
			continue;
		}

		bit = PRS_BIT ();
		if (bit < 0)
			return false;
		control >>= 1;
		int offset, length;
		if (bit)
		{
			if (ip + 2 > in_size)
				return false;
			const uint word = in[ip] | in[ip + 1] << 8;
			ip += 2;
			if (!word)
				return op == out_size;
			length = word & 7;
			offset = (int)(word >> 3) - 0x2000;
			if (!length)
			{
				if (ip >= in_size)
					return false;
				length = in[ip++] + 1;
			}
			else
				length += 2;
		}
		else
		{
			int b0 = PRS_BIT ();
			if (b0 < 0)
				return false;
			control >>= 1;
			int b1 = PRS_BIT ();
			if (b1 < 0 || ip >= in_size)
				return false;
			control >>= 1;
			length = (b0 << 1 | b1) + 2;
			offset = (int)in[ip++] - 0x100;
		}
		if (offset >= 0 || (uint)-offset > op || length <= 0 || (uint)length > out_size - op)
			return false;
		while (length--)
			out[op] = out[op + offset], op++;
#undef PRS_BIT
	}
}
