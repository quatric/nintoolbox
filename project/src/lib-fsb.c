// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// FMOD FSB4 / FSB5 sound bank scanner; see lib-fsb.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-fsb.h"
#include "lib-dspadpcm.h"
#include <string.h>

#define FSB_MAX_SAMPLES 0x40000
#define FSB_MAX_CHANNELS 8

static u32 fsb_le32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u32 fsb_le16 (const u8 *p) { return p[0] | p[1] << 8; }
static u32 fsb_be16 (const u8 *p) { return p[0] << 8 | p[1]; }

typedef struct
{
	char name[64];
	uint samples, channels, freq;
	const u8 *data;
	size_t bytes;
	int codec; // 0 unknown, 1 GCADPCM, 2 PCM16 big-endian
	s16 coefs[FSB_MAX_CHANNELS][16];
	bool have_coefs;
} fsb_sample_t;

// Build a 16-bit PCM WAV; pcm is channel-interleaved. Returns an owned buffer.
static u8 *fsb_wav (const s16 *pcm, uint frames, uint channels, uint freq, size_t *out_size)
{
	const size_t bytes = (size_t)frames * channels * 2;
	u8 *w = MALLOC (44 + bytes);
	if (!w)
		return 0;
	memcpy (w, "RIFF", 4);
	const u32 riff = (u32)(36 + bytes);
	w[4] = riff, w[5] = riff >> 8, w[6] = riff >> 16, w[7] = riff >> 24;
	memcpy (w + 8, "WAVEfmt ", 8);
	const u32 fmt[] = { 16, 1 | (channels << 16), freq, freq * channels * 2 };
	for (uint i = 0; i < 4; i++)
	{
		w[16 + 4 * i] = fmt[i];
		w[17 + 4 * i] = fmt[i] >> 8;
		w[18 + 4 * i] = fmt[i] >> 16;
		w[19 + 4 * i] = fmt[i] >> 24;
	}
	w[32] = channels * 2, w[33] = 0, w[34] = 16, w[35] = 0;
	memcpy (w + 36, "data", 4);
	w[40] = bytes, w[41] = bytes >> 8, w[42] = bytes >> 16, w[43] = bytes >> 24;
	for (size_t i = 0; i < (size_t)frames * channels; i++)
	{
		w[44 + 2 * i] = pcm[i];
		w[45 + 2 * i] = pcm[i] >> 8;
	}
	*out_size = 44 + bytes;
	return w;
}

// Decode one sample to a WAV. Returns NULL when the codec is unsupported.
static u8 *fsb_decode (const fsb_sample_t *s, size_t *out_size)
{
	if (!s->samples || !s->channels || s->channels > FSB_MAX_CHANNELS || s->samples > 0x10000000u / s->channels)
		return 0;
	const uint ch = s->channels;
	s16 *pcm = CALLOC ((size_t)s->samples * ch, sizeof (s16));
	if (!pcm)
		return 0;
	bool ok = false;
	if (s->codec == 2 && s->bytes >= (size_t)s->samples * ch * 2)
	{
		for (size_t i = 0; i < (size_t)s->samples * ch; i++)
			pcm[i] = (s16)fsb_be16 (s->data + 2 * i);
		ok = true;
	}
	else if (s->codec == 1 && s->have_coefs)
	{
		ok = true;
		const size_t per_ch = s->bytes / ch;
		u8 *tmp = MALLOC (per_ch + 8);
		if (!tmp)
			ok = false;
		for (uint c = 0; ok && c < ch; c++)
		{
			// gather this channel's bytes (2-byte interleave)
			size_t n = 0;
			for (size_t i = c * 2; i + 1 < s->bytes && n + 2 <= per_ch; i += 2 * (size_t)ch, n += 2)
				tmp[n] = s->data[i], tmp[n + 1] = s->data[i + 1];
			if (ch == 1)
			{
				memcpy (tmp, s->data, per_ch);
				n = per_ch;
			}
			int h1 = 0, h2 = 0;
			for (uint f = 0, done = 0; done < s->samples && (size_t)(f + 1) * 8 <= n; f++)
			{
				s16 out[14];
				const uint cnt = s->samples - done < 14 ? s->samples - done : 14;
				DspAdpcmDecodeBlock (tmp + (size_t)f * 8, cnt, out, s->coefs[c], &h1, &h2);
				for (uint k = 0; k < cnt; k++)
					pcm[(size_t)(done + k) * ch + c] = out[k];
				done += cnt;
			}
		}
		FREE (tmp);
	}
	u8 *wav = 0;
	if (ok)
		wav = fsb_wav (pcm, s->samples, ch, s->freq ? s->freq : 22050, out_size);
	FREE (pcm);
	return wav;
}

static bool fsb_emit (nintendo_sarc_entry_t *out, uint *n, const fsb_sample_t *s, uint idx)
{
	size_t wsz = 0;
	u8 *wav = fsb_decode (s, &wsz);
	char base[96], path[128];
	if (s->name[0] && OwnedNameOk (s->name))
		snprintf (base, sizeof (base), "%s", s->name);
	else
		snprintf (base, sizeof (base), "sample%04u", idx);
	const size_t bl = strlen (base);
	if (bl > 4 && !strcasecmp (base + bl - 4, ".wav"))
		base[bl - 4] = 0;
	// Names are not unique inside a bank (and macOS is case-insensitive).
	uint dup = 0;
	for (uint j = *n; j-- > 0 && dup < 1000;)
	{
		char probe[160];
		snprintf (probe, sizeof (probe), "%s.%s", base, wav ? "wav" : "bin");
		if (!strcasecmp (out[j].name, probe))
		{
			dup = 1;
			break;
		}
	}
	if (dup)
		snprintf (path, sizeof (path), "%s_%04u.%s", base, idx, wav ? "wav" : "bin");
	else
		snprintf (path, sizeof (path), "%s.%s", base, wav ? "wav" : "bin");
	const bool ok = wav ? OwnedEntryAdd (out, *n, path, wav, (uint)wsz)
			    : OwnedEntryAdd (out, *n, path, s->data, (uint)s->bytes);
	FREE (wav);
	if (ok)
		(*n)++;
	return ok;
}

static enumError scan_fsb4 (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	const u32 n = fsb_le32 (d + 4), shd = fsb_le32 (d + 8), gmode = fsb_le32 (d + 20);
	if (!n || n > FSB_MAX_SAMPLES || 0x30ull + shd > size)
		return EINVAL;
	nintendo_sarc_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint cnt = 0;
	u64 hp = 0x30, dp = 0x30 + (u64)shd;
	fsb_sample_t first;
	u32 first_mode = 0;
	memset (&first, 0, sizeof (first));
	for (uint i = 0; i < n; i++)
	{
		const u8 *h = d + hp;
		fsb_sample_t s;
		memset (&s, 0, sizeof (s));
		u32 mode, hsz;
		if (i && (gmode & 2)) // FSOUND_FSB_SOURCE_BASICHEADERS: 8 bytes + codec data
		{
			if (hp + 8 > 0x30ull + shd)
				break;
			s = first;
			s.samples = fsb_le32 (h);
			s.bytes = fsb_le32 (h + 4);
			mode = first_mode;
			hsz = 8 + (mode & 0x02000000 ? 0x2eu * s.channels : 0);
			s.name[0] = 0;
			s.have_coefs = false;
			if (hp + hsz > 0x30ull + shd)
				break;
			if (mode & 0x02000000)
			{
				for (uint c = 0; c < s.channels && c < FSB_MAX_CHANNELS; c++)
					for (uint k = 0; k < 16; k++)
						s.coefs[c][k] = (s16)fsb_be16 (h + 8 + 0x2e * c + 2 * k);
				s.have_coefs = true;
			}
		}
		else
		{
			if (hp + 0x50 > 0x30ull + shd)
				break;
			hsz = fsb_le16 (h);
			if (hsz < 0x50 || hp + hsz > 0x30ull + shd)
				break;
			memcpy (s.name, h + 2, 30);
			s.samples = fsb_le32 (h + 32);
			s.bytes = fsb_le32 (h + 36);
			mode = fsb_le32 (h + 48);
			s.freq = fsb_le32 (h + 52);
			s.channels = fsb_le16 (h + 62);
			if (mode & 0x02000000)
			{
				if (hsz >= 0x50 + 0x2eu * s.channels && s.channels <= FSB_MAX_CHANNELS)
				{
					for (uint c = 0; c < s.channels; c++)
						for (uint k = 0; k < 16; k++)
							s.coefs[c][k] = (s16)fsb_be16 (h + 0x50 + 0x2e * c + 2 * k);
					s.have_coefs = true;
				}
			}
			if (!i)
			{
				first = s;
				first_mode = mode;
			}
		}
		if (dp + s.bytes > size)
			break;
		s.data = d + dp;
		s.codec = 0;
		if (mode & 0x02000000)
			s.codec = 1;
		else if (!(mode & 0x0dc00000) && (mode & 0x100) && s.bytes == (size_t)s.samples * s.channels * 2)
			s.codec = 2;
		if (!fsb_emit (out, &cnt, &s, i))
		{
			ResetOwnedEntries (out, cnt);
			return ERR_CANT_CREATE;
		}
		hp += hsz;
		dp += s.bytes;
	}
	if (!cnt)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = cnt;
	return ERR_OK;
}

static enumError scan_fsb5 (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	static const uint freqs[] = { 4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 };
	if (size < 0x3c || fsb_le32 (d + 4) != 1)
		return EINVAL;
	const u32 n = fsb_le32 (d + 8), shd = fsb_le32 (d + 12), nsz = fsb_le32 (d + 16), dsz = fsb_le32 (d + 20);
	const u32 codec = fsb_le32 (d + 24);
	const u64 hs = 0x3c, names = hs + shd, data = names + nsz;
	if (!n || n > FSB_MAX_SAMPLES || data + dsz > size)
		return EINVAL;
	nintendo_sarc_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint cnt = 0;
	u64 hp = hs;
	fsb_sample_t *sm = CALLOC (n, sizeof (*sm));
	if (!sm)
	{
		FREE (out);
		return ERR_CANT_CREATE;
	}
	for (uint i = 0; i < n; i++)
	{
		if (hp + 8 > names)
			break;
		const u64 v = (u64)fsb_le32 (d + hp) | (u64)fsb_le32 (d + hp + 4) << 32;
		hp += 8;
		fsb_sample_t *s = &sm[i];
		s->samples = (uint)(v >> 34);
		s->freq = ((v >> 1) & 15) < 10 ? freqs[(v >> 1) & 15] : 0;
		s->channels = ((v >> 5) & 1) + 1;
		u64 off = ((v >> 6) & 0xfffffff) * 16;
		s->bytes = off; // temporarily the offset
		bool more = v & 1;
		while (more && hp + 4 <= names)
		{
			const u32 c = fsb_le32 (d + hp);
			hp += 4;
			more = c & 1;
			const u32 csz = (c >> 1) & 0xffffff, type = c >> 25;
			if (hp + csz > names)
				break;
			if (type == 1 && csz >= 1)
				s->channels = d[hp];
			else if (type == 2 && csz >= 4)
				s->freq = fsb_le32 (d + hp);
			else if (type == 7 && s->channels <= FSB_MAX_CHANNELS && csz >= 0x2eu * s->channels)
			{
				for (uint ch = 0; ch < s->channels; ch++)
					for (uint k = 0; k < 16; k++)
						s->coefs[ch][k] = (s16)fsb_be16 (d + hp + 0x2e * ch + 2 * k);
				s->have_coefs = true;
			}
			hp += csz;
		}
	}
	// Sample byte lengths come from ascending data offsets.
	for (uint i = 0; i < n; i++)
	{
		const u64 off = sm[i].bytes;
		u64 end = dsz;
		for (uint j = 0; j < n; j++)
			if (sm[j].bytes > off && sm[j].bytes < end)
				end = sm[j].bytes;
		sm[i].data = d + data + (off < dsz ? off : dsz);
		sm[i].bytes = (off < dsz ? end : dsz) - (off < dsz ? off : dsz);
		sm[i].codec = codec == 6 ? 1 : codec == 2 ? 2 : 0;
		if (i * 4 + 4 <= nsz && nsz >= n * 4)
		{
			const u32 no = fsb_le32 (d + names + 4ull * i);
			if (no < nsz && memchr (d + names + no, 0, nsz - no))
				snprintf (sm[i].name, sizeof (sm[i].name), "%s", (ccp)d + names + no);
		}
		if (!fsb_emit (out, &cnt, &sm[i], i))
		{
			ResetOwnedEntries (out, cnt);
			FREE (sm);
			return ERR_CANT_CREATE;
		}
	}
	FREE (sm);
	if (!cnt)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = cnt;
	return ERR_OK;
}

enumError ScanFSB (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x30)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;
	if (!memcmp (data, "FSB4", 4))
		return scan_fsb4 (entries, n_entries, data, size);
	if (!memcmp (data, "FSB5", 4))
		return scan_fsb5 (entries, n_entries, data, size);
	return EINVAL;
}
