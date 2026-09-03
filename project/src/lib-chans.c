/*
 * Nintendo Wii ChannelScript (.cs / RCHE) format support
 *
 * Implements decoding, full opcode evaluation, disassembly, and
 * ECMAScript decompilation for Wii System Menu and WiiConnect24 channel scripts.
 */

#include "lib-chans.h"
#include "lib-std.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// String buffer for clean formatting
typedef struct chans_sb_t
{
	char *buf;
	size_t len;
	size_t cap;
} chans_sb_t;

static void csb_init (chans_sb_t *s)
{
	s->cap = 8192;
	s->len = 0;
	s->buf = MALLOC(s->cap);
	if (s->buf) s->buf[0] = 0;
}

static void csb_grow (chans_sb_t *s, size_t extra)
{
	if (!s->buf) return;
	if (s->len + extra + 1 <= s->cap) return;
	size_t nc = s->cap * 2 + extra + 128;
	char *nb = REALLOC(s->buf, nc);
	if (!nb) return;
	s->buf = nb;
	s->cap = nc;
}

static void csb_printf (chans_sb_t *s, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

static void csb_printf (chans_sb_t *s, const char *fmt, ...)
{
	if (!s->buf) return;
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0) { va_end(ap2); return; }
	csb_grow(s, (size_t)need);
	if (s->buf)
	{
		vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap2);
		s->len += (size_t)need;
	}
	va_end(ap2);
}

bool IsChannelScript (const void *data, size_t size)
{
	if (!data || size < 0x20)
		return false;

	const u8 *p = (const u8 *)data;
	return memcmp(p, CHANS_MAGIC, 4) == 0;
}

void ResetChannelScript (chans_script_t *cs)
{
	if (!cs)
		return;

	if (cs->bytecode)
		FREE(cs->bytecode);
	if (cs->methods)
		FREE(cs->methods);

	if (cs->imported)
	{
		for (u32 i = 0; i < cs->imported_count; i++)
		{
			if (cs->imported[i].name)
				FREE(cs->imported[i].name);
		}
		FREE(cs->imported);
	}

	if (cs->strings)
	{
		for (u32 i = 0; i < cs->string_count; i++)
		{
			if (cs->strings[i].utf8)
				FREE(cs->strings[i].utf8);
		}
		FREE(cs->strings);
	}

	if (cs->exported)
	{
		for (u32 i = 0; i < cs->exported_count; i++)
		{
			if (cs->exported[i].name)
				FREE(cs->exported[i].name);
		}
		FREE(cs->exported);
	}

	if (cs->blocks)
		FREE(cs->blocks);

	memset(cs, 0, sizeof(*cs));
}

// Convert UTF-16BE bytes to UTF-8 null-terminated string
static char *DecodeUtf16be (const u8 *src, size_t byte_len)
{
	// Maximum UTF-8 size for 2-byte BMP is 3 bytes per code unit + 1 null
	size_t max_utf8 = (byte_len / 2) * 3 + 4;
	char *out = MALLOC(max_utf8);
	if (!out)
		return NULL;

	char *dst = out;
	for (size_t i = 0; i + 1 < byte_len; i += 2)
	{
		u16 ch = (src[i] << 8) | src[i + 1];
		if (ch == 0)
			break;
		if (ch < 0x80)
		{
			*dst++ = (char)ch;
		}
		else if (ch < 0x800)
		{
			*dst++ = (char)(0xC0 | (ch >> 6));
			*dst++ = (char)(0x80 | (ch & 0x3F));
		}
		else
		{
			*dst++ = (char)(0xE0 | (ch >> 12));
			*dst++ = (char)(0x80 | ((ch >> 6) & 0x3F));
			*dst++ = (char)(0x80 | (ch & 0x3F));
		}
	}
	*dst = '\0';
	return out;
}

enumError ScanChannelScript (chans_script_t *cs, const void *data, size_t size)
{
	if (!cs || !data || size < 0x80)
		return ERR_INVALID_DATA;

	memset(cs, 0, sizeof(*cs));
	const u8 *p = (const u8 *)data;

	if (memcmp(p, CHANS_MAGIC, 4) != 0)
		return ERR_INVALID_DATA;

	cs->version = be32((p + 4));
	cs->file_size = be32((p + 8));

	u32 fds_size = be32((p + 0x2C));
	u32 fds_offset = be32((p + 0x30));
	u32 table4_count = be32((p + 0x34));
	u32 table1_count = be32((p + 0x40));
	u32 table1_offset = be32((p + 0x44));
	u32 table2_count = be32((p + 0x48));
	u32 table2_offset = be32((p + 0x4C));
	u32 table3_count = be32((p + 0x50));
	u32 table3_offset = be32((p + 0x54));
	u32 table4_offset = be32((p + 0x60));
	u32 table5_offset = be32((p + 0x64));

	// All section offsets are from 0x20
	u32 base = 0x20;

	// Load bytecode (FDS)
	if (fds_size > 0 && base + fds_offset + fds_size <= size)
	{
		cs->bytecode_size = fds_size;
		cs->bytecode = MALLOC(fds_size);
		if (cs->bytecode)
			memcpy(cs->bytecode, p + base + fds_offset, fds_size);
	}

	// Table 1: Local methods
	if (table1_count > 0 && base + table1_offset + table1_count * 8 <= size)
	{
		cs->method_count = table1_count;
		cs->methods = MALLOC(table1_count * sizeof(chans_method_t));
		if (cs->methods)
		{
			const u8 *t1 = p + base + table1_offset;
			for (u32 i = 0; i < table1_count; i++)
			{
				cs->methods[i].offset = be32((t1 + i * 8));
				cs->methods[i].symbol_id = be16((t1 + i * 8 + 4));
				cs->methods[i].param_count = t1[i * 8 + 6];
				cs->methods[i].temp_count = t1[i * 8 + 7];
			}
		}
	}

	// Table 2: Imported symbols
	if (table2_count > 0 && base + table2_offset + table2_count * 4 <= size)
	{
		cs->imported_count = table2_count;
		cs->imported = MALLOC(table2_count * sizeof(chans_symbol_t));
		if (cs->imported)
		{
			const u8 *t2 = p + base + table2_offset;
			for (u32 i = 0; i < table2_count; i++)
			{
				cs->imported[i].length = t2[i * 4];
				cs->imported[i].padding = t2[i * 4 + 1];
				cs->imported[i].offset = be16((t2 + i * 4 + 2));
				cs->imported[i].name = NULL;

				if (cs->imported[i].length > 0 &&
					base + table2_offset + cs->imported[i].offset + cs->imported[i].length <= size)
				{
					cs->imported[i].name = MALLOC(cs->imported[i].length + 1);
					if (cs->imported[i].name)
					{
						memcpy(cs->imported[i].name,
							   p + base + table2_offset + cs->imported[i].offset,
							   cs->imported[i].length);
						cs->imported[i].name[cs->imported[i].length] = '\0';
					}
				}
			}
		}
	}

	// Table 3: String literals (UTF-16BE)
	if (table3_count > 0 && base + table3_offset < size)
	{
		cs->string_count = table3_count;
		cs->strings = MALLOC(table3_count * sizeof(chans_string_t));
		if (cs->strings)
		{
			const u8 *cur = p + base + table3_offset;
			for (u32 i = 0; i < table3_count; i++)
			{
				if (cur + 2 > p + size)
					break;
				u16 slen = be16(cur);
				cur += 2;
				cs->strings[i].byte_len = slen;
				if (cur + slen <= p + size)
				{
					cs->strings[i].utf8 = DecodeUtf16be(cur, slen);
					cur += slen;
				}
				else
				{
					cs->strings[i].utf8 = NULL;
				}
			}
		}
	}

	// Table 4: Exported symbols
	if (table4_count > 0 && base + table4_offset + table4_count * 4 <= size)
	{
		cs->exported_count = table4_count;
		cs->exported = MALLOC(table4_count * sizeof(chans_symbol_t));
		if (cs->exported)
		{
			const u8 *t4 = p + base + table4_offset;
			for (u32 i = 0; i < table4_count; i++)
			{
				cs->exported[i].length = t4[i * 4];
				cs->exported[i].padding = t4[i * 4 + 1];
				cs->exported[i].offset = be16((t4 + i * 4 + 2));
				cs->exported[i].name = NULL;

				if (cs->exported[i].length > 0 &&
					base + table4_offset + cs->exported[i].offset + cs->exported[i].length <= size)
				{
					cs->exported[i].name = MALLOC(cs->exported[i].length + 1);
					if (cs->exported[i].name)
					{
						memcpy(cs->exported[i].name,
							   p + base + table4_offset + cs->exported[i].offset,
							   cs->exported[i].length);
						cs->exported[i].name[cs->exported[i].length] = '\0';
					}
				}
			}
		}
	}

	// Table 5: Line start bitmasks
	u32 block_count = (fds_size + 255) / 256;
	if (block_count > 0 && base + table5_offset + block_count * 0x24 <= size)
	{
		cs->block_count = block_count;
		cs->blocks = MALLOC(block_count * sizeof(chans_line_block_t));
		if (cs->blocks)
		{
			const u8 *t5 = p + base + table5_offset;
			for (u32 i = 0; i < block_count; i++)
			{
				cs->blocks[i].offset = be32((t5 + i * 0x24));
				memcpy(cs->blocks[i].data, t5 + i * 0x24 + 4, 0x20);
			}
		}
	}

	return ERR_OK;
}

static int IsStartOfLine (const chans_script_t *cs, u32 offset)
{
	if (!cs->blocks || cs->block_count == 0)
		return 0;
	u32 block = (offset + 255) / 256;
	if (block == 0 || block > cs->block_count)
		return 0;
	block -= 1;
	u32 blockoff = offset & 0xFF;
	u32 byte = blockoff / 8;
	if (byte >= 0x20)
		return 0;
	u32 byteoff = 8 - (offset & 0x07) - 1;
	u8 b = cs->blocks[block].data[byte];
	return (b >> byteoff) & 1;
}

// Opcode decoder helper
typedef struct decoded_op_t
{
	u8 opcode;
	int length;
	char desc[256];
} decoded_op_t;

static int DecodeInstruction (const chans_script_t *cs, u32 pc, decoded_op_t *dop)
{
	if (!cs->bytecode || pc >= cs->bytecode_size)
		return 0;

	const u8 *data = cs->bytecode + pc;
	u32 rem = cs->bytecode_size - pc;
	u8 b = data[0];

	memset(dop, 0, sizeof(*dop));
	dop->opcode = b;
	dop->length = 1;

	if (b < 0x40)
	{
		switch (b)
		{
			case 0x00:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "END_OF_CODE");
				break;
			case 0x01:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "RETURN ACC");
				break;
			case 0x02:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "NEW [0x%02x args]", (rem >= 2) ? data[1] : 0);
				break;
			case 0x03:
				dop->length = (rem >= 3) ? 3 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "CLOSURE [id=0x%04x]", (rem >= 3) ? be16((data + 1)) : 0);
				break;
			case 0x04:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "PUSH ACC");
				break;
			case 0x05:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "DROP ACC");
				break;
			case 0x06:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP + ACC");
				break;
			case 0x07:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP - ACC");
				break;
			case 0x08:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP * ACC");
				break;
			case 0x09:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP / ACC");
				break;
			case 0x0a:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP %% ACC");
				break;
			case 0x0b:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP & ACC");
				break;
			case 0x0c:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP | ACC");
				break;
			case 0x0d:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP ^ ACC");
				break;
			case 0x0e:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP << ACC");
				break;
			case 0x0f:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = POP >> ACC");
				break;
			case 0x10:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (POP == ACC)");
				break;
			case 0x11:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (POP != ACC)");
				break;
			case 0x12:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (POP < ACC)");
				break;
			case 0x13:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (POP > ACC)");
				break;
			case 0x14:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (POP <= ACC)");
				break;
			case 0x15:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (POP >= ACC)");
				break;
			case 0x16:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ~ACC");
				break;
			case 0x17:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = !ACC");
				break;
			case 0x18:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = %d (0x%02x)", (rem >= 2) ? (s8)data[1] : 0, (rem >= 2) ? data[1] : 0);
				break;
			case 0x19:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC + %u", (rem >= 2) ? data[1] : 0);
				break;
			case 0x1a:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC - %u", (rem >= 2) ? data[1] : 0);
				break;
			case 0x1b:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC * %u", (rem >= 2) ? data[1] : 0);
				break;
			case 0x1c:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC / %u", (rem >= 2) ? data[1] : 0);
				break;
			case 0x1d:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC %% %u", (rem >= 2) ? data[1] : 0);
				break;
			case 0x1e:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC & 0x%02x", (rem >= 2) ? data[1] : 0);
				break;
			case 0x1f:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC | 0x%02x", (rem >= 2) ? data[1] : 0);
				break;
			case 0x20:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = ACC ^ 0x%02x", (rem >= 2) ? data[1] : 0);
				break;
			case 0x21:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (ACC == %u)", (rem >= 2) ? data[1] : 0);
				break;
			case 0x22:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (ACC != %u)", (rem >= 2) ? data[1] : 0);
				break;
			case 0x23:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (ACC < %u)", (rem >= 2) ? data[1] : 0);
				break;
			case 0x24:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (ACC > %u)", (rem >= 2) ? data[1] : 0);
				break;
			case 0x25:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (ACC <= %u)", (rem >= 2) ? data[1] : 0);
				break;
			case 0x26:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = (ACC >= %u)", (rem >= 2) ? data[1] : 0);
				break;
			case 0x27:
				dop->length = (rem >= 3) ? 3 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = 0x%04x (%u)",
						 (rem >= 3) ? be16((data + 1)) : 0,
						 (rem >= 3) ? be16((data + 1)) : 0);
				break;
			case 0x28:
				dop->length = (rem >= 5) ? 5 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = 0x%08x (%u)",
						 (rem >= 5) ? be32((data + 1)) : 0,
						 (rem >= 5) ? be32((data + 1)) : 0);
				break;
			case 0x29:
			case 0x2a:
			{
				dop->length = (rem >= 9) ? 9 : 1;
				double fval = 0.0;
				if (rem >= 9)
				{
					u64 uval = be64((data + 1));
					memcpy(&fval, &uval, sizeof(double));
				}
				snprintf(dop->desc, sizeof(dop->desc), "ACC = %f", fval);
				break;
			}
			case 0x2b:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = null");
				break;
			case 0x2c:
			{
				dop->length = (rem >= 3) ? 3 : 1;
				u16 sidx = (rem >= 3) ? be16((data + 1)) : 0;
				const char *str = (sidx < cs->string_count && cs->strings[sidx].utf8) ? cs->strings[sidx].utf8 : "";
				snprintf(dop->desc, sizeof(dop->desc), "ACC = \"%s\"", str);
				break;
			}
			case 0x2d:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = &POP[ACC]");
				break;
			case 0x30:
			{
				dop->length = (rem >= 4) ? 4 : 1;
				u16 sym_id = (rem >= 4) ? be16((data + 1)) : 0;
				u8 args = (rem >= 4) ? data[3] : 0;
				const char *name = (sym_id < cs->imported_count && cs->imported[sym_id].name)
									   ? cs->imported[sym_id].name
									   : "unknown";
				snprintf(dop->desc, sizeof(dop->desc), "CALL ACC.%s WITH 0x%02x PARAMS FROM STACK", name, args);
				break;
			}
			case 0x31:
				dop->length = (rem >= 2) ? 2 : 1;
				snprintf(dop->desc, sizeof(dop->desc), "CALL ACC WITH 0x%02x PARAMS FROM STACK", (rem >= 2) ? data[1] : 0);
				break;
			case 0x32:
			{
				dop->length = (rem >= 3) ? 3 : 1;
				u16 sym_id = (rem >= 3) ? be16((data + 1)) : 0;
				const char *name = (sym_id < cs->imported_count && cs->imported[sym_id].name)
									   ? cs->imported[sym_id].name
									   : "unknown";
				snprintf(dop->desc, sizeof(dop->desc), "GET ACC.%s", name);
				break;
			}
			case 0x33:
			{
				dop->length = (rem >= 3) ? 3 : 1;
				u16 sym_id = (rem >= 3) ? be16((data + 1)) : 0;
				const char *name = (sym_id < cs->imported_count && cs->imported[sym_id].name)
									   ? cs->imported[sym_id].name
									   : "unknown";
				snprintf(dop->desc, sizeof(dop->desc), "SET ACC.%s = POP", name);
				break;
			}
			case 0x34:
			{
				dop->length = (rem >= 5) ? 5 : 1;
				u16 raw_var = (rem >= 5) ? be16((data + 1)) : 0;
				u16 var_id = raw_var & 0x1FFF;
				s16 jmp = (rem >= 5) ? (s16)be16((data + 3)) : 0;
				u32 dst = pc + 5 + jmp;
				char vbuf[32];
				const char *vname = NULL;
				if (var_id < cs->exported_count && cs->exported[var_id].name)
					vname = cs->exported[var_id].name;
				else
				{
					snprintf(vbuf, sizeof(vbuf), "var_%03x", var_id);
					vname = vbuf;
				}
				snprintf(dop->desc, sizeof(dop->desc), "FOR_IN %s in ACC [break -> 0x%04x]", vname, dst);
				break;
			}
			case 0x35:
			{
				dop->length = (rem >= 4) ? 4 : 1;
				s32 jmp = 0;
				if (rem >= 4)
				{
					jmp = (data[1] << 16) | (data[2] << 8) | data[3];
					if (jmp & 0x800000)
						jmp -= 0x1000000;
				}
				u32 dst = pc + 4 + jmp;
				snprintf(dop->desc, sizeof(dop->desc), "GOTO_24 [dst: 0x%04x]", dst);
				break;
			}
			case 0x36:
			{
				dop->length = (rem >= 3) ? 3 : 1;
				u16 sym_id = (rem >= 3) ? be16((data + 1)) : 0;
				const char *name = (sym_id < cs->exported_count && cs->exported[sym_id].name)
									   ? cs->exported[sym_id].name
									   : "var";
				snprintf(dop->desc, sizeof(dop->desc), "DELETE %s", name);
				break;
			}
			case 0x37:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "DELETE POP[ACC]");
				break;
			case 0x3e:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "ACC = *ACC (DEREF)");
				break;
			case 0x3f:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "*ACC = POP (ASSIGN)");
				break;
			default:
				dop->length = 1;
				snprintf(dop->desc, sizeof(dop->desc), "UNKNOWN_0x%02x", b);
				break;
		}
	}
	else
	{
		dop->length = (rem >= 2) ? 2 : 1;
		u16 w = (rem >= 2) ? be16(data) : (b << 8);
		u8 op = (b & 0xF0);
		u16 idx = w & 0x1FFF;
		s32 sign = (idx & 0x1000) ? (s32)idx - 0x2000 : (s32)idx;
		u32 dst = pc + ((idx & 0x1000) ? (sign + 1) : (sign + 2));

		const char *var_name = NULL;
		char tmp_name[32];
		if (idx < cs->exported_count && cs->exported[idx].name)
		{
			var_name = cs->exported[idx].name;
		}
		else
		{
			snprintf(tmp_name, sizeof(tmp_name), "var_%03x", idx);
			var_name = tmp_name;
		}

		if (op == 0x40 || op == 0x50)
		{
			snprintf(dop->desc, sizeof(dop->desc), "ACC = %s", var_name);
		}
		else if (op == 0x60 || op == 0x70)
		{
			snprintf(dop->desc, sizeof(dop->desc), "%s = ACC", var_name);
		}
		else if (op == 0x80 || op == 0x90)
		{
			snprintf(dop->desc, sizeof(dop->desc), "IF(POP === ACC) PC = 0x%04x", dst);
		}
		else if (op == 0xA0 || op == 0xB0)
		{
			snprintf(dop->desc, sizeof(dop->desc), "IF(!ACC) PC = 0x%04x", dst);
		}
		else if (op == 0xC0 || op == 0xD0)
		{
			snprintf(dop->desc, sizeof(dop->desc), "IF(ACC) PC = 0x%04x", dst);
		}
		else if (op == 0xE0 || op == 0xF0)
		{
			snprintf(dop->desc, sizeof(dop->desc), "PC = 0x%04x", dst);
		}
		else
		{
			snprintf(dop->desc, sizeof(dop->desc), "UNKNOWN_HI_0x%02x", b);
		}
	}

	return dop->length;
}

// Disassembly dump
enumError DumpChannelScriptDisasm (const chans_script_t *cs, char **out_buf, size_t *out_len)
{
	if (!cs || !out_buf || !out_len)
		return ERR_INVALID_DATA;

	chans_sb_t sb;
	csb_init(&sb);

	csb_printf(&sb, "// ChannelScript Disassembly\n");
	csb_printf(&sb, "// Version: %u, Total size: %u bytes\n\n", cs->version, cs->file_size);

	// Exported symbols
	csb_printf(&sb, "Exported Symbols (%u):\n", cs->exported_count);
	for (u32 i = 0; i < cs->exported_count; i++)
	{
		csb_printf(&sb, "  [%02x] %s\n", i, cs->exported[i].name ? cs->exported[i].name : "");
	}
	csb_printf(&sb, "\n");

	// Imported symbols
	csb_printf(&sb, "Imported Symbols (%u):\n", cs->imported_count);
	for (u32 i = 0; i < cs->imported_count; i++)
	{
		csb_printf(&sb, "  [%02x] %s\n", i, cs->imported[i].name ? cs->imported[i].name : "");
	}
	csb_printf(&sb, "\n");

	// String literals
	csb_printf(&sb, "String Literals (%u):\n", cs->string_count);
	for (u32 i = 0; i < cs->string_count; i++)
	{
		csb_printf(&sb, "  [%02x] \"%s\"\n", i, cs->strings[i].utf8 ? cs->strings[i].utf8 : "");
	}
	csb_printf(&sb, "\n");

	// Methods disassembly
	csb_printf(&sb, "Methods (%u):\n", cs->method_count);
	u32 entry_end = (cs->method_count > 0) ? cs->methods[0].offset : cs->bytecode_size;

	csb_printf(&sb, "--- Method: entryPoint (start: 0x0001, end: 0x%04x) ---\n", entry_end);
	u32 pc = 1;
	while (pc < entry_end && pc < cs->bytecode_size)
	{
		decoded_op_t dop;
		int len = DecodeInstruction(cs, pc, &dop);
		if (len <= 0) break;

		char hex_bytes[32] = "";
		for (int j = 0; j < len && j < 8; j++)
		{
			char tmp[8];
			snprintf(tmp, sizeof(tmp), "%02X ", cs->bytecode[pc + j]);
			strcat(hex_bytes, tmp);
		}

		csb_printf(&sb, "  %dx%04X:  %-24s %s\n",
						IsStartOfLine(cs, pc), pc, hex_bytes, dop.desc);
		pc += len;
	}
	csb_printf(&sb, "\n");

	for (u32 i = 0; i < cs->method_count; i++)
	{
		u32 m_start = cs->methods[i].offset;
		u32 m_end = (i + 1 < cs->method_count) ? cs->methods[i + 1].offset : cs->bytecode_size;
		const char *m_name = (cs->methods[i].symbol_id < cs->exported_count &&
							  cs->exported[cs->methods[i].symbol_id].name)
								 ? cs->exported[cs->methods[i].symbol_id].name
								 : "unnamed";

		csb_printf(&sb, "--- Method [%02x]: %s (start: 0x%04x, end: 0x%04x, params: %u, temps: %u) ---\n",
						i, m_name, m_start, m_end, cs->methods[i].param_count, cs->methods[i].temp_count);

		pc = m_start;
		while (pc < m_end && pc < cs->bytecode_size)
		{
			decoded_op_t dop;
			int len = DecodeInstruction(cs, pc, &dop);
			if (len <= 0) break;

			char hex_bytes[32] = "";
			for (int j = 0; j < len && j < 8; j++)
			{
				char tmp[8];
				snprintf(tmp, sizeof(tmp), "%02X ", cs->bytecode[pc + j]);
				strcat(hex_bytes, tmp);
			}

			csb_printf(&sb, "  %dx%04X:  %-24s %s\n",
							IsStartOfLine(cs, pc), pc, hex_bytes, dop.desc);
			pc += len;
		}
		csb_printf(&sb, "\n");
	}

	*out_len = sb.len;
	*out_buf = sb.buf;
	return ERR_OK;
}

// Simple expression stack node for decompilation
typedef struct expr_node_t
{
	char *str;
	struct expr_node_t *next;
} expr_node_t;

static void PushExpr (expr_node_t **stack, const char *s)
{
	expr_node_t *node = MALLOC(sizeof(expr_node_t));
	if (node)
	{
		node->str = STRDUP(s ? s : "");
		node->next = *stack;
		*stack = node;
	}
}

static char *PopExpr (expr_node_t **stack)
{
	if (!*stack)
		return STRDUP("undefined");
	expr_node_t *top = *stack;
	*stack = top->next;
	char *res = top->str;
	FREE(top);
	return res;
}

static void ClearExprStack (expr_node_t **stack)
{
	while (*stack)
	{
		char *s = PopExpr(stack);
		FREE(s);
	}
}

static void DecompileMethodBody (const chans_script_t *cs, u32 start_off, u32 end_off, chans_sb_t *sb)
{
	u32 pc = start_off;
	expr_node_t *stack = NULL;
	char *acc = NULL;

	while (pc < end_off && pc < cs->bytecode_size)
	{
		const u8 *data = cs->bytecode + pc;
		u32 rem = cs->bytecode_size - pc;
		u8 b = data[0];

		if (b < 0x40)
		{
			switch (b)
			{
				case 0x00: // EOC
					if (acc)
					{
						csb_printf(sb, "    %s;\n", acc);
						FREE(acc);
						acc = NULL;
					}
					pc += 1;
					break;
				case 0x01: // RETURN
					if (acc)
					{
						csb_printf(sb, "    return %s;\n", acc);
						FREE(acc);
						acc = NULL;
					}
					else
					{
						csb_printf(sb, "    return;\n");
					}
					pc += 1;
					break;
				case 0x02: // NEW
				{
					u8 args_cnt = (rem >= 2) ? data[1] : 0;
					pc += 2;
					char **arg_strs = (args_cnt > 0) ? MALLOC(args_cnt * sizeof(char *)) : NULL;
					for (int i = 0; i < args_cnt; i++)
						arg_strs[i] = PopExpr(&stack);
					// Reverse args
					char arg_buf[512] = "";
					for (int i = args_cnt - 1; i >= 0; i--)
					{
						if (strlen(arg_buf) + strlen(arg_strs[i]) + 4 < sizeof(arg_buf))
						{
							if (arg_buf[0]) strcat(arg_buf, ", ");
							strcat(arg_buf, arg_strs[i]);
						}
						FREE(arg_strs[i]);
					}
					if (arg_strs) FREE(arg_strs);

					char new_expr[1024];
					snprintf(new_expr, sizeof(new_expr), "new %s(%s)", acc ? acc : "Object", arg_buf);
					if (acc) FREE(acc);
					acc = STRDUP(new_expr);
					break;
				}
				case 0x03: // CLOSURE
				{
					u16 f_id = (rem >= 3) ? be16((data + 1)) : 0;
					pc += 3;
					char cl_expr[64];
					snprintf(cl_expr, sizeof(cl_expr), "closure_%04x", f_id);
					if (acc) FREE(acc);
					acc = STRDUP(cl_expr);
					break;
				}
				case 0x04: // PUSH_ACC
					PushExpr(&stack, acc ? acc : "undefined");
					if (acc) FREE(acc);
					acc = NULL;
					pc += 1;
					break;
				case 0x05: // DROP_ACC
					if (acc)
					{
						csb_printf(sb, "    %s;\n", acc);
						FREE(acc);
						acc = NULL;
					}
					pc += 1;
					break;
				case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a:
				case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
				case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
				{
					static const char *ops[] = {
						"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
						"==", "!=", "<", ">", "<=", ">="
					};
					const char *op_str = ops[b - 0x06];
					char *left = PopExpr(&stack);
					char bin_expr[1024];
					snprintf(bin_expr, sizeof(bin_expr), "(%s %s %s)", left, op_str, acc ? acc : "undefined");
					FREE(left);
					if (acc) FREE(acc);
					acc = STRDUP(bin_expr);
					pc += 1;
					break;
				}
				case 0x16: // BIT_NOT
				{
					char un_expr[1024];
					snprintf(un_expr, sizeof(un_expr), "(~%s)", acc ? acc : "undefined");
					if (acc) FREE(acc);
					acc = STRDUP(un_expr);
					pc += 1;
					break;
				}
				case 0x17: // LOG_NOT
				{
					char un_expr[1024];
					snprintf(un_expr, sizeof(un_expr), "(!%s)", acc ? acc : "undefined");
					if (acc) FREE(acc);
					acc = STRDUP(un_expr);
					pc += 1;
					break;
				}
				case 0x18: // LOAD_S8
				{
					char num_expr[32];
					snprintf(num_expr, sizeof(num_expr), "%d", (rem >= 2) ? (s8)data[1] : 0);
					if (acc) FREE(acc);
					acc = STRDUP(num_expr);
					pc += 2;
					break;
				}
				case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
				case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22:
				case 0x23: case 0x24: case 0x25: case 0x26:
				{
					static const char *imm_ops[] = {
						"+", "-", "*", "/", "%", "&", "|", "^",
						"==", "!=", "<", ">", "<=", ">="
					};
					const char *op_str = imm_ops[b - 0x19];
					u8 imm = (rem >= 2) ? data[1] : 0;
					char imm_expr[1024];
					snprintf(imm_expr, sizeof(imm_expr), "(%s %s %u)", acc ? acc : "undefined", op_str, imm);
					if (acc) FREE(acc);
					acc = STRDUP(imm_expr);
					pc += 2;
					break;
				}
				case 0x27: // LOAD_U16
				{
					u16 imm = (rem >= 3) ? be16((data + 1)) : 0;
					char num_expr[32];
					snprintf(num_expr, sizeof(num_expr), "%u", imm);
					if (acc) FREE(acc);
					acc = STRDUP(num_expr);
					pc += 3;
					break;
				}
				case 0x28: // LOAD_U32
				{
					u32 imm = (rem >= 5) ? be32((data + 1)) : 0;
					char num_expr[32];
					snprintf(num_expr, sizeof(num_expr), "%u", imm);
					if (acc) FREE(acc);
					acc = STRDUP(num_expr);
					pc += 5;
					break;
				}
				case 0x29: case 0x2a: // LOAD_F64
				{
					double fval = 0.0;
					if (rem >= 9)
					{
						u64 uval = be64((data + 1));
						memcpy(&fval, &uval, sizeof(double));
					}
					char num_expr[64];
					snprintf(num_expr, sizeof(num_expr), "%f", fval);
					if (acc) FREE(acc);
					acc = STRDUP(num_expr);
					pc += 9;
					break;
				}
				case 0x2b: // LOAD_NULL
					if (acc) FREE(acc);
					acc = STRDUP("null");
					pc += 1;
					break;
				case 0x2c: // LOAD_STR
				{
					u16 sidx = (rem >= 3) ? be16((data + 1)) : 0;
					const char *raw_str = (sidx < cs->string_count && cs->strings[sidx].utf8)
											  ? cs->strings[sidx].utf8
											  : "";
					char str_buf[2048];
					snprintf(str_buf, sizeof(str_buf), "\"%s\"", raw_str);
					if (acc) FREE(acc);
					acc = STRDUP(str_buf);
					pc += 3;
					break;
				}
				case 0x2d: // GET_MEMBER_REF
				{
					char *left = PopExpr(&stack);
					char ref_expr[1024];
					snprintf(ref_expr, sizeof(ref_expr), "%s[%s]", left, acc ? acc : "undefined");
					FREE(left);
					if (acc) FREE(acc);
					acc = STRDUP(ref_expr);
					pc += 1;
					break;
				}
				case 0x30: // CALL_EXT_SYMBOL_ARGS
				{
					u16 sym_id = (rem >= 4) ? be16((data + 1)) : 0;
					u8 args_cnt = (rem >= 4) ? data[3] : 0;
					const char *m_name = (sym_id < cs->imported_count && cs->imported[sym_id].name)
											 ? cs->imported[sym_id].name
											 : "unknown";
					pc += 4;
					char **arg_strs = (args_cnt > 0) ? MALLOC(args_cnt * sizeof(char *)) : NULL;
					for (int i = 0; i < args_cnt; i++)
						arg_strs[i] = PopExpr(&stack);
					char arg_buf[512] = "";
					for (int i = args_cnt - 1; i >= 0; i--)
					{
						if (strlen(arg_buf) + strlen(arg_strs[i]) + 4 < sizeof(arg_buf))
						{
							if (arg_buf[0]) strcat(arg_buf, ", ");
							strcat(arg_buf, arg_strs[i]);
						}
						FREE(arg_strs[i]);
					}
					if (arg_strs) FREE(arg_strs);

					char call_expr[1024];
					snprintf(call_expr, sizeof(call_expr), "%s.%s(%s)", acc ? acc : "this", m_name, arg_buf);
					if (acc) FREE(acc);
					acc = STRDUP(call_expr);
					break;
				}
				case 0x31: // CALL_ACC_ARGS
				{
					u8 args_cnt = (rem >= 2) ? data[1] : 0;
					pc += 2;
					char **arg_strs = (args_cnt > 0) ? MALLOC(args_cnt * sizeof(char *)) : NULL;
					for (int i = 0; i < args_cnt; i++)
						arg_strs[i] = PopExpr(&stack);
					char arg_buf[512] = "";
					for (int i = args_cnt - 1; i >= 0; i--)
					{
						if (strlen(arg_buf) + strlen(arg_strs[i]) + 4 < sizeof(arg_buf))
						{
							if (arg_buf[0]) strcat(arg_buf, ", ");
							strcat(arg_buf, arg_strs[i]);
						}
						FREE(arg_strs[i]);
					}
					if (arg_strs) FREE(arg_strs);

					char call_expr[1024];
					snprintf(call_expr, sizeof(call_expr), "%s(%s)", acc ? acc : "callee", arg_buf);
					if (acc) FREE(acc);
					acc = STRDUP(call_expr);
					break;
				}
				case 0x32: // GET_EXT_SYMBOL_DEREF
				{
					u16 sym_id = (rem >= 3) ? be16((data + 1)) : 0;
					const char *p_name = (sym_id < cs->imported_count && cs->imported[sym_id].name)
											 ? cs->imported[sym_id].name
											 : "prop";
					char prop_expr[1024];
					snprintf(prop_expr, sizeof(prop_expr), "%s.%s", acc ? acc : "this", p_name);
					if (acc) FREE(acc);
					acc = STRDUP(prop_expr);
					pc += 3;
					break;
				}
				case 0x33: // SET_EXT_SYMBOL_DEREF
				{
					u16 sym_id = (rem >= 3) ? be16((data + 1)) : 0;
					const char *p_name = (sym_id < cs->imported_count && cs->imported[sym_id].name)
											 ? cs->imported[sym_id].name
											 : "prop";
					char *val = PopExpr(&stack);
					csb_printf(sb, "    %s.%s = %s;\n", acc ? acc : "this", p_name, val);
					FREE(val);
					if (acc) FREE(acc);
					acc = NULL;
					pc += 3;
					break;
				}
				case 0x34: // FOR_IN
				{
					u16 raw_var = (rem >= 5) ? be16((data + 1)) : 0;
					u16 var_id = raw_var & 0x1FFF;
					s16 jmp = (rem >= 5) ? (s16)be16((data + 3)) : 0;
					u32 dst = pc + 5 + jmp;
					char vbuf[32];
					const char *vname = NULL;
					if (var_id < cs->exported_count && cs->exported[var_id].name)
						vname = cs->exported[var_id].name;
					else
					{
						snprintf(vbuf, sizeof(vbuf), "var_%03x", var_id);
						vname = vbuf;
					}
					csb_printf(sb, "    for (%s in %s) /* break -> 0x%04x */ {\n",
									vname, acc ? acc : "this", dst);
					if (acc) FREE(acc);
					acc = NULL;
					pc += 5;
					break;
				}
				case 0x35: // GOTO_24
				{
					s32 jmp = 0;
					if (rem >= 4)
					{
						jmp = (data[1] << 16) | (data[2] << 8) | data[3];
						if (jmp & 0x800000)
							jmp -= 0x1000000;
					}
					u32 dst = pc + 4 + jmp;
					csb_printf(sb, "    goto 0x%04x;\n", dst);
					pc += 4;
					break;
				}
				case 0x36: // DELETE_VAR
				{
					u16 sym_id = (rem >= 3) ? be16((data + 1)) : 0;
					const char *vname = (sym_id < cs->exported_count && cs->exported[sym_id].name)
											? cs->exported[sym_id].name
											: "var";
					char del_expr[64];
					snprintf(del_expr, sizeof(del_expr), "delete %s", vname);
					if (acc) FREE(acc);
					acc = STRDUP(del_expr);
					pc += 3;
					break;
				}
				case 0x37: // DELETE_MEMBER
				{
					char *left = PopExpr(&stack);
					char del_expr[1024];
					snprintf(del_expr, sizeof(del_expr), "delete %s[%s]", left, acc ? acc : "undefined");
					FREE(left);
					if (acc) FREE(acc);
					acc = STRDUP(del_expr);
					pc += 1;
					break;
				}
				case 0x3e: // DEREF_REF
					// acc already contains left[key]
					pc += 1;
					break;
				case 0x3f: // ASSIGN_REF
				{
					char *val = PopExpr(&stack);
					csb_printf(sb, "    %s = %s;\n", acc ? acc : "dest", val);
					FREE(val);
					if (acc) FREE(acc);
					acc = NULL;
					pc += 1;
					break;
				}
				default:
					pc += 1;
					break;
			}
		}
		else // 4-bit opcodes
		{
			u16 w = (rem >= 2) ? be16(data) : (b << 8);
			u8 op = (b & 0xF0);
			u16 idx = w & 0x1FFF;
			s32 sign = (idx & 0x1000) ? (s32)idx - 0x2000 : (s32)idx;
			u32 dst = pc + ((idx & 0x1000) ? (sign + 1) : (sign + 2));

			const char *var_name = NULL;
			char tmp_name[32];
			if (idx < cs->exported_count && cs->exported[idx].name)
			{
				var_name = cs->exported[idx].name;
			}
			else
			{
				snprintf(tmp_name, sizeof(tmp_name), "var_%03x", idx);
				var_name = tmp_name;
			}

			if (op == 0x40 || op == 0x50)
			{
				if (acc) FREE(acc);
				acc = STRDUP(var_name);
			}
			else if (op == 0x60 || op == 0x70)
			{
				csb_printf(sb, "    %s = %s;\n", var_name, acc ? acc : "undefined");
				if (acc) FREE(acc);
				acc = NULL;
			}
			else if (op == 0x80 || op == 0x90)
			{
				char *val = PopExpr(&stack);
				csb_printf(sb, "    if (%s === %s) goto 0x%04x;\n", val, acc ? acc : "undefined", dst);
				FREE(val);
			}
			else if (op == 0xA0 || op == 0xB0)
			{
				csb_printf(sb, "    if (!%s) goto 0x%04x;\n", acc ? acc : "undefined", dst);
				if (acc) FREE(acc);
				acc = NULL;
			}
			else if (op == 0xC0 || op == 0xD0)
			{
				csb_printf(sb, "    if (%s) goto 0x%04x;\n", acc ? acc : "undefined", dst);
				if (acc) FREE(acc);
				acc = NULL;
			}
			else if (op == 0xE0 || op == 0xF0)
			{
				csb_printf(sb, "    goto 0x%04x;\n", dst);
			}
			pc += 2;
		}
	}

	if (acc)
	{
		csb_printf(sb, "    %s;\n", acc);
		FREE(acc);
	}
	ClearExprStack(&stack);
}

// Decompilation dump
enumError DumpChannelScriptDecompiled (const chans_script_t *cs, char **out_buf, size_t *out_len)
{
	if (!cs || !out_buf || !out_len)
		return ERR_INVALID_DATA;

	chans_sb_t sb;
	csb_init(&sb);

	csb_printf(&sb, "// ChannelScript Decompiled ECMAScript\n\n");

	// Entry point
	u32 entry_end = (cs->method_count > 0) ? cs->methods[0].offset : cs->bytecode_size;
	csb_printf(&sb, "function entryPoint()\n{\n");
	DecompileMethodBody(cs, 1, entry_end, &sb);
	csb_printf(&sb, "}\n\n");

	for (u32 i = 0; i < cs->method_count; i++)
	{
		u32 m_start = cs->methods[i].offset;
		u32 m_end = (i + 1 < cs->method_count) ? cs->methods[i + 1].offset : cs->bytecode_size;
		const char *m_name = (cs->methods[i].symbol_id < cs->exported_count &&
							  cs->exported[cs->methods[i].symbol_id].name)
								 ? cs->exported[cs->methods[i].symbol_id].name
								 : "unnamed";

		csb_printf(&sb, "function %s()\n{\n", m_name);
		DecompileMethodBody(cs, m_start, m_end, &sb);
		csb_printf(&sb, "}\n\n");
	}

	*out_len = sb.len;
	*out_buf = sb.buf;
	return ERR_OK;
}

enumError DumpChannelScriptAll (const chans_script_t *cs, char **out_buf, size_t *out_len)
{
	char *dis = NULL, *dec = NULL;
	size_t dis_len = 0, dec_len = 0;

	enumError err = DumpChannelScriptDecompiled(cs, &dec, &dec_len);
	if (err) return err;

	err = DumpChannelScriptDisasm(cs, &dis, &dis_len);
	if (err)
	{
		if (dec) FREE(dec);
		return err;
	}

	size_t total = dec_len + dis_len + 128;
	char *buf = MALLOC(total);
	if (!buf)
	{
		if (dec) FREE(dec);
		if (dis) FREE(dis);
		return ERR_OUT_OF_MEMORY;
	}

	snprintf(buf, total, "%s\n/*\n===============================================================================\n                               DISASSEMBLY\n===============================================================================\n*/\n\n%s",
			 dec ? dec : "", dis ? dis : "");

	if (dec) FREE(dec);
	if (dis) FREE(dis);

	*out_buf = buf;
	*out_len = strlen(buf);
	return ERR_OK;
}
