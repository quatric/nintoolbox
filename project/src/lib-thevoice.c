// SPDX-License-Identifier: GPL-2.0+
// See lib-thevoice.h for format documentation.

#include "lib-thevoice.h"
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// small local helpers -- these formats are plain text, so detection/parsing
// is done with ordinary string scanning rather than binary field reads.

// True if 'needle' occurs within the first 'limit' bytes of 'data' (or the
// whole buffer, whichever is smaller). Used only for cheap header sniffing.
static int has_token (const u8 *data, size_t size, size_t limit, const char *needle)
{
	if (limit > size)
		limit = size;
	if (!limit)
		return 0;

	// bounded, NUL-safe substring search (data is not guaranteed to be
	// NUL-terminated)
	const size_t nlen = strlen (needle);
	if (nlen > limit)
		return 0;
	for (size_t i = 0; i + nlen <= limit; i++)
		if (!memcmp (data + i, needle, nlen))
			return 1;
	return 0;
}

// crude "looks like text" gate shared by all five formats: reject files
// that are mostly binary noise before even looking for a keyword, so a
// random binary blob doesn't get misdetected just because a keyword
// substring happens to occur in it.
static int looks_like_text (const u8 *data, size_t size)
{
	if (!size)
		return 0;
	size_t check = size < 512 ? size : 512;
	size_t bad = 0;
	for (size_t i = 0; i < check; i++)
	{
		u8 c = data[i];
		if (c == '\t' || c == '\r' || c == '\n')
			continue;
		if (c < 0x20 || c == 0x7f)
			bad++;
	}
	return bad * 20 < check; // allow a little slack, but not much
}

//-----------------------------------------------------------------------------
// detection

int IsVoiceSong (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!looks_like_text (data, size))
		return 0;
	// root element + namespace, both required, order-independent within
	// the header region
	return has_token (data, size, 512, "<Song ")
		&& has_token (data, size, 512, "zoe:Song");
}

int IsVoiceAmc (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!looks_like_text (data, size))
		return 0;
	return has_token (data, size, 256, "animeshcatalogue");
}

int IsVoiceAms (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!looks_like_text (data, size))
		return 0;
	return has_token (data, size, 256, "animeshsequence");
}

int IsVoicePalcat (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!looks_like_text (data, size))
		return 0;
	return has_token (data, size, 256, "palettecatalogue");
}

int IsVoicePalseq (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!looks_like_text (data, size))
		return 0;
	return has_token (data, size, 256, "palettesequence");
}

//-----------------------------------------------------------------------------
// tiny attribute-value extraction helpers used by the parsers below.
// These operate on a NUL-terminated copy of the source text (made once per
// decode call) so ordinary C string functions can be used safely.

// Finds 'key=' (XML key="..." or Lua key=...) starting the search at
// *pos, and returns a pointer to the first byte after the opening quote
// (XML) or '=' (Lua), or NULL if not found before 'end'. On success,
// *pos is advanced to just past the key match (caller re-scans for the
// value terminator itself, since XML uses '"' and Lua uses one of
// [;,}\n]).
static const char *find_key (const char *text, const char *end, const char *key)
{
	size_t klen = strlen (key);
	const char *p = text;
	while (p && p < end)
	{
		const char *hit = strstr (p, key);
		if (!hit || hit >= end)
			return 0;
		// require the char right before the key to be a non-identifier
		// char (so "FileName" doesn't match inside some longer key)
		if (hit == text || !(isalnum ((unsigned char) hit[-1]) || hit[-1] == '_'))
			return hit + klen;
		p = hit + klen;
	}
	return 0;
}

// XML-style: key="value" -- 'after_key' points just past 'key='; expects a
// '"' next. Returns a malloc'd NUL-terminated copy, or NULL.
static char *read_xml_value (const char *after_key, const char *end)
{
	const char *p = after_key;
	while (p < end && *p != '"' && *p != 0)
		p++;
	if (p >= end || *p != '"')
		return 0;
	p++;
	const char *start = p;
	while (p < end && *p != '"' && *p != 0)
		p++;
	if (p >= end)
		return 0;
	size_t len = p - start;
	char *out = MALLOC (len + 1);
	if (!out)
		return 0;
	memcpy (out, start, len);
	out[len] = 0;
	return out;
}

// Lua-style: key=value terminated by one of [;,}\n]. 'after_key' points
// just past 'key='. Trims surrounding quotes if present (for string
// values). Returns a malloc'd NUL-terminated copy, or NULL.
static char *read_lua_value (const char *after_key, const char *end)
{
	const char *p = after_key;
	while (p < end && (*p == ' ' || *p == '\t'))
		p++;
	const char *start = p;
	int quoted = (p < end && *p == '"');
	if (quoted)
	{
		p++;
		start = p;
		while (p < end && *p != '"' && *p != 0)
			p++;
		if (p >= end)
			return 0;
		size_t len = p - start;
		char *out = MALLOC (len + 1);
		if (!out)
			return 0;
		memcpy (out, start, len);
		out[len] = 0;
		return out;
	}

	while (p < end && *p != ';' && *p != ',' && *p != '}' && *p != '\n' && *p != 0)
		p++;
	size_t len = p - start;
	while (len && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\r'))
		len--;
	char *out = MALLOC (len + 1);
	if (!out)
		return 0;
	memcpy (out, start, len);
	out[len] = 0;
	return out;
}

// Loads the whole buffer into a NUL-terminated heap string so strstr()
// etc. can be used; caller must FREE() the result.
static char *dup_text (const u8 *data, size_t size)
{
	char *out = MALLOC (size + 1);
	if (!out)
		return 0;
	memcpy (out, data, size);
	out[size] = 0;
	return out;
}

//-----------------------------------------------------------------------------
// (1) ".song" -- XML

enumError DecodeVoiceSong_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!f)
		return EINVAL;
	if (!IsVoiceSong (data, size, size))
		return EINVAL;

	char *text = dup_text (data, size);
	if (!text)
		return ERR_OUT_OF_MEMORY;
	const char *end = text + size;

	fprintf (f, "# The Voice: song script (.song)\n");

	{
		const char *k = find_key (text, end, "<Song ");
		const char *sk = k ? find_key (k, end, "start=") : 0;
		const char *ek = k ? find_key (k, end, "end=") : 0;
		char *start_v = sk ? read_xml_value (sk, end) : 0;
		char *end_v   = ek ? read_xml_value (ek, end) : 0;
		fprintf (f, "start = %s\n", start_v ? start_v : "?");
		fprintf (f, "end   = %s\n", end_v ? end_v : "?");
		if (start_v) FREE (start_v);
		if (end_v) FREE (end_v);
	}

	fprintf (f, "\n# game modes\n");
	{
		const char *p = text;
		for (;;)
		{
			const char *mk = find_key (p, end, "<Mode ");
			if (!mk)
				break;
			const char *idk = find_key (mk, end, "id=");
			char *id = idk ? read_xml_value (idk, end) : 0;
			const char *mick = find_key (mk, end, "mics=");
			char *mics = mick ? read_xml_value (mick, end) : 0;
			fprintf (f, "mode %-14s mics=%s\n", id ? id : "?", mics ? mics : "?");

			// list <Player> entries up to the next </Mode> (approximated
			// by the next <Mode or </SongConfig, whichever comes first)
			const char *scope_end = find_key (mk, end, "<Mode ");
			const char *sc_end2 = find_key (mk, end, "</SongConfig");
			if (!scope_end || (sc_end2 && sc_end2 < scope_end))
				scope_end = sc_end2 ? sc_end2 : end;
			const char *q = mk;
			for (;;)
			{
				const char *pk = find_key (q, scope_end, "<Player ");
				if (!pk)
					break;
				const char *partk = find_key (pk, scope_end, "part=");
				char *part = partk ? read_xml_value (partk, scope_end) : 0;
				fprintf (f, "  player part=%s\n", part ? part : "?");
				if (part) FREE (part);
				q = pk;
			}

			if (id) FREE (id);
			if (mics) FREE (mics);
			p = mk;
		}
	}

	fprintf (f, "\n# performance events (file order; %-*s)\n", 0, "start,name,enabled");
	{
		uint count = 0;
		const char *p = text;
		for (;;)
		{
			const char *evk = find_key (p, end, "<Event ");
			if (!evk)
				break;
			const char *stk = find_key (evk, end, "start=");
			const char *nak = find_key (evk, end, "name=");
			const char *enk = find_key (evk, end, "enabled=");
			char *st = stk ? read_xml_value (stk, end) : 0;
			char *na = nak ? read_xml_value (nak, end) : 0;
			char *en = enk ? read_xml_value (enk, end) : 0;
			fprintf (f, "event start=%-10s name=%-24s enabled=%s\n",
				st ? st : "?", na ? na : "?", en ? en : "?");
			if (st) FREE (st);
			if (na) FREE (na);
			if (en) FREE (en);
			count++;
			p = evk;
		}
		fprintf (f, "# %u event(s)\n", count);
	}

	FREE (text);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2)/(3) ".amc" / ".ams" -- Lua-like entry lists

enumError DecodeVoiceAmc_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!f)
		return EINVAL;
	if (!IsVoiceAmc (data, size, size))
		return EINVAL;

	char *text = dup_text (data, size);
	const char *end = text + size;

	fprintf (f, "# The Voice: animesh catalogue (.amc)\n");
	fprintf (f, "# referenced .tas animated-mesh scenes, in file order\n\n");

	uint count = 0;
	const char *p = text;
	for (;;)
	{
		const char *k = find_key (p, end, "animesh_scene=");
		if (!k)
			break;
		char *v = read_lua_value (k, end);
		fprintf (f, "scene = %s\n", v ? v : "?");
		if (v) FREE (v);
		count++;
		p = k;
	}
	fprintf (f, "\n# %u scene(s)\n", count);

	FREE (text);
	return ERR_OK;
}

enumError DecodeVoiceAms_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!f)
		return EINVAL;
	if (!IsVoiceAms (data, size, size))
		return EINVAL;

	char *text = dup_text (data, size);
	const char *end = text + size;

	fprintf (f, "# The Voice: animesh sequence (.ams)\n");
	fprintf (f, "# timed .tas placements, in file order\n");
	fprintf (f, "# StartTime  FileName  PlayMode  PlayRate  FadeIn  FadeOut\n\n");

	// each entry is a single '{' ... '}' block; scan block-by-block so
	// fields belonging to different entries can't be cross-matched
	uint count = 0;
	const char *p = text;
	for (;;)
	{
		const char *open = strchr (p, '{');
		if (!open || open >= end)
			break;
		const char *close = strchr (open, '}');
		if (!close || close >= end)
			close = end;

		const char *k;
		char *st   = (k = find_key (open, close, "StartTime=")) ? read_lua_value (k, close) : 0;
		char *fn   = (k = find_key (open, close, "FileName=")) ? read_lua_value (k, close) : 0;
		char *pm   = (k = find_key (open, close, "PlayMode=")) ? read_lua_value (k, close) : 0;
		char *pr   = (k = find_key (open, close, "PlayRate=")) ? read_lua_value (k, close) : 0;
		char *fi   = (k = find_key (open, close, "FadeIn=")) ? read_lua_value (k, close) : 0;
		char *fo   = (k = find_key (open, close, "FadeOut=")) ? read_lua_value (k, close) : 0;

		if (fn) // only count/print real entries (skips a stray '{' before the list)
		{
			fprintf (f, "start=%-10s file=%-40s mode=%-3s rate=%-10s in=%-10s out=%s\n",
				st ? st : "?", fn, pm ? pm : "?", pr ? pr : "?",
				fi ? fi : "?", fo ? fo : "?");
			count++;
		}

		if (st) FREE (st);
		if (fn) FREE (fn);
		if (pm) FREE (pm);
		if (pr) FREE (pr);
		if (fi) FREE (fi);
		if (fo) FREE (fo);

		p = close < end ? close + 1 : end;
		if (p >= end)
			break;
	}
	fprintf (f, "\n# %u placement(s)\n", count);

	FREE (text);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4)/(5) ".palcat" / ".palseq" -- Lua-like entry lists

enumError DecodeVoicePalcat_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!f)
		return EINVAL;
	if (!IsVoicePalcat (data, size, size))
		return EINVAL;

	char *text = dup_text (data, size);
	const char *end = text + size;

	fprintf (f, "# The Voice: palette catalogue (.palcat)\n");
	fprintf (f, "# referenced .pal raw-RGBA palettes, in file order\n\n");

	uint count = 0;
	const char *p = text;
	for (;;)
	{
		const char *k = find_key (p, end, "palette=");
		if (!k)
			break;
		char *v = read_lua_value (k, end);
		fprintf (f, "[%u] palette = %s\n", count, v ? v : "?");
		if (v) FREE (v);
		count++;
		p = k;
	}
	fprintf (f, "\n# %u palette(s)\n", count);
	if (count != 1)
		fprintf (f, "# note: every sample on the source disc had exactly 1 entry here;\n"
			"# this file has %u -- unconfirmed territory for this format\n", count);

	FREE (text);
	return ERR_OK;
}

enumError DecodeVoicePalseq_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!f)
		return EINVAL;
	if (!IsVoicePalseq (data, size, size))
		return EINVAL;

	char *text = dup_text (data, size);
	const char *end = text + size;

	fprintf (f, "# The Voice: palette sequence (.palseq)\n");
	fprintf (f, "# timed palette-catalogue index swaps, in file order\n");
	fprintf (f, "# (PaletteIndex is presumed to index the paired .palcat by\n");
	fprintf (f, "#  matching base filename -- not cross-referenced by the format itself)\n");
	fprintf (f, "# StartTime  PaletteIndex  FadeIn  FadeOut\n\n");

	uint count = 0;
	const char *p = text;
	for (;;)
	{
		const char *open = strchr (p, '{');
		if (!open || open >= end)
			break;
		const char *close = strchr (open, '}');
		if (!close || close >= end)
			close = end;

		const char *k;
		char *st = (k = find_key (open, close, "StartTime=")) ? read_lua_value (k, close) : 0;
		char *pi = (k = find_key (open, close, "PaletteIndex=")) ? read_lua_value (k, close) : 0;
		char *fi = (k = find_key (open, close, "FadeIn=")) ? read_lua_value (k, close) : 0;
		char *fo = (k = find_key (open, close, "FadeOut=")) ? read_lua_value (k, close) : 0;

		if (pi)
		{
			fprintf (f, "start=%-10s index=%-4s in=%-10s out=%s\n",
				st ? st : "?", pi, fi ? fi : "?", fo ? fo : "?");
			count++;
		}

		if (st) FREE (st);
		if (pi) FREE (pi);
		if (fi) FREE (fi);
		if (fo) FREE (fo);

		p = close < end ? close + 1 : end;
		if (p >= end)
			break;
	}
	fprintf (f, "\n# %u swap(s)\n", count);

	FREE (text);
	return ERR_OK;
}
