#ifndef SZS_LIB_NWSEQ_H
#define SZS_LIB_NWSEQ_H 1

#include <stddef.h>
#include <stdint.h>

// Native NintendoWare sequence renderer.
//
// Plays an NW4R (Wii) style MML sequence -- RSEQ, and the CSEQ/FSEQ data of the 3DS/Wii U/Switch
// successors, which keep the same command set -- through a model of the NintendoWare sound engine
// and returns the audio directly, instead of going through MIDI + SoundFont:
//
//   * MML parser with variables, if/random/variable prefixes, call/loop stacks, opentrack; the
//     CSEQ/FSEQ additions (bank select, modulation curve/phase/period, extra modulators) are parsed
//   * tracks: velocity range, volume/volume2/main volume (squared), pan, init pan, bend + bend
//     range, transpose, tie, monophonic, portamento + sweep, damper, mute, alternate assign
//   * channels: NW4R envelope generator (attack table, hold, decay/release in dB*10 per ms,
//     sustain in the "decibel square" scale), LFO (pitch/volume/pan, delay, range), sweep,
//     instrument tune / original key / volume / pan, note-off-ignore
//   * voices: 3 ms audio frames, per-frame volume ramps, the engine's pan curves and pan modes,
//     stereo waves split across L/R, 4 tap interpolation, 32 kHz output (the DSP mixing rate);
//     a render that would clip is redone once with just enough headroom
//
// The format specific part (finding the instrument and wave for a program / key / velocity) is
// supplied by the caller through nws_inst_fn.

typedef struct nws_wave_t
{
	const int16_t *pcm[2]; // decoded channels (one buffer per channel)
	int channels;          // 1 or 2
	uint32_t rate;         // sample rate in Hz
	uint32_t length;       // playable samples (exclusive end; the loop end when looping)
	int loop;              // non-zero: loops back to loop_start at length
	uint32_t loop_start;   // in samples
} nws_wave_t;

typedef struct nws_inst_t
{
	const nws_wave_t *wave;
	uint8_t attack, hold, decay, sustain, release; // 0..127
	int release_ignore;    // note off type "ignore"
	uint8_t alternate_assign;
	uint8_t original_key;
	uint8_t volume;        // 0..127 (127 = unity)
	uint8_t pan;           // 0..127 (64 = centre)
	float tune;            // frequency multiplier (1.0 = none)
} nws_inst_t;

// Looks up the instrument region for a note. "bank" is the bank slot chosen by the sequence
// (bank select, 3DS/Wii U/Switch only; always 0 for RSEQ). Return 0 if the note has no region.
typedef int (*nws_inst_fn) (void *ctx, int bank, int prg, int key, int velocity, nws_inst_t *out);

typedef struct nws_params_t
{
	const uint8_t *seq;    // sequence data base (offsets in the MML are relative to this)
	size_t seq_size;
	uint32_t start;        // start offset of the sound
	uint32_t alloc_track;  // track bit mask; 0 = read an alloctrack command at "start" (default: track 0)
	float volume;          // sound volume (archive volume / 127)
	int pan_mode;          // 0 = dual, 1 = balance
	int pan_curve;         // NW pan curve id (0 = sqrt, 3 = sin/cos, 6 = linear, +1 = 0 dB, +2 = 0 dB clamp)
	nws_inst_fn inst;
	void *ctx;
	int loops;             // loop passes before fading out (looping sequences), default 2
	double fade_sec;       // fade out length, default 10
	double max_sec;        // hard limit, default 900
	double tail_sec;       // limit on the release tail once every track finished, default 15
} nws_params_t;

#define NWS_RATE 32000

// Renders to 16 bit stereo interleaved PCM at NWS_RATE. *out must be freed with NWSeqFree().
// Returns 0 on success, -1 on bad input or out of memory. *looped (may be NULL) is set if the sequence loops.
int NWSeqRender (const nws_params_t *p, int16_t **out, size_t *frames, int *looped);

void NWSeqFree (void *p);

// Writes 16 bit stereo PCM as a WAV file. Returns 0 on success.
int NWSeqWriteWav (const char *path, const int16_t *pcm, size_t frames, unsigned rate);

#endif
