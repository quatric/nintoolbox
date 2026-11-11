#ifndef SZS_LIB_MIDIRENDER_H
#define SZS_LIB_MIDIRENDER_H 1

// Built-in MIDI + SoundFont -> WAV renderer (vendored TinySoundFont/TinyMidiLoader, MIT;
// see third_party/tinysoundfont). No external fluidsynth needed. 16 bit stereo PCM.
// Returns 0 on success, non-zero on unreadable input or output. Deliberately free of
// lib-std.h (which redefines malloc/free).
// Options: preset override list "ch=program[:bank],..." (channels 1-16) and built-in reverb wet level 0..100.
extern const char *g_midi_preset_spec;
extern const char *g_midi_mute_spec;
extern int g_midi_reverb;
int RenderMIDIToWAV (const char *mid_path, const char *sf2_path, const char *wav_path, unsigned sample_rate);

#endif
