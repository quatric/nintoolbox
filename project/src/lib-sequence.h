#ifndef SZS_LIB_SEQUENCE_H
#define SZS_LIB_SEQUENCE_H 1

#include "lib-std.h"
#include "file-type.h"

// Sequence opcodes (NintendoWare / GotaSequenceLib bytecode)
enum
{
	SEQ_OP_WAIT = 0x80, // [VLQ ticks]
	SEQ_OP_PRG = 0x81, // [VLQ program]
	SEQ_OP_OPEN_TRACK = 0x88, // [u8 track_no, u24 offset]
	SEQ_OP_JUMP = 0x89, // [u24 offset]
	SEQ_OP_CALL = 0x8A, // [u24 offset]
	SEQ_OP_RANDOM = 0xA0, // [s16 min, s16 max]
	SEQ_OP_VARIABLE = 0xA1, // [u8 var_id, s16 val]
	SEQ_OP_IF = 0xA2, // [u8 cond]
	SEQ_OP_TIME = 0xA3, // [u8 time]
	SEQ_OP_TIME_RANDOM = 0xA4, // [s16 min, s16 max]
	SEQ_OP_TIME_VARIABLE = 0xA5, // [u8 var_id]
	SEQ_OP_TIMEBASE = 0xB0, // [u8/u16 timebase]
	SEQ_OP_ENV_HOLD = 0xB1, // [u8 hold]
	SEQ_OP_MONOPHONIC = 0xB2, // [u8 mono]
	SEQ_OP_VELOCITY_RANGE = 0xB3, // [u8 range]
	SEQ_OP_PAN = 0xC0, // [u8 pan]
	SEQ_OP_VOLUME = 0xC1, // [u8 vol]
	SEQ_OP_MAIN_VOLUME = 0xC2, // [u8 master_vol]
	SEQ_OP_TRANSPOSE = 0xC3, // [s8 transpose]
	SEQ_OP_PITCH_BEND = 0xC4, // [s8 bend]
	SEQ_OP_BEND_RANGE = 0xC5, // [u8 range]
	SEQ_OP_PRIO = 0xC6, // [u8 prio]
	SEQ_OP_NOTE_WAIT = 0xC7, // [u8 flag]
	SEQ_OP_TIE = 0xC8, // [u8 flag]
	SEQ_OP_PORTA = 0xC9, // [u8 key]
	SEQ_OP_MOD_DEPTH = 0xCA, // [u8 depth]
	SEQ_OP_MOD_SPEED = 0xCB, // [u8 speed]
	SEQ_OP_MOD_TYPE = 0xCC, // [u8 type]
	SEQ_OP_MOD_RANGE = 0xCD, // [u8 range]
	SEQ_OP_PORTA_SW = 0xCE, // [u8 on_off]
	SEQ_OP_PORTA_TIME = 0xCF, // [u8 time]
	SEQ_OP_ATTACK = 0xD0, // [u8 attack]
	SEQ_OP_DECAY = 0xD1, // [u8 decay]
	SEQ_OP_SUSTAIN = 0xD2, // [u8 sustain]
	SEQ_OP_RELEASE = 0xD3, // [u8 release]
	SEQ_OP_LOOP_START = 0xD4, // [u8 count]
	SEQ_OP_EXPRESSION = 0xD5, // [u8 expr / vol2]
	SEQ_OP_PRINTVAR = 0xD6, // [u8 var_id]
	SEQ_OP_SURROUND_PAN = 0xD7, // [u8 pan]
	SEQ_OP_LPF_CUTOFF = 0xD8, // [u8 cutoff]
	SEQ_OP_FXSEND_A = 0xD9, // [u8 reverb / send_a]
	SEQ_OP_FXSEND_B = 0xDA, // [u8 send_b]
	SEQ_OP_MAINSEND = 0xDB, // [u8 main_send]
	SEQ_OP_INIT_PAN = 0xDC, // [u8 init_pan]
	SEQ_OP_MUTE = 0xDD, // [u8 mute]
	SEQ_OP_FXSEND_C = 0xDE, // [u8 send_c]
	SEQ_OP_DAMPER = 0xDF, // [u8 damper / sustain_pedal]
	SEQ_OP_MOD_DELAY = 0xE0, // [u16 delay]
	SEQ_OP_TEMPO = 0xE1, // [u16 tempo]
	SEQ_OP_SWEEP_PITCH = 0xE3, // [s16 sweep]
	SEQ_OP_EX_COMMAND = 0xF0, // [extended]
	SEQ_OP_LOOP_END = 0xFC, // []
	SEQ_OP_RET = 0xFD, // []
	SEQ_OP_ALLOC_TRACK = 0xFE, // [u16 track_bitmask]
	SEQ_OP_FIN = 0xFF, // []
};

typedef enum seq_format_t
{
	SEQ_FMT_UNKNOWN = 0,
	SEQ_FMT_RSEQ, // Wii (Revolution Sequence) - Big Endian
	SEQ_FMT_CSEQ, // 3DS (CTR Sequence) - Little Endian
	SEQ_FMT_FSEQ_BE, // Wii U (Format Sequence) - Big Endian
	SEQ_FMT_FSEQ_LE, // Switch (Format Sequence) - Little Endian
	SEQ_FMT_SSEQ, // NDS (Nitro Sequence) - Little Endian
	SEQ_FMT_BMS, // GameCube / Wii (JAudio Binary Music Sequence) - Big Endian
} seq_format_t;

// Disassemble a sequence (RSEQ/CSEQ/FSEQ/SSEQ) to human-readable MML text.
enumError DisassembleSequence (char **out_text, size_t *out_size, const u8 *data, size_t size);

// Assemble human-readable MML text to binary sequence (RSEQ/CSEQ/FSEQ/SSEQ).
enumError AssembleSequence (
	u8 **out_data, size_t *out_size, const char *text, seq_format_t target_fmt);

// Convert a binary sequence (RSEQ/CSEQ/FSEQ/SSEQ) to a Standard MIDI File (.mid).
enumError SequenceToMIDI (u8 **out_midi, size_t *out_size, const u8 *seq_data, size_t seq_size);

// Convert a Standard MIDI File (.mid) to a binary sequence (RSEQ/CSEQ/FSEQ/SSEQ).
enumError SequenceFromMIDI (
	u8 **out_seq, size_t *out_size, const u8 *midi_data, size_t midi_size, seq_format_t target_fmt);

// Invert notes in a binary sequence around center note.
enumError InvertSequence (
	u8 **out_data, size_t *out_size, const u8 *seq_data, size_t seq_size, int center_note);

// Detect sequence format from header or signature
seq_format_t DetectSequenceFormat (const u8 *data, size_t size);

// Helper to convert format string to enum
seq_format_t ParseSequenceFormatName (const char *name);
const char *GetSequenceFormatName (seq_format_t fmt);

#endif // SZS_LIB_SEQUENCE_H
