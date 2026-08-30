#include "lib-rbnk.h"
#include <string.h>

static u32 rd_u32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static u16 rd_u16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}
static float rd_f32 (const u8 *p)
{
	u32 bits = rd_u32 (p);
	float f;
	memcpy (&f, &bits, 4);
	return f;
}

// -----------------------------------------------------------------------------
///////////////		    ruint helpers			///////////////
// -----------------------------------------------------------------------------
// A 'ruint' (NW4R's own name) is an 8 byte tagged reference: refType(u8),
// dataType(u8), reserved(u16BE), dataOffset(s32BE). See lib-rbnk.h's header
// comment: every ruint's dataOffset in the Data section, at ANY nesting
// depth, is relative to the same top-of-section RuintList base -- ported
// from BrawlLib's RBNKDataGroupNode/RangeTable/IndexTable Initialize() call
// chain, not obvious from the raw struct layout alone.

typedef struct rbnk_ctx_t
{
	const u8 *data;
	uint size;
	const u8 *base; // top of the Data section's RuintList (list_base)
	uint depth;
	bool error;
} rbnk_ctx_t;

static bool in_bounds (const rbnk_ctx_t *ctx, const u8 *p, uint len)
{
	return p >= ctx->data && (uint)(p - ctx->data) + len <= ctx->size;
}

static rbnk_entry_type_t ruint_type (const u8 *r)
{
	return (rbnk_entry_type_t)r[1];
}
static s32 ruint_offset (const u8 *r)
{
	return (s32)rd_u32 (r + 4);
}

static void parse_entry (rbnk_ctx_t *ctx, const u8 *addr, rbnk_entry_type_t type, rbnk_node_t *out);

// RangeTable: tableCount(u8) + tableCount key bytes, aligned to 4, then a
// tableCount-entry ruint array (the "RuintCollection").
static void parse_range_table (rbnk_ctx_t *ctx, const u8 *addr, rbnk_node_t *out)
{
	if (ctx->error || !in_bounds (ctx, addr, 1))
	{
		ctx->error = true;
		return;
	}
	uint n = addr[0];
	const u8 *keys = addr + 1;
	const u8 *coll = addr + ((1 + n + 3) & ~3u);
	if (!in_bounds (ctx, keys, n) || !in_bounds (ctx, coll, 8u * n))
	{
		ctx->error = true;
		return;
	}

	out->type = RBNK_ENTRY_RANGE;
	out->n_child = n;
	out->keys = n ? MALLOC (n) : 0;
	out->child = n ? CALLOC (n, sizeof (*out->child)) : 0;
	memcpy (out->keys, keys, n);

	for (uint i = 0; i < n && !ctx->error; i++)
	{
		const u8 *r = coll + 8 * i;
		rbnk_entry_type_t t = ruint_type (r);
		if (t == RBNK_ENTRY_INST || t == RBNK_ENTRY_RANGE || t == RBNK_ENTRY_INDEX)
		{
			const u8 *child_addr = ctx->base + ruint_offset (r);
			parse_entry (ctx, child_addr, t, out->child + i);
		}
		else
			out->child[i].type = t == RBNK_ENTRY_NULL ? RBNK_ENTRY_NULL : RBNK_ENTRY_INVALID;
	}
}

// IndexTable: min(u8) + max(u8) + reserved(u16) + a dense (max-min+1)-entry
// ruint array, indexed directly by (note - min).
static void parse_index_table (rbnk_ctx_t *ctx, const u8 *addr, rbnk_node_t *out)
{
	if (ctx->error || !in_bounds (ctx, addr, 4))
	{
		ctx->error = true;
		return;
	}
	u8 kmin = addr[0], kmax = addr[1];
	if (kmax < kmin)
	{
		ctx->error = true;
		return;
	}
	uint n = (uint)kmax - kmin + 1;
	const u8 *coll = addr + 4;
	if (!in_bounds (ctx, coll, 8u * n))
	{
		ctx->error = true;
		return;
	}

	out->type = RBNK_ENTRY_INDEX;
	out->key_min = kmin;
	out->n_child = n;
	out->keys = 0;
	out->child = CALLOC (n, sizeof (*out->child));

	for (uint i = 0; i < n && !ctx->error; i++)
	{
		const u8 *r = coll + 8 * i;
		rbnk_entry_type_t t = ruint_type (r);
		if (t == RBNK_ENTRY_INST || t == RBNK_ENTRY_RANGE || t == RBNK_ENTRY_INDEX)
		{
			const u8 *child_addr = ctx->base + ruint_offset (r);
			parse_entry (ctx, child_addr, t, out->child + i);
		}
		else
			out->child[i].type = t == RBNK_ENTRY_NULL ? RBNK_ENTRY_NULL : RBNK_ENTRY_INVALID;
	}
}

static void parse_inst (rbnk_ctx_t *ctx, const u8 *addr, rbnk_node_t *out)
{
	if (ctx->error || !in_bounds (ctx, addr, 0x30))
	{
		ctx->error = true;
		return;
	}
	out->type = RBNK_ENTRY_INST;
	rbnk_inst_t *p = &out->inst;
	p->wave_index = rd_u32 (addr);
	p->attack = addr[4];
	p->decay = addr[5];
	p->sustain = addr[6];
	p->release = addr[7];
	p->hold = addr[8];
	p->wave_data_location_type = addr[9];
	p->note_off_type = addr[10];
	p->alternate_assign = addr[11];
	p->original_key = addr[12];
	p->volume = addr[13];
	p->pan = addr[14];
	p->surround_pan = addr[15];
	p->pitch = rd_f32 (addr + 16);
}

static void parse_entry (rbnk_ctx_t *ctx, const u8 *addr, rbnk_entry_type_t type, rbnk_node_t *out)
{
	memset (out, 0, sizeof (*out));
	if (ctx->error || ++ctx->depth > 16)
	{
		ctx->error = true;
		--ctx->depth;
		return;
	}

	switch (type)
	{
		case RBNK_ENTRY_INST:
			parse_inst (ctx, addr, out);
			break;
		case RBNK_ENTRY_RANGE:
			parse_range_table (ctx, addr, out);
			break;
		case RBNK_ENTRY_INDEX:
			parse_index_table (ctx, addr, out);
			break;
		default:
			out->type = type;
			break;
	}
	--ctx->depth;
}

static void reset_node (rbnk_node_t *n)
{
	if (!n)
		return;
	for (uint i = 0; i < n->n_child; i++)
		reset_node (n->child + i);
	FREE (n->keys);
	FREE (n->child);
	memset (n, 0, sizeof (*n));
}

// -----------------------------------------------------------------------------
///////////////		    top-level scan			///////////////
// -----------------------------------------------------------------------------

enumError ScanRBNK (rbnk_t *rbnk, const u8 *data, uint size)
{
	memset (rbnk, 0, sizeof (*rbnk));

	if (size < 0x20 || memcmp (data, "RBNK", 4))
		return ERROR0 (ERR_INVALID_DATA, "ScanRBNK: not a RBNK file\n");

	u16 version = rd_u16 (data + 6);
	rbnk->version_major = version >> 8;
	rbnk->version_minor = version & 0xff;

	u32 data_off = rd_u32 (data + 0x10);
	u32 wave_off = rd_u32 (data + 0x18);

	if ((u64)data_off + 12 > size || memcmp (data + data_off, "DATA", 4))
		return ERROR0 (ERR_INVALID_DATA, "ScanRBNK: missing DATA chunk\n");

	const u8 *dlist = data + data_off + 8; // RuintList: numEntries(4) + ruint[]
	u32 n_program = rd_u32 (dlist);
	if ((u64)(dlist - data) + 4 + 8ull * n_program > size)
		return ERROR0 (ERR_INVALID_DATA,
			"ScanRBNK: Data program list exceeds buffer (%u entries)\n", n_program);

	rbnk_ctx_t ctx = { .data = data, .size = size, .base = dlist };

	rbnk->n_program = n_program;
	rbnk->program = n_program ? CALLOC (n_program, sizeof (*rbnk->program)) : 0;

	const u8 *entries = dlist + 4;
	for (u32 i = 0; i < n_program && !ctx.error; i++)
	{
		const u8 *r = entries + 8 * i;
		rbnk_entry_type_t t = ruint_type (r);
		if (t == RBNK_ENTRY_INST || t == RBNK_ENTRY_RANGE || t == RBNK_ENTRY_INDEX)
		{
			const u8 *addr = ctx.base + ruint_offset (r);
			parse_entry (&ctx, addr, t, rbnk->program + i);
		}
		else
			rbnk->program[i].type = t == RBNK_ENTRY_NULL ? RBNK_ENTRY_NULL : RBNK_ENTRY_INVALID;
	}

	if (ctx.error)
	{
		ResetRBNK (rbnk);
		return ERROR0 (ERR_INVALID_DATA,
			"ScanRBNK: malformed Data section (offset out of range or nesting too deep)\n");
	}

	// Wave section: only the version < 2 direct-WaveInfo-list layout is
	// handled (see lib-rbnk.h); version >= 2 banks reference an embedded
	// RWAR archive instead, a different, unverified-against-a-real-sample
	// structure this doesn't guess at.
	if (rbnk->version_minor < 2 && wave_off)
	{
		if ((u64)wave_off + 12 > size || memcmp (data + wave_off, "WAVE", 4))
			return ERROR0 (ERR_INVALID_DATA, "ScanRBNK: missing WAVE chunk\n");

		const u8 *wlist = data + wave_off + 8;
		u32 n_wave = rd_u32 (wlist);
		if ((u64)(wlist - data) + 4 + 8ull * n_wave > size)
			return ERROR0 (
				ERR_INVALID_DATA, "ScanRBNK: Wave list exceeds buffer (%u entries)\n", n_wave);

		rbnk->n_wave = n_wave;
		rbnk->wave = n_wave ? CALLOC (n_wave, sizeof (*rbnk->wave)) : 0;

		const u8 *wentries = wlist + 4;
		for (u32 i = 0; i < n_wave; i++)
		{
			const u8 *r = wentries + 8 * i;
			const u8 *wi = wlist + ruint_offset (r);
			if ((u64)(wi - data) + 0x1C > size)
			{
				ResetRBNK (rbnk);
				return ERROR0 (ERR_INVALID_DATA, "ScanRBNK: WaveInfo %u out of range\n", i);
			}

			u8 encoding = wi[0];
			rbnk->wave[i].encoding = encoding;
			rbnk->wave[i].looped = wi[1];
			rbnk->wave[i].channels = wi[2];
			rbnk->wave[i].sample_rate = rd_u16 (wi + 4);
			s32 loop_start_raw = (s32)rd_u32 (wi + 8);
			s32 nibbles = (s32)rd_u32 (wi + 0xC);
			rbnk->wave[i].n_samples = encoding == 2
				? ((s64)nibbles / 16 * 14 + (nibbles % 16 ? (nibbles % 16 - 2) : 0))
				: nibbles;
			rbnk->wave[i].loop_start = encoding == 2
				? ((s64)loop_start_raw / 16 * 14
					  + (loop_start_raw % 16 ? (loop_start_raw % 16 - 2) : 0))
				: loop_start_raw;
		}
	}

	return ERR_OK;
}

void ResetRBNK (rbnk_t *rbnk)
{
	if (!rbnk)
		return;
	for (uint i = 0; i < rbnk->n_program; i++)
		reset_node (rbnk->program + i);
	FREE (rbnk->program);
	FREE (rbnk->wave);
	memset (rbnk, 0, sizeof (*rbnk));
}

// -----------------------------------------------------------------------------
///////////////		    XML dump				///////////////
// -----------------------------------------------------------------------------

static void dump_node (const rbnk_node_t *n, FILE *f, int indent)
{
	switch (n->type)
	{
		case RBNK_ENTRY_INST:
			fprintf (f,
				"%*s<inst wave-index=\"%u\" attack=\"%u\" decay=\"%u\" sustain=\"%u\""
				" release=\"%u\" hold=\"%u\" note-off=\"%u\" alt-assign=\"%u\""
				" original-key=\"%u\" volume=\"%u\" pan=\"%u\" surround-pan=\"%u\" "
				"pitch=\"%.6f\"/>\n",
				indent, "", n->inst.wave_index, n->inst.attack, n->inst.decay, n->inst.sustain,
				n->inst.release, n->inst.hold, n->inst.note_off_type, n->inst.alternate_assign,
				n->inst.original_key, n->inst.volume, n->inst.pan, n->inst.surround_pan,
				n->inst.pitch);
			break;

		case RBNK_ENTRY_RANGE:
			fprintf (f, "%*s<range-table n=\"%u\">\n", indent, "", n->n_child);
			for (uint i = 0; i < n->n_child; i++)
			{
				fprintf (f, "%*s<entry key=\"%u\">\n", indent + 2, "", n->keys[i]);
				dump_node (n->child + i, f, indent + 4);
				fprintf (f, "%*s</entry>\n", indent + 2, "");
			}
			fprintf (f, "%*s</range-table>\n", indent, "");
			break;

		case RBNK_ENTRY_INDEX:
			fprintf (f, "%*s<index-table min=\"%u\" max=\"%u\">\n", indent, "", n->key_min,
				n->key_min + (n->n_child ? n->n_child - 1 : 0));
			for (uint i = 0; i < n->n_child; i++)
			{
				fprintf (f, "%*s<entry key=\"%u\">\n", indent + 2, "", n->key_min + i);
				dump_node (n->child + i, f, indent + 4);
				fprintf (f, "%*s</entry>\n", indent + 2, "");
			}
			fprintf (f, "%*s</index-table>\n", indent, "");
			break;

		case RBNK_ENTRY_NULL:
			fprintf (f, "%*s<null/>\n", indent, "");
			break;

		default:
			fprintf (f, "%*s<invalid/>\n", indent, "");
			break;
	}
}

enumError DumpRBNK_XML (const rbnk_t *rbnk, FILE *f, ccp source_name)
{
	fprintf (f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf (f, "<rbnk source=\"%s\" version=\"%u.%u\" n-program=\"%u\" n-wave=\"%u\">\n",
		source_name ? source_name : "", rbnk->version_major, rbnk->version_minor, rbnk->n_program,
		rbnk->n_wave);

	fprintf (f, "  <programs>\n");
	for (uint i = 0; i < rbnk->n_program; i++)
	{
		if (rbnk->program[i].type == RBNK_ENTRY_INVALID)
			continue;
		fprintf (f, "    <program index=\"%u\">\n", i);
		dump_node (rbnk->program + i, f, 6);
		fprintf (f, "    </program>\n");
	}
	fprintf (f, "  </programs>\n");

	if (rbnk->wave)
	{
		fprintf (f, "  <waves>\n");
		for (uint i = 0; i < rbnk->n_wave; i++)
		{
			const rbnk_wave_t *w = rbnk->wave + i;
			ccp enc = w->encoding == 2 ? "ADPCM_THP" : w->encoding == 1 ? "PCM16" : "PCM8";
			fprintf (f,
				"    <wave index=\"%u\" encoding=\"%s\" channels=\"%u\" sample-rate=\"%u\""
				" samples=\"%lld\" loop=\"%s\"",
				i, enc, w->channels, w->sample_rate, (long long)w->n_samples,
				w->looped ? "yes" : "no");
			if (w->looped)
				fprintf (f, " loop-start=\"%lld\"", (long long)w->loop_start);
			fprintf (f, "/>\n");
		}
		fprintf (f, "  </waves>\n");
	}

	fprintf (f, "</rbnk>\n");
	return ERR_OK;
}

// -----------------------------------------------------------------------------
///////////////		    XML parser & Binary encoder		///////////////
// -----------------------------------------------------------------------------

static const char *xml_attr (const char *tag, const char *attr, char *buf, size_t buf_size)
{
	size_t alen = strlen (attr);
	const char *p = tag;
	while (*p && *p != '>')
	{
		if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n') && !strncmp (p, attr, alen)
			&& p[alen] == '=')
		{
			const char *val_start = p + alen + 1;
			char quote = *val_start;
			if (quote == '"' || quote == '\'')
			{
				val_start++;
				const char *val_end = strchr (val_start, quote);
				if (val_end)
				{
					size_t len = (size_t)(val_end - val_start);
					if (len >= buf_size)
						len = buf_size - 1;
					memcpy (buf, val_start, len);
					buf[len] = 0;
					return buf;
				}
			}
		}
		p++;
	}
	return NULL;
}

static const char *parse_node_xml (const char *p, rbnk_node_t *node)
{
	char val[64];
	while (p && *p)
	{
		p = strchr (p, '<');
		if (!p)
			break;
		if (!strncmp (p, "<!--", 4))
		{
			const char *e = strstr (p, "-->");
			p = e ? e + 3 : NULL;
			continue;
		}
		if (!strncmp (p, "</", 2))
			return p;

		if (!strncmp (p, "<inst", 5))
		{
			node->type = RBNK_ENTRY_INST;
			rbnk_inst_t *inst = &node->inst;
			memset (inst, 0, sizeof (*inst));
			if (xml_attr (p, "wave-index", val, sizeof (val)))
				inst->wave_index = (u32)strtoul (val, NULL, 0);
			if (xml_attr (p, "attack", val, sizeof (val)))
				inst->attack = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "decay", val, sizeof (val)))
				inst->decay = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "sustain", val, sizeof (val)))
				inst->sustain = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "release", val, sizeof (val)))
				inst->release = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "hold", val, sizeof (val)))
				inst->hold = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "note-off", val, sizeof (val)))
				inst->note_off_type = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "alt-assign", val, sizeof (val)))
				inst->alternate_assign = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "original-key", val, sizeof (val)))
				inst->original_key = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "volume", val, sizeof (val)))
				inst->volume = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "pan", val, sizeof (val)))
				inst->pan = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "surround-pan", val, sizeof (val)))
				inst->surround_pan = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "pitch", val, sizeof (val)))
				inst->pitch = (float)strtod (val, NULL);
			const char *end = strchr (p, '>');
			return end ? end + 1 : NULL;
		}
		else if (!strncmp (p, "<null", 5))
		{
			node->type = RBNK_ENTRY_NULL;
			const char *end = strchr (p, '>');
			return end ? end + 1 : NULL;
		}
		else if (!strncmp (p, "<range-table", 12))
		{
			node->type = RBNK_ENTRY_RANGE;
			uint n = 0;
			if (xml_attr (p, "n", val, sizeof (val)))
				n = (uint)strtoul (val, NULL, 0);
			node->n_child = n;
			node->keys = n ? MALLOC (n) : NULL;
			node->child = n ? CALLOC (n, sizeof (rbnk_node_t)) : NULL;
			const char *cur = strchr (p, '>');
			if (cur)
				cur++;
			uint idx = 0;
			while (cur && *cur && idx < n)
			{
				cur = strchr (cur, '<');
				if (!cur)
					break;
				if (!strncmp (cur, "</range-table>", 14))
				{
					cur += 14;
					break;
				}
				if (!strncmp (cur, "<entry", 6))
				{
					if (xml_attr (cur, "key", val, sizeof (val)))
						node->keys[idx] = (u8)strtoul (val, NULL, 0);
					const char *ent_close = strchr (cur, '>');
					if (ent_close)
					{
						cur = parse_node_xml (ent_close + 1, node->child + idx);
						if (cur)
						{
							const char *ec = strstr (cur, "</entry>");
							if (ec)
								cur = ec + 8;
						}
					}
					idx++;
				}
				else
					cur++;
			}
			return cur;
		}
		else if (!strncmp (p, "<index-table", 12))
		{
			node->type = RBNK_ENTRY_INDEX;
			u8 kmin = 0, kmax = 0;
			if (xml_attr (p, "min", val, sizeof (val)))
				kmin = (u8)strtoul (val, NULL, 0);
			if (xml_attr (p, "max", val, sizeof (val)))
				kmax = (u8)strtoul (val, NULL, 0);
			uint n = kmax >= kmin ? (uint)(kmax - kmin + 1) : 0;
			node->key_min = kmin;
			node->n_child = n;
			node->child = n ? CALLOC (n, sizeof (rbnk_node_t)) : NULL;
			const char *cur = strchr (p, '>');
			if (cur)
				cur++;
			uint idx = 0;
			while (cur && *cur && idx < n)
			{
				cur = strchr (cur, '<');
				if (!cur)
					break;
				if (!strncmp (cur, "</index-table>", 14))
				{
					cur += 14;
					break;
				}
				if (!strncmp (cur, "<entry", 6))
				{
					const char *ent_close = strchr (cur, '>');
					if (ent_close)
					{
						cur = parse_node_xml (ent_close + 1, node->child + idx);
						if (cur)
						{
							const char *ec = strstr (cur, "</entry>");
							if (ec)
								cur = ec + 8;
						}
					}
					idx++;
				}
				else
					cur++;
			}
			return cur;
		}
		p++;
	}
	return p;
}

enumError ParseRBNK_XML (rbnk_t *rbnk, const char *xml_str, size_t xml_len)
{
	if (!rbnk || !xml_str || !xml_len)
		return ERROR0 (ERR_INVALID_DATA, "ParseRBNK_XML: empty input\n");

	memset (rbnk, 0, sizeof (*rbnk));
	char val[128];

	const char *rbnk_tag = strstr (xml_str, "<rbnk");
	if (!rbnk_tag)
		return ERROR0 (ERR_INVALID_DATA, "ParseRBNK_XML: missing <rbnk> tag\n");

	if (xml_attr (rbnk_tag, "version", val, sizeof (val)))
	{
		unsigned vmaj = 1, vmin = 0;
		if (sscanf (val, "%u.%u", &vmaj, &vmin) >= 1)
		{
			rbnk->version_major = (u16)vmaj;
			rbnk->version_minor = (u16)vmin;
		}
	}
	else
	{
		rbnk->version_major = 1;
		rbnk->version_minor = 1;
	}

	uint n_prog = 0;
	if (xml_attr (rbnk_tag, "n-program", val, sizeof (val)))
		n_prog = (uint)strtoul (val, NULL, 0);

	uint n_wv = 0;
	if (xml_attr (rbnk_tag, "n-wave", val, sizeof (val)))
		n_wv = (uint)strtoul (val, NULL, 0);

	rbnk->n_program = n_prog;
	rbnk->program = n_prog ? CALLOC (n_prog, sizeof (rbnk_node_t)) : NULL;

	const char *progs_tag = strstr (rbnk_tag, "<programs>");
	if (progs_tag)
	{
		const char *cur = progs_tag + 10;
		while (cur && *cur)
		{
			cur = strchr (cur, '<');
			if (!cur)
				break;
			if (!strncmp (cur, "</programs>", 11))
				break;
			if (!strncmp (cur, "<program", 8))
			{
				uint pidx = 0;
				if (xml_attr (cur, "index", val, sizeof (val)))
					pidx = (uint)strtoul (val, NULL, 0);
				const char *pclose = strchr (cur, '>');
				if (pclose && pidx < n_prog)
				{
					cur = parse_node_xml (pclose + 1, rbnk->program + pidx);
					if (cur)
					{
						const char *ep = strstr (cur, "</program>");
						if (ep)
							cur = ep + 10;
					}
				}
				else
					cur++;
			}
			else
				cur++;
		}
	}

	const char *waves_tag = strstr (rbnk_tag, "<waves>");
	if (waves_tag && n_wv > 0)
	{
		rbnk->n_wave = n_wv;
		rbnk->wave = CALLOC (n_wv, sizeof (rbnk_wave_t));
		const char *cur = waves_tag + 7;
		while (cur && *cur)
		{
			cur = strchr (cur, '<');
			if (!cur)
				break;
			if (!strncmp (cur, "</waves>", 8))
				break;
			if (!strncmp (cur, "<wave", 5))
			{
				uint widx = 0;
				if (xml_attr (cur, "index", val, sizeof (val)))
					widx = (uint)strtoul (val, NULL, 0);
				if (widx < n_wv)
				{
					rbnk_wave_t *w = rbnk->wave + widx;
					if (xml_attr (cur, "encoding", val, sizeof (val)))
					{
						if (!strcmp (val, "ADPCM_THP") || !strcmp (val, "2"))
							w->encoding = 2;
						else if (!strcmp (val, "PCM16") || !strcmp (val, "1"))
							w->encoding = 1;
						else
							w->encoding = 0;
					}
					if (xml_attr (cur, "channels", val, sizeof (val)))
						w->channels = (u8)strtoul (val, NULL, 0);
					if (xml_attr (cur, "sample-rate", val, sizeof (val)))
						w->sample_rate = (u16)strtoul (val, NULL, 0);
					if (xml_attr (cur, "samples", val, sizeof (val)))
						w->n_samples = (s64)strtoll (val, NULL, 0);
					if (xml_attr (cur, "loop", val, sizeof (val)))
						w->looped = (!strcmp (val, "yes") || !strcmp (val, "1")) ? 1 : 0;
					if (xml_attr (cur, "loop-start", val, sizeof (val)))
						w->loop_start = (s64)strtoll (val, NULL, 0);
				}
				const char *end = strchr (cur, '>');
				cur = end ? end + 1 : cur + 1;
			}
			else
				cur++;
		}
	}

	return ERR_OK;
}

typedef struct rbnk_bld_t
{
	u8 *buf;
	size_t size;
	size_t cap;
} rbnk_bld_t;

static void bld_u8 (rbnk_bld_t *bld, u8 v)
{
	if (bld->size + 1 > bld->cap)
	{
		bld->cap = bld->cap ? bld->cap * 2 : 1024;
		bld->buf = REALLOC (bld->buf, bld->cap);
	}
	bld->buf[bld->size++] = v;
}

static void bld_u16 (rbnk_bld_t *bld, u16 v)
{
	bld_u8 (bld, (u8)(v >> 8));
	bld_u8 (bld, (u8)v);
}

static void bld_u32 (rbnk_bld_t *bld, u32 v)
{
	bld_u8 (bld, (u8)(v >> 24));
	bld_u8 (bld, (u8)(v >> 16));
	bld_u8 (bld, (u8)(v >> 8));
	bld_u8 (bld, (u8)v);
}

static void bld_write (rbnk_bld_t *bld, const void *ptr, size_t len)
{
	if (bld->size + len > bld->cap)
	{
		while (bld->size + len > bld->cap)
			bld->cap = bld->cap ? bld->cap * 2 : 1024;
		bld->buf = REALLOC (bld->buf, bld->cap);
	}
	memcpy (bld->buf + bld->size, ptr, len);
	bld->size += len;
}

static void bld_align (rbnk_bld_t *bld, size_t align)
{
	while (bld->size % align)
		bld_u8 (bld, 0);
}

static u32 serialize_node (rbnk_bld_t *bld, u32 base_off, const rbnk_node_t *node)
{
	if (!node || node->type == RBNK_ENTRY_NULL || node->type == RBNK_ENTRY_INVALID)
		return 0;

	if (node->type == RBNK_ENTRY_INST)
	{
		bld_align (bld, 4);
		u32 off = (u32)(bld->size - base_off);
		bld_u32 (bld, node->inst.wave_index);
		bld_u8 (bld, node->inst.attack);
		bld_u8 (bld, node->inst.decay);
		bld_u8 (bld, node->inst.sustain);
		bld_u8 (bld, node->inst.release);
		bld_u8 (bld, node->inst.hold);
		bld_u8 (bld, node->inst.wave_data_location_type);
		bld_u8 (bld, node->inst.note_off_type);
		bld_u8 (bld, node->inst.alternate_assign);
		bld_u8 (bld, node->inst.original_key);
		bld_u8 (bld, node->inst.volume);
		bld_u8 (bld, node->inst.pan);
		bld_u8 (bld, node->inst.surround_pan);
		u32 pbits;
		memcpy (&pbits, &node->inst.pitch, 4);
		bld_u32 (bld, pbits);
		for (int i = 0; i < 28; i++)
			bld_u8 (bld, 0);
		return off;
	}
	else if (node->type == RBNK_ENTRY_RANGE)
	{
		bld_align (bld, 4);
		u32 off = (u32)(bld->size - base_off);
		uint n = node->n_child;
		bld_u8 (bld, (u8)n);
		if (n && node->keys)
			bld_write (bld, node->keys, n);
		bld_align (bld, 4);
		u32 coll_pos = (u32)bld->size;
		for (uint i = 0; i < n * 8; i++)
			bld_u8 (bld, 0);

		for (uint i = 0; i < n; i++)
		{
			u32 child_off = serialize_node (bld, base_off, node->child + i);
			u32 ruint_pos = coll_pos + 8 * i;
			rbnk_entry_type_t ct = node->child[i].type;
			if (ct == RBNK_ENTRY_INST || ct == RBNK_ENTRY_RANGE || ct == RBNK_ENTRY_INDEX)
			{
				bld->buf[ruint_pos] = 1;
				bld->buf[ruint_pos + 1] = (u8)ct;
				bld->buf[ruint_pos + 2] = 0;
				bld->buf[ruint_pos + 3] = 0;
				bld->buf[ruint_pos + 4] = (child_off >> 24) & 0xff;
				bld->buf[ruint_pos + 5] = (child_off >> 16) & 0xff;
				bld->buf[ruint_pos + 6] = (child_off >> 8) & 0xff;
				bld->buf[ruint_pos + 7] = child_off & 0xff;
			}
		}
		return off;
	}
	else if (node->type == RBNK_ENTRY_INDEX)
	{
		bld_align (bld, 4);
		u32 off = (u32)(bld->size - base_off);
		uint n = node->n_child;
		u8 kmin = node->key_min;
		u8 kmax = kmin + (n ? (u8)(n - 1) : 0);
		bld_u8 (bld, kmin);
		bld_u8 (bld, kmax);
		bld_u16 (bld, 0);
		u32 coll_pos = (u32)bld->size;
		for (uint i = 0; i < n * 8; i++)
			bld_u8 (bld, 0);

		for (uint i = 0; i < n; i++)
		{
			u32 child_off = serialize_node (bld, base_off, node->child + i);
			u32 ruint_pos = coll_pos + 8 * i;
			rbnk_entry_type_t ct = node->child[i].type;
			if (ct == RBNK_ENTRY_INST || ct == RBNK_ENTRY_RANGE || ct == RBNK_ENTRY_INDEX)
			{
				bld->buf[ruint_pos] = 1;
				bld->buf[ruint_pos + 1] = (u8)ct;
				bld->buf[ruint_pos + 2] = 0;
				bld->buf[ruint_pos + 3] = 0;
				bld->buf[ruint_pos + 4] = (child_off >> 24) & 0xff;
				bld->buf[ruint_pos + 5] = (child_off >> 16) & 0xff;
				bld->buf[ruint_pos + 6] = (child_off >> 8) & 0xff;
				bld->buf[ruint_pos + 7] = child_off & 0xff;
			}
		}
		return off;
	}
	return 0;
}

enumError EncodeRBNK (const rbnk_t *rbnk, u8 **out_data, uint *out_size)
{
	if (!rbnk || !out_data || !out_size)
		return ERROR0 (ERR_INVALID_DATA, "EncodeRBNK: NULL argument\n");

	rbnk_bld_t bld = { 0, 0, 0 };

	// 1. Header (0x20 bytes)
	bld_write (&bld, "RBNK", 4);
	bld_u16 (&bld, 0xFEFF); // BOM
	u16 ver = (u16)((rbnk->version_major << 8) | (rbnk->version_minor & 0xff));
	if (!ver)
		ver = 0x0101;
	bld_u16 (&bld, ver);
	bld_u32 (&bld, 0); // placeholder for file_size
	bld_u16 (&bld, 0x0020); // header_size
	bool has_wave = (rbnk->version_minor < 2 && rbnk->n_wave > 0 && rbnk->wave != NULL);
	bld_u16 (&bld, has_wave ? 2 : 1); // num_sections
	bld_u32 (&bld, 0x00000020); // data_offset
	bld_u32 (&bld, 0); // placeholder for data_size
	bld_u32 (&bld, 0); // placeholder for wave_offset
	bld_u32 (&bld, 0); // placeholder for wave_size

	// 2. DATA chunk
	u32 data_sec_start = (u32)bld.size;
	bld_write (&bld, "DATA", 4);
	bld_u32 (&bld, 0); // placeholder for data_len
	u32 dlist_base = (u32)bld.size;
	bld_u32 (&bld, rbnk->n_program);
	u32 prog_ruints = (u32)bld.size;
	for (uint i = 0; i < rbnk->n_program * 8; i++)
		bld_u8 (&bld, 0);

	for (uint i = 0; i < rbnk->n_program; i++)
	{
		const rbnk_node_t *prog = rbnk->program + i;
		if (prog->type == RBNK_ENTRY_INST || prog->type == RBNK_ENTRY_RANGE
			|| prog->type == RBNK_ENTRY_INDEX)
		{
			u32 off = serialize_node (&bld, dlist_base, prog);
			u32 rpos = prog_ruints + 8 * i;
			bld.buf[rpos] = 1;
			bld.buf[rpos + 1] = (u8)prog->type;
			bld.buf[rpos + 2] = 0;
			bld.buf[rpos + 3] = 0;
			bld.buf[rpos + 4] = (off >> 24) & 0xff;
			bld.buf[rpos + 5] = (off >> 16) & 0xff;
			bld.buf[rpos + 6] = (off >> 8) & 0xff;
			bld.buf[rpos + 7] = off & 0xff;
		}
	}
	bld_align (&bld, 32);
	u32 data_len = (u32)(bld.size - data_sec_start);
	bld.buf[data_sec_start + 4] = (data_len >> 24) & 0xff;
	bld.buf[data_sec_start + 5] = (data_len >> 16) & 0xff;
	bld.buf[data_sec_start + 6] = (data_len >> 8) & 0xff;
	bld.buf[data_sec_start + 7] = data_len & 0xff;

	bld.buf[0x14] = (data_len >> 24) & 0xff;
	bld.buf[0x15] = (data_len >> 16) & 0xff;
	bld.buf[0x16] = (data_len >> 8) & 0xff;
	bld.buf[0x17] = data_len & 0xff;

	// 3. WAVE chunk (if present)
	if (has_wave)
	{
		u32 wave_sec_start = (u32)bld.size;
		bld.buf[0x18] = (wave_sec_start >> 24) & 0xff;
		bld.buf[0x19] = (wave_sec_start >> 16) & 0xff;
		bld.buf[0x1A] = (wave_sec_start >> 8) & 0xff;
		bld.buf[0x1B] = wave_sec_start & 0xff;

		bld_write (&bld, "WAVE", 4);
		bld_u32 (&bld, 0); // placeholder for wave_len
		u32 wlist_base = (u32)bld.size;
		bld_u32 (&bld, rbnk->n_wave);
		u32 wave_ruints = (u32)bld.size;
		for (uint i = 0; i < rbnk->n_wave * 8; i++)
			bld_u8 (&bld, 0);

		for (uint i = 0; i < rbnk->n_wave; i++)
		{
			const rbnk_wave_t *w = rbnk->wave + i;
			bld_align (&bld, 4);
			u32 wi_off = (u32)(bld.size - wlist_base);
			u32 rpos = wave_ruints + 8 * i;
			bld.buf[rpos] = 1;
			bld.buf[rpos + 1] = 0;
			bld.buf[rpos + 2] = 0;
			bld.buf[rpos + 3] = 0;
			bld.buf[rpos + 4] = (wi_off >> 24) & 0xff;
			bld.buf[rpos + 5] = (wi_off >> 16) & 0xff;
			bld.buf[rpos + 6] = (wi_off >> 8) & 0xff;
			bld.buf[rpos + 7] = wi_off & 0xff;

			// WaveInfo (0x1C bytes)
			bld_u8 (&bld, w->encoding);
			bld_u8 (&bld, w->looped);
			bld_u8 (&bld, w->channels);
			bld_u8 (&bld, 0);
			bld_u16 (&bld, w->sample_rate);
			bld_u16 (&bld, 0);
			u32 raw_loop = w->encoding == 2
				? (u32)((w->loop_start / 14) * 16
					  + (w->loop_start % 14 ? (w->loop_start % 14 + 2) : 0))
				: (u32)w->loop_start;
			u32 raw_samp = w->encoding == 2
				? (u32)((w->n_samples / 14) * 16
					  + (w->n_samples % 14 ? (w->n_samples % 14 + 2) : 0))
				: (u32)w->n_samples;
			bld_u32 (&bld, raw_loop);
			bld_u32 (&bld, raw_samp);
			bld_u32 (&bld, 0x1C);
			bld_u32 (&bld, 0);
			bld_u32 (&bld, 0);
		}
		bld_align (&bld, 32);
		u32 wave_len = (u32)(bld.size - wave_sec_start);
		bld.buf[wave_sec_start + 4] = (wave_len >> 24) & 0xff;
		bld.buf[wave_sec_start + 5] = (wave_len >> 16) & 0xff;
		bld.buf[wave_sec_start + 6] = (wave_len >> 8) & 0xff;
		bld.buf[wave_sec_start + 7] = wave_len & 0xff;

		bld.buf[0x1C] = (wave_len >> 24) & 0xff;
		bld.buf[0x1D] = (wave_len >> 16) & 0xff;
		bld.buf[0x1E] = (wave_len >> 8) & 0xff;
		bld.buf[0x1F] = wave_len & 0xff;
	}

	u32 total_sz = (u32)bld.size;
	bld.buf[0x08] = (total_sz >> 24) & 0xff;
	bld.buf[0x09] = (total_sz >> 16) & 0xff;
	bld.buf[0x0A] = (total_sz >> 8) & 0xff;
	bld.buf[0x0B] = total_sz & 0xff;

	*out_data = bld.buf;
	*out_size = total_sz;
	return ERR_OK;
}
