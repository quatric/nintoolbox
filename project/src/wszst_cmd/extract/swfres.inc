// Adobe Flash movies (".swf", "FWS" plain or "CWS" zlib; Spaghetti Western Shooter's flash/ menus and music, seen in about 40 census games): the tag stream is walked and the embedded media written
// to "<file>.d/": DefineSound (14) as "sound_<id>.mp3" (MP3, the 2-byte seek field removed) or "sound_<id>.wav" (uncompressed PCM), DefineBitsJPEG (6, with the JPEGTables of tag 8) /
// DefineBitsJPEG2 (21) / DefineBitsJPEG3 (35, alpha ignored) as "image_<id>.jpg", DefineBitsLossless (20) / DefineBitsLossless2 (36) as "image_<id>.png". ADPCM / Nellymoser / Speex sounds, shapes, text and
// ActionScript are not decoded.
static void swf_le32 (u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static enumError extract_swfres_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".swf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *b = raw, *own = 0, *jt = 0;
	size_t bs = sz, jts = 0;
	if (sz < 20 || (memcmp (raw, "FWS", 3) && memcmp (raw, "CWS", 3)) || raw[3] < 3 || raw[3] > 40)
		goto done;
	if (raw[0] == 'C')
	{
		uint osz = 0;
		size_t used = 0;
		u8 *body = fst_inflate (raw + 8, sz - 8, &osz, &used);
		if (!body)
			goto done;
		own = MALLOC (8 + (size_t)osz);
		if (!own)
		{
			FREE (body);
			goto done;
		}
		memcpy (own, raw, 8);
		memcpy (own + 8, body, osz);
		FREE (body);
		b = own;
		bs = 8 + (size_t)osz;
	}
	{
		size_t p = 8 + (5 + 4 * (size_t)(b[8] >> 3) + 7) / 8 + 4;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.d", arg);
		uint n = 0;
		static const uint rates[4] = {5512, 11025, 22050, 44100};
		while (p + 2 <= bs)
		{
			const uint v = b[p] | b[p + 1] << 8, t = v >> 6;
			size_t l = v & 63;
			p += 2;
			if (l == 63)
			{
				if (p + 4 > bs)
					break;
				l = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | (size_t)b[p + 3] << 24;
				p += 4;
			}
			if (p + l > bs || !t)
				break;
			const u8 *d = b + p;
			char name[64];
			u8 *out = 0;
			const u8 *data = 0;
			size_t dsz = 0;
			name[0] = 0;
			if (t == 8)
			{
				FREE (jt);
				jt = MALLOC (l + 1);
				if (jt)
				{
					memcpy (jt, d, l);
					jts = l;
				}
			}
			else if (t == 14 && l > 7)
			{
				const uint id = d[0] | d[1] << 8, fmt = d[2] >> 4, rate = rates[(d[2] >> 2) & 3], bits16 = (d[2] >> 1) & 1, st = d[2] & 1;
				if (fmt == 2 && l > 9)
				{
					snprintf (name, sizeof (name), "sound_%u.mp3", id);
					data = d + 9;
					dsz = l - 9;
				}
				else if ((fmt == 0 || fmt == 3) && l > 7)
				{
					snprintf (name, sizeof (name), "sound_%u.wav", id);
					dsz = l - 7;
					out = MALLOC (44 + dsz);
					if (out)
					{
						memcpy (out, "RIFF", 4);
						swf_le32 (out + 4, 36 + (u32)dsz);
						memcpy (out + 8, "WAVEfmt ", 8);
						swf_le32 (out + 16, 16);
						out[20] = 1;
						out[21] = 0;
						out[22] = st + 1;
						out[23] = 0;
						swf_le32 (out + 24, rate);
						swf_le32 (out + 28, rate * (st + 1) * (bits16 + 1));
						out[32] = (st + 1) * (bits16 + 1);
						out[33] = 0;
						out[34] = bits16 ? 16 : 8;
						out[35] = 0;
						memcpy (out + 36, "data", 4);
						swf_le32 (out + 40, (u32)dsz);
						memcpy (out + 44, d + 7, dsz);
						data = out;
						dsz += 44;
					}
				}
			}
			else if ((t == 21 || t == 35 || t == 6) && l > 6)
			{
				const uint id = d[0] | d[1] << 8;
				size_t off = 2;
				size_t len = l - 2;
				if (t == 35)
				{
					const size_t ao = d[2] | d[3] << 8 | d[4] << 16 | (size_t)d[5] << 24;
					off = 6;
					len = ao < l - 6 ? ao : l - 6;
				}
				const u8 *j = d + off;
				// some encoders prefix a bogus "FF D9 FF D8" pair
				if (len > 4 && j[0] == 0xff && j[1] == 0xd9 && j[2] == 0xff && j[3] == 0xd8)
				{
					j += 4;
					len -= 4;
				}
				if (len > 4 && j[0] == 0xff && j[1] == 0xd8)
				{
					snprintf (name, sizeof (name), "image_%u.jpg", id);
					if (t == 6 && jts > 4)
					{
						size_t tl = jts;
						if (tl > 2 && jt[tl - 2] == 0xff && jt[tl - 1] == 0xd9)
							tl -= 2;
						out = MALLOC (tl + len);
						if (out)
						{
							memcpy (out, jt, tl);
							memcpy (out + tl, j + 2, len - 2);
							data = out;
							dsz = tl + len - 2;
						}
					}
					else
					{
						data = j;
						dsz = len;
					}
				}
			}
			else if ((t == 20 || t == 36) && l > 8)
			{
				const uint id = d[0] | d[1] << 8, fmt = d[2], w = d[3] | d[4] << 8, h = d[5] | d[6] << 8;
				size_t hp = 7;
				uint nc = 0;
				if (fmt == 3 && l > 8)
					nc = d[hp++] + 1;
				if (w && h && w <= 4096 && h <= 4096 && (fmt == 3 || fmt == 4 || fmt == 5))
				{
					uint osz = 0;
					size_t used = 0;
					u8 *px = fst_inflate (d + hp, l - hp, &osz, &used);
					const uint es = t == 36 ? 4 : 3;
					const size_t stride = fmt == 3 ? (w + 3) & ~3u : fmt == 4 ? (size_t)((w * 2 + 3) & ~3u) : (size_t)w * 4;
					const size_t need = (fmt == 3 ? (size_t)nc * es : 0) + stride * h;
					u8 *rgba = px && osz >= need ? MALLOC ((size_t)w * h * 4) : 0;
					if (rgba)
					{
						const u8 *tab = px, *rows = px + (fmt == 3 ? (size_t)nc * es : 0);
						for (uint y = 0; y < h; y++)
							for (uint x = 0; x < w; x++)
							{
								u8 *o = rgba + ((size_t)y * w + x) * 4;
								const u8 *r = rows + stride * y;
								if (fmt == 3)
								{
									const uint ix = r[x];
									if (ix < nc)
									{
										o[0] = tab[ix * es];
										o[1] = tab[ix * es + 1];
										o[2] = tab[ix * es + 2];
										o[3] = es == 4 ? tab[ix * es + 3] : 255;
									}
									else
										o[0] = o[1] = o[2] = o[3] = 0;
								}
								else if (fmt == 4)
								{
									const uint c = r[2 * x] << 8 | r[2 * x + 1];
									o[0] = ((c >> 10) & 31) * 255 / 31;
									o[1] = ((c >> 5) & 31) * 255 / 31;
									o[2] = (c & 31) * 255 / 31;
									o[3] = 255;
								}
								else
								{
									const u8 *q = r + 4 * x;
									const uint a = t == 36 ? q[0] : 255;
									o[3] = a;
									for (uint k = 0; k < 3; k++)
										o[k] = (t == 36 && a) ? (q[1 + k] * 255 / a > 255 ? 255 : q[1 + k] * 255 / a) : q[1 + k];
								}
							}
						snprintf (name, sizeof (name), "image_%u.png", id);
						if (!testmode)
						{
							char path[PATH_MAX];
							snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
							CreatePath (dest, true);
							if (!SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
								n++;
						}
						else
						{
							FREE (rgba);
							n++;
						}
						name[0] = 0;
					}
					FREE (px);
				}
			}
			if (name[0] && data)
			{
				if (n == 0 && (verbose >= 0 || testmode))
					fprintf (stdlog, "%s%sEXTRACT SWF:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
				if (!testmode)
				{
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
					File_t F;
					enumError e2 = CreateFileOpt (&F, true, path, false, arg);
					if (F.f && fwrite (data, 1, dsz, F.f) != dsz)
						e2 = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)dsz, path);
					ResetFile (&F, opt_preserve);
					if (!e2)
						n++;
				}
				else
					n++;
			}
			FREE (out);
			p += l;
		}
		if (n)
			err = ERR_OK;
	}
done:
	FREE (jt);
	FREE (own);
	FREE (raw);
	return err;
}
