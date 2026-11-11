// XAR archives (".xar", ".pkg" installers, ".safariextz"): "xar!" header, zlib compressed XML table of contents, then the heap. Every <file> with a <data> element is written to
// "<file>.d/<path>"; heap data is stored raw ("application/octet-stream") or as zlib / gzip ("application/x-gzip", "application/zlib"); other encodings (bzip2, lzma) are
// skipped. Directories nest their children in the XML.
typedef struct xar_file_t { char name[256]; char type[16]; u64 off, len, size; char enc[64]; bool has_data; } xar_file_t;

static enumError extract_xar_file (ccp arg, ccp basedir, uint depth)
{
	if ((!is_ext (arg, ".xar") && !is_ext (arg, ".pkg")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 28 && !memcmp (raw, "xar!", 4))
	{
		const uint hs = raw[4] << 8 | raw[5];
		u64 clen = 0, ulen = 0;
		for (uint i = 0; i < 8; i++) { clen = clen << 8 | raw[8 + i]; ulen = ulen << 8 | raw[16 + i]; }
		if (hs >= 28 && hs + clen <= sz && ulen < 0x10000000 && clen)
		{
			char *xml = MALLOC (ulen + 1);
			uLongf xl = ulen;
			if (xml && uncompress ((Bytef *)xml, &xl, raw + hs, clen) == Z_OK)
			{
				xml[xl] = 0;
				const size_t heap = hs + clen;
				char dest[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				xar_file_t stack[32];
				char pathstack[32][256];
				int sp = -1;
				uint count = 0;
				err = ERR_OK;
				for (char *p = xml; *p && err == ERR_OK;)
				{
					if (*p != '<') { p++; continue; }
					char *e = strchr (p, '>');
					if (!e) break;
					*e = 0;
					const char *tag = p + 1;
					char *next = e + 1;
					// element text up to the next '<'
					char *te = strchr (next, '<');
					if (te) *te = 0;
					const char *text = next;
					if (!strncmp (tag, "file ", 5) || !strcmp (tag, "file"))
					{
						if (sp < 31)
						{
							sp++;
							memset (&stack[sp], 0, sizeof (stack[sp]));
						}
					}
					else if (sp >= 0 && !strcmp (tag, "name")) snprintf (stack[sp].name, sizeof (stack[sp].name), "%s", text);
					else if (sp >= 0 && !strcmp (tag, "type")) snprintf (stack[sp].type, sizeof (stack[sp].type), "%s", text);
					else if (sp >= 0 && !strcmp (tag, "data")) stack[sp].has_data = true;
					else if (sp >= 0 && stack[sp].has_data && !strcmp (tag, "offset")) stack[sp].off = strtoull (text, 0, 10);
					else if (sp >= 0 && stack[sp].has_data && !strcmp (tag, "length")) stack[sp].len = strtoull (text, 0, 10);
					else if (sp >= 0 && stack[sp].has_data && !strcmp (tag, "size")) stack[sp].size = strtoull (text, 0, 10);
					else if (sp >= 0 && stack[sp].has_data && !strncmp (tag, "encoding", 8))
					{
						const char *st = strstr (tag, "style=\"");
						if (st) { st += 7; size_t l = strcspn (st, "\""); snprintf (stack[sp].enc, sizeof (stack[sp].enc), "%.*s", (int)(l < 63 ? l : 63), st); }
					}
					else if (sp >= 0 && !strcmp (tag, "/data"))
					{
						// emit now: name, type and data are known by the end of <data> in the files produced by xar / libarchive
						xar_file_t *f = &stack[sp];
						if (!strcmp (f->type, "file") && f->name[0])
						{
							char path[PATH_MAX] = "";
							for (int i = 0; i < sp; i++)
							{
								strncat (path, stack[i].name, sizeof (path) - strlen (path) - 2);
								strcat (path, "/");
							}
							strncat (path, f->name, sizeof (path) - strlen (path) - 1);
							if (unixarch_safe (path) && heap + f->off + f->len <= sz)
							{
								const u8 *src = raw + heap + f->off;
								u8 *out = 0;
								size_t on = 0;
								bool freeout = false;
								if (!strcmp (f->enc, "application/octet-stream") || !f->enc[0]) { out = (u8 *)src; on = f->len; }
								else if (!strcmp (f->enc, "application/x-gzip") || !strcmp (f->enc, "application/zlib"))
								{
									out = MALLOC (f->size ? f->size : 1);
									uLongf ol = f->size;
									z_stream zs;
									memset (&zs, 0, sizeof (zs));
									if (out && inflateInit2 (&zs, 15 + 32) == Z_OK)
									{
										zs.next_in = (Bytef *)src; zs.avail_in = f->len; zs.next_out = out; zs.avail_out = ol;
										const int r = inflate (&zs, Z_FINISH);
										on = zs.total_out;
										inflateEnd (&zs);
										freeout = true;
										if (r != Z_STREAM_END && !(r == Z_OK && on == f->size)) { FREE (out); out = 0; freeout = false; }
									}
									else { FREE (out); out = 0; }
								}
								if (out)
								{
									if (verbose >= 0 || testmode)
										fprintf (stdlog, "%s%sEXTRACT XAR:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", path, on);
									err = shade_write (arg, dest, basedir, path, out, (uint)on);
									count++;
									if (freeout) FREE (out);
								}
								else
									fprintf (stdlog, "XAR: skipping %s (encoding %s)\n", path, f->enc);
							}
						}
						f->has_data = false;
					}
					else if (sp >= 0 && !strcmp (tag, "/file"))
						sp--;
					p = te ? te : next + strlen (next);
					if (te) *te = '<';
				}
				(void)pathstack;
				if (!count && err == ERR_OK)
					err = ERR_NOTHING_TO_DO;
			}
			FREE (xml);
		}
	}
	FREE (raw);
	return err;
}
