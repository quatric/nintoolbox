// X11 Bitmap Distribution Format fonts ".bdf" (text): every glyph (STARTCHAR .. ENDCHAR with BBX and BITMAP rows) is placed into a cell of the FONTBOUNDINGBOX size and the font
// is written as a 16-column sheet "<name>.png" (glyphs in file order, white on black) plus "info.txt" (font name, size, glyph count, encodings).
static enumError extract_bdf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bdf") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 20 && !memcmp (raw, "STARTFONT", 9))
	{
		char *t = MALLOC (sz + 1);
		if (t)
		{
			memcpy (t, raw, sz);
			t[sz] = 0;
			int fw = 0, fh = 0, fx = 0, fy = 0;
			uint nglyph = 0;
			char fname[128] = "";
			// first pass: bounding box and glyph count
			for (char *l = t; l && *l;)
			{
				char *nl = strchr (l, '\n');
				if (nl) *nl = 0;
				if (!strncmp (l, "FONTBOUNDINGBOX ", 16)) sscanf (l + 16, "%d %d %d %d", &fw, &fh, &fx, &fy);
				else if (!strncmp (l, "FONT ", 5)) snprintf (fname, sizeof (fname), "%.100s", l + 5);
				else if (!strncmp (l, "STARTCHAR", 9)) nglyph++;
				if (nl) *nl = '\n';
				l = nl ? nl + 1 : 0;
			}
			if (fw > 0 && fh > 0 && fw <= 128 && fh <= 128 && nglyph && nglyph <= 70000)
			{
				const uint cols = 16, rows = (nglyph + cols - 1) / cols, cw = fw + 1, chh = fh + 1, pw = cols * cw, ph = rows * chh;
				u8 *rgba = CALLOC ((size_t)pw * ph, 4);
				if (rgba)
				{
					for (size_t i = 0; i < (size_t)pw * ph; i++)
						rgba[i * 4 + 3] = 255;
					uint g = 0;
					int bw = 0, bh = 0, bx = 0, by = 0;
					bool inbitmap = false;
					int row = 0;
					for (char *l = t; l && *l && g <= nglyph;)
					{
						char *nl = strchr (l, '\n');
						if (nl) *nl = 0;
						if (!strncmp (l, "STARTCHAR", 9)) { bw = bh = bx = by = 0; inbitmap = false; }
						else if (!strncmp (l, "BBX ", 4)) sscanf (l + 4, "%d %d %d %d", &bw, &bh, &bx, &by);
						else if (!strncmp (l, "BITMAP", 6)) { inbitmap = true; row = 0; }
						else if (!strncmp (l, "ENDCHAR", 7)) { inbitmap = false; g++; }
						else if (inbitmap && row < bh)
						{
							// row y position: top of the glyph box is at fy + fh - (by + bh) from the cell top
							const int top = fh + fy - (by + bh) ;
							const int gy = top + row;
							for (int x = 0; x < bw; x++)
							{
								const char hc = l[x / 4];
								const int v = hc >= '0' && hc <= '9' ? hc - '0' : (hc | 32) >= 'a' && (hc | 32) <= 'f' ? (hc | 32) - 'a' + 10 : 0;
								const int cx = bx - fx + x;
								if ((v >> (3 - x % 4) & 1) && gy >= 0 && gy < fh && cx >= 0 && cx < fw && g < nglyph)
								{
									u8 *o = rgba + (((size_t)(g / cols) * chh + gy) * pw + (g % cols) * cw + cx) * 4;
									o[0] = o[1] = o[2] = 255;
								}
							}
							row++;
						}
						if (nl) *nl = '\n';
						l = nl ? nl + 1 : 0;
					}
					char stem[PATH_MAX], dest[PATH_MAX], text[256];
					const char *sl = strrchr (arg, '/');
					snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
					char *dot = strrchr (stem, '.');
					if (dot) *dot = 0;
					beside_source_dest (dest, sizeof (dest), arg);
					err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, stem, (uint)-1, pw, ph, rgba);
					const int n = snprintf (text, sizeof (text), "font: %s\nglyphs: %u\nbounding box: %dx%d (%d,%d)\n", fname, nglyph, fw, fh, fx, fy);
					if (!err)
						err = shade_write (arg, dest, basedir, "info.txt", (u8 *)text, (uint)n);
					FREE (rgba);
				}
			}
			FREE (t);
		}
	}
	FREE (raw);
	return err;
}
