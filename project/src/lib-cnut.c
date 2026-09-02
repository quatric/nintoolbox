#include "lib-cnut.h"
#include "lib-std.h"

static const char *const sq_opnames[] = {
	"LINE", "LOAD", "LOADINT", "LOADFLOAT", "DLOAD", "TAILCALL", "CALL",
	"PREPCALL", "PREPCALLK", "GETK", "MOVE", "NEWSLOT", "DELETE", "SET",
	"GET", "EQ", "NE", "ARITH", "BITW", "RETURN", "LOADNULLS", "LOADROOTTABLE",
	"LOADBOOL", "DMOVE", "JMP", "JCMP", "JZ", "SETOUTER", "GETOUTER",
	"NEWOBJ", "APPENDARRAY", "COMPARITH", "INC", "INCL", "PINC", "PINCL",
	"CMP", "EXISTS", "INSTANCEOF", "AND", "OR", "NEG", "NOT", "BWNOT",
	"CLOSURE", "YIELD", "RESUME", "FOREACH", "POSTFOREACH", "CLONE",
	"TYPEOF", "PUSHTRAP", "POPTRAP", "THROW", "NEWSLOTA", "GETBASE", "CLOSE"
};

#define SQ_NUM_OPNAMES (sizeof (sq_opnames) / sizeof (sq_opnames[0]))

bool IsCNUT (const u8 *data, size_t size)
{
	if (!data || size < 10)
		return false;
	u16 tag = be16 (data);
	if (tag != SQ_BYTECODE_STREAM_TAG)
		return false;
	return !memcmp (data + 2, "SQIR", 4);
}

static inline void wr_be16 (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static inline void wr_be32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static enumError read_object (const u8 **pos_ptr, const u8 *end, cnut_object_t *obj)
{
	const u8 *p = *pos_ptr;
	if (p + 4 > end)
		return ERR_INVALID_DATA;

	u32 raw_t = be32 (p);
	p += 4;

	obj->raw_type = raw_t;
	obj->type = (u16)(raw_t & 0xFFFF);
	obj->str = 0;
	obj->len = 0;
	obj->ival = 0;
	obj->fval = 0.0f;
	obj->bval = false;

	if (obj->type == SQ_RT_STRING)
	{
		if (p + 4 > end)
			return ERR_INVALID_DATA;
		u32 slen = be32 (p);
		p += 4;
		if (p + slen > end)
			return ERR_INVALID_DATA;

		obj->len = slen;
		obj->str = MALLOC (slen + 1);
		if (!obj->str)
			return ERR_OUT_OF_MEMORY;
		memcpy (obj->str, p, slen);
		obj->str[slen] = 0;
		p += slen;
	}
	else if (obj->type == SQ_RT_INTEGER)
	{
		if (p + 4 > end)
			return ERR_INVALID_DATA;
		obj->ival = (s32)be32 (p);
		p += 4;
	}
	else if (obj->type == SQ_RT_FLOAT)
	{
		if (p + 4 > end)
			return ERR_INVALID_DATA;
		u32 raw_f = be32 (p);
		memcpy (&obj->fval, &raw_f, 4);
		p += 4;
	}
	else if (obj->type == SQ_RT_BOOL)
	{
		if (p + 4 > end)
			return ERR_INVALID_DATA;
		obj->bval = (be32 (p) != 0);
		p += 4;
	}
	else if (obj->type == SQ_RT_NULL)
	{
		// Null has 0 additional payload bytes
	}

	*pos_ptr = p;
	return ERR_OK;
}

static void free_object (cnut_object_t *obj)
{
	if (obj && obj->str)
	{
		FREE (obj->str);
		obj->str = 0;
	}
}

static void free_proto (cnut_funcproto_t *proto)
{
	if (!proto)
		return;

	if (proto->literals)
	{
		for (uint i = 0; i < proto->n_literals; i++)
			free_object (&proto->literals[i]);
		FREE (proto->literals);
		proto->literals = 0;
	}

	if (proto->parameters)
	{
		for (uint i = 0; i < proto->n_parameters; i++)
		{
			if (proto->parameters[i])
				FREE (proto->parameters[i]);
		}
		FREE (proto->parameters);
		proto->parameters = 0;
	}

	if (proto->outervalues)
	{
		for (uint i = 0; i < proto->n_outervalues; i++)
		{
			free_object (&proto->outervalues[i].src);
			free_object (&proto->outervalues[i].name);
		}
		FREE (proto->outervalues);
		proto->outervalues = 0;
	}

	if (proto->localvars)
	{
		FREE (proto->localvars);
		proto->localvars = 0;
	}

	if (proto->lineinfos)
	{
		FREE (proto->lineinfos);
		proto->lineinfos = 0;
	}

	if (proto->defaultparams)
	{
		FREE (proto->defaultparams);
		proto->defaultparams = 0;
	}

	if (proto->instructions)
	{
		FREE (proto->instructions);
		proto->instructions = 0;
	}

	if (proto->functions)
	{
		for (uint i = 0; i < proto->n_functions; i++)
			free_proto (&proto->functions[i]);
		FREE (proto->functions);
		proto->functions = 0;
	}

	memset (proto, 0, sizeof (*proto));
}

static enumError parse_proto (const u8 **pos_ptr, const u8 *end, cnut_funcproto_t *proto)
{
	const u8 *p = *pos_ptr;
	memset (proto, 0, sizeof (*proto));

	// 1. PART tag
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	// Source name
	cnut_object_t src_obj;
	enumError err = read_object (&p, end, &src_obj);
	if (err)
		return err;
	if (src_obj.str)
	{
		snprintf (proto->source_name, sizeof (proto->source_name), "%s", src_obj.str);
		free_object (&src_obj);
	}

	// Function name
	cnut_object_t name_obj;
	err = read_object (&p, end, &name_obj);
	if (err)
		return err;
	if (name_obj.str)
	{
		snprintf (proto->func_name, sizeof (proto->func_name), "%s", name_obj.str);
		free_object (&name_obj);
	}

	// 2. PART tag
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (p + 32 > end)
		return ERR_INVALID_DATA;

	proto->n_literals = be32 (p + 0);
	proto->n_parameters = be32 (p + 4);
	proto->n_outervalues = be32 (p + 8);
	proto->n_localvars = be32 (p + 12);
	proto->n_lineinfos = be32 (p + 16);
	proto->n_defaultparams = be32 (p + 20);
	proto->n_instructions = be32 (p + 24);
	proto->n_functions = be32 (p + 28);
	p += 32;

	// 3. Literals
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_literals > 0)
	{
		proto->literals = CALLOC (proto->n_literals, sizeof (cnut_object_t));
		if (!proto->literals)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_literals; i++)
		{
			err = read_object (&p, end, &proto->literals[i]);
			if (err)
				return err;
		}
	}

	// 4. Parameters
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_parameters > 0)
	{
		proto->parameters = CALLOC (proto->n_parameters, sizeof (char *));
		if (!proto->parameters)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_parameters; i++)
		{
			cnut_object_t p_obj;
			err = read_object (&p, end, &p_obj);
			if (err)
				return err;
			proto->parameters[i] = p_obj.str ? p_obj.str : STRDUP ("");
		}
	}

	// 5. Outer values
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_outervalues > 0)
	{
		proto->outervalues = CALLOC (proto->n_outervalues, sizeof (cnut_outerval_t));
		if (!proto->outervalues)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_outervalues; i++)
		{
			if (p + 4 > end)
				return ERR_INVALID_DATA;
			proto->outervalues[i].type = be32 (p);
			p += 4;
			err = read_object (&p, end, &proto->outervalues[i].src);
			if (err)
				return err;
			err = read_object (&p, end, &proto->outervalues[i].name);
			if (err)
				return err;
		}
	}

	// 6. Local variables
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_localvars > 0)
	{
		proto->localvars = CALLOC (proto->n_localvars, sizeof (cnut_localvar_t));
		if (!proto->localvars)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_localvars; i++)
		{
			cnut_object_t lv_obj;
			err = read_object (&p, end, &lv_obj);
			if (err)
				return err;
			if (lv_obj.str)
			{
				snprintf (proto->localvars[i].name, sizeof (proto->localvars[i].name), "%s", lv_obj.str);
				free_object (&lv_obj);
			}
			if (p + 12 > end)
				return ERR_INVALID_DATA;
			proto->localvars[i].pos = be32 (p + 0);
			proto->localvars[i].start_op = be32 (p + 4);
			proto->localvars[i].end_op = be32 (p + 8);
			p += 12;
		}
	}

	// 7. Line infos
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_lineinfos > 0)
	{
		if (p + proto->n_lineinfos * 8 > end)
			return ERR_INVALID_DATA;

		proto->lineinfos = CALLOC (proto->n_lineinfos, sizeof (cnut_lineinfo_t));
		if (!proto->lineinfos)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_lineinfos; i++)
		{
			proto->lineinfos[i].line = be32 (p + i * 8 + 0);
			proto->lineinfos[i].op = be32 (p + i * 8 + 4);
		}
		p += proto->n_lineinfos * 8;
	}

	// 8. Default params
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_defaultparams > 0)
	{
		if (p + proto->n_defaultparams * 4 > end)
			return ERR_INVALID_DATA;

		proto->defaultparams = CALLOC (proto->n_defaultparams, sizeof (u32));
		if (!proto->defaultparams)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_defaultparams; i++)
			proto->defaultparams[i] = be32 (p + i * 4);
		p += proto->n_defaultparams * 4;
	}

	// 9. Instructions
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_instructions > 0)
	{
		if (p + proto->n_instructions * 8 > end)
			return ERR_INVALID_DATA;

		proto->instructions = CALLOC (proto->n_instructions, sizeof (cnut_instruction_t));
		if (!proto->instructions)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_instructions; i++)
		{
			const u8 *ip = p + i * 8;
			proto->instructions[i].arg1 = (s32)be32 (ip + 0);
			proto->instructions[i].op = ip[4];
			proto->instructions[i].arg0 = ip[5];
			proto->instructions[i].arg2 = ip[6];
			proto->instructions[i].arg3 = ip[7];
		}
		p += proto->n_instructions * 8;
	}

	// 10. Child functions
	if (p + 4 > end || memcmp (p, "PART", 4))
		return ERR_INVALID_DATA;
	p += 4;

	if (proto->n_functions > 0)
	{
		proto->functions = CALLOC (proto->n_functions, sizeof (cnut_funcproto_t));
		if (!proto->functions)
			return ERR_OUT_OF_MEMORY;

		for (uint i = 0; i < proto->n_functions; i++)
		{
			err = parse_proto (&p, end, &proto->functions[i]);
			if (err)
				return err;
		}
	}

	// Function metadata
	if (p + 6 > end)
		return ERR_INVALID_DATA;
	proto->stacksize = be32 (p);
	proto->bgenerator = p[4];
	proto->varparams = p[5];
	p += 6;

	*pos_ptr = p;
	return ERR_OK;
}

enumError ScanCNUT (cnut_t *cnut, const u8 *data, size_t size)
{
	if (!cnut || !data || size < 16 || !IsCNUT (data, size))
		return ERR_INVALID_DATA;

	memset (cnut, 0, sizeof (*cnut));
	cnut->data = data;
	cnut->size = size;

	cnut->stream_tag = be16 (data);
	memcpy (cnut->magic, data + 2, 4);
	cnut->magic[4] = 0;
	cnut->char_size = be32 (data + 6);

	const u8 *p = data + 10;
	const u8 *end = data + size;

	enumError err = parse_proto (&p, end, &cnut->root);
	if (err)
	{
		ResetCNUT (cnut);
		return err;
	}

	return ERR_OK;
}

void ResetCNUT (cnut_t *cnut)
{
	if (!cnut)
		return;
	free_proto (&cnut->root);
	memset (cnut, 0, sizeof (*cnut));
}

typedef struct strbuf_t
{
	char *buf;
	size_t len;
	size_t cap;
} strbuf_t;

static void sb_init (strbuf_t *sb)
{
	sb->cap = 4096;
	sb->len = 0;
	sb->buf = MALLOC (sb->cap);
	if (sb->buf)
		sb->buf[0] = 0;
}

static void sb_printf (strbuf_t *sb, const char *fmt, ...)
{
	if (!sb->buf)
		return;

	va_list args;
	va_start (args, fmt);
	int need = vsnprintf (0, 0, fmt, args);
	va_end (args);

	if (need <= 0)
		return;

	if (sb->len + need + 1 > sb->cap)
	{
		size_t new_cap = (sb->cap * 2) + need + 256;
		char *nb = REALLOC (sb->buf, new_cap);
		if (!nb)
			return;
		sb->buf = nb;
		sb->cap = new_cap;
	}

	va_start (args, fmt);
	vsnprintf (sb->buf + sb->len, sb->cap - sb->len, fmt, args);
	va_end (args);
	sb->len += need;
}

static void disasm_proto (strbuf_t *sb, const cnut_funcproto_t *proto, int depth)
{
	char ind[64] = { 0 };
	int idepth = depth < 30 ? depth : 30;
	for (int i = 0; i < idepth * 2; i++)
		ind[i] = ' ';

	sb_printf (sb, "%sfunction %s (", ind, proto->func_name[0] ? proto->func_name : "main");
	for (uint i = 0; i < proto->n_parameters; i++)
	{
		sb_printf (sb, "%s%s", i > 0 ? ", " : "", proto->parameters[i] ? proto->parameters[i] : "arg");
	}
	sb_printf (sb, ") // source: \"%s\", stack: %u, instructions: %u\n",
		proto->source_name, proto->stacksize, proto->n_instructions);

	if (proto->n_literals > 0)
	{
		sb_printf (sb, "%s  // Literals (%u):\n", ind, proto->n_literals);
		for (uint i = 0; i < proto->n_literals; i++)
		{
			const cnut_object_t *lit = &proto->literals[i];
			if (lit->type == SQ_RT_STRING && lit->str)
				sb_printf (sb, "%s  //   [%u] \"%s\"\n", ind, i, lit->str);
			else if (lit->type == SQ_RT_INTEGER)
				sb_printf (sb, "%s  //   [%u] %d\n", ind, i, lit->ival);
			else if (lit->type == SQ_RT_FLOAT)
				sb_printf (sb, "%s  //   [%u] %f\n", ind, i, lit->fval);
			else if (lit->type == SQ_RT_BOOL)
				sb_printf (sb, "%s  //   [%u] %s\n", ind, i, lit->bval ? "true" : "false");
			else if (lit->type == SQ_RT_NULL)
				sb_printf (sb, "%s  //   [%u] null\n", ind, i);
		}
	}

	for (uint i = 0; i < proto->n_instructions; i++)
	{
		const cnut_instruction_t *inst = &proto->instructions[i];
		const char *opname = (inst->op < SQ_NUM_OPNAMES) ? sq_opnames[inst->op] : "UNKNOWN";

		char comment[256] = { 0 };
		if ((!strcmp (opname, "GETK") || !strcmp (opname, "PREPCALLK") || !strcmp (opname, "LOAD"))
			&& inst->arg1 >= 0 && (uint)inst->arg1 < proto->n_literals)
		{
			const cnut_object_t *lit = &proto->literals[inst->arg1];
			if (lit->type == SQ_RT_STRING && lit->str)
				snprintf (comment, sizeof (comment), " // \"%s\"", lit->str);
			else if (lit->type == SQ_RT_INTEGER)
				snprintf (comment, sizeof (comment), " // %d", lit->ival);
		}
		else if (!strcmp (opname, "CLOSURE") && inst->arg1 >= 0 && (uint)inst->arg1 < proto->n_functions)
		{
			snprintf (comment, sizeof (comment), " // function %s", proto->functions[inst->arg1].func_name);
		}

		sb_printf (sb, "%s  [%04u] %-14s r%u, %d, r%u, %u%s\n",
			ind, i, opname, inst->arg0, inst->arg1, inst->arg2, inst->arg3, comment);
	}

	for (uint i = 0; i < proto->n_functions; i++)
	{
		sb_printf (sb, "\n");
		disasm_proto (sb, &proto->functions[i], depth + 1);
	}
}

enumError DisassembleCNUT (const cnut_t *cnut, char **out_text, size_t *out_size)
{
	if (!cnut || !out_text || !out_size)
		return ERR_INVALID_DATA;

	strbuf_t sb;
	sb_init (&sb);
	sb_printf (&sb, "// Squirrel Bytecode Disassembly (Wii Party CNUT / SQIR)\n");
	sb_printf (&sb, "// Magic: %s, CharSize: %u\n\n", cnut->magic, cnut->char_size);

	disasm_proto (&sb, &cnut->root, 0);

	*out_text = sb.buf;
	*out_size = sb.len;
	return ERR_OK;
}

static void extract_strings_proto (strbuf_t *sb, const cnut_funcproto_t *proto)
{
	for (uint i = 0; i < proto->n_literals; i++)
	{
		const cnut_object_t *lit = &proto->literals[i];
		if (lit->type == SQ_RT_STRING && lit->str && lit->len > 0)
			sb_printf (sb, "%s\n", lit->str);
	}

	for (uint i = 0; i < proto->n_functions; i++)
		extract_strings_proto (sb, &proto->functions[i]);
}

enumError ExtractCNUTStrings (const cnut_t *cnut, char **out_text, size_t *out_size)
{
	if (!cnut || !out_text || !out_size)
		return ERR_INVALID_DATA;

	strbuf_t sb;
	sb_init (&sb);

	extract_strings_proto (&sb, &cnut->root);

	*out_text = sb.buf;
	*out_size = sb.len;
	return ERR_OK;
}

enumError CreateCNUT (u8 **dest, size_t *dest_size, const char *source_name, const char *func_name,
	uint n_strings, const char *const *strings, uint n_instructions, const cnut_instruction_t *instructions)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;

	const char *src_nm = source_name ? source_name : "script.nut";
	const char *fn_nm = func_name ? func_name : "main";

	size_t src_len = strlen (src_nm);
	size_t fn_len = strlen (fn_nm);

	size_t literals_size = 0;
	for (uint i = 0; i < n_strings; i++)
	{
		const char *s = strings && strings[i] ? strings[i] : "";
		literals_size += 8 + strlen (s);
	}

	size_t total_size = 10 // Header
		+ 4 // PART
		+ (8 + src_len) // sourcename object
		+ (8 + fn_len) // funcname object
		+ 4 // PART2
		+ 32 // counts
		+ 4 // PART3
		+ literals_size // literals
		+ 4 // PART4 (params)
		+ (8 + 4) // param "this"
		+ 4 // PART5 (outers)
		+ 4 // PART6 (locals)
		+ 4 // PART7 (lines)
		+ 4 // PART8 (defaultparams)
		+ 4 // PART9 (instructions)
		+ (n_instructions * 8)
		+ 4 // PART10 (functions)
		+ 6 // stacksize, bgen, varparams
		+ 4; // TAIL

	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	u8 *p = buf;

	// 1. Header
	wr_be16 (p, SQ_BYTECODE_STREAM_TAG); p += 2;
	memcpy (p, "SQIR", 4); p += 4;
	wr_be32 (p, 1); p += 4; // sizeof SQChar

	// 2. PART tag
	memcpy (p, "PART", 4); p += 4;

	// sourcename string object
	wr_be32 (p, 0x08000010); p += 4;
	wr_be32 (p, (u32)src_len); p += 4;
	memcpy (p, src_nm, src_len); p += src_len;

	// funcname string object
	wr_be32 (p, 0x08000010); p += 4;
	wr_be32 (p, (u32)fn_len); p += 4;
	memcpy (p, fn_nm, fn_len); p += fn_len;

	// PART2 tag
	memcpy (p, "PART", 4); p += 4;

	wr_be32 (p + 0, n_strings); // n_literals
	wr_be32 (p + 4, 1); // n_parameters ("this")
	wr_be32 (p + 8, 0); // n_outervalues
	wr_be32 (p + 12, 0); // n_localvars
	wr_be32 (p + 16, 0); // n_lineinfos
	wr_be32 (p + 20, 0); // n_defaultparams
	wr_be32 (p + 24, n_instructions);
	wr_be32 (p + 28, 0); // n_functions
	p += 32;

	// PART3 tag (literals)
	memcpy (p, "PART", 4); p += 4;
	for (uint i = 0; i < n_strings; i++)
	{
		const char *s = strings && strings[i] ? strings[i] : "";
		size_t slen = strlen (s);
		wr_be32 (p, 0x08000010); p += 4;
		wr_be32 (p, (u32)slen); p += 4;
		memcpy (p, s, slen); p += slen;
	}

	// PART4 tag (parameters)
	memcpy (p, "PART", 4); p += 4;
	wr_be32 (p, 0x08000010); p += 4;
	wr_be32 (p, 4); p += 4;
	memcpy (p, "this", 4); p += 4;

	// PART5 tag (outers)
	memcpy (p, "PART", 4); p += 4;

	// PART6 tag (locals)
	memcpy (p, "PART", 4); p += 4;

	// PART7 tag (lines)
	memcpy (p, "PART", 4); p += 4;

	// PART8 tag (defaultparams)
	memcpy (p, "PART", 4); p += 4;

	// PART9 tag (instructions)
	memcpy (p, "PART", 4); p += 4;
	for (uint i = 0; i < n_instructions; i++)
	{
		wr_be32 (p + 0, instructions ? (u32)instructions[i].arg1 : 0);
		p[4] = instructions ? instructions[i].op : 19; // RETURN
		p[5] = instructions ? instructions[i].arg0 : 255;
		p[6] = instructions ? instructions[i].arg2 : 0;
		p[7] = instructions ? instructions[i].arg3 : 0;
		p += 8;
	}

	// PART10 tag (functions)
	memcpy (p, "PART", 4); p += 4;

	// Metadata
	wr_be32 (p, 10); p += 4; // stacksize
	*p++ = 0; // bgenerator
	*p++ = 0; // varparams

	// TAIL tag
	memcpy (p, "TAIL", 4); p += 4;

	*dest = buf;
	*dest_size = (size_t)(p - buf);
	return ERR_OK;
}
