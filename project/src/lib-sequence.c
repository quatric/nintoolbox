#include "lib-sequence.h"
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
	if (!data || size < 16)
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

enumError DisassembleSequence (char **out_text, size_t *out_size, const u8 *data, size_t size)
{
	if (!data || size < 12 || !out_text)
		return ERR_INVALID_DATA;

	seq_format_t fmt = DetectSequenceFormat (data, size);
	bool is_le = (fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_LE || fmt == SEQ_FMT_SSEQ);

	const u8 *code = data;
	size_t code_size = size;

	if (size >= 0x20
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

	size_t pos = 0;
	while (pos < code_size)
	{
		u8 op = code[pos++];
		if (op < 0x80)
		{
			if (pos < code_size)
				pos++; // velocity
			read_vlq (code, code_size, &pos); // duration
		}
		else
		{
			switch (op)
			{
				case SEQ_OP_WAIT:
				case SEQ_OP_PRG:
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
		"; Format: %s\n\n",
		GetSequenceFormatName (fmt));

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

		u8 op = code[pos++];
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

		// Strip comments
		char *cmt = strchr (s, ';');
		if (cmt)
			*cmt = 0;
		cmt = strchr (s, '#');
		if (cmt)
			*cmt = 0;

		// Trim trailing whitespace
		size_t slen = strlen (s);
		while (slen > 0 && (s[slen - 1] == ' ' || s[slen - 1] == '\t'))
		{
			s[--slen] = 0;
		}
		if (!*s)
			continue;

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
		int n_args = sscanf (s, "%63s %63s %63s %63s", cmd, arg1, arg2, arg3);
		if (n_args < 1)
			continue;

		if (code_len + 64 >= code_cap)
		{
			code_cap *= 2;
			code = REALLOC (code, code_cap);
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

	if (target_fmt == SEQ_FMT_SSEQ)
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
	else
	{
		// RSEQ, CSEQ, FSEQ: 0x20 Header + 0x0C DATA header + code
		const char *magic = (target_fmt == SEQ_FMT_RSEQ) ? "RSEQ"
			: (target_fmt == SEQ_FMT_CSEQ)				 ? "CSEQ"
														 : "FSEQ";
		u16 ver = (target_fmt == SEQ_FMT_RSEQ) ? 0x0100
			: (target_fmt == SEQ_FMT_CSEQ)	   ? 0x0200
											   : 0x0001;

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

	FREE (code);
	*out_data = out;
	if (out_size)
		*out_size = total_size;
	return ERR_OK;
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

enumError SequenceToMIDI (u8 **out_midi, size_t *out_size, const u8 *seq_data, size_t seq_size)
{
	if (!seq_data || seq_size < 12 || !out_midi)
		return ERR_INVALID_DATA;

	seq_format_t fmt = DetectSequenceFormat (seq_data, seq_size);
	bool is_le = (fmt == SEQ_FMT_CSEQ || fmt == SEQ_FMT_FSEQ_LE || fmt == SEQ_FMT_SSEQ);

	const u8 *code = seq_data;
	size_t code_size = seq_size;

	if (seq_size >= 0x20
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

	size_t pos = 0;
	while (pos < code_size)
	{
		u8 op = code[pos++];
		if (op < 0x80)
		{
			if (pos < code_size)
				pos++;
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
		else if (op == SEQ_OP_FIN)
		{
			break;
		}
		else
		{
			// skip other opcodes in header scan
			if (op == SEQ_OP_WAIT || op == SEQ_OP_PRG)
				read_vlq (code, code_size, &pos);
			else if (op == SEQ_OP_TEMPO || op == SEQ_OP_TIMEBASE || op == SEQ_OP_MOD_DELAY)
				pos += 2;
			else if (op == SEQ_OP_JUMP || op == SEQ_OP_CALL)
				pos += 3;
			else if (op >= 0xA0 && op <= 0xDF)
				pos += 1;
		}
	}

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
			u8 op = code[t_pos++];
			if (op < 0x80)
			{
				u8 vel = (t_pos < code_size) ? code[t_pos++] : 100;
				u32 dur = read_vlq (code, code_size, &t_pos);
				add_midi_event (&tracks[t], cur_time, 0x90, ch, op, vel);
				add_midi_event (&tracks[t], cur_time + dur, 0x80, ch, op, 0);
			}
			else if (op == SEQ_OP_WAIT)
			{
				u32 dur = read_vlq (code, code_size, &t_pos);
				cur_time += dur;
			}
			else if (op == SEQ_OP_PRG)
			{
				u32 prg = read_vlq (code, code_size, &t_pos);
				add_midi_event (&tracks[t], cur_time, 0xC0, ch, (u8)(prg & 0x7F), 0);
			}
			else if (op == SEQ_OP_VOLUME)
			{
				u8 vol = (t_pos < code_size) ? code[t_pos++] : 127;
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 7, vol);
			}
			else if (op == SEQ_OP_PAN)
			{
				u8 pan = (t_pos < code_size) ? code[t_pos++] : 64;
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 10, pan);
			}
			else if (op == SEQ_OP_EXPRESSION)
			{
				u8 expr = (t_pos < code_size) ? code[t_pos++] : 127;
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 11, expr);
			}
			else if (op == SEQ_OP_DAMPER)
			{
				u8 dmp = (t_pos < code_size) ? code[t_pos++] : 0;
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 64, dmp);
			}
			else if (op == SEQ_OP_FXSEND_A)
			{
				u8 rev = (t_pos < code_size) ? code[t_pos++] : 0;
				add_midi_event (&tracks[t], cur_time, 0xB0, ch, 91, rev);
			}
			else if (op == SEQ_OP_PITCH_BEND)
			{
				int bend = (t_pos < code_size) ? (int)(signed char)code[t_pos++] : 0;
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
				if (t_pos + 2 <= code_size)
				{
					u16 tempo = is_le ? read_le16 (code + t_pos) : read_be16 (code + t_pos);
					t_pos += 2;
					// Add tempo on track 0
					if (tempo > 0)
					{
						u32 us_pqn = 60000000 / tempo;
						if (tracks[0].n_events >= tracks[0].alloc_events)
						{
							tracks[0].alloc_events
								= tracks[0].alloc_events ? tracks[0].alloc_events * 2 : 128;
							tracks[0].events = REALLOC (
								tracks[0].events, tracks[0].alloc_events * sizeof (midi_event_t));
						}
						midi_event_t *me = &tracks[0].events[tracks[0].n_events++];
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
			}
			else if (op == SEQ_OP_FIN || op == SEQ_OP_RET)
			{
				break;
			}
			else
			{
				if (op == SEQ_OP_TIMEBASE || op == SEQ_OP_ALLOC_TRACK || op == SEQ_OP_MOD_DELAY)
					t_pos += 2;
				else if (op == SEQ_OP_JUMP || op == SEQ_OP_CALL || op == SEQ_OP_OPEN_TRACK)
					t_pos += 3;
				else if (op >= 0xA0 && op <= 0xDF)
					t_pos += 1;
			}
		}
	}

	// Sort events in each track and compute delta times
	size_t midi_cap = 65536;
	u8 *midi = MALLOC (midi_cap);
	if (!midi)
	{
		for (uint t = 0; t < active_tracks; t++)
		{
			for (uint i = 0; i < tracks[t].n_events; i++)
				FREE (tracks[t].events[i].meta_data);
			FREE (tracks[t].events);
		}
		FREE (tracks);
		return ERR_CANT_CREATE;
	}

	// MThd Header
	memcpy (midi, "MThd", 4);
	write_be32 (midi + 4, 6);
	write_be16 (midi + 8, (active_tracks > 1) ? 1 : 0); // format 1 or 0
	write_be16 (midi + 10, (u16)active_tracks);
	write_be16 (midi + 12, 48); // 48 PPQN

	size_t midi_pos = 14;

	for (uint t = 0; t < active_tracks; t++)
	{
		if (tracks[t].n_events > 1)
			qsort (
				tracks[t].events, tracks[t].n_events, sizeof (midi_event_t), compare_midi_events);

		size_t trk_head_pos = midi_pos;
		if (midi_pos + 8 >= midi_cap)
		{
			midi_cap *= 2;
			midi = REALLOC (midi, midi_cap);
		}
		memcpy (midi + midi_pos, "MTrk", 4);
		midi_pos += 8;

		size_t trk_data_start = midi_pos;
		u32 last_time = 0;

		for (uint i = 0; i < tracks[t].n_events; i++)
		{
			const midi_event_t *e = &tracks[t].events[i];
			u32 delta = (e->time >= last_time) ? (e->time - last_time) : 0;
			last_time = e->time;

			if (midi_pos + 32 >= midi_cap)
			{
				midi_cap *= 2;
				midi = REALLOC (midi, midi_cap);
			}

			write_vlq (midi, &midi_pos, delta);

			if (e->type == 0xFF) // Meta event
			{
				midi[midi_pos++] = 0xFF;
				midi[midi_pos++] = e->channel; // meta event type
				write_vlq (midi, &midi_pos, e->meta_len);
				if (e->meta_len && e->meta_data)
					memcpy (midi + midi_pos, e->meta_data, e->meta_len);
				midi_pos += e->meta_len;
			}
			else if ((e->type & 0xF0) == 0xC0) // Program change (1 data byte)
			{
				midi[midi_pos++] = (u8)(e->type | (e->channel & 0x0F));
				midi[midi_pos++] = e->data1;
			}
			else // 2 data bytes
			{
				midi[midi_pos++] = (u8)(e->type | (e->channel & 0x0F));
				midi[midi_pos++] = e->data1;
				midi[midi_pos++] = e->data2;
			}
		}

		// End of Track meta event: delta 0, FF 2F 00
		if (midi_pos + 8 >= midi_cap)
		{
			midi_cap *= 2;
			midi = REALLOC (midi, midi_cap);
		}
		midi[midi_pos++] = 0x00;
		midi[midi_pos++] = 0xFF;
		midi[midi_pos++] = 0x2F;
		midi[midi_pos++] = 0x00;

		u32 trk_len = (u32)(midi_pos - trk_data_start);
		write_be32 (midi + trk_head_pos + 4, trk_len);
	}

	for (uint t = 0; t < active_tracks; t++)
	{
		for (uint i = 0; i < tracks[t].n_events; i++)
			FREE (tracks[t].events[i].meta_data);
		FREE (tracks[t].events);
	}
	FREE (tracks);

	*out_midi = midi;
	if (out_size)
		*out_size = midi_pos;
	return ERR_OK;
}

// Convert MIDI file to binary sequence
enumError SequenceFromMIDI (
	u8 **out_seq, size_t *out_size, const u8 *midi_data, size_t midi_size, seq_format_t target_fmt)
{
	if (!midi_data || midi_size < 14 || memcmp (midi_data, "MThd", 4) != 0)
		return ERR_INVALID_DATA;

	u16 num_tracks = read_be16 (midi_data + 10);
	u16 ppqn = read_be16 (midi_data + 12);
	if (ppqn == 0)
		ppqn = 48;

	double time_scale = 48.0 / (double)ppqn;

	// Disassemble each track into an MML text buffer, then AssembleSequence
	size_t txt_cap = 65536;
	char *txt = MALLOC (txt_cap);
	if (!txt)
		return ERR_CANT_CREATE;
	size_t txt_len = 0;

	txt_len += snprintf (txt + txt_len, txt_cap - txt_len,
		"; Converted from Standard MIDI File\n"
		"timebase 48\n");

	// Track pointers and allocation
	u16 track_mask = 0;
	for (uint t = 0; t < num_tracks && t < 16; t++)
		track_mask |= (1 << t);

	txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "alloc_track 0x%04X\n", track_mask);
	for (uint t = 1; t < num_tracks && t < 16; t++)
	{
		txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "open_track %u @Track%u\n", t, t);
	}

	size_t pos = 14 + read_be32 (midi_data + 4) - 6; // start of first track chunk

	for (uint t = 0; t < num_tracks && pos + 8 <= midi_size; t++)
	{
		if (memcmp (midi_data + pos, "MTrk", 4) != 0)
			break;
		u32 trk_size = read_be32 (midi_data + pos + 4);
		pos += 8;

		size_t trk_end = pos + trk_size;
		if (trk_end > midi_size)
			trk_end = midi_size;

		if (t > 0)
		{
			txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "\n@Track%u:\n", t);
		}

		u8 running_status = 0;
		u32 pending_wait = 0;

		while (pos < trk_end)
		{
			u32 delta = read_vlq (midi_data, trk_end, &pos);
			u32 scaled_delta = (u32)round ((double)delta * time_scale);
			pending_wait += scaled_delta;

			if (pos >= trk_end)
				break;
			u8 status = midi_data[pos];
			if (status & 0x80)
			{
				running_status = status;
				pos++;
			}
			else
			{
				status = running_status;
			}

			if (txt_len + 256 >= txt_cap)
			{
				txt_cap *= 2;
				txt = REALLOC (txt, txt_cap);
			}

			if (status == 0xFF) // Meta event
			{
				if (pos >= trk_end)
					break;
				u8 meta_type = midi_data[pos++];
				u32 meta_len = read_vlq (midi_data, trk_end, &pos);
				if (meta_type == 0x51 && meta_len >= 3 && pos + 3 <= trk_end)
				{
					u32 us_pqn = read_be24 (midi_data + pos);
					if (us_pqn > 0)
					{
						u32 bpm = 60000000 / us_pqn;
						if (pending_wait > 0)
						{
							txt_len += snprintf (
								txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
							pending_wait = 0;
						}
						txt_len
							+= snprintf (txt + txt_len, txt_cap - txt_len, "    tempo %u\n", bpm);
					}
				}
				pos += meta_len;
			}
			else if ((status & 0xF0) == 0x90) // Note On
			{
				if (pos + 2 > trk_end)
					break;
				u8 pitch = midi_data[pos++];
				u8 vel = midi_data[pos++];
				if (vel > 0)
				{
					if (pending_wait > 0)
					{
						txt_len += snprintf (
							txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
						pending_wait = 0;
					}
					char nstr[16];
					pitch_to_name (nstr, sizeof (nstr), pitch);
					txt_len += snprintf (
						txt + txt_len, txt_cap - txt_len, "    note %s %u 48\n", nstr, vel);
				}
			}
			else if ((status & 0xF0) == 0x80) // Note Off
			{
				if (pos + 2 > trk_end)
					break;
				pos += 2;
			}
			else if ((status & 0xF0) == 0xC0) // Program Change
			{
				if (pos + 1 > trk_end)
					break;
				u8 prg = midi_data[pos++];
				if (pending_wait > 0)
				{
					txt_len += snprintf (
						txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
					pending_wait = 0;
				}
				txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "    prg %u\n", prg);
			}
			else if ((status & 0xF0) == 0xB0) // Control Change
			{
				if (pos + 2 > trk_end)
					break;
				u8 cc = midi_data[pos++];
				u8 val = midi_data[pos++];
				if (cc == 7)
				{
					if (pending_wait > 0)
					{
						txt_len += snprintf (
							txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
						pending_wait = 0;
					}
					txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "    vol %u\n", val);
				}
				else if (cc == 10)
				{
					if (pending_wait > 0)
					{
						txt_len += snprintf (
							txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
						pending_wait = 0;
					}
					txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "    pan %u\n", val);
				}
			}
			else if ((status & 0xF0) == 0xE0) // Pitch Bend
			{
				if (pos + 2 > trk_end)
					break;
				u8 lsb = midi_data[pos++];
				u8 msb = midi_data[pos++];
				int pb = (int)((msb << 7) | lsb) - 8192;
				int bend = pb / 64;
				if (pending_wait > 0)
				{
					txt_len += snprintf (
						txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
					pending_wait = 0;
				}
				txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "    bend %d\n", bend);
			}
			else
			{
				// other MIDI commands
				if ((status & 0xF0) == 0xD0)
					pos += 1;
				else
					pos += 2;
			}
		}

		if (pending_wait > 0)
		{
			txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "    wait %u\n", pending_wait);
		}
		txt_len += snprintf (txt + txt_len, txt_cap - txt_len, "    fin\n");
		pos = trk_end;
	}

	enumError err = AssembleSequence (out_seq, out_size, txt, target_fmt);
	FREE (txt);
	return err;
}

// Invert notes in sequence
enumError InvertSequence (
	u8 **out_data, size_t *out_size, const u8 *seq_data, size_t seq_size, int center_note)
{
	if (!seq_data || seq_size < 12 || !out_data)
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
