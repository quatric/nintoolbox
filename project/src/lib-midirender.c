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

	unsigned len_ms = 0;
	tml_get_info (midi, 0, 0, 0, 0, &len_ms);

	FILE *f = fopen (wav_path, "wb");
	if (!f)
	{
		tsf_close (sf);
		tml_free (midi);
		return 2;
	}
	// header patched at the end
	fwrite ("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32 (f, 16); put16 (f, 1); put16 (f, 2); put32 (f, rate); put32 (f, rate * 4); put16 (f, 4); put16 (f, 16);
	fwrite ("data\0\0\0\0", 1, 8, f);

	enum { BLOCK = 512 };
	short pcm[BLOCK * 2];
	double ms = 0;
	const double ms_per_block = BLOCK * 1000.0 / rate;
	const double tail_ms = 3000; // let the last notes ring out
	tml_message *m = midi;
	uint32_t frames = 0;
	bool silent_tail = false;
	while (!silent_tail)
	{
		for (ms += ms_per_block; m && m->time < ms; m = m->next)
		{
			switch (m->type)
			{
			case TML_PROGRAM_CHANGE:
				tsf_channel_set_presetnumber (sf, m->channel, m->program, 0);
				break;
			case TML_NOTE_ON:
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
		tsf_render_short (sf, pcm, BLOCK, 0);
		fwrite (pcm, 4, BLOCK, f);
		frames += BLOCK;
		if (ms > len_ms + tail_ms || (!m && tsf_active_voice_count (sf) == 0))
			silent_tail = true;
		if (frames > rate * 60u * 30u) // 30 minute safety cap
			break;
	}
	const uint32_t bytes = frames * 4;
	fseek (f, 4, SEEK_SET);
	put32 (f, 36 + bytes);
	fseek (f, 40, SEEK_SET);
	put32 (f, bytes);
	fclose (f);
	tsf_close (sf);
	tml_free (midi);
	return 0;
}
