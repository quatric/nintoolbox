// Built-in MIDI + SF2 -> WAV renderer based on TinySoundFont / TinyMidiLoader (MIT).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "lib-midirender.h"

#define TSF_IMPLEMENTATION
#include "../third_party/tinysoundfont/tsf.h"
#define TML_IMPLEMENTATION
#include "../third_party/tinysoundfont/tml.h"

// ---- options (set by wszst --midi-preset= / --midi-reverb=) ----
const char *g_midi_preset_spec = 0; // "ch=program[:bank],..." channels 1..16, e.g. "4=7,10=0:128"
const char *g_midi_mute_spec = 0; // "4,5,6" channels 1..16 to silence (alternate arrangements the game mutes at runtime)
int g_midi_declick_ms = 12; // minimum amp-envelope release in ms (attack gets a fifth of it); 0 = use the SoundFont values as they are
int g_midi_reverb = 0; // 0..100 wet level of the built-in reverb (0 = off)

// ---- Freeverb (public domain design by Jezar), stereo ----
typedef struct { float *buf; int n, i; float store; } fv_comb_t;
typedef struct { float *buf; int n, i; } fv_ap_t;
typedef struct { fv_comb_t comb[8]; fv_ap_t ap[4]; } fv_chan_t;
static const int fv_comb_len[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int fv_ap_len[4] = { 556, 441, 341, 225 };

static void fv_init (fv_chan_t *c, int spread, unsigned rate)
{
	memset (c, 0, sizeof (*c));
	for (int k = 0; k < 8; k++)
	{
		c->comb[k].n = (int)((fv_comb_len[k] + spread) * (double)rate / 44100.0);
		c->comb[k].buf = calloc (c->comb[k].n, sizeof (float));
	}
	for (int k = 0; k < 4; k++)
	{
		c->ap[k].n = (int)((fv_ap_len[k] + spread) * (double)rate / 44100.0);
		c->ap[k].buf = calloc (c->ap[k].n, sizeof (float));
	}
}
static void fv_free (fv_chan_t *c)
{
	for (int k = 0; k < 8; k++) free (c->comb[k].buf);
	for (int k = 0; k < 4; k++) free (c->ap[k].buf);
}
static float fv_process (fv_chan_t *c, float in)
{
	const float feedback = 0.84f, damp = 0.2f;
	float out = 0;
	const float x = in * 0.015f;
	for (int k = 0; k < 8; k++)
	{
		fv_comb_t *q = c->comb + k;
		const float o = q->buf[q->i];
		q->store = o * (1 - damp) + q->store * damp;
		q->buf[q->i] = x + q->store * feedback;
		if (++q->i >= q->n) q->i = 0;
		out += o;
	}
	for (int k = 0; k < 4; k++)
	{
		fv_ap_t *q = c->ap + k;
		const float b = q->buf[q->i];
		q->buf[q->i] = out + b * 0.5f;
		out = b - out;
		if (++q->i >= q->n) q->i = 0;
	}
	return out;
}

// "ch=program[:bank]" list -> override[ch] = program | bank << 8 (or -1)
static void parse_presets (const char *spec, int *ov)
{
	for (int c = 0; c < 16; c++) ov[c] = -1;
	while (spec && *spec)
	{
		int ch = 0, prog = 0, bank = 0, n = 0;
		if (sscanf (spec, "%d=%d:%d%n", &ch, &prog, &bank, &n) < 3)
		{
			bank = 0;
			if (sscanf (spec, "%d=%d%n", &ch, &prog, &n) < 2)
				break;
		}
		if (ch >= 1 && ch <= 16)
			ov[ch - 1] = prog | bank << 8;
		spec += n;
		while (*spec == ',' || *spec == ' ' || *spec == ';') spec++;
	}
}

static void put32 (FILE *f, uint32_t v) { fputc (v, f); fputc (v >> 8, f); fputc (v >> 16, f); fputc (v >> 24, f); }
static void put16 (FILE *f, uint32_t v) { fputc (v, f); fputc (v >> 8, f); }

int RenderMIDIToWAV (const char *mid_path, const char *sf2_path, const char *wav_path, unsigned rate)
{
	if (!rate)
		rate = 44100;
	tml_message *midi = tml_load_filename (mid_path);
	if (!midi)
		return 1;
	tsf *sf = tsf_load_filename (sf2_path);
	if (!sf)
	{
		tml_free (midi);
		return 1;
	}
	tsf_set_output (sf, TSF_STEREO_INTERLEAVED, rate, 0.0f);
	if (g_midi_declick_ms > 0)
	{
		// very short attack/release times cut the waveform abruptly (audible as clicks/pops); keep a small floor
		const float rel = g_midi_declick_ms / 1000.0f, att = rel / 5.0f;
		for (int pi = 0; pi < sf->presetNum; pi++)
			for (int ri = 0; ri < sf->presets[pi].regionNum; ri++)
			{
				struct tsf_region *rg = sf->presets[pi].regions + ri;
				if (rg->ampenv.release < rel)
					rg->ampenv.release = rel;
				if (rg->ampenv.attack < att)
					rg->ampenv.attack = att;
			}
	}

	bool muted[16] = { false };
	for (const char *q = g_midi_mute_spec; q && *q;)
	{
		int ch = 0, n = 0;
		if (sscanf (q, "%d%n", &ch, &n) < 1)
			break;
		if (ch >= 1 && ch <= 16)
			muted[ch - 1] = true;
		q += n;
		while (*q == ',' || *q == ' ' || *q == ';') q++;
	}
	int ov[16];
	parse_presets (g_midi_preset_spec, ov);
	for (int c = 0; c < 16; c++)
		if (ov[c] >= 0)
			tsf_channel_set_bank_preset (sf, c, ov[c] >> 8, ov[c] & 255);

	unsigned len_ms = 0;
	tml_get_info (midi, 0, 0, 0, 0, &len_ms);

	// the whole mix is kept as float so it can be scaled down instead of clipping, then written as 16 bit
	enum { BLOCK = 512 };
	size_t cap = (size_t)rate * 2 * 60, used = 0;
	float *mix = malloc (cap * sizeof (float));
	if (!mix)
	{
		tsf_close (sf);
		tml_free (midi);
		return 2;
	}
	float blk[BLOCK * 2];
	double ms = 0;
	const double ms_per_block = BLOCK * 1000.0 / rate;
	const double tail_ms = 3000; // let the last notes ring out
	tml_message *m = midi;
	bool silent_tail = false;
	while (!silent_tail)
	{
		for (ms += ms_per_block; m && m->time < ms; m = m->next)
		{
			switch (m->type)
			{
			case TML_PROGRAM_CHANGE:
				if (m->channel < 16 && ov[m->channel] >= 0)
					break; // forced preset for this channel
				tsf_channel_set_presetnumber (sf, m->channel, m->program, 0);
				break;
			case TML_NOTE_ON:
				if (m->channel < 16 && muted[m->channel])
					break;
				tsf_channel_note_on (sf, m->channel, m->key, m->velocity / 127.0f);
				break;
			case TML_NOTE_OFF:
				tsf_channel_note_off (sf, m->channel, m->key);
				break;
			case TML_PITCH_BEND:
				tsf_channel_set_pitchwheel (sf, m->channel, m->pitch_bend);
				break;
			case TML_CONTROL_CHANGE:
				tsf_channel_midi_control (sf, m->channel, m->control, m->control_value);
				break;
			}
		}
		tsf_render_float (sf, blk, BLOCK, 0);
		if (used + BLOCK * 2 > cap)
		{
			cap *= 2;
			float *n2 = realloc (mix, cap * sizeof (float));
			if (!n2)
				break;
			mix = n2;
		}
		memcpy (mix + used, blk, sizeof (blk));
		used += BLOCK * 2;
		if (ms > len_ms + tail_ms || (!m && tsf_active_voice_count (sf) == 0))
			silent_tail = true;
		if (used > (size_t)rate * 2 * 60 * 30) // 30 minute safety cap
			break;
	}
	tsf_close (sf);
	tml_free (midi);

	const size_t frames = used / 2;
	if (g_midi_reverb > 0)
	{
		const float wet = g_midi_reverb / 100.0f * 3.0f;
		fv_chan_t l, r;
		fv_init (&l, 0, rate);
		fv_init (&r, 23, rate);
		for (size_t i = 0; i < frames; i++)
		{
			const float mono = (mix[2 * i] + mix[2 * i + 1]) * 0.5f;
			mix[2 * i] += wet * fv_process (&l, mono);
			mix[2 * i + 1] += wet * fv_process (&r, mono);
		}
		fv_free (&l);
		fv_free (&r);
	}
	float peak = 0;
	for (size_t i = 0; i < used; i++)
		if (fabsf (mix[i]) > peak)
			peak = fabsf (mix[i]);
	const float gain = peak > 0.98f ? 0.98f / peak : 1.0f; // only ever scales down, so quiet files keep their level

	FILE *f = fopen (wav_path, "wb");
	if (!f)
	{
		free (mix);
		return 2;
	}
	fwrite ("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32 (f, 16); put16 (f, 1); put16 (f, 2); put32 (f, rate); put32 (f, rate * 4); put16 (f, 4); put16 (f, 16);
	fwrite ("data\0\0\0\0", 1, 8, f);
	short pcm[BLOCK * 2];
	for (size_t i = 0; i < used; i += BLOCK * 2)
	{
		size_t n = used - i < BLOCK * 2 ? used - i : BLOCK * 2;
		for (size_t k = 0; k < n; k++)
		{
			float v = mix[i + k] * gain * 32767.0f;
			pcm[k] = (short)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : lrintf (v));
		}
		fwrite (pcm, 2, n, f);
	}
	const uint32_t bytes = (uint32_t)(frames * 4);
	fseek (f, 4, SEEK_SET);
	put32 (f, 36 + bytes);
	fseek (f, 40, SEEK_SET);
	put32 (f, bytes);
	// a full disk shows up only as a write error here: report it instead of leaving a truncated WAV
	const int write_failed = ferror (f);
	if (fclose (f) || write_failed)
	{
		remove (wav_path);
		free (mix);
		return 3;
	}
	free (mix);
	return 0;
}
