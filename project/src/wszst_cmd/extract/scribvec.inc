// Scribblenauts Unlimited vector meshes (the extracted index.bin members that
// hold object / effect artwork; little-endian even on Wii U). No magic, so the
// layout is validated by consuming the file exactly. Format after GMMan's
// scrib-vecviz:
//   u8 flags (low 6 bits == 2; 0x40 fixed-point points, 0x80 bounds block);
//   u16 xscale<<3, u16 yscale<<3 (stored >> 3 << 2); u16 groups; u8 layers;
//   u16 npoints; points (0x40: runs of {u16 n, u16 color, n * s16x2 / 8196};
//   else npoints * {f32 x, f32 y, u16 color}); u16 nidx + u16 indices;
//   [0x80: u16 n, n * {u16 group, 4 * f32}]; u16 ngroups, {u16 id, u16 start,
//   u16 end}; two outline tables (ngroups * {u16 id, u16 n, n * s16});
//   u16 ncolors, 4 bytes each, then ncolors * u16 z-order.
// Triangles are written to an SVG, one path per colour in z order.
typedef struct { const u8 *p, *e; bool bad; } sv_rd_t;
static uint sv_u (sv_rd_t *r, uint n)
{
	if ((size_t)(r->e - r->p) < n)
	{
		r->bad = true;
		return 0;
	}
	uint v = 0;
	for (uint i = 0; i < n; i++)
		v |= (uint)r->p[i] << (8 * i);
	r->p += n;
	return v;
}
static float sv_f (sv_rd_t *r)
{
	const u32 v = sv_u (r, 4);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_scribvec_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 16 || (raw[0] & 0x3f) != 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	sv_rd_t r = { raw, raw + raw_size, false };
	const uint flags = sv_u (&r, 1);
	const float xs = (float)(((sv_u (&r, 2) >> 3) << 2));
	const float ys = (float)(((sv_u (&r, 2) >> 3) << 2));
	const uint ngroups = sv_u (&r, 2);
	sv_u (&r, 1);
	const uint npts = sv_u (&r, 2);
	if (!ngroups || !npts || (flags & 0x40 ? npts * 4 : npts * 10) > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	float *px = MALLOC (npts * sizeof (float)), *py = MALLOC (npts * sizeof (float));
	u16 *pc = MALLOC (npts * sizeof (u16));
	u16 *idx = 0, *gs = 0, *ge = 0;
	u8 *cols = 0;
	u16 *z = 0;
	u8 *drawn = 0;
	bool ok = px && py && pc;
	uint n = 0, nidx = 0, ncols = 0;
	if (ok && (flags & 0x40))
	{
		for (uint left = npts; left && !r.bad && ok;)
		{
			const uint g = sv_u (&r, 2), c = sv_u (&r, 2);
			if (!g || g > left)
			{
				ok = false;
				break;
			}
			for (uint i = 0; i < g && !r.bad; i++, n++)
			{
				px[n] = (int16_t)sv_u (&r, 2) / 8196.0f * xs;
				py[n] = (int16_t)sv_u (&r, 2) / 8196.0f * ys;
				pc[n] = c;
			}
			left -= g;
		}
	}
	else if (ok)
	{
		for (; n < npts && !r.bad; n++)
		{
			px[n] = sv_f (&r) * xs;
			py[n] = sv_f (&r) * ys;
			pc[n] = sv_u (&r, 2);
		}
	}
	if (ok && !r.bad)
	{
		nidx = sv_u (&r, 2);
		idx = MALLOC ((nidx + 1) * sizeof (u16));
		ok = idx != 0;
		for (uint i = 0; ok && i < nidx && !r.bad; i++)
			idx[i] = sv_u (&r, 2);
	}
	if (ok && !r.bad && (flags & 0x80))
	{
		const uint nb = sv_u (&r, 2);
		for (uint i = 0; i < nb && !r.bad; i++)
		{
			sv_u (&r, 2);
			sv_u (&r, 16);
		}
	}
	uint ng = 0;
	if (ok && !r.bad)
	{
		ng = sv_u (&r, 2);
		gs = MALLOC ((ng + 1) * sizeof (u16));
		ge = MALLOC ((ng + 1) * sizeof (u16));
		ok = gs && ge;
		for (uint i = 0; ok && i < ng && !r.bad; i++)
		{
			sv_u (&r, 2);
			gs[i] = sv_u (&r, 2);
			ge[i] = sv_u (&r, 2);
		}
		for (uint t = 0; ok && t < 2; t++)
			for (uint i = 0; i < ng && !r.bad; i++)
			{
				sv_u (&r, 2);
				const uint c = sv_u (&r, 2);
				if ((size_t)(r.e - r.p) < 2ull * c)
					r.bad = true;
				else
					r.p += 2 * c;
			}
	}
	if (ok && !r.bad)
	{
		ncols = sv_u (&r, 2);
		if ((size_t)(r.e - r.p) < 6ull * ncols)
			r.bad = true;
		else
		{
			cols = MALLOC (4 * ncols + 1);
			z = MALLOC ((ncols + 1) * sizeof (u16));
			ok = cols && z;
			if (ok)
			{
				memcpy (cols, r.p, 4 * ncols);
				r.p += 4 * ncols;
				for (uint i = 0; i < ncols; i++)
					z[i] = sv_u (&r, 2);
			}
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	// exact fit, sane triangle ranges
	if (ok && !r.bad && r.p == r.e && n == npts && ncols)
	{
		for (uint i = 0; i < ng && ok; i++)
			ok = ge[i] <= nidx && gs[i] <= ge[i];
		for (uint i = 0; i < nidx && ok; i++)
			ok = idx[i] < npts;
		for (uint i = 0; i < npts && ok; i++)
			ok = pc[i] < ncols;
	}
	else
		ok = false;
	if (ok)
	{
		float minx = 0, miny = 0, maxx = 0, maxy = 0;
		for (uint i = 0; i < npts; i++)
		{
			if (px[i] < minx) minx = px[i];
			if (py[i] < miny) miny = py[i];
		}
		for (uint i = 0; i < npts; i++)
		{
			if (px[i] - minx > maxx) maxx = px[i] - minx;
			if (py[i] - miny > maxy) maxy = py[i] - miny;
		}
		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".svg", false);
		else
			snprintf (dest, sizeof (dest), "%s.svg", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE SCRIBVEC:%s -> %s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		drawn = CALLOC (ncols + 1, 1);
		if (!testmode && drawn)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f)
			{
				fprintf (F.f, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\">\n",
					maxx, maxy);
				// colours in ascending (z, index) order
				for (uint k = 0; k < ncols; k++)
				{
					uint best = ncols;
					for (uint c = 0; c < ncols; c++)
						if (!drawn[c] && (best == ncols || z[c] < z[best]))
							best = c;
					drawn[best] = 1;
					bool open = false;
					for (uint g = 0; g < ng; g++)
						for (uint i = gs[g]; i + 3 <= ge[g]; i += 3)
						{
							if (pc[idx[i]] != best)
								continue;
							if (!open)
							{
								const u8 *c = cols + 4 * best;
								uint R, G, B, A;
								if (best & 1)
									A = 255, R = c[2], G = c[0], B = c[1];
								else
									A = c[3], R = c[2], G = c[1], B = c[0];
								fprintf (F.f, "<path fill=\"#%02x%02x%02x\" fill-opacity=\"%.3f\" d=\"",
									R, G, B, A / 255.0);
								open = true;
							}
							for (uint v = 0; v < 3; v++)
								fprintf (F.f, "%c%g %g", v ? 'L' : 'M',
									px[idx[i + v]] - minx, py[idx[i + v]] - miny);
							fputc ('z', F.f);
						}
					if (open)
						fputs ("\"/>\n", F.f);
				}
				fputs ("</svg>\n", F.f);
			}
			ResetFile (&F, opt_preserve);
		}
	}
	FREE (raw);
	FREE (px);
	FREE (py);
	FREE (pc);
	FREE (idx);
	FREE (gs);
	FREE (ge);
	FREE (cols);
	FREE (z);
	FREE (drawn);
	return err;
}
