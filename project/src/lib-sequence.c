#include "lib-sequence.h"
#include "midilib/midi_file.h"
#include "midilib/midi_reader.h"
#include "midilib/stream.h"
#include "midilib/buffer.h"
#include <math.h>

// Note name lookup table
static const char *const note_names[12]
	= { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static void pitch_to_name (char *buf, size_t buf_size, int pitch)
{
	if (pitch < 0 || pitch > 127)
	{
		snprintf (buf, buf_size, "%d", pitch);
		return;
	}
	int octave = (pitch / 12) - 1;
	int note_idx = pitch % 12;
	snprintf (buf, buf_size, "%s%d", note_names[note_idx], octave);
}

static int name_to_pitch (const char *s)
{
	if (!s || !*s)
		return -1;
	if (isdigit ((unsigned char)*s) || (*s == '-' && isdigit ((unsigned char)s[1])))
	{
		return atoi (s);
	}
	char note_letter = toupper ((unsigned char)s[0]);
	int note_idx = -1;
	switch (note_letter)
	{
		case 'C':
			note_idx = 0;
			break;
		case 'D':
			note_idx = 2;
			break;
		case 'E':
			note_idx = 4;
			break;
		case 'F':
			note_idx = 5;
			break;
		case 'G':
			note_idx = 7;
			break;
		case 'A':
			note_idx = 9;
			break;
		case 'B':
			note_idx = 11;
			break;
		default:
			return -1;
	}
	int pos = 1;
	if (s[pos] == '#' || s[pos] == '+')
	{
		note_idx = (note_idx + 1) % 12;
		pos++;
	}
	else if (s[pos] == 'b' || s[pos] == '-')
	{
		note_idx = (note_idx + 11) % 12;
		pos++;
	}
	int octave = atoi (s + pos);
	int pitch = (octave + 1) * 12 + note_idx;
	return (pitch >= 0 && pitch <= 127) ? pitch : -1;
}

// Endian reading helpers using dclib
#define read_be16(p) be16 (p)
#define read_be24(p) be24 (p)
#define read_be32(p) be32 (p)

#define read_le16(p) le16 (p)
#define read_le24(p) le24 (p)
#define read_le32(p) le32 (p)

static inline u32 read_vlq (const u8 *data, size_t max_len, size_t *inout_pos)
{
	u32 val = 0;
	size_t pos = *inout_pos;
	while (pos < max_len)
	{
		u8 b = data[pos++];
		val = (val << 7) | (b & 0x7F);
		if (!(b & 0x80))
			break;
	}
	*inout_pos = pos;
	return val;
}

static inline void write_vlq (u8 *buf, size_t *inout_pos, u32 val)
{
	u8 temp[5];
	int len = 0;
	temp[len++] = (u8)(val & 0x7F);
	val >>= 7;
	while (val > 0)
	{
		temp[len++] = (u8)((val & 0x7F) | 0x80);
		val >>= 7;
	}
	size_t pos = *inout_pos;
	for (int i = len - 1; i >= 0; i--)
		buf[pos++] = temp[i];
	*inout_pos = pos;
}

static inline size_t vlq_len (u32 val)
{
	size_t len = 1;
	val >>= 7;
	while (val > 0)
	{
		len++;
		val >>= 7;
	}
	return len;
}

seq_format_t DetectSequenceFormat (const u8 *data, size_t size)
{
	if (!data || size < 4)
		return SEQ_FMT_UNKNOWN;

	if (size >= 0x20 && !memcmp (data, "RSEQ", 4))
		return SEQ_FMT_RSEQ;
	if (size >= 0x20 && !memcmp (data, "CSEQ", 4))
		return SEQ_FMT_CSEQ;
	if (size >= 0x20 && !memcmp (data, "FSEQ", 4))
	{
		u16 bom = read_be16 (data + 4);
		return (bom == 0xFEFF) ? SEQ_FMT_FSEQ_BE : SEQ_FMT_FSEQ_LE;
	}
	if (size >= 0x10 && !memcmp (data, "SSEQ", 4))
		return SEQ_FMT_SSEQ;
	if (size >= 12 && !memcmp (data, "DATA", 4))
		return SEQ_FMT_RSEQ;

	// Check for raw JAudio BMS bytecode sequence (starts with standard opcodes)
	if (size >= 4
		&& (data[0] == SEQ_OP_OPEN_TRACK || data[0] == SEQ_OP_ALLOC_TRACK || data[0] == SEQ_OP_TEMPO
			|| data[0] == SEQ_OP_TIMEBASE || data[0] == SEQ_OP_PRG || data[0] == SEQ_OP_WAIT
			|| data[0] == SEQ_OP_VOLUME || data[0] < 0x80))
		return SEQ_FMT_BMS;

	return SEQ_FMT_UNKNOWN;
}

seq_format_t ParseSequenceFormatName (const char *name)
{
	if (!name)
		return SEQ_FMT_RSEQ;
	if (!strcasecmp (name, "RSEQ") || !strcasecmp (name, "BRSEQ") || !strcasecmp (name, "WII"))
		return SEQ_FMT_RSEQ;
	if (!strcasecmp (name, "CSEQ") || !strcasecmp (name, "BCSEQ") || !strcasecmp (name, "3DS")
		|| !strcasecmp (name, "CTR"))
		return SEQ_FMT_CSEQ;
	if (!strcasecmp (name, "FSEQ") || !strcasecmp (name, "BFSEQ") || !strcasecmp (name, "WIIU"))
		return SEQ_FMT_FSEQ_BE;
	if (!strcasecmp (name, "FSEQ_LE") || !strcasecmp (name, "SWITCH") || !strcasecmp (name, "NX"))
		return SEQ_FMT_FSEQ_LE;
	if (!strcasecmp (name, "SSEQ") || !strcasecmp (name, "NDS") || !strcasecmp (name, "NITRO")
		|| !strcasecmp (name, "DS"))
		return SEQ_FMT_SSEQ;
	if (!strcasecmp (name, "BMS") || !strcasecmp (name, "BMC") || !strcasecmp (name, "JAUDIO")
		|| !strcasecmp (name, "GC") || !strcasecmp (name, "GAMECUBE"))
		return SEQ_FMT_BMS;
	return SEQ_FMT_RSEQ;
}

const char *GetSequenceFormatName (seq_format_t fmt)
{
	switch (fmt)
	{
		case SEQ_FMT_RSEQ:
			return "RSEQ";
		case SEQ_FMT_CSEQ:
			return "CSEQ";
		case SEQ_FMT_FSEQ_BE:
			return "FSEQ (Wii U)";
		case SEQ_FMT_FSEQ_LE:
			return "FSEQ (Switch)";
		case SEQ_FMT_SSEQ:
			return "SSEQ";
		case SEQ_FMT_BMS:
			return "BMS (GameCube/Wii)";
		default:
			return "Unknown";
	}
}

// Disassembler implementation
typedef struct label_map_t
{
	u32 offset;
	char name[32];
} label_map_t;

static const char *find_label (const label_map_t *labels, uint n_labels, u32 off)
{
	for (uint i = 0; i < n_labels; i++)
	{
		if (labels[i].offset == off)
			return labels[i].name;
	}
	return NULL;
}

static void add_label (
	label_map_t **labels, uint *n_labels, uint *alloc_labels, u32 off, const char *prefix)
{
	if (find_label (*labels, *n_labels, off))
		return;
	if (*n_labels >= *alloc_labels)
	{
		*alloc_labels = *alloc_labels ? *alloc_labels * 2 : 64;
		*labels = REALLOC (*labels, *alloc_labels * sizeof (label_map_t));
	}
	label_map_t *l = &(*labels)[(*n_labels)++];
	l->offset = off;
	if (prefix && *prefix)
		snprintf (l->name, sizeof (l->name), "%s_%04X", prefix, off);
	else
		snprintf (l->name, sizeof (l->name), "Label_%04X", off);
}

// Add a label with an explicit real name (from a sequence LABEL block).
// The name is sanitized to identifier characters so the assembler reads
// it back; overlong names are truncated to the map's field width.
static void add_real_label (
	label_map_t **labels, uint *n_labels, uint *alloc_labels, u32 off, const char *name)
{
	if (find_label (*labels, *n_labels, off))
		return;
	if (*n_labels >= *alloc_labels)
	{
		*alloc_labels = *alloc_labels ? *alloc_labels * 2 : 64;
		*labels = REALLOC (*labels, *alloc_labels * sizeof (label_map_t));
	}
	label_map_t *l = &(*labels)[(*n_labels)++];
	l->offset = off;
	size_t j = 0;
	for (size_t i = 0; name[i] && j + 1 < sizeof (l->name); i++)
	{
		char c = name[i];
		l->name[j++] = (c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
				|| (c >= 'a' && c <= 'z'))
			? c
			: '_';
	}
	l->name[j] = 0;
	if (!j)
		snprintf (l->name, sizeof (l->name), "Label_%04X", off);
}

// Sequence container locations per the NintendoWare sound headers
// (snd_SequenceSoundFile.h: SoundFileHeader = BinaryFileHeader +
// BlockReferenceTable; DATA block 0x5000, LABEL block 0x5001).
// Resolves the DATA code span (and, when present, the LABEL block) for
// RSEQ/CSEQ/FSEQ via the block table. Returns 1 on success; 0 leaves
// everything untouched so the caller keeps its legacy fixed-offset path
// (which is what this tool's own assembler historically emits).
static int seq_find_blocks (const u8 *data, size_t size, bool is_le,
	const u8 **code_out, size_t *code_size_out, const u8 **labl_out, size_t *labl_size_out)
{
	if (size < 20 || (read_be16 (data + 4) != 0xFEFF && read_be16 (data + 4) != 0xFFFE))
		return 0;
	bool file_le = read_be16 (data + 4) == 0xFFFE;
	if (file_le != is_le)
		return 0;
	u16 nblk = file_le ? read_le16 (data + 16) : read_be16 (data + 16);
	if (!nblk || nblk > 16 || 20 + 12u * nblk > size)
		return 0;
	const u8 *code = 0;
	size_t code_size = 0;
	const u8 *labl = 0;
	size_t labl_size = 0;
	for (uint i = 0; i < nblk; i++)
	{
		size_t r = 20 + 12u * i;
		u16 type = file_le ? read_le16 (data + r) : read_be16 (data + r);
		u32 off = file_le ? read_le32 (data + r + 4) : read_be32 (data + r + 4);
		u32 sz = file_le ? read_le32 (data + r + 8) : read_be32 (data + r + 8);
		if ((u64)off + 8 > size || (u64)off + sz > size)
			return 0;
		if (memcmp (data + off, type == 0x5000 ? "DATA" : "LABL", 4))
			return 0;
		if (type == 0x5000)
		{
			code = data + off + 8;
			code_size = sz >= 8 ? sz - 8 : 0;
		}
		else if (type == 0x5001)
		{
			labl = data + off;
			labl_size = sz;
		}
		else
			return 0;
	}
	if (!code || !code_size)
		return 0;
	if (code_out)
		*code_out = code;
	if (code_size_out)
		*code_size_out = code_size;
	if (labl_out)
		*labl_out = labl;
	if (labl_size_out)
		*labl_size_out = labl_size;
	return 1;
}

// Read a sequence LABEL block's (data-offset -> name) entries into the
// label map. LabelInfo = 8-byte data reference (type 0x1F00, offset from
// the DATA body) + u32 length + NUL-padded name; entries are 8-byte
// table references. Anything malformed is skipped, never fatal.
static void seq_read_labels (const u8 *labl, size_t labl_size, bool is_le, label_map_t **labels,
	uint *n_labels, uint *alloc_labels)
{
	if (!labl || labl_size < 12 || memcmp (labl, "LABL", 4))
		return;
	u32 count = is_le ? read_le32 (labl + 8) : read_be32 (labl + 8);
	// ReferenceTable base is the address of count (labl + 8).
	for (u32 i = 0; i < count; i++)
	{
		size_t er = 12 + 8u * i;
		if (er + 8 > labl_size)
			break;
		s32 eoff = is_le ? (s32)read_le32 (labl + er + 4) : (s32)read_be32 (labl + er + 4);
		if (eoff < 0 || (u64)8 + (u64)eoff + 16 > labl_size)
			continue;
		size_t li = 8 + (size_t)eoff; // LabelInfo, table-relative
		u32 data_off = is_le ? read_le32 (labl + li + 4) : read_be32 (labl + li + 4);
		u32 len = is_le ? read_le32 (labl + li + 8) : read_be32 (labl + li + 8);
		if (len > 64 || li + 12 + len > labl_size)
			continue;
		char name[64];
		size_t k = 0;
		while (k < len && k + 1 < sizeof (name))
		{
			char c = (char)labl[li + 12 + k];
			if (!c)
				break;
			name[k++] = c;
		}
		name[k] = 0;
		if (k)
			add_real_label (labels, n_labels, alloc_labels, data_off, name);
	}
}

	// MML prefix commands per Nintendo's own MmlParser::Parse: IF (0xA2)
// wraps the next command conditionally; TIME (0xA3), TIME_RANDOM (0xA4)
// and TIME_VARIABLE (0xA5) wrap it with a trailing second parameter;
// RANDOM (0xA0) and VARIABLE (0xA1) wrap it replacing its first ReadArg
// operand (with an s16 min/max pair and a u8 variable id respectively).
// Each prefix occurs at most once, in that order. seq_consume_prefix()
// eats them and reports what was seen; the caller then reads the real
// command, applying has_random/has_variable to its first ReadArg operand
// and appending has_time's trailing operand afterwards (TIME's trailing
// parameter is only read by u8-operand commands -- matching the player's
// argType2 behavior -- notes, jumps and EX commands take none).
typedef struct
{
	bool has_if;
	bool has_time;
	u8 time_kind; // 0xA3/0xA4/0xA5
	int16_t time_a, time_b;
	u8 time_var;
	bool has_random;
	bool has_variable;
} seq_prefix_t;

static bool seq_consume_prefix (
	const u8 *code, size_t code_size, size_t *pos, seq_prefix_t *pre, u8 *op_out)
{
	memset (pre, 0, sizeof (*pre));
	for (;;)
	{
		if (*pos >= code_size)
			return false;
		u8 b = code[*pos];
		if (b == 0xA2 && !pre->has_if)
		{
			pre->has_if = true;
			(*pos)++;
		}
		else if ((b == 0xA3 || b == 0xA4 || b == 0xA5) && !pre->has_time)
		{
			pre->has_time = true;
			pre->time_kind = b;
			(*pos)++;
		}
		else if (b == 0xA0 && !pre->has_random && !pre->has_variable)
		{
			pre->has_random = true;
			(*pos)++;
		}
		else if (b == 0xA1 && !pre->has_random && !pre->has_variable)
		{
			pre->has_variable = true;
			(*pos)++;
		}
		else
			break;
	}
	if (*pos >= code_size)
		return false;
	*op_out = code[(*pos)++];
	return true;
}

// Skip an extended (0xF0) command's operands. Sub-command classes per
// MmlParser: 0x80/0x90 u8+s16, 0xA0/0xB0 u8, 0xE0 s16 (endian-aware).
static void seq_skip_ex (const u8 *code, size_t code_size, size_t *pos, bool is_le)
{
	(void)is_le; // sub-operands are raw bytes / endian u16 handled by callers
	if (*pos >= code_size)
		return;
	u8 sub = code[(*pos)++];
	u8 cls = sub & 0xF0;
	if (cls == 0x80 || cls == 0x90)
		*pos += 3;
	else if (cls == 0xA0 || cls == 0xB0)
		*pos += 1;
	else if (cls == 0xE0)
		*pos += 2;
	if (*pos > code_size)
		*pos = code_size;
}

// Mnemonics for u8-operand commands (primary names; the assembler takes
// these plus its own aliases). kind 0 = u8, 1 = s8.
static const struct
{
	u8 op;
	ccp name;
	u8 kind;
} seq_u8_mnemonics[] = {
	{ 0xB0, "timebase", 0 }, { 0xB1, "env_hold", 0 }, { 0xB2, "monophonic", 0 },
	{ 0xB3, "velocity_range", 0 }, { 0xB4, "biquad_type", 0 }, { 0xB5, "biquad_value", 0 },
	{ 0xBD, "mod_phase", 0 }, { 0xBE, "mod_curve", 0 }, { 0xBF, "front_bypass", 0 },
	{ 0xC0, "pan", 0 }, { 0xC1, "vol", 0 }, { 0xC2, "master_vol", 0 },
	{ 0xC3, "transpose", 1 }, { 0xC4, "bend", 1 }, { 0xC5, "bend_range", 0 },
	{ 0xC6, "prio", 0 }, { 0xC7, "note_wait", 0 }, { 0xC8, "tie", 0 },
	{ 0xC9, "porta", 0 }, { 0xCA, "mod_depth", 0 }, { 0xCB, "mod_speed", 0 },
	{ 0xCC, "mod_type", 0 }, { 0xCD, "mod_range", 0 }, { 0xCE, "porta_sw", 0 },
	{ 0xCF, "porta_time", 0 }, { 0xD0, "attack", 0 }, { 0xD1, "decay", 0 },
	{ 0xD2, "sustain", 0 }, { 0xD3, "release", 0 }, { 0xD4, "loop_start", 0 },
	{ 0xD5, "expr", 0 }, { 0xD6, "printvar", 0 }, { 0xD7, "surround_pan", 0 },
	{ 0xD8, "lpf_cutoff", 0 }, { 0xD9, "reverb", 0 }, { 0xDA, "fxsend_b", 0 },
	{ 0xDB, "mainsend", 0 }, { 0xDC, "init_pan", 0 }, { 0xDD, "mute", 0 },
	{ 0xDE, "fxsend_c", 0 }, { 0xDF, "damper", 0 },
};

static ccp seq_u8_name (u8 op, bool *is_s8)
{
	for (size_t i = 0; i < sizeof (seq_u8_mnemonics) / sizeof (*seq_u8_mnemonics); i++)
		if (seq_u8_mnemonics[i].op == op)
		{
			if (is_s8)
				*is_s8 = seq_u8_mnemonics[i].kind != 0;
			return seq_u8_mnemonics[i].name;
		}
	return 0;
}

// Print one prefix-wrapped command (SDK MmlParser semantics) and advance
// *pos past it. Returns printed length (0 on truncation). Covers every
// opcode class so prefixed commands never desync the walk.
static size_t print_prefixed_command (char *out, size_t out_cap, const u8 *code, size_t code_size,
	size_t *pos, bool is_le, u8 op, const seq_prefix_t *pre, const label_map_t *labels,
	uint n_labels)
{
	size_t len = 0;
	len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "    %s%s%s%s%s%s",
		pre->has_if ? "if " : "", pre->has_random ? "random " : "",
		pre->has_variable ? "variable " : "",
		pre->has_time && pre->time_kind == 0xA3 ? "time " : "",
		pre->has_time && pre->time_kind == 0xA4 ? "time_random " : "",
		pre->has_time && pre->time_kind == 0xA5 ? "time_variable " : "");
	if (op < 0x80)
	{
		u8 vel = (*pos < code_size) ? code[(*pos)++] : 100;
		char note_str[16];
		pitch_to_name (note_str, sizeof (note_str), op);
		if (pre->has_random && *pos + 4 <= code_size)
		{
			int16_t lo = is_le ? (int16_t)read_le16 (code + *pos)
							   : (int16_t)read_be16 (code + *pos);
			int16_t hi = is_le ? (int16_t)read_le16 (code + *pos + 2)
							   : (int16_t)read_be16 (code + *pos + 2);
			*pos += 4;
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0,
				"note %s %u %d %d  ; pitch=%u\n", note_str, vel, lo, hi, op);
		}
		else if (pre->has_variable && *pos < code_size)
		{
			u8 var = code[(*pos)++];
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0,
				"note %s %u %u  ; pitch=%u\n", note_str, vel, var, op);
		}
		else
		{
			u32 dur = read_vlq (code, code_size, pos);
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0,
				"note %s %u %u  ; pitch=%u\n", note_str, vel, dur, op);
		}
		return len;
	}
	if (op == SEQ_OP_WAIT || op == SEQ_OP_PRG)
	{
		ccp mn = (op == SEQ_OP_WAIT) ? "wait" : "prg";
		if (pre->has_random && *pos + 4 <= code_size)
		{
			int16_t lo = is_le ? (int16_t)read_le16 (code + *pos)
							   : (int16_t)read_be16 (code + *pos);
			int16_t hi = is_le ? (int16_t)read_le16 (code + *pos + 2)
							   : (int16_t)read_be16 (code + *pos + 2);
			*pos += 4;
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %d %d\n",
				mn, lo, hi);
		}
		else if (pre->has_variable && *pos < code_size)
		{
			u8 var = code[(*pos)++];
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %u\n", mn, var);
		}
		else
		{
			u32 v = read_vlq (code, code_size, pos);
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %u\n", mn, v);
		}
		return len;
	}
	if (op == SEQ_OP_JUMP || op == SEQ_OP_CALL || op == SEQ_OP_OPEN_TRACK)
	{
		u8 trk = 0;
		if (op == SEQ_OP_OPEN_TRACK && *pos < code_size)
			trk = code[(*pos)++];
		u32 target = 0;
		if (*pos + 3 <= code_size)
		{
			target = is_le ? read_le24 (code + *pos) : read_be24 (code + *pos);
			*pos += 3;
		}
		const char *tl = find_label (labels, n_labels, target);
		if (op == SEQ_OP_OPEN_TRACK)
		{
			if (tl)
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0,
					"open_track %u @%s\n", trk, tl);
			else
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0,
					"open_track %u 0x%06X\n", trk, target);
		}
		else if (tl)
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s @%s\n",
				op == SEQ_OP_JUMP ? "jump" : "call", tl);
		else
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s 0x%06X\n",
				op == SEQ_OP_JUMP ? "jump" : "call", target);
		return len;
	}
	if (op == SEQ_OP_EX_COMMAND && *pos < code_size)
	{
		u8 sub = code[(*pos)++];
		u8 cls = sub & 0xF0;
		if ((cls == 0x80 || cls == 0x90) && *pos + 3 <= code_size)
		{
			u8 a1 = code[*pos];
			int16_t a2 = is_le ? (int16_t)read_le16 (code + *pos + 1)
							   : (int16_t)read_be16 (code + *pos + 1);
			*pos += 3;
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "ex 0x%02X %u %d\n",
				sub, a1, a2);
		}
		else if ((cls == 0xA0 || cls == 0xB0) && *pos < code_size)
		{
			u8 a1 = code[(*pos)++];
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "ex 0x%02X %u\n",
				sub, a1);
		}
		else if (cls == 0xE0 && *pos + 2 <= code_size)
		{
			int16_t a1 = is_le ? (int16_t)read_le16 (code + *pos)
							   : (int16_t)read_be16 (code + *pos);
			*pos += 2;
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "ex 0x%02X %d\n",
				sub, a1);
		}
		else
			len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "ex 0x%02X\n", sub);
		return len;
	}
	if ((op >= 0xB0 && op <= 0xDF) || op == SEQ_OP_MOD_DELAY || op == SEQ_OP_TEMPO
		|| op == SEQ_OP_SWEEP_PITCH || op == SEQ_OP_MOD_PERIOD)
	{
		bool s8arg = (op == SEQ_OP_TRANSPOSE || op == SEQ_OP_PITCH_BEND);
		bool s16arg = (op == SEQ_OP_MOD_DELAY || op == SEQ_OP_TEMPO || op == SEQ_OP_SWEEP_PITCH
			|| op == SEQ_OP_MOD_PERIOD);
		bool named = false;
		if (op >= 0xB0 && op <= 0xDF)
		{
			bool is_s8 = false;
			ccp mn = seq_u8_name (op, &is_s8);
			if (mn)
			{
				named = true;
				s8arg = is_s8;
				if (pre->has_random && *pos + 4 <= code_size)
				{
					int16_t lo = is_le ? (int16_t)read_le16 (code + *pos)
									   : (int16_t)read_be16 (code + *pos);
					int16_t hi = is_le ? (int16_t)read_le16 (code + *pos + 2)
									   : (int16_t)read_be16 (code + *pos + 2);
					*pos += 4;
					len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %d %d",
						mn, lo, hi);
				}
				else if (pre->has_variable && *pos < code_size)
				{
					u8 var = code[(*pos)++];
					len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %u",
						mn, var);
				}
				else if (*pos < code_size)
				{
					u8 a = code[(*pos)++];
					if (s8arg)
						len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %d",
							mn, (int)(signed char)a);
					else
						len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %u",
							mn, a);
				}
			}
		}
		if (!named)
		{
			if (pre->has_random && *pos + 4 <= code_size)
			{
				int16_t lo = is_le ? (int16_t)read_le16 (code + *pos)
								   : (int16_t)read_be16 (code + *pos);
				int16_t hi = is_le ? (int16_t)read_le16 (code + *pos + 2)
								   : (int16_t)read_be16 (code + *pos + 2);
				*pos += 4;
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "raw 0x%02X %d %d",
					op, lo, hi);
			}
			else if (pre->has_variable && *pos < code_size)
			{
				u8 var = code[(*pos)++];
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "raw 0x%02X %u",
					op, var);
			}
			else if (s16arg && *pos + 2 <= code_size)
			{
				int16_t a = is_le ? (int16_t)read_le16 (code + *pos)
								  : (int16_t)read_be16 (code + *pos);
				*pos += 2;
				ccp mn = (op == SEQ_OP_TEMPO) ? "tempo"
					: (op == SEQ_OP_MOD_DELAY) ? "mod_delay"
					: (op == SEQ_OP_SWEEP_PITCH) ? "sweep_pitch"
												 : "mod_period";
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "%s %d", mn, a);
			}
			else
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "raw 0x%02X", op);
		}
		if (pre->has_time && op >= 0xB0 && op <= 0xDF)
		{
			if (pre->time_kind == 0xA3 && *pos + 2 <= code_size)
			{
				int16_t a = is_le ? (int16_t)read_le16 (code + *pos)
								  : (int16_t)read_be16 (code + *pos);
				*pos += 2;
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, " %d", a);
			}
			else if (pre->time_kind == 0xA4 && *pos + 4 <= code_size)
			{
				int16_t lo = is_le ? (int16_t)read_le16 (code + *pos)
								   : (int16_t)read_be16 (code + *pos);
				int16_t hi = is_le ? (int16_t)read_le16 (code + *pos + 2)
								   : (int16_t)read_be16 (code + *pos + 2);
				*pos += 4;
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, " %d %d", lo, hi);
			}
			else if (pre->time_kind == 0xA5 && *pos < code_size)
			{
				u8 var = code[(*pos)++];
				len += snprintf (out + len, out_cap > len ? out_cap - len : 0, " %u", var);
			}
		}
		len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "\n");
		return len;
	}
	len += snprintf (out + len, out_cap > len ? out_cap - len : 0, "raw 0x%02X\n", op);
	return len;
}


enumError DisassembleSequence (char **out_text, size_t *out_size, const u8 *data, size_t size)
{
	if (!data || size < 4 || !out_text)
		return ERR_INVALID_DATA;

	seq_format_t fmt = DetectSequenceFormat (data, size);
	bool is_le = (fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_LE || fmt == SEQ_FMT_SSEQ);

	const u8 *code = data;
	size_t code_size = size;
	const u8 *labl = 0;
	size_t labl_size = 0;

	if ((fmt == SEQ_FMT_RSEQ || fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_BE
			|| fmt == SEQ_FMT_FSEQ_LE)
		&& seq_find_blocks (data, size, is_le, &code, &code_size, &labl, &labl_size))
	{
		// code/code_size/labl come straight from the block table.
	}
	else if (size >= 0x20
		&& (!memcmp (data, "RSEQ", 4) || !memcmp (data, "CSEQ", 4) || !memcmp (data, "FSEQ", 4)))
	{
		u32 data_off = is_le ? read_le32 (data + 0x10) : read_be32 (data + 0x10);
		if (data_off + 12 <= size && !memcmp (data + data_off, "DATA", 4))
		{
			u32 base_off
				= is_le ? read_le32 (data + data_off + 8) : read_be32 (data + data_off + 8);
			u32 sec_size
				= is_le ? read_le32 (data + data_off + 4) : read_be32 (data + data_off + 4);
			code = data + data_off + base_off;
			code_size = (sec_size > base_off) ? (sec_size - base_off) : 0;
			if (data_off + base_off + code_size > size)
				code_size = size - (data_off + base_off);
		}
	}
	else if (size >= 0x10 && !memcmp (data, "SSEQ", 4))
	{
		if (size >= 0x1C && !memcmp (data + 0x10, "DATA", 4))
		{
			u32 base_off = read_le32 (data + 0x10 + 8);
			u32 sec_size = read_le32 (data + 0x10 + 4);
			code = data + 0x10 + base_off;
			code_size = (sec_size > base_off) ? (sec_size - base_off) : 0;
			if (0x10 + base_off + code_size > size)
				code_size = size - (0x10 + base_off);
		}
	}
	else if (!memcmp (data, "DATA", 4))
	{
		u32 base_off = is_le ? read_le32 (data + 8) : read_be32 (data + 8);
		u32 sec_size = is_le ? read_le32 (data + 4) : read_be32 (data + 4);
		code = data + base_off;
		code_size = (sec_size > base_off) ? (sec_size - base_off) : (size - base_off);
	}

	if (code_size == 0)
		return ERR_INVALID_DATA;

// Pass 1: find all label / jump / call / track targets
	label_map_t *labels = NULL;
	uint n_labels = 0, alloc_labels = 0;

	// Seed real names from the sequence LABEL block first so they win
	// over synthesized ones for the same offsets.
	seq_read_labels (labl, labl_size, is_le, &labels, &n_labels, &alloc_labels);
	// Snapshot: entries below this count came from the block (pass 1
	// only appends synthesized ones afterwards); directives print just
	// these so the assembler rebuilds the LABL block exactly.
	uint n_real_labels = n_labels;

	size_t pos = 0;
	while (pos < code_size)
	{
		seq_prefix_t pre;
		u8 op;
		if (!seq_consume_prefix (code, code_size, &pos, &pre, &op))
			break;
		if (op < 0x80)
		{
			if (pos < code_size)
				pos++; // velocity (always one raw byte, even under RANDOM)
			if (pre.has_random)
				pos += 4; // s16 min/max replace the length operand
			else if (pre.has_variable)
				pos += 1; // u8 variable id replaces it
			else
				read_vlq (code, code_size, &pos); // duration
		}
		else
		{
			switch (op)
			{
				case SEQ_OP_WAIT:
				case SEQ_OP_PRG:
					if (pre.has_random)
						pos += 4;
					else if (pre.has_variable)
						pos += 1;
					else
						read_vlq (code, code_size, &pos);
					break;
				case SEQ_OP_OPEN_TRACK:
					if (pos + 4 <= code_size)
					{
						u8 trk = code[pos++];
						u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
						pos += 3;
						char pfx[16];
						snprintf (pfx, sizeof (pfx), "Track%u", trk);
						add_label (&labels, &n_labels, &alloc_labels, target, pfx);
					}
					break;
				case SEQ_OP_JUMP:
					if (pos + 3 <= code_size)
					{
						u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
						pos += 3;
						add_label (&labels, &n_labels, &alloc_labels, target, "Jump");
					}
					break;
				case SEQ_OP_CALL:
					if (pos + 3 <= code_size)
					{
						u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
						pos += 3;
						add_label (&labels, &n_labels, &alloc_labels, target, "Sub");
					}
					break;
				case SEQ_OP_EX_COMMAND:
					seq_skip_ex (code, code_size, &pos, is_le);
					break;
				case SEQ_OP_TIMEBASE:
				case SEQ_OP_ALLOC_TRACK:
				case SEQ_OP_MOD_DELAY:
				case SEQ_OP_TEMPO:
				case SEQ_OP_SWEEP_PITCH:
				case SEQ_OP_MOD_PERIOD:
					pos += 2;
					break;
				case SEQ_OP_ENV_RESET:
				case SEQ_OP_FIN:
				case SEQ_OP_LOOP_END:
				case SEQ_OP_RET:
					break;
				default:
					if (op >= 0xA0 && op <= 0xDF)
						pos += 1;
					break;
			}
			// TIME prefixes append a trailing operand after a u8-operand
			// command's own args (only that class reads argType2).
			if (pre.has_time && op >= 0xB0 && op <= 0xDF && op != SEQ_OP_EX_COMMAND)
			{
				if (pre.time_kind == 0xA3)
					pos += 2;
				else if (pre.time_kind == 0xA4)
					pos += 4;
				else
					pos += 1;
			}
			if (pos > code_size)
				pos = code_size;
		}
	}

	// Pass 2: format output MML text
	size_t out_cap = code_size * 16 + 1024;
	char *out = MALLOC (out_cap);
	if (!out)
	{
		FREE (labels);
		return ERR_CANT_CREATE;
	}
	size_t len = 0;

	len += snprintf (out + len, out_cap - len,
		"; NintendoWare Sequence Disassembly\n"
		"; Format: %s\n",
		GetSequenceFormatName (fmt));

	// Real LABEL-block names travel as file directives (the assembler
	// rebuilds the LABL block from exactly these, in order) so the
	// container round-trips losslessly. Only block names are in the map
	// at this point: pass 1 appends synthesized ones afterwards.
	for (uint li = 0; li < n_real_labels && li < n_labels; li++)
		len += snprintf (out + len, out_cap - len, "label \"%s\" 0x%06X\n",
			labels[li].name, labels[li].offset);
	len += snprintf (out + len, out_cap - len, "\n");

	pos = 0;
	while (pos < code_size)
	{
		u32 cur_off = (u32)pos;
		const char *lbl = find_label (labels, n_labels, cur_off);
		if (lbl)
		{
			len += snprintf (out + len, out_cap - len, "\n@%s:\n", lbl);
		}

		if (len + 256 >= out_cap)
		{
			out_cap *= 2;
			out = REALLOC (out, out_cap);
		}

		u8 op = 0;
		seq_prefix_t pre;
		if (!seq_consume_prefix (code, code_size, &pos, &pre, &op))
			break;
		if (op == 0)
		{
			bool all_zeros = true;
			for (size_t k = cur_off; k < code_size; k++)
			{
				if (code[k] != 0)
				{
					all_zeros = false;
					break;
				}
			}
			if (all_zeros)
			{
				bool has_future_label = false;
				for (uint l = 0; l < n_labels; l++)
				{
					if (labels[l].offset >= cur_off)
					{
						has_future_label = true;
						break;
					}
				}
				if (!has_future_label)
					break;
			}
		}

		// Prefix-wrapped commands print through one shared routine so
		// their altered operand shapes can never desync this walk.
		if (pre.has_if || pre.has_random || pre.has_variable || pre.has_time)
		{
			len += print_prefixed_command (out + len, out_cap - len, code, code_size, &pos,
				is_le, op, &pre, labels, n_labels);
			continue;
		}

		if (op < 0x80)
		{
			u8 vel = (pos < code_size) ? code[pos++] : 100;
			u32 dur = read_vlq (code, code_size, &pos);
			char note_str[16];
			pitch_to_name (note_str, sizeof (note_str), op);
			len += snprintf (out + len, out_cap - len, "    note %s %u %u  ; pitch=%u\n", note_str,
				vel, dur, op);
		}
		else
		{
			switch (op)
			{
				case SEQ_OP_WAIT:
				{
					u32 dur = read_vlq (code, code_size, &pos);
					len += snprintf (out + len, out_cap - len, "    wait %u\n", dur);
					break;
				}
				case SEQ_OP_PRG:
				{
					u32 prg = read_vlq (code, code_size, &pos);
					len += snprintf (out + len, out_cap - len, "    prg %u\n", prg);
					break;
				}
				case SEQ_OP_OPEN_TRACK:
				{
					if (pos + 4 <= code_size)
					{
						u8 trk = code[pos++];
						u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
						pos += 3;
						const char *target_lbl = find_label (labels, n_labels, target);
						if (target_lbl)
							len += snprintf (out + len, out_cap - len, "    open_track %u @%s\n",
								trk, target_lbl);
						else
							len += snprintf (out + len, out_cap - len, "    open_track %u 0x%06X\n",
								trk, target);
					}
					break;
				}
				case SEQ_OP_JUMP:
				{
					if (pos + 3 <= code_size)
					{
						u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
						pos += 3;
						const char *target_lbl = find_label (labels, n_labels, target);
						if (target_lbl)
							len += snprintf (
								out + len, out_cap - len, "    jump @%s\n", target_lbl);
						else
							len += snprintf (out + len, out_cap - len, "    jump 0x%06X\n", target);
					}
					break;
				}
				case SEQ_OP_CALL:
				{
					if (pos + 3 <= code_size)
					{
						u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
						pos += 3;
						const char *target_lbl = find_label (labels, n_labels, target);
						if (target_lbl)
							len += snprintf (
								out + len, out_cap - len, "    call @%s\n", target_lbl);
						else
							len += snprintf (out + len, out_cap - len, "    call 0x%06X\n", target);
					}
					break;
				}
				case SEQ_OP_ALLOC_TRACK:
				{
					if (pos + 2 <= code_size)
					{
						u16 mask = is_le ? read_le16 (code + pos) : read_be16 (code + pos);
						pos += 2;
						len += snprintf (
							out + len, out_cap - len, "    alloc_track 0x%04X\n", mask);
					}
					break;
				}
				case SEQ_OP_TEMPO:
				{
					if (pos + 2 <= code_size)
					{
						u16 tempo = is_le ? read_le16 (code + pos) : read_be16 (code + pos);
						pos += 2;
						len += snprintf (out + len, out_cap - len, "    tempo %u\n", tempo);
					}
					break;
				}
				case SEQ_OP_TIMEBASE:
				{
					u8 tb = (pos < code_size) ? code[pos++] : 48;
					len += snprintf (out + len, out_cap - len, "    timebase %u\n", tb);
					break;
				}
				case SEQ_OP_VOLUME:
				{
					u8 vol = (pos < code_size) ? code[pos++] : 127;
					len += snprintf (out + len, out_cap - len, "    vol %u\n", vol);
					break;
				}
				case SEQ_OP_MAIN_VOLUME:
				{
					u8 vol = (pos < code_size) ? code[pos++] : 127;
					len += snprintf (out + len, out_cap - len, "    master_vol %u\n", vol);
					break;
				}
				case SEQ_OP_PAN:
				{
					u8 pan = (pos < code_size) ? code[pos++] : 64;
					len += snprintf (out + len, out_cap - len, "    pan %u\n", pan);
					break;
				}
				case SEQ_OP_EXPRESSION:
				{
					u8 expr = (pos < code_size) ? code[pos++] : 127;
					len += snprintf (out + len, out_cap - len, "    expr %u\n", expr);
					break;
				}
				case SEQ_OP_PITCH_BEND:
				{
					int bend = (pos < code_size) ? (int)(signed char)code[pos++] : 0;
					len += snprintf (out + len, out_cap - len, "    bend %d\n", bend);
					break;
				}
				case SEQ_OP_BEND_RANGE:
				{
					u8 range = (pos < code_size) ? code[pos++] : 2;
					len += snprintf (out + len, out_cap - len, "    bend_range %u\n", range);
					break;
				}
				case SEQ_OP_TRANSPOSE:
				{
					int tr = (pos < code_size) ? (int)(signed char)code[pos++] : 0;
					len += snprintf (out + len, out_cap - len, "    transpose %d\n", tr);
					break;
				}
				case SEQ_OP_PRIO:
				{
					u8 p = (pos < code_size) ? code[pos++] : 64;
					len += snprintf (out + len, out_cap - len, "    prio %u\n", p);
					break;
				}
				case SEQ_OP_NOTE_WAIT:
				{
					u8 nw = (pos < code_size) ? code[pos++] : 1;
					len += snprintf (out + len, out_cap - len, "    note_wait %u\n", nw);
					break;
				}
				case SEQ_OP_TIE:
				{
					u8 tie = (pos < code_size) ? code[pos++] : 0;
					len += snprintf (out + len, out_cap - len, "    tie %u\n", tie);
					break;
				}
				case SEQ_OP_LOOP_START:
				{
					u8 cnt = (pos < code_size) ? code[pos++] : 0;
					len += snprintf (out + len, out_cap - len, "    loop_start %u\n", cnt);
					break;
				}
				case SEQ_OP_LOOP_END:
				{
					len += snprintf (out + len, out_cap - len, "    loop_end\n");
					break;
				}
				case SEQ_OP_RET:
				{
					len += snprintf (out + len, out_cap - len, "    ret\n");
					break;
				}
				case SEQ_OP_FIN:
				{
					len += snprintf (out + len, out_cap - len, "    fin\n");
					break;
				}
				case SEQ_OP_FXSEND_A:
				{
					u8 rev = (pos < code_size) ? code[pos++] : 0;
					len += snprintf (out + len, out_cap - len, "    reverb %u\n", rev);
					break;
				}
				case SEQ_OP_DAMPER:
				{
					u8 dmp = (pos < code_size) ? code[pos++] : 0;
					len += snprintf (out + len, out_cap - len, "    damper %u\n", dmp);
					break;
				}
				case SEQ_OP_BIQUAD_TYPE:
				case SEQ_OP_BIQUAD_VALUE:
				case SEQ_OP_MOD_PHASE:
				case SEQ_OP_MOD_CURVE:
				case SEQ_OP_FRONT_BYPASS:
				case SEQ_OP_ENV_HOLD:
				case SEQ_OP_MONOPHONIC:
				case SEQ_OP_VELOCITY_RANGE:
				case SEQ_OP_PORTA:
				case SEQ_OP_MOD_DEPTH:
				case SEQ_OP_MOD_SPEED:
				case SEQ_OP_MOD_TYPE:
				case SEQ_OP_MOD_RANGE:
				case SEQ_OP_PORTA_SW:
				case SEQ_OP_PORTA_TIME:
				case SEQ_OP_ATTACK:
				case SEQ_OP_DECAY:
				case SEQ_OP_SUSTAIN:
				case SEQ_OP_RELEASE:
				case SEQ_OP_PRINTVAR:
				case SEQ_OP_SURROUND_PAN:
				case SEQ_OP_LPF_CUTOFF:
				case SEQ_OP_FXSEND_B:
				case SEQ_OP_MAINSEND:
				case SEQ_OP_INIT_PAN:
				case SEQ_OP_MUTE:
				case SEQ_OP_FXSEND_C:
				{
					bool is_s8 = false;
					ccp mn = seq_u8_name (op, &is_s8);
					u8 a = (pos < code_size) ? code[pos++] : 0;
					if (is_s8)
						len += snprintf (out + len, out_cap - len, "    %s %d\n",
							mn ? mn : "unk", (int)(signed char)a);
					else
						len += snprintf (out + len, out_cap - len, "    %s %u\n",
							mn ? mn : "unk", a);
					break;
				}
				case SEQ_OP_MOD_DELAY:
				{
					u16 d = (pos + 2 <= code_size)
						? (is_le ? read_le16 (code + pos) : read_be16 (code + pos))
						: 0;
					pos += 2;
					len += snprintf (out + len, out_cap - len, "    mod_delay %u\n", d);
					break;
				}
				case SEQ_OP_SWEEP_PITCH:
				{
					int16_t s = (pos + 2 <= code_size)
						? (is_le ? (int16_t)read_le16 (code + pos)
								 : (int16_t)read_be16 (code + pos))
						: 0;
					pos += 2;
					len += snprintf (out + len, out_cap - len, "    sweep_pitch %d\n", s);
					break;
				}
				case SEQ_OP_MOD_PERIOD:
				{
					u16 p = (pos + 2 <= code_size)
						? (is_le ? read_le16 (code + pos) : read_be16 (code + pos))
						: 0;
					pos += 2;
					len += snprintf (out + len, out_cap - len, "    mod_period %u\n", p);
					break;
				}
				case SEQ_OP_ENV_RESET:
				{
					len += snprintf (out + len, out_cap - len, "    env_reset\n");
					break;
				}
				case SEQ_OP_EX_COMMAND:
				{
					if (pos >= code_size)
						break;
					u8 sub = code[pos++];
					u8 cls = sub & 0xF0;
					if ((cls == 0x80 || cls == 0x90) && pos + 3 <= code_size)
					{
						u8 a1 = code[pos];
						int16_t a2 = is_le ? (int16_t)read_le16 (code + pos + 1)
										   : (int16_t)read_be16 (code + pos + 1);
						pos += 3;
						len += snprintf (out + len, out_cap - len, "    ex 0x%02X %u %d\n",
							sub, a1, a2);
					}
					else if ((cls == 0xA0 || cls == 0xB0) && pos < code_size)
					{
						u8 a1 = code[pos++];
						len += snprintf (out + len, out_cap - len, "    ex 0x%02X %u\n",
							sub, a1);
					}
					else if (cls == 0xE0 && pos + 2 <= code_size)
					{
						int16_t a1 = is_le ? (int16_t)read_le16 (code + pos)
										   : (int16_t)read_be16 (code + pos);
						pos += 2;
						len += snprintf (out + len, out_cap - len, "    ex 0x%02X %d\n",
							sub, a1);
					}
					else
						len += snprintf (out + len, out_cap - len, "    ex 0x%02X\n", sub);
					break;
				}
				default:
				{
					len += snprintf (out + len, out_cap - len, "    raw 0x%02X\n", op);
					break;
				}
			}
		}
	}

	FREE (labels);
	*out_text = out;
	if (out_size)
		*out_size = len;
	return ERR_OK;
}

// Assembler implementation
typedef struct asm_label_t
{
	char name[64];
	u32 offset;
} asm_label_t;

typedef struct asm_fixup_t
{
	u32 code_offset;
	char target_name[64];
	bool is_24bit;
	bool is_le;
} asm_fixup_t;

enumError AssembleSequence (
	u8 **out_data, size_t *out_size, const char *text, seq_format_t target_fmt)
{
	if (!text || !out_data)
		return ERR_INVALID_DATA;
	if (target_fmt == SEQ_FMT_UNKNOWN)
		target_fmt = SEQ_FMT_RSEQ;

	bool is_le = (target_fmt == SEQ_FMT_CSEQ || target_fmt == SEQ_FMT_FSEQ_LE
		|| target_fmt == SEQ_FMT_SSEQ);

	asm_label_t *labels = NULL;
	uint n_labels = 0, alloc_labels = 0;

	// LABEL-block directives (`label "NAME" 0xOFFSET` as printed by the
	// disassembler): carried separately from jump labels and emitted
	// verbatim, offset-sorted, into the container's LABL block.
	asm_label_t *labl_defs = NULL;
	uint n_labl_defs = 0, alloc_labl_defs = 0;

	asm_fixup_t *fixups = NULL;
	uint n_fixups = 0, alloc_fixups = 0;

	size_t code_cap = 4096;
	u8 *code = MALLOC (code_cap);
	if (!code)
		return ERR_CANT_CREATE;
	size_t code_len = 0;

	const char *p = text;
	char line[512];

	while (*p)
	{
		size_t l_len = 0;
		while (*p && *p != '\n' && *p != '\r' && l_len + 1 < sizeof (line))
		{
			line[l_len++] = *p++;
		}
		line[l_len] = 0;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			p++;

		// Trim leading whitespace
		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		// Ignore comment or empty lines
		if (!*s || *s == ';' || *s == '#')
			continue;

		// Strip comments. ';' always starts one; '#' only at line start
		// or after whitespace, so sharps in note names (C#3, F#4, ...)
		// survive -- the disassembler prints them and the assembler
		// must read them back.
		char *cmt = strchr (s, ';');
		if (cmt)
			*cmt = 0;
		cmt = strchr (s, '#');
		if (cmt && (cmt == s || cmt[-1] == ' ' || cmt[-1] == '\t'))
			*cmt = 0;

		// Trim trailing whitespace
		size_t slen = strlen (s);
		while (slen > 0 && (s[slen - 1] == ' ' || s[slen - 1] == '\t'))
		{
			s[--slen] = 0;
		}
		if (!*s)
			continue;

		// LABEL-block directive: label "NAME" 0xOFFSET (as printed by
		// the disassembler for real container names; rebuilt verbatim).
		if ((s[0] == 'l' || s[0] == 'L') && !strncasecmp (s, "label", 5)
			&& (s[5] == ' ' || s[5] == '\t'))
		{
			char lname[64] = "";
			char loff[64] = "";
			if (sscanf (s + 5, " \"%63[^\"]\" %63s", lname, loff) == 2)
			{
				if (n_labl_defs >= alloc_labl_defs)
				{
					alloc_labl_defs = alloc_labl_defs ? alloc_labl_defs * 2 : 16;
					labl_defs = REALLOC (labl_defs, alloc_labl_defs * sizeof (*labl_defs));
				}
				if (labl_defs)
				{
					asm_label_t *ld = &labl_defs[n_labl_defs++];
					snprintf (ld->name, sizeof (ld->name), "%s", lname);
					ld->offset = (u32)strtoul (loff, NULL, 0);
				}
			}
			continue;
		}

		// Label definition: @Name: or Name:
		if (s[slen - 1] == ':')
		{
			s[--slen] = 0;
			if (*s == '@')
				s++;
			if (n_labels >= alloc_labels)
			{
				alloc_labels = alloc_labels ? alloc_labels * 2 : 64;
				labels = REALLOC (labels, alloc_labels * sizeof (asm_label_t));
			}
			asm_label_t *lbl = &labels[n_labels++];
			snprintf (lbl->name, sizeof (lbl->name), "%s", s);
			lbl->offset = (u32)code_len;
			continue;
		}

		char cmd[64] = "", arg1[64] = "", arg2[64] = "", arg3[64] = "";
		char arg4[64] = "", arg5[64] = "", arg6[64] = "";
		int n_args = sscanf (s, "%63s %63s %63s %63s %63s %63s %63s", cmd, arg1, arg2,
			arg3, arg4, arg5, arg6);
		if (n_args < 1)
			continue;

		if (code_len + 64 >= code_cap)
		{
			code_cap *= 2;
			code = REALLOC (code, code_cap);
		}

		// Prefix-wrapped commands (SDK MML IF/TIME/RANDOM/VARIABLE
		// semantics), extended commands, and mnemonics the legacy chain
		// below never learned: all handled here so the historic paths
		// underneath stay byte-identical for what they already cover.
		{
			char *toks[7] = { cmd, arg1, arg2, arg3, arg4, arg5, arg6 };
			int ti = 0;
			bool pf_if = false, pf_rand = false, pf_var = false, pf_time = false;
			u8 pf_timekind = 0;
			int npfx = 0;
			while (ti < n_args && npfx < 3)
			{
				if (!strcasecmp (toks[ti], "if"))
					pf_if = true;
				else if (!strcasecmp (toks[ti], "random"))
					pf_rand = true;
				else if (!strcasecmp (toks[ti], "variable"))
					pf_var = true;
				else if (!strcasecmp (toks[ti], "time"))
				{
					pf_time = true;
					pf_timekind = 0xA3;
				}
				else if (!strcasecmp (toks[ti], "time_random"))
				{
					pf_time = true;
					pf_timekind = 0xA4;
				}
				else if (!strcasecmp (toks[ti], "time_variable"))
				{
					pf_time = true;
					pf_timekind = 0xA5;
				}
				else
					break;
				ti++;
				npfx++;
			}
			bool handled = (ti < n_args) && (pf_if || pf_rand || pf_var || pf_time);
			ccp sub = (ti < n_args) ? toks[ti] : "";
			bool is_new_mn = !strcasecmp (sub, "ex") || !strcasecmp (sub, "porta")
				|| !strcasecmp (sub, "mod_depth") || !strcasecmp (sub, "mod_speed")
				|| !strcasecmp (sub, "mod_type") || !strcasecmp (sub, "mod_range")
				|| !strcasecmp (sub, "porta_sw") || !strcasecmp (sub, "porta_time")
				|| !strcasecmp (sub, "attack") || !strcasecmp (sub, "decay")
				|| !strcasecmp (sub, "sustain") || !strcasecmp (sub, "release")
				|| !strcasecmp (sub, "printvar") || !strcasecmp (sub, "surround_pan")
				|| !strcasecmp (sub, "lpf_cutoff") || !strcasecmp (sub, "fxsend_b")
				|| !strcasecmp (sub, "mainsend") || !strcasecmp (sub, "init_pan")
				|| !strcasecmp (sub, "mute") || !strcasecmp (sub, "fxsend_c")
				|| !strcasecmp (sub, "env_hold") || !strcasecmp (sub, "monophonic")
				|| !strcasecmp (sub, "velocity_range") || !strcasecmp (sub, "biquad_type")
				|| !strcasecmp (sub, "biquad_value") || !strcasecmp (sub, "mod_phase")
				|| !strcasecmp (sub, "mod_curve") || !strcasecmp (sub, "front_bypass")
				|| !strcasecmp (sub, "mod_delay") || !strcasecmp (sub, "sweep_pitch")
				|| !strcasecmp (sub, "mod_period") || !strcasecmp (sub, "env_reset");
			if (handled || is_new_mn)
			{
				// Emit prefixes in SDK order, then the command.
				if (pf_if)
					code[code_len++] = 0xA2;
				if (pf_time)
					code[code_len++] = pf_timekind;
				if (pf_rand)
					code[code_len++] = 0xA0;
				else if (pf_var)
					code[code_len++] = 0xA1;
				int ai = ti + 1; // first operand token
				if (!strcasecmp (sub, "ex"))
				{
					u8 exsub = (u8)strtoul (toks[ai], NULL, 0);
					code[code_len++] = SEQ_OP_EX_COMMAND;
					code[code_len++] = exsub;
					u8 cls = exsub & 0xF0;
					if (cls == 0x80 || cls == 0x90)
					{
						code[code_len++] = (u8)strtoul (toks[ai + 1], NULL, 0);
						int16_t a2 = (int16_t)strtol (toks[ai + 2], NULL, 0);
						if (is_le)
							write_le16 (code + code_len, (u16)a2);
						else
							write_be16 (code + code_len, (u16)a2);
						code_len += 2;
					}
					else if (cls == 0xA0 || cls == 0xB0)
						code[code_len++] = (u8)strtoul (toks[ai + 1], NULL, 0);
					else if (cls == 0xE0)
					{
						int16_t a1 = (int16_t)strtol (toks[ai + 1], NULL, 0);
						if (is_le)
							write_le16 (code + code_len, (u16)a1);
						else
							write_be16 (code + code_len, (u16)a1);
						code_len += 2;
					}
				}
				else if (!strcasecmp (sub, "note") || !strcasecmp (sub, "n"))
				{
					int pitch = name_to_pitch (toks[ai]);
					if (pitch < 0)
						pitch = 60;
					code[code_len++] = (u8)(pitch & 0x7F);
					code[code_len++] = (u8)(atoi (toks[ai + 1]) & 0x7F);
					if (pf_rand)
					{
						int16_t lo = (int16_t)strtol (toks[ai + 2], NULL, 0);
						int16_t hi = (int16_t)strtol (toks[ai + 3], NULL, 0);
						if (is_le)
						{
							write_le16 (code + code_len, (u16)lo);
							write_le16 (code + code_len + 2, (u16)hi);
						}
						else
						{
							write_be16 (code + code_len, (u16)lo);
							write_be16 (code + code_len + 2, (u16)hi);
						}
						code_len += 4;
					}
					else if (pf_var)
						code[code_len++] = (u8)strtoul (toks[ai + 2], NULL, 0);
					else
						write_vlq (code, &code_len, (u32)strtoul (toks[ai + 2], NULL, 0));
				}
				else if (!strcasecmp (sub, "wait") || !strcasecmp (sub, "rest")
					|| !strcasecmp (sub, "w") || !strcasecmp (sub, "prg")
					|| !strcasecmp (sub, "program") || !strcasecmp (sub, "patch"))
				{
					bool is_wait = !strcasecmp (sub, "wait") || !strcasecmp (sub, "rest")
						|| !strcasecmp (sub, "w");
					code[code_len++] = is_wait ? SEQ_OP_WAIT : SEQ_OP_PRG;
					if (pf_rand)
					{
						int16_t lo = (int16_t)strtol (toks[ai], NULL, 0);
						int16_t hi = (int16_t)strtol (toks[ai + 1], NULL, 0);
						if (is_le)
						{
							write_le16 (code + code_len, (u16)lo);
							write_le16 (code + code_len + 2, (u16)hi);
						}
						else
						{
							write_be16 (code + code_len, (u16)lo);
							write_be16 (code + code_len + 2, (u16)hi);
						}
						code_len += 4;
					}
					else if (pf_var)
						code[code_len++] = (u8)strtoul (toks[ai], NULL, 0);
					else
						write_vlq (code, &code_len, (u32)strtoul (toks[ai], NULL, 0));
				}
				else if (!strcasecmp (sub, "jump") || !strcasecmp (sub, "call")
					|| !strcasecmp (sub, "open_track"))
				{
					u8 opj = !strcasecmp (sub, "jump") ? SEQ_OP_JUMP
						: !strcasecmp (sub, "call")	  ? SEQ_OP_CALL
													  : SEQ_OP_OPEN_TRACK;
					code[code_len++] = opj;
					int tj = ai;
					if (opj == SEQ_OP_OPEN_TRACK)
						code[code_len++] = (u8)strtoul (toks[tj++], NULL, 0);
					if (n_fixups >= alloc_fixups)
					{
						alloc_fixups = alloc_fixups ? alloc_fixups * 2 : 64;
						fixups = REALLOC (fixups, alloc_fixups * sizeof (asm_fixup_t));
					}
					asm_fixup_t *f = &fixups[n_fixups++];
					f->code_offset = (u32)code_len;
					const char *target = toks[tj];
					if (target[0] == '@')
						target++;
					snprintf (f->target_name, sizeof (f->target_name), "%s", target);
					f->is_24bit = true;
					f->is_le = is_le;
					code_len += 3;
				}
				else if (!strcasecmp (sub, "tempo") || !strcasecmp (sub, "bpm")
					|| !strcasecmp (sub, "mod_delay") || !strcasecmp (sub, "sweep_pitch")
					|| !strcasecmp (sub, "mod_period"))
				{
					u8 ops = !strcasecmp (sub, "tempo") || !strcasecmp (sub, "bpm")
						? SEQ_OP_TEMPO
						: !strcasecmp (sub, "mod_delay") ? SEQ_OP_MOD_DELAY
						: !strcasecmp (sub, "sweep_pitch") ? SEQ_OP_SWEEP_PITCH
														   : SEQ_OP_MOD_PERIOD;
					code[code_len++] = ops;
					if (pf_rand)
					{
						int16_t lo = (int16_t)strtol (toks[ai], NULL, 0);
						int16_t hi = (int16_t)strtol (toks[ai + 1], NULL, 0);
						if (is_le)
						{
							write_le16 (code + code_len, (u16)lo);
							write_le16 (code + code_len + 2, (u16)hi);
						}
						else
						{
							write_be16 (code + code_len, (u16)lo);
							write_be16 (code + code_len + 2, (u16)hi);
						}
						code_len += 4;
					}
					else if (pf_var)
						code[code_len++] = (u8)strtoul (toks[ai], NULL, 0);
					else
					{
						int16_t a = (int16_t)strtol (toks[ai], NULL, 0);
						if (is_le)
							write_le16 (code + code_len, (u16)a);
						else
							write_be16 (code + code_len, (u16)a);
						code_len += 2;
					}
				}
				else if (!strcasecmp (sub, "fin") || !strcasecmp (sub, "end")
					|| !strcasecmp (sub, "ret") || !strcasecmp (sub, "return")
					|| !strcasecmp (sub, "loop_end") || !strcasecmp (sub, "env_reset"))
				{
					code[code_len++] = !strcasecmp (sub, "fin") || !strcasecmp (sub, "end")
						? SEQ_OP_FIN
						: !strcasecmp (sub, "ret") || !strcasecmp (sub, "return")
						? SEQ_OP_RET
						: !strcasecmp (sub, "loop_end") ? SEQ_OP_LOOP_END
														: SEQ_OP_ENV_RESET;
				}
				else
				{
					// u8-operand command (all other mnemonics incl. the new
					// ones): RANDOM replaces the operand with an s16 pair,
					// VARIABLE with a var id, else one raw byte.
					bool is_s8 = !strcasecmp (sub, "transpose") || !strcasecmp (sub, "bend")
						|| !strcasecmp (sub, "pitch_bend");
					u8 ops = 0xFF;
					if (!strcasecmp (sub, "timebase"))
						ops = SEQ_OP_TIMEBASE;
					else if (!strcasecmp (sub, "vol") || !strcasecmp (sub, "volume"))
						ops = SEQ_OP_VOLUME;
					else if (!strcasecmp (sub, "master_vol"))
						ops = SEQ_OP_MAIN_VOLUME;
					else if (!strcasecmp (sub, "pan"))
						ops = SEQ_OP_PAN;
					else if (!strcasecmp (sub, "expr") || !strcasecmp (sub, "expression"))
						ops = SEQ_OP_EXPRESSION;
					else if (!strcasecmp (sub, "bend") || !strcasecmp (sub, "pitch_bend"))
						ops = SEQ_OP_PITCH_BEND;
					else if (!strcasecmp (sub, "bend_range"))
						ops = SEQ_OP_BEND_RANGE;
					else if (!strcasecmp (sub, "transpose"))
						ops = SEQ_OP_TRANSPOSE;
					else if (!strcasecmp (sub, "prio") || !strcasecmp (sub, "priority"))
						ops = SEQ_OP_PRIO;
					else if (!strcasecmp (sub, "note_wait"))
						ops = SEQ_OP_NOTE_WAIT;
					else if (!strcasecmp (sub, "tie"))
						ops = SEQ_OP_TIE;
					else if (!strcasecmp (sub, "loop_start"))
						ops = SEQ_OP_LOOP_START;
					else if (!strcasecmp (sub, "reverb") || !strcasecmp (sub, "fx_send_a"))
						ops = SEQ_OP_FXSEND_A;
					else if (!strcasecmp (sub, "damper"))
						ops = SEQ_OP_DAMPER;
					else if (!strcasecmp (sub, "porta"))
						ops = SEQ_OP_PORTA;
					else if (!strcasecmp (sub, "mod_depth"))
						ops = SEQ_OP_MOD_DEPTH;
					else if (!strcasecmp (sub, "mod_speed"))
						ops = SEQ_OP_MOD_SPEED;
					else if (!strcasecmp (sub, "mod_type"))
						ops = SEQ_OP_MOD_TYPE;
					else if (!strcasecmp (sub, "mod_range"))
						ops = SEQ_OP_MOD_RANGE;
					else if (!strcasecmp (sub, "porta_sw"))
						ops = SEQ_OP_PORTA_SW;
					else if (!strcasecmp (sub, "porta_time"))
						ops = SEQ_OP_PORTA_TIME;
					else if (!strcasecmp (sub, "attack"))
						ops = SEQ_OP_ATTACK;
					else if (!strcasecmp (sub, "decay"))
						ops = SEQ_OP_DECAY;
					else if (!strcasecmp (sub, "sustain"))
						ops = SEQ_OP_SUSTAIN;
					else if (!strcasecmp (sub, "release"))
						ops = SEQ_OP_RELEASE;
					else if (!strcasecmp (sub, "printvar"))
						ops = SEQ_OP_PRINTVAR;
					else if (!strcasecmp (sub, "surround_pan"))
						ops = SEQ_OP_SURROUND_PAN;
					else if (!strcasecmp (sub, "lpf_cutoff"))
						ops = SEQ_OP_LPF_CUTOFF;
					else if (!strcasecmp (sub, "fxsend_b"))
						ops = SEQ_OP_FXSEND_B;
					else if (!strcasecmp (sub, "mainsend"))
						ops = SEQ_OP_MAINSEND;
					else if (!strcasecmp (sub, "init_pan"))
						ops = SEQ_OP_INIT_PAN;
					else if (!strcasecmp (sub, "mute"))
						ops = SEQ_OP_MUTE;
					else if (!strcasecmp (sub, "fxsend_c"))
						ops = SEQ_OP_FXSEND_C;
					else if (!strcasecmp (sub, "env_hold"))
						ops = SEQ_OP_ENV_HOLD;
					else if (!strcasecmp (sub, "monophonic"))
						ops = SEQ_OP_MONOPHONIC;
					else if (!strcasecmp (sub, "velocity_range"))
						ops = SEQ_OP_VELOCITY_RANGE;
					else if (!strcasecmp (sub, "biquad_type"))
						ops = SEQ_OP_BIQUAD_TYPE;
					else if (!strcasecmp (sub, "biquad_value"))
						ops = SEQ_OP_BIQUAD_VALUE;
					else if (!strcasecmp (sub, "mod_phase"))
						ops = SEQ_OP_MOD_PHASE;
					else if (!strcasecmp (sub, "mod_curve"))
						ops = SEQ_OP_MOD_CURVE;
					else if (!strcasecmp (sub, "front_bypass"))
						ops = SEQ_OP_FRONT_BYPASS;
					if (ops == 0xFF)
						continue; // unknown mnemonic: same as legacy chain
					code[code_len++] = ops;
					if (pf_rand)
					{
						int16_t lo = (int16_t)strtol (toks[ai], NULL, 0);
						int16_t hi = (int16_t)strtol (toks[ai + 1], NULL, 0);
						if (is_le)
						{
							write_le16 (code + code_len, (u16)lo);
							write_le16 (code + code_len + 2, (u16)hi);
						}
						else
						{
							write_be16 (code + code_len, (u16)lo);
							write_be16 (code + code_len + 2, (u16)hi);
						}
						code_len += 4;
					}
					else if (pf_var)
						code[code_len++] = (u8)strtoul (toks[ai], NULL, 0);
					else if (is_s8)
						code[code_len++] = (u8)(signed char)atoi (toks[ai]);
					else
						code[code_len++] = (u8)strtoul (toks[ai], NULL, 0);
				}
				// TIME trailing operand (u8-class commands only --
				// the only class the player reads argType2 for).
				// The disassembler prints it last, so take it from
				// the token tail: ti+2 (arity is 1 here).
				if (pf_time && ti + 2 < n_args)
				{
					if (pf_timekind == 0xA3)
					{
						int16_t a = (int16_t)strtol (toks[ti + 2], NULL, 0);
						if (is_le)
							write_le16 (code + code_len, (u16)a);
						else
							write_be16 (code + code_len, (u16)a);
						code_len += 2;
					}
					else if (pf_timekind == 0xA4 && ti + 3 < n_args)
					{
						int16_t lo = (int16_t)strtol (toks[ti + 2], NULL, 0);
						int16_t hi = (int16_t)strtol (toks[ti + 3], NULL, 0);
						if (is_le)
						{
							write_le16 (code + code_len, (u16)lo);
							write_le16 (code + code_len + 2, (u16)hi);
						}
						else
						{
							write_be16 (code + code_len, (u16)lo);
							write_be16 (code + code_len + 2, (u16)hi);
						}
						code_len += 4;
					}
					else if (pf_timekind == 0xA5)
						code[code_len++] = (u8)strtoul (toks[ti + 2], NULL, 0);
				}
				continue;
			}
		}

		if (!strcasecmp (cmd, "note") || !strcasecmp (cmd, "n"))
		{
			int pitch = name_to_pitch (arg1);
			if (pitch < 0)
				pitch = 60;
			int vel = (n_args >= 3) ? atoi (arg2) : 100;
			u32 dur = (n_args >= 4) ? (u32)strtoul (arg3, NULL, 0) : 48;
			code[code_len++] = (u8)(pitch & 0x7F);
			code[code_len++] = (u8)(vel & 0x7F);
			write_vlq (code, &code_len, dur);
		}
		else if (!strcasecmp (cmd, "wait") || !strcasecmp (cmd, "rest") || !strcasecmp (cmd, "w"))
		{
			u32 dur = (n_args >= 2) ? (u32)strtoul (arg1, NULL, 0) : 48;
			code[code_len++] = SEQ_OP_WAIT;
			write_vlq (code, &code_len, dur);
		}
		else if (!strcasecmp (cmd, "prg") || !strcasecmp (cmd, "program")
			|| !strcasecmp (cmd, "patch"))
		{
			u32 prg = (n_args >= 2) ? (u32)strtoul (arg1, NULL, 0) : 0;
			code[code_len++] = SEQ_OP_PRG;
			write_vlq (code, &code_len, prg);
		}
		else if (!strcasecmp (cmd, "open_track"))
		{
			u8 trk = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_OPEN_TRACK;
			code[code_len++] = trk;
			if (n_fixups >= alloc_fixups)
			{
				alloc_fixups = alloc_fixups ? alloc_fixups * 2 : 64;
				fixups = REALLOC (fixups, alloc_fixups * sizeof (asm_fixup_t));
			}
			asm_fixup_t *f = &fixups[n_fixups++];
			f->code_offset = (u32)code_len;
			const char *target = (arg2[0] == '@') ? arg2 + 1 : arg2;
			snprintf (f->target_name, sizeof (f->target_name), "%s", target);
			f->is_24bit = true;
			f->is_le = is_le;
			code_len += 3;
		}
		else if (!strcasecmp (cmd, "jump"))
		{
			code[code_len++] = SEQ_OP_JUMP;
			if (n_fixups >= alloc_fixups)
			{
				alloc_fixups = alloc_fixups ? alloc_fixups * 2 : 64;
				fixups = REALLOC (fixups, alloc_fixups * sizeof (asm_fixup_t));
			}
			asm_fixup_t *f = &fixups[n_fixups++];
			f->code_offset = (u32)code_len;
			const char *target = (arg1[0] == '@') ? arg1 + 1 : arg1;
			snprintf (f->target_name, sizeof (f->target_name), "%s", target);
			f->is_24bit = true;
			f->is_le = is_le;
			code_len += 3;
		}
		else if (!strcasecmp (cmd, "call"))
		{
			code[code_len++] = SEQ_OP_CALL;
			if (n_fixups >= alloc_fixups)
			{
				alloc_fixups = alloc_fixups ? alloc_fixups * 2 : 64;
				fixups = REALLOC (fixups, alloc_fixups * sizeof (asm_fixup_t));
			}
			asm_fixup_t *f = &fixups[n_fixups++];
			f->code_offset = (u32)code_len;
			const char *target = (arg1[0] == '@') ? arg1 + 1 : arg1;
			snprintf (f->target_name, sizeof (f->target_name), "%s", target);
			f->is_24bit = true;
			f->is_le = is_le;
			code_len += 3;
		}
		else if (!strcasecmp (cmd, "alloc_track"))
		{
			u16 mask = (u16)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_ALLOC_TRACK;
			if (is_le)
				write_le16 (code + code_len, mask);
			else
				write_be16 (code + code_len, mask);
			code_len += 2;
		}
		else if (!strcasecmp (cmd, "tempo") || !strcasecmp (cmd, "bpm"))
		{
			u16 tempo = (u16)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_TEMPO;
			if (is_le)
				write_le16 (code + code_len, tempo);
			else
				write_be16 (code + code_len, tempo);
			code_len += 2;
		}
		else if (!strcasecmp (cmd, "timebase"))
		{
			u8 tb = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_TIMEBASE;
			code[code_len++] = tb;
		}
		else if (!strcasecmp (cmd, "vol") || !strcasecmp (cmd, "volume"))
		{
			u8 vol = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_VOLUME;
			code[code_len++] = vol;
		}
		else if (!strcasecmp (cmd, "master_vol"))
		{
			u8 vol = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_MAIN_VOLUME;
			code[code_len++] = vol;
		}
		else if (!strcasecmp (cmd, "pan"))
		{
			u8 pan = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_PAN;
			code[code_len++] = pan;
		}
		else if (!strcasecmp (cmd, "expr") || !strcasecmp (cmd, "expression"))
		{
			u8 expr = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_EXPRESSION;
			code[code_len++] = expr;
		}
		else if (!strcasecmp (cmd, "bend") || !strcasecmp (cmd, "pitch_bend"))
		{
			int bend = atoi (arg1);
			code[code_len++] = SEQ_OP_PITCH_BEND;
			code[code_len++] = (u8)(signed char)bend;
		}
		else if (!strcasecmp (cmd, "bend_range"))
		{
			u8 rng = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_BEND_RANGE;
			code[code_len++] = rng;
		}
		else if (!strcasecmp (cmd, "transpose"))
		{
			int tr = atoi (arg1);
			code[code_len++] = SEQ_OP_TRANSPOSE;
			code[code_len++] = (u8)(signed char)tr;
		}
		else if (!strcasecmp (cmd, "prio") || !strcasecmp (cmd, "priority"))
		{
			u8 p_val = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_PRIO;
			code[code_len++] = p_val;
		}
		else if (!strcasecmp (cmd, "note_wait"))
		{
			u8 nw = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_NOTE_WAIT;
			code[code_len++] = nw;
		}
		else if (!strcasecmp (cmd, "tie"))
		{
			u8 tie = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_TIE;
			code[code_len++] = tie;
		}
		else if (!strcasecmp (cmd, "loop_start"))
		{
			u8 cnt = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_LOOP_START;
			code[code_len++] = cnt;
		}
		else if (!strcasecmp (cmd, "loop_end"))
		{
			code[code_len++] = SEQ_OP_LOOP_END;
		}
		else if (!strcasecmp (cmd, "ret") || !strcasecmp (cmd, "return"))
		{
			code[code_len++] = SEQ_OP_RET;
		}
		else if (!strcasecmp (cmd, "fin") || !strcasecmp (cmd, "end"))
		{
			code[code_len++] = SEQ_OP_FIN;
		}
		else if (!strcasecmp (cmd, "reverb") || !strcasecmp (cmd, "fx_send_a"))
		{
			u8 rev = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_FXSEND_A;
			code[code_len++] = rev;
		}
		else if (!strcasecmp (cmd, "damper"))
		{
			u8 dmp = (u8)strtoul (arg1, NULL, 0);
			code[code_len++] = SEQ_OP_DAMPER;
			code[code_len++] = dmp;
		}
		else if (!strcasecmp (cmd, "raw"))
		{
			for (int i = 1; i <= n_args; i++)
			{
				const char *val_s = (i == 1) ? arg1 : (i == 2) ? arg2 : arg3;
				if (*val_s)
				{
					code[code_len++] = (u8)strtoul (val_s, NULL, 0);
				}
			}
		}
	}

	// Resolve fixups
	for (uint i = 0; i < n_fixups; i++)
	{
		asm_fixup_t *f = &fixups[i];
		u32 target_off = 0;
		bool found = false;
		for (uint j = 0; j < n_labels; j++)
		{
			if (!strcasecmp (labels[j].name, f->target_name))
			{
				target_off = labels[j].offset;
				found = true;
				break;
			}
		}
		if (!found)
		{
			target_off = (u32)strtoul (f->target_name, NULL, 0);
		}

		if (f->is_24bit)
		{
			if (f->is_le)
				write_le24 (code + f->code_offset, target_off);
			else
				write_be24 (code + f->code_offset, target_off);
		}
	}

	FREE (labels);
	FREE (fixups);

	// Build binary container
	size_t total_size = 0;
	u8 *out = NULL;

	// Sort LABEL directives by offset (containers store them ordered).
	for (uint i = 0; i < n_labl_defs; i++)
		for (uint j = i + 1; j < n_labl_defs; j++)
			if (labl_defs[j].offset < labl_defs[i].offset)
			{
				asm_label_t tmp = labl_defs[i];
				labl_defs[i] = labl_defs[j];
				labl_defs[j] = tmp;
			}

	if (target_fmt == SEQ_FMT_BMS)
	{
		total_size = code_len;
		out = code;
		*out_data = out;
		if (out_size)
			*out_size = total_size;
		FREE (labl_defs);
		return ERR_OK;
	}
	else if (target_fmt == SEQ_FMT_SSEQ)
	{
		// 0x10 Header + 0x0C DATA header + code
		uint data_sec_size = (uint)(12 + code_len);
		data_sec_size = (data_sec_size + 3) & ~3u;
		total_size = 0x10 + data_sec_size;
		out = CALLOC (1, total_size);
		if (!out)
		{
			FREE (code);
			return ERR_CANT_CREATE;
		}

		memcpy (out, "SSEQ", 4);
		write_le16 (out + 4, 0xFEFF); // BOM
		write_le16 (out + 6, 0x0100); // Version
		write_le32 (out + 8, (u32)total_size);
		write_le16 (out + 12, 0x0010); // Header size
		write_le16 (out + 14, 0x0001); // 1 section

		memcpy (out + 0x10, "DATA", 4);
		write_le32 (out + 0x14, data_sec_size);
		write_le32 (out + 0x18, 0x0000000C);
		memcpy (out + 0x10 + 12, code, code_len);
	}
	else if (target_fmt == SEQ_FMT_FSEQ_BE || target_fmt == SEQ_FMT_FSEQ_LE)
	{
		// Real FSEQ container per the NintendoWare sound headers
		// (SoundFileHeader + BlockReferenceTable; DATA block 0x5000,
		// LABEL block 0x5001): 20-byte file header, two 12-byte block
		// references, zero padding to 0x40, then DATA (code straight
		// after its 8-byte block header) and LABL (count + 8-byte table
		// entries + LabelInfos with NUL-terminated names padded so
		// len+1 rounds to 32). Verified byte-for-byte against retail
		// Wii U files; LE shape mirrored (Switch version unconfirmed).
		uint data_size = (uint)(8 + code_len);
		uint labl_size = 8 + 4 + 8 * n_labl_defs;
		for (uint i = 0; i < n_labl_defs; i++)
		{
			size_t nl = strlen (labl_defs[i].name);
			labl_size += (uint)(8 + 4 + ((nl + 1 + 3) & ~3u));
		}
		// Both blocks are padded to 32 bytes in real files (verified
		// across 811 retail sequences: every DATA and LABL size is a
		// multiple of 32; names are NUL-terminated and 4-padded).
		data_size = (data_size + 31) & ~31u;
		labl_size = (labl_size + 31) & ~31u;
		total_size = 0x40 + data_size + labl_size;
		out = CALLOC (1, total_size);
		if (!out)
		{
			FREE (code);
			return ERR_CANT_CREATE;
		}

		memcpy (out, "FSEQ", 4);
		if (is_le)
		{
			write_le16 (out + 4, 0xFEFF);
			write_le16 (out + 6, 0x0040); // header size
			write_le32 (out + 8, 0x00020000); // version
			write_le32 (out + 12, (u32)total_size);
			write_le16 (out + 16, 2); // block count
			write_le16 (out + 18, 0);
			write_le16 (out + 20, 0x5000);
			write_le16 (out + 22, 0);
			write_le32 (out + 24, 0x40);
			write_le32 (out + 28, data_size);
			write_le16 (out + 32, 0x5001);
			write_le16 (out + 34, 0);
			write_le32 (out + 36, 0x40 + data_size);
			write_le32 (out + 40, labl_size);
		}
		else
		{
			write_be16 (out + 4, 0xFEFF);
			write_be16 (out + 6, 0x0040); // header size
			write_be32 (out + 8, 0x00020000); // version
			write_be32 (out + 12, (u32)total_size);
			write_be16 (out + 16, 2); // block count
			write_be16 (out + 18, 0);
			write_be16 (out + 20, 0x5000);
			write_be16 (out + 22, 0);
			write_be32 (out + 24, 0x40);
			write_be32 (out + 28, data_size);
			write_be16 (out + 32, 0x5001);
			write_be16 (out + 34, 0);
			write_be32 (out + 36, 0x40 + data_size);
			write_be32 (out + 40, labl_size);
		}
		// (0x2C..0x40 stays zero from CALLOC: observed padding.)

		size_t dp = 0x40;
		memcpy (out + dp, "DATA", 4);
		if (is_le)
			write_le32 (out + dp + 4, data_size);
		else
			write_be32 (out + dp + 4, data_size);
		memcpy (out + dp + 8, code, code_len);

		size_t lp = 0x40 + data_size;
		memcpy (out + lp, "LABL", 4);
		if (is_le)
			write_le32 (out + lp + 4, labl_size);
		else
			write_be32 (out + lp + 4, labl_size);
		if (is_le)
			write_le32 (out + lp + 8, n_labl_defs);
		else
			write_be32 (out + lp + 8, n_labl_defs);
		size_t ipos = lp + 12 + 8 * n_labl_defs;
		for (uint i = 0; i < n_labl_defs; i++)
		{
			size_t er = lp + 12 + 8u * i;
			u32 eoff = (u32)(ipos - (lp + 8));
			if (is_le)
			{
				write_le16 (out + er, 0x5100);
				write_le16 (out + er + 2, 0);
				write_le32 (out + er + 4, eoff);
			}
			else
			{
				write_be16 (out + er, 0x5100);
				write_be16 (out + er + 2, 0);
				write_be32 (out + er + 4, eoff);
			}
			size_t nl = strlen (labl_defs[i].name);
			size_t strf = (nl + 1 + 3) & ~3u;
			if (is_le)
			{
				write_le16 (out + ipos, 0x1F00);
				write_le16 (out + ipos + 2, 0);
				write_le32 (out + ipos + 4, labl_defs[i].offset);
				write_le32 (out + ipos + 8, (u32)nl);
			}
			else
			{
				write_be16 (out + ipos, 0x1F00);
				write_be16 (out + ipos + 2, 0);
				write_be32 (out + ipos + 4, labl_defs[i].offset);
				write_be32 (out + ipos + 8, (u32)nl);
			}
			memcpy (out + ipos + 12, labl_defs[i].name, nl);
			ipos += 8 + 4 + strf;
		}
	}
	else
	{
		// RSEQ, CSEQ: legacy 0x20 header + 0x0C DATA header + code
		// (this tool's own historical shape; FSEQ has the real
		// block-table container above).
		const char *magic = (target_fmt == SEQ_FMT_RSEQ) ? "RSEQ" : "CSEQ";
		u16 ver = (target_fmt == SEQ_FMT_RSEQ) ? 0x0100 : 0x0200;

		uint data_sec_size = (uint)(12 + code_len);
		data_sec_size = (data_sec_size + 3) & ~3u;
		total_size = 0x20 + data_sec_size;
		out = CALLOC (1, total_size);
		if (!out)
		{
			FREE (code);
			return ERR_CANT_CREATE;
		}

		memcpy (out, magic, 4);
		if (is_le)
		{
			write_le16 (out + 4, 0xFEFF);
			write_le16 (out + 6, ver);
			write_le32 (out + 8, (u32)total_size);
			write_le16 (out + 12, 0x0020);
			write_le16 (out + 14, 0x0001);
			write_le32 (out + 16, 0x0020); // DATA section offset
			write_le32 (out + 20, data_sec_size);

			memcpy (out + 0x20, "DATA", 4);
			write_le32 (out + 0x24, data_sec_size);
			write_le32 (out + 0x28, 0x0000000C);
		}
		else
		{
			write_be16 (out + 4, 0xFEFF);
			write_be16 (out + 6, ver);
			write_be32 (out + 8, (u32)total_size);
			write_be16 (out + 12, 0x0020);
			write_be16 (out + 14, 0x0001);
			write_be32 (out + 16, 0x0020);
			write_be32 (out + 20, data_sec_size);

			memcpy (out + 0x20, "DATA", 4);
			write_be32 (out + 0x24, data_sec_size);
			write_be32 (out + 0x28, 0x0000000C);
		}
		memcpy (out + 0x20 + 12, code, code_len);
	}

	FREE (labl_defs);
	FREE (code);
	*out_data = out;
	if (out_size)
		*out_size = total_size;
	return ERR_OK;
}

// Callback for midi_file_write: appends raw bytes into a midilib buffer.
// Must return number of bytes written (fwrite convention), not 0.
static int midi_buf_write_cb (void *data, size_t len, void *userdata)
{
	struct buffer *b = (struct buffer *)userdata;
	int r = buffer_write (b, (uint8_t *)data, (int)len);
	return r < 0 ? 0 : (int)len;
}

// Sequence to MIDI conversion
typedef struct midi_event_t
{
	u32 time;
	u8 type;
	u8 channel;
	u8 data1;
	u8 data2;
	u32 meta_len;
	u8 *meta_data;
} midi_event_t;

typedef struct midi_track_build_t
{
	midi_event_t *events;
	uint n_events;
	uint alloc_events;
} midi_track_build_t;

static void add_midi_event (midi_track_build_t *tr, u32 time, u8 type, u8 ch, u8 d1, u8 d2)
{
	if (tr->n_events >= tr->alloc_events)
	{
		tr->alloc_events = tr->alloc_events ? tr->alloc_events * 2 : 128;
		tr->events = REALLOC (tr->events, tr->alloc_events * sizeof (midi_event_t));
	}
	midi_event_t *e = &tr->events[tr->n_events++];
	e->time = time;
	e->type = type;
	e->channel = ch;
	e->data1 = d1;
	e->data2 = d2;
	e->meta_len = 0;
	e->meta_data = NULL;
}

static int compare_midi_events (const void *a, const void *b)
{
	const midi_event_t *ea = (const midi_event_t *)a;
	const midi_event_t *eb = (const midi_event_t *)b;
	if (ea->time != eb->time)
		return (ea->time < eb->time) ? -1 : 1;
	// Note Off before Note On at the same timestamp
	if ((ea->type & 0xF0) == 0x80 && (eb->type & 0xF0) == 0x90)
		return -1;
	if ((ea->type & 0xF0) == 0x90 && (eb->type & 0xF0) == 0x80)
		return 1;
	return 0;
}

// Read one u8-class musical value with RANDOM/VARIABLE replacement for
// MIDI conversion (bounds midpoint / var id literal), advancing *pos.
static u8 seq_midi_u8arg (const u8 *code, size_t code_size, size_t *pos,
	bool is_le, const seq_prefix_t *pre, u8 def)
{
	(void)is_le;
	if (pre->has_random && *pos + 4 <= code_size)
	{
		int16_t lo = is_le ? (int16_t)read_le16 (code + *pos)
						   : (int16_t)read_be16 (code + *pos);
		int16_t hi = is_le ? (int16_t)read_le16 (code + *pos + 2)
						   : (int16_t)read_be16 (code + *pos + 2);
		*pos += 4;
		int mid = (lo + hi) / 2;
		return (u8)(mid < 0 ? 0 : mid > 255 ? 255 : mid);
	}
	if (pre->has_variable && *pos < code_size)
		return code[(*pos)++];
	if (*pos < code_size)
		return code[(*pos)++];
	return def;
}

// Skip a TIME prefix's trailing operand (u8-class commands only).
static void seq_skip_time_tail (const u8 *code, size_t code_size, size_t *pos,
	const seq_prefix_t *pre)
{
	if (!pre->has_time)
		return;
	if (pre->time_kind == 0xA3)
		*pos += 2;
	else if (pre->time_kind == 0xA4)
		*pos += 4;
	else
		*pos += 1;
	if (*pos > code_size)
		*pos = code_size;
}

enumError SequenceToMIDI (u8 **out_midi, size_t *out_size, const u8 *seq_data, size_t seq_size)
{
	if (!seq_data || seq_size < 4 || !out_midi)
		return ERR_INVALID_DATA;

	seq_format_t fmt = DetectSequenceFormat (seq_data, seq_size);
	bool is_le = (fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_LE || fmt == SEQ_FMT_SSEQ);

	const u8 *code = seq_data;
	size_t code_size = seq_size;

	if ((fmt == SEQ_FMT_RSEQ || fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_BE
			|| fmt == SEQ_FMT_FSEQ_LE)
		&& seq_find_blocks (seq_data, seq_size, is_le, &code, &code_size, 0, 0))
	{
		// code/code_size come straight from the block table.
	}
	else if (seq_size >= 0x20
		&& (!memcmp (seq_data, "RSEQ", 4) || !memcmp (seq_data, "CSEQ", 4)
			|| !memcmp (seq_data, "FSEQ", 4)))
	{
		u32 data_off = is_le ? read_le32 (seq_data + 0x10) : read_be32 (seq_data + 0x10);
		if (data_off + 12 <= seq_size && !memcmp (seq_data + data_off, "DATA", 4))
		{
			u32 base_off
				= is_le ? read_le32 (seq_data + data_off + 8) : read_be32 (seq_data + data_off + 8);
			u32 sec_size
				= is_le ? read_le32 (seq_data + data_off + 4) : read_be32 (seq_data + data_off + 4);
			code = seq_data + data_off + base_off;
			code_size = (sec_size > base_off) ? (sec_size - base_off) : 0;
			if (data_off + base_off + code_size > seq_size)
				code_size = seq_size - (data_off + base_off);
		}
	}
	else if (seq_size >= 0x10 && !memcmp (seq_data, "SSEQ", 4))
	{
		if (seq_size >= 0x1C && !memcmp (seq_data + 0x10, "DATA", 4))
		{
			u32 base_off = read_le32 (seq_data + 0x10 + 8);
			u32 sec_size = read_le32 (seq_data + 0x10 + 4);
			code = seq_data + 0x10 + base_off;
			code_size = (sec_size > base_off) ? (sec_size - base_off) : 0;
			if (0x10 + base_off + code_size > seq_size)
				code_size = seq_size - (0x10 + base_off);
		}
	}
	else if (!memcmp (seq_data, "DATA", 4))
	{
		u32 base_off = is_le ? read_le32 (seq_data + 8) : read_be32 (seq_data + 8);
		u32 sec_size = is_le ? read_le32 (seq_data + 4) : read_be32 (seq_data + 4);
		code = seq_data + base_off;
		code_size = (sec_size > base_off) ? (sec_size - base_off) : (seq_size - base_off);
	}

	if (code_size == 0)
		return ERR_INVALID_DATA;

	// Track targets: up to 16 tracks
	u32 track_offsets[16] = { 0 };
	uint active_tracks = 1;
	u16 ppq = 48;

	size_t pos = 0;
	while (pos < code_size)
	{
		seq_prefix_t pre;
		u8 op;
		if (!seq_consume_prefix (code, code_size, &pos, &pre, &op))
			break;
		if (op < 0x80)
		{
			if (pos < code_size)
				pos++;
			if (pre.has_random)
				pos += 4;
			else if (pre.has_variable)
				pos += 1;
			else
				read_vlq (code, code_size, &pos);
		}
		else if (op == SEQ_OP_OPEN_TRACK)
		{
			if (pos + 4 <= code_size)
			{
				u8 trk = code[pos++];
				u32 target = is_le ? read_le24 (code + pos) : read_be24 (code + pos);
				pos += 3;
				if (trk < 16)
				{
					track_offsets[trk] = target;
					if (trk + 1 > active_tracks)
						active_tracks = trk + 1;
				}
			}
		}
		else if (op == SEQ_OP_ALLOC_TRACK)
		{
			pos += 2;
		}
		else if (op == SEQ_OP_TIMEBASE)
		{
			if (pos < code_size)
			{
				if (pre.has_random && pos + 4 <= code_size)
					pos += 4;
				else if (pre.has_variable)
					pos += 1;
				else
					ppq = code[pos++];
			}
		}
		else if (op == SEQ_OP_FIN)
		{
			break;
		}
		else
		{
			// skip other opcodes in header scan
			if (op == SEQ_OP_WAIT || op == SEQ_OP_PRG)
			{
				if (pre.has_random)
					pos += 4;
				else if (pre.has_variable)
					pos += 1;
				else
					read_vlq (code, code_size, &pos);
			}
			else if (op == SEQ_OP_TEMPO || op == SEQ_OP_MOD_DELAY || op == SEQ_OP_MOD_PERIOD)
				pos += 2;
			else if (op == SEQ_OP_EX_COMMAND)
				seq_skip_ex (code, code_size, &pos, is_le);
			else if (op == SEQ_OP_JUMP || op == SEQ_OP_CALL)
				pos += 3;
			else if (op >= 0xA0 && op <= 0xDF)
				pos += 1;
			if (pre.has_time && op >= 0xB0 && op <= 0xDF)
			{
				if (pre.time_kind == 0xA3)
					pos += 2;
				else if (pre.time_kind == 0xA4)
					pos += 4;
				else
					pos += 1;
			}
			if (pos > code_size)
				pos = code_size;
		}
	}

	midi_track_build_t conductor_track = { 0 };
	midi_track_build_t *tracks = CALLOC (active_tracks, sizeof (midi_track_build_t));
	if (!tracks)
		return ERR_CANT_CREATE;

	for (uint t = 0; t < active_tracks; t++)
	{
		size_t t_pos = track_offsets[t];
		if (t_pos >= code_size)
			continue;

		u32 cur_time = 0;
		u8 ch = (u8)t;

		while (t_pos < code_size)
		{
			seq_prefix_t pre;
			u8 op;
			if (!seq_consume_prefix (code, code_size, &t_pos, &pre, &op))
				break;
			// Prefix musical simplifications (documented): IF branches are
			// always taken, TIME's extra parameter only shifts timing the
			// player resolves at runtime (ignored here), RANDOM uses the
			// midpoint of its bounds, VARIABLE is statically unknowable
			// and reads as 0. Sync (operand widths) is always exact.
			if (op < 0x80)
			{
				u8 vel = (t_pos < code_size) ? code[t_pos++] : 100;
				u32 dur;
				if (pre.has_random && t_pos + 4 <= code_size)
				{
					int16_t lo = is_le ? (int16_t)read_le16 (code + t_pos)
									   : (int16_t)read_be16 (code + t_pos);
					int16_t hi = is_le ? (int16_t)read_le16 (code + t_pos + 2)
									   : (int16_t)read_be16 (code + t_pos + 2);
					t_pos += 4;
					dur = (u32)((lo + hi) / 2 >= 0 ? (lo + hi) / 2 : 0);
				}
				else if (pre.has_variable)
				{
					if (t_pos < code_size)
						t_pos++;
					dur = 0;
				}
				else
					dur = read_vlq (code, code_size, &t_pos);
				add_midi_event (&tracks[t], cur_time, 0x90, ch, op, vel);
				add_midi_event (&tracks[t], cur_time + dur, 0x80, ch, op, 0);
			}
			else if (op == SEQ_OP_WAIT)
			{
				u32 dur;
				if (pre.has_random && t_pos + 4 <= code_size)
				{
					int16_t lo = is_le ? (int16_t)read_le16 (code + t_pos)
									   : (int16_t)read_be16 (code + t_pos);
					int16_t hi = is_le ? (int16_t)read_le16 (code + t_pos + 2)
									   : (int16_t)read_be16 (code + t_pos + 2);
					t_pos += 4;
					dur = (u32)((lo + hi) / 2 >= 0 ? (lo + hi) / 2 : 0);
				}
				else if (pre.has_variable)
				{
					if (t_pos < code_size)
						t_pos++;
					dur = 0;
				}
				else
					dur = read_vlq (code, code_size, &t_pos);
				cur_time += dur;
			}
			else if (op == SEQ_OP_PRG)
			{
				u32 prg;
				if (pre.has_random && t_pos + 4 <= code_size)
				{
					int16_t lo = is_le ? (int16_t)read_le16 (code + t_pos)
									   : (int16_t)read_be16 (code + t_pos);
					int16_t hi = is_le ? (int16_t)read_le16 (code + t_pos + 2)
									   : (int16_t)read_be16 (code + t_pos + 2);
					t_pos += 4;
					prg = (u32)((lo + hi) / 2);
				}
				else if (pre.has_variable && t_pos < code_size)
					prg = code[t_pos++];
				else
					prg = read_vlq (code, code_size, &t_pos);
				add_midi_event (&tracks[t], cur_time, 0xC0, ch, (u8)(prg & 0x7F), 0);
			}
			else if (op == SEQ_OP_VOLUME)
			{
				u8 vol = seq_midi_u8arg (code, code_size, &t_pos, is_le, &pre, 127);
				seq_skip_time_tail (code, code_size, &t_pos, &pre);
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 7, vol);
			}
			else if (op == SEQ_OP_PAN)
			{
				u8 pan = seq_midi_u8arg (code, code_size, &t_pos, is_le, &pre, 64);
				seq_skip_time_tail (code, code_size, &t_pos, &pre);
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 10, pan);
			}
			else if (op == SEQ_OP_EXPRESSION)
			{
				u8 expr = seq_midi_u8arg (code, code_size, &t_pos, is_le, &pre, 127);
				seq_skip_time_tail (code, code_size, &t_pos, &pre);
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 11, expr);
			}
			else if (op == SEQ_OP_DAMPER)
			{
				u8 dmp = seq_midi_u8arg (code, code_size, &t_pos, is_le, &pre, 0);
				seq_skip_time_tail (code, code_size, &t_pos, &pre);
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 64, dmp);
			}
			else if (op == SEQ_OP_FXSEND_A)
			{
				u8 rev = seq_midi_u8arg (code, code_size, &t_pos, is_le, &pre, 0);
				seq_skip_time_tail (code, code_size, &t_pos, &pre);
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 91, rev);
			}
			else if (op == SEQ_OP_PITCH_BEND)
			{
				int bend;
				if (pre.has_random && t_pos + 4 <= code_size)
				{
					int16_t lo = is_le ? (int16_t)read_le16 (code + t_pos)
									   : (int16_t)read_be16 (code + t_pos);
					int16_t hi = is_le ? (int16_t)read_le16 (code + t_pos + 2)
									   : (int16_t)read_be16 (code + t_pos + 2);
					t_pos += 4;
					bend = (lo + hi) / 2;
				}
				else if (pre.has_variable && t_pos < code_size)
					bend = (int)(signed char)code[t_pos++];
				else
					bend = (t_pos < code_size) ? (int)(signed char)code[t_pos++] : 0;
				seq_skip_time_tail (code, code_size, &t_pos, &pre);
				int midi_bend = 8192 + bend * 64;
				if (midi_bend < 0)
					midi_bend = 0;
				if (midi_bend > 16383)
					midi_bend = 16383;
				add_midi_event (&tracks[t], cur_time, 0xE0, ch, (u8)(midi_bend & 0x7F),
					(u8)((midi_bend >> 7) & 0x7F));
			}
			else if (op == SEQ_OP_TEMPO)
			{
				u16 tempo = 0;
				if (pre.has_random && t_pos + 4 <= code_size)
				{
					int16_t lo = is_le ? (int16_t)read_le16 (code + t_pos)
									   : (int16_t)read_be16 (code + t_pos);
					int16_t hi = is_le ? (int16_t)read_le16 (code + t_pos + 2)
									   : (int16_t)read_be16 (code + t_pos + 2);
					t_pos += 4;
					tempo = (u16)((lo + hi) / 2);
				}
				else if (pre.has_variable && t_pos < code_size)
					tempo = code[t_pos++];
				else if (t_pos + 2 <= code_size)
				{
					tempo = is_le ? read_le16 (code + t_pos) : read_be16 (code + t_pos);
					t_pos += 2;
				}
				// Add tempo on conductor track
				if (tempo > 0)
				{
					u32 us_pqn = 60000000 / tempo;
					if (conductor_track.n_events >= conductor_track.alloc_events)
					{
						conductor_track.alloc_events
							= conductor_track.alloc_events ? conductor_track.alloc_events * 2 : 128;
						conductor_track.events = REALLOC (
							conductor_track.events, conductor_track.alloc_events * sizeof (midi_event_t));
					}
					midi_event_t *me = &conductor_track.events[conductor_track.n_events++];
					me->time = cur_time;
					me->type = 0xFF; // Meta
					me->channel = 0x51; // Set Tempo
					me->data1 = 0;
					me->data2 = 0;
					me->meta_len = 3;
					me->meta_data = MALLOC (3);
					me->meta_data[0] = (u8)(us_pqn >> 16);
					me->meta_data[1] = (u8)(us_pqn >> 8);
					me->meta_data[2] = (u8)us_pqn;
				}
			}
			else if (op == SEQ_OP_FIN || op == SEQ_OP_RET)
			{
				// FIN/RET end one execution path, not necessarily the
				// track: SFX dispatch code lays subroutines out inline
				// past them (reached via jumps the linear walk below
				// already covers in layout order). Only stop when
				// nothing but zeros remains (same trailing-padding
				// rule the disassembler uses).
				bool rest_zero = true;
				for (size_t k = t_pos; k < code_size; k++)
					if (code[k] != 0)
					{
						rest_zero = false;
						break;
					}
				if (rest_zero)
					break;
			}
			else
			{
				if (op == SEQ_OP_TIMEBASE || op == SEQ_OP_ALLOC_TRACK || op == SEQ_OP_MOD_DELAY
					|| op == SEQ_OP_MOD_PERIOD)
					t_pos += 2;
				else if (op == SEQ_OP_JUMP || op == SEQ_OP_CALL || op == SEQ_OP_OPEN_TRACK)
					t_pos += 3;
				else if (op == SEQ_OP_EX_COMMAND)
				{
					size_t sk = t_pos;
					seq_skip_ex (code, code_size, &sk, is_le);
					t_pos = sk;
				}
				else if (op >= 0xA0 && op <= 0xDF)
					t_pos += 1;
			}
		}
	}

	// Always emit Standard MIDI Format 1 with Track 0 dedicated to conductor/tempo map
	struct midi_file mf;
	midi_file_init (&mf, 1, 0, ppq ? ppq : 48);

	// Write Track 0: Conductor track (tempo map and track name)
	{
		struct midi_track *mtr = midi_file_append_empty_track (&mf);
		midi_track_write_track_name (mtr, 0, "Conductor Track", 15);
		midi_track_write_time_signature (mtr, 0, 4, 2, 24, 8); // 4/4

		if (conductor_track.n_events > 1)
			qsort (
				conductor_track.events, conductor_track.n_events, sizeof (midi_event_t), compare_midi_events);

		u32 last_time = 0;
		for (uint i = 0; i < conductor_track.n_events; i++)
		{
			const midi_event_t *e = &conductor_track.events[i];
			u32 delta = (e->time >= last_time) ? (e->time - last_time) : 0;
			last_time = e->time;
			midi_track_write_meta_event_buf (
				mtr, delta, e->channel, (uint8_t)e->meta_len, e->meta_data);
		}
		midi_track_write_track_end (mtr, 0);
	}

	// Write tracks 1..N: Musical sequence tracks
	for (uint t = 0; t < active_tracks; t++)
	{
		struct midi_track *mtr = midi_file_append_empty_track (&mf);
		char trk_name[32];
		snprintf (trk_name, sizeof (trk_name), "Track %u", t);
		midi_track_write_track_name (mtr, 0, trk_name, (int)strlen (trk_name));

		if (tracks[t].n_events > 1)
			qsort (
				tracks[t].events, tracks[t].n_events, sizeof (midi_event_t), compare_midi_events);

		u32 last_time = 0;
		for (uint i = 0; i < tracks[t].n_events; i++)
		{
			const midi_event_t *e = &tracks[t].events[i];
			u32 delta = (e->time >= last_time) ? (e->time - last_time) : 0;
			last_time = e->time;

			if (e->type == 0xFF) // Meta event
			{
				midi_track_write_meta_event_buf (
					mtr, delta, e->channel, (uint8_t)e->meta_len, e->meta_data);
			}
			else if ((e->type & 0xF0) == 0xC0) // Program change
			{
				midi_track_write_program_change (mtr, delta, e->channel & 0x0F, e->data1);
			}
			else if ((e->type & 0xF0) == 0xB0) // Control Change
			{
				midi_track_write_control_change (mtr, delta, e->channel & 0x0F, e->data1, e->data2);
			}
			else if ((e->type & 0xF0) == 0xE0) // Pitch bend
			{
				midi_track_write_pitch_bend (
					mtr, delta, e->channel & 0x0F, ((uint16_t)e->data2 << 7) | e->data1);
			}
			else if ((e->type & 0xF0) == 0x90) // Note On
			{
				midi_track_write_note_on (mtr, delta, e->channel & 0x0F, e->data1, e->data2);
			}
			else if ((e->type & 0xF0) == 0x80) // Note Off
			{
				midi_track_write_note_off (mtr, delta, e->channel & 0x0F, e->data1, e->data2);
			}
		}
		midi_track_write_track_end (mtr, 0);
	}

	for (uint i = 0; i < conductor_track.n_events; i++)
		FREE (conductor_track.events[i].meta_data);
	FREE (conductor_track.events);

	for (uint t = 0; t < active_tracks; t++)
	{
		for (uint i = 0; i < tracks[t].n_events; i++)
			FREE (tracks[t].events[i].meta_data);
		FREE (tracks[t].events);
	}
	FREE (tracks);

	struct buffer buf;
	buffer_init (&buf);

	midi_file_write (&mf, midi_buf_write_cb, &buf);
	midi_file_clear (&mf);

	u8 *midi_copy = MALLOC (buf.data_len);
	if (midi_copy)
		memcpy (midi_copy, buf.data, buf.data_len);
	size_t final_size = buf.data_len;
	buffer_destroy (&buf);

	*out_midi = midi_copy;
	if (out_size)
		*out_size = final_size;
	return ERR_OK;
}

// Convert MIDI file to binary sequence

struct my_midi_reader
{
	struct midi_reader reader;
	char *txt;
	size_t txt_len;
	size_t txt_cap;
	u32 pending_wait;
	double time_scale;
	int track_index;
};

static void txt_append (struct my_midi_reader *my, const char *fmt, ...)
{
	if (my->txt_len + 256 >= my->txt_cap)
	{
		my->txt_cap *= 2;
		my->txt = REALLOC (my->txt, my->txt_cap);
	}
	va_list args;
	va_start (args, fmt);
	my->txt_len += vsnprintf (my->txt + my->txt_len, my->txt_cap - my->txt_len, fmt, args);
	va_end (args);
}

static void my_handle_track (struct midi_reader *h, int number, int length)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my->track_index = number;
	if (number > 0)
	{
		txt_append (my, "\n@Track%u:\n", number);
	}
	my->pending_wait = 0;
}

static void my_add_wait (struct my_midi_reader *my, int duration)
{
	u32 scaled_delta = (u32)round ((double)duration * my->time_scale);
	my->pending_wait += scaled_delta;
}

static void my_flush_wait (struct my_midi_reader *my)
{
	if (my->pending_wait > 0)
	{
		txt_append (my, "    wait %u\n", my->pending_wait);
		my->pending_wait = 0;
	}
}

static void my_handle_tempo (struct midi_reader *h, int duration, uint32_t tempo)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
	if (tempo > 0)
	{
		u32 bpm = 60000000 / tempo;
		my_flush_wait (my);
		txt_append (my, "    tempo %u\n", bpm);
	}
}

static void my_handle_note_on (
	struct midi_reader *h, uint8_t channel, int duration, int note, int vel)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
	if (vel > 0)
	{
		my_flush_wait (my);
		char nstr[16];
		pitch_to_name (nstr, sizeof (nstr), note);
		txt_append (my, "    note %s %u 48\n", nstr, vel);
	}
}

static void my_handle_note_off (
	struct midi_reader *h, uint8_t channel, int duration, int note, int vel)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
}

static void my_handle_program_change (
	struct midi_reader *h, uint8_t channel, int duration, int program)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
	my_flush_wait (my);
	txt_append (my, "    prg %u\n", program);
}

static void my_handle_control_change (
	struct midi_reader *h, uint8_t channel, int duration, int controller, int value)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
	if (controller == 7)
	{
		my_flush_wait (my);
		txt_append (my, "    vol %u\n", value);
	}
	else if (controller == 10)
	{
		my_flush_wait (my);
		txt_append (my, "    pan %u\n", value);
	}
	else if (controller == 11)
	{
		my_flush_wait (my);
		txt_append (my, "    expr %u\n", value);
	}
	else if (controller == 64)
	{
		my_flush_wait (my);
		txt_append (my, "    dmp %u\n", value);
	}
	else if (controller == 91)
	{
		my_flush_wait (my);
		txt_append (my, "    rev %u\n", value);
	}
}

static void my_handle_pitch_wheel_change (
	struct midi_reader *h, uint8_t channel, uint32_t duration, uint16_t value)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
	int bend = ((int)value - 8192) / 64;
	if (bend < -128)
		bend = -128;
	if (bend > 127)
		bend = 127;
	my_flush_wait (my);
	txt_append (my, "    bend %d\n", bend);
}

static void my_handle_track_end (struct midi_reader *h, int duration)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
	my_flush_wait (my);
	txt_append (my, "    fin\n");
}

static void my_handle_meta_event (
	struct midi_reader *h, int duration, int cmd, int len, uint8_t *data)
{
	struct my_midi_reader *my = (struct my_midi_reader *)h;
	my_add_wait (my, duration);
}

enumError SequenceFromMIDI (
	u8 **out_seq, size_t *out_size, const u8 *midi_data, size_t midi_size, seq_format_t target_fmt)
{
	struct buffer buf;
	buf.data = (uint8_t *)midi_data;
	buf.data_len = midi_size;
	buf.allocated_len = midi_size;

	struct mem_stream mstream;
	mem_stream_init (&mstream, &buf);

	struct my_midi_reader my;
	midi_reader_init (&my.reader);
	my.reader.handle_track = my_handle_track;
	my.reader.handle_tempo = my_handle_tempo;
	my.reader.handle_note_on = my_handle_note_on;
	my.reader.handle_note_off = my_handle_note_off;
	my.reader.handle_program_change = my_handle_program_change;
	my.reader.handle_control_change = my_handle_control_change;
	my.reader.handle_pitch_wheel_change = my_handle_pitch_wheel_change;
	my.reader.handle_track_end = my_handle_track_end;
	my.reader.handle_meta_event = my_handle_meta_event;

	my.txt_cap = 65536;
	my.txt = MALLOC (my.txt_cap);
	if (!my.txt)
		return ERR_CANT_CREATE;
	my.txt_len = 0;
	my.pending_wait = 0;
	my.time_scale = 1.0;

	if (midi_reader_load (&my.reader, &mstream.stream) != 0)
	{
		FREE (my.txt);
		return ERR_INVALID_DATA;
	}

	if (my.reader.ticks_per_quarter_note == 0)
		my.reader.ticks_per_quarter_note = 48;
	my.time_scale = 48.0 / (double)my.reader.ticks_per_quarter_note;

	txt_append (&my, "; Converted from Standard MIDI File\ntimebase 48\n");

	u16 num_tracks = my.reader.num_tracks;
	u16 track_mask = 0;
	for (uint t = 0; t < num_tracks && t < 16; t++)
		track_mask |= (1 << t);

	txt_append (&my, "alloc_track 0x%04X\n", track_mask);
	for (uint t = 1; t < num_tracks && t < 16; t++)
	{
		txt_append (&my, "open_track %u @Track%u\n", t, t);
	}

	for (int i = 0; i < my.reader.num_tracks; i++)
	{
		midi_reader_read_track (&my.reader, i);
	}

	txt_append (&my, "\n");

	enumError err = AssembleSequence (out_seq, out_size, my.txt, target_fmt);
	FREE (my.txt);
	return err;
}

// Invert notes in sequence
enumError InvertSequence (
	u8 **out_data, size_t *out_size, const u8 *seq_data, size_t seq_size, int center_note)
{
	if (!seq_data || seq_size < 4 || !out_data)
		return ERR_INVALID_DATA;

	u8 *buf = MALLOC (seq_size);
	if (!buf)
		return ERR_CANT_CREATE;
	memcpy (buf, seq_data, seq_size);

	seq_format_t fmt = DetectSequenceFormat (seq_data, seq_size);
	bool is_le = (fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_LE || fmt == SEQ_FMT_SSEQ);

	u8 *code = buf;
	size_t code_size = seq_size;

	if (seq_size >= 0x20
		&& (!memcmp (buf, "RSEQ", 4) || !memcmp (buf, "CSEQ", 4) || !memcmp (buf, "FSEQ", 4)))
	{
		u32 data_off = is_le ? read_le32 (buf + 0x10) : read_be32 (buf + 0x10);
		if (data_off + 12 <= seq_size && !memcmp (buf + data_off, "DATA", 4))
		{
			u32 base_off = is_le ? read_le32 (buf + data_off + 8) : read_be32 (buf + data_off + 8);
			u32 sec_size = is_le ? read_le32 (buf + data_off + 4) : read_be32 (buf + data_off + 4);
			code = buf + data_off + base_off;
			code_size = (sec_size > base_off) ? (sec_size - base_off) : 0;
			if (data_off + base_off + code_size > seq_size)
				code_size = seq_size - (data_off + base_off);
		}
	}
	else if (seq_size >= 0x10 && !memcmp (buf, "SSEQ", 4))
	{
		if (seq_size >= 0x1C && !memcmp (buf + 0x10, "DATA", 4))
		{
			u32 base_off = read_le32 (buf + 0x10 + 8);
			u32 sec_size = read_le32 (buf + 0x10 + 4);
			code = buf + 0x10 + base_off;
			code_size = (sec_size > base_off) ? (sec_size - base_off) : 0;
			if (0x10 + base_off + code_size > seq_size)
				code_size = seq_size - (0x10 + base_off);
		}
	}

	size_t pos = 0;
	while (pos < code_size)
	{
		u8 op = code[pos];
		if (op < 0x80)
		{
			int inverted = 2 * center_note - (int)op;
			if (inverted < 0)
				inverted = 0;
			if (inverted > 127)
				inverted = 127;
			code[pos++] = (u8)inverted;

			if (pos < code_size)
				pos++; // velocity
			read_vlq (code, code_size, &pos); // duration
		}
		else
		{
			pos++;
			switch (op)
			{
				case SEQ_OP_WAIT:
				case SEQ_OP_PRG:
					read_vlq (code, code_size, &pos);
					break;
				case SEQ_OP_OPEN_TRACK:
					pos += 4;
					break;
				case SEQ_OP_JUMP:
				case SEQ_OP_CALL:
					pos += 3;
					break;
				case SEQ_OP_RANDOM:
					pos += 4;
					break;
				case SEQ_OP_VARIABLE:
					pos += 3;
					break;
				case SEQ_OP_TIMEBASE:
				case SEQ_OP_ALLOC_TRACK:
				case SEQ_OP_MOD_DELAY:
				case SEQ_OP_TEMPO:
				case SEQ_OP_SWEEP_PITCH:
					pos += 2;
					break;
				case SEQ_OP_FIN:
				case SEQ_OP_LOOP_END:
				case SEQ_OP_RET:
					break;
				default:
					if (op >= 0xA0 && op <= 0xDF)
						pos += 1;
					break;
			}
		}
	}

	*out_data = buf;
	if (out_size)
		*out_size = seq_size;
	return ERR_OK;
}
