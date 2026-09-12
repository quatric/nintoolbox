#include "lib-std.h"
#include "lib-byml.h"
#include "lib-bflyt.h"
#include <yaml.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

///////////////////////////////////////////////////////////////////////////////

typedef struct byml_ctx_t
{
	const u8 *data;
	size_t size;
	bool is_le;
	u16 version;
	const char **hash_keys;
	uint n_hash_keys;
	const char **strings;
	uint n_strings;
	u32 visited_stack[256];
	uint visited_depth;
} byml_ctx_t;

static bool byml_is_visited (const byml_ctx_t *ctx, u32 off)
{
	for (uint i = 0; i < ctx->visited_depth; i++)
		if (ctx->visited_stack[i] == off)
			return true;
	return false;
}

static inline u32 byml_u24 (const u8 *p, bool is_le)
{
	return is_le ? ((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16))
				 : (((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2]);
}

static inline u32 byml_u32 (const u8 *p, bool is_le)
{
	return is_le ? rd_le32 (p) : rd_be32 (p);
}

static inline u16 byml_u16 (const u8 *p, bool is_le)
{
	return is_le ? rd_le16 (p) : rd_be16 (p);
}

static inline u64 byml_u64 (const u8 *p, bool is_le)
{
	if (is_le)
		return (u64)rd_le32 (p) | ((u64)rd_le32 (p + 4) << 32);
	else
		return ((u64)rd_be32 (p) << 32) | (u64)rd_be32 (p + 4);
}

static bool is_valid_utf8 (const char *s)
{
	const u8 *p = (const u8 *)s;
	while (*p)
	{
		if (*p < 0x80)
		{
			p++;
		}
		else if ((*p & 0xE0) == 0xC0)
		{
			if ((p[1] & 0xC0) != 0x80)
				return false;
			p += 2;
		}
		else if ((*p & 0xF0) == 0xE0)
		{
			if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
				return false;
			p += 3;
		}
		else if ((*p & 0xF8) == 0xF0)
		{
			if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
				return false;
			p += 4;
		}
		else
			return false;
	}
	return true;
}

static void yaml_print_string (FILE *out, const char *s)
{
	if (!s || !*s)
	{
		fprintf (out, "\"\"");
		return;
	}

	bool valid_u8 = is_valid_utf8 (s);
	bool need_quote = !valid_u8;
	if (!need_quote)
	{
		if (s[0] == ' ' || s[strlen (s) - 1] == ' ' || s[0] == '-' || s[0] == '?' || s[0] == ':'
			|| s[0] == '%' || s[0] == '@' || s[0] == '`' || s[0] == '&' || s[0] == '*'
			|| s[0] == '!' || s[0] == '|' || s[0] == '>' || s[0] == '\'' || s[0] == '"'
			|| s[0] == '#' || s[0] == '[' || s[0] == ']' || s[0] == '{' || s[0] == '}')
			need_quote = true;
		else if (!strcmp (s, "true") || !strcmp (s, "false") || !strcmp (s, "null")
			|| !strcmp (s, "yes") || !strcmp (s, "no") || !strcmp (s, "on") || !strcmp (s, "off")
			|| !strcmp (s, "~"))
			need_quote = true;
		else
		{
			for (const char *p = s; *p; p++)
			{
				if (*p == ':' && (*(p + 1) == ' ' || *(p + 1) == '\0'))
				{
					need_quote = true;
					break;
				}
				if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '"' || *p == '\\')
				{
					need_quote = true;
					break;
				}
			}
		}

		if (!need_quote)
		{
			char *endp = 0;
			strtod (s, &endp);
			if (endp && *endp == '\0' && endp != s)
				need_quote = true;
		}
	}

	if (need_quote)
	{
		fputc ('"', out);
		for (const u8 *p = (const u8 *)s; *p; p++)
		{
			if (*p == '"')
				fprintf (out, "\\\"");
			else if (*p == '\\')
				fprintf (out, "\\\\");
			else if (*p == '\n')
				fprintf (out, "\\n");
			else if (*p == '\r')
				fprintf (out, "\\r");
			else if (*p == '\t')
				fprintf (out, "\\t");
			else if (*p < 0x20 || (!valid_u8 && *p >= 0x80))
				// Emit a doubled backslash: libyaml decodes a *single*-backslash
				// "\xNN" quoted-scalar escape as the Unicode codepoint U+00NN
				// (re-encoded as multi-byte UTF-8), not as the raw byte NN.
				// "\\\\xNN" decodes (by libyaml) to the literal two chars "\x"
				// followed by "NN", which our own yaml_eval_scalar() below then
				// unescapes back into the single raw byte.
				fprintf (out, "\\\\x%02x", *p);
			else
				fputc (*p, out);
		}
		fputc ('"', out);
	}
	else
	{
		fprintf (out, "%s", s);
	}
}

static enumError byml_parse_str_table (
	byml_ctx_t *ctx, u32 off, const char ***table_out, uint *count_out)
{
	*table_out = 0;
	*count_out = 0;
	if (!off)
		return ERR_OK;
	if (off + 4 > ctx->size)
		return ERR_INVALID_DATA;
	const u8 *p = ctx->data + off;
	if (p[0] != 0xC2)
		return ERR_INVALID_DATA;
	uint count = byml_u24 (p + 1, ctx->is_le);
	if (!count)
		return ERR_OK;
	if (off + 4 + (count + 1) * 4 > ctx->size)
		return ERR_INVALID_DATA;

	const char **table = CALLOC (count, sizeof (char *));
	for (uint i = 0; i < count; i++)
	{
		u32 st_off = byml_u32 (p + 4 + i * 4, ctx->is_le);
		if (off + st_off >= ctx->size)
			continue;
		table[i] = (const char *)(ctx->data + off + st_off);
	}
	*table_out = table;
	*count_out = count;
	return ERR_OK;
}

static enumError byml_print_node (
	FILE *out, byml_ctx_t *ctx, u8 type, u32 val, int indent, int depth)
{
	if (depth > 128)
		return EFBIG;

	switch (type)
	{
		case 0xA0:
		case 0x20:
		{
			if (val < ctx->n_strings && ctx->strings[val])
				yaml_print_string (out, ctx->strings[val]);
			else
				fprintf (out, "\"\"");
			break;
		}
		case 0xA1:
		case 0x21:
		{
			fprintf (out, "\"<blob_idx_%u>\"", val);
			break;
		}
		case 0xD0:
		{
			fprintf (out, "%s", val ? "true" : "false");
			break;
		}
		case 0xD1:
		{
			int32_t sval = (int32_t)val;
			fprintf (out, "%d", sval);
			break;
		}
		case 0xD2:
		{
			fprintf (out, "%u", val);
			break;
		}
		case 0xD3:
		{
			float fval;
			memcpy (&fval, &val, 4);
			if (isnan (fval))
				fprintf (out, ".nan");
			else if (isinf (fval))
				fprintf (out, "%s.inf", fval < 0 ? "-" : "");
			else
			{
				char buf[64];
				snprintf (buf, sizeof (buf), "%.8g", fval);
				if (!strchr (buf, '.') && !strchr (buf, 'e') && !strchr (buf, 'E'))
					strcat (buf, ".0");
				fprintf (out, "%s", buf);
			}
			break;
		}
		case 0xD4:
		{
			if (val + 8 <= ctx->size)
			{
				long long sval = (long long)byml_u64 (ctx->data + val, ctx->is_le);
				fprintf (out, "%lld", sval);
			}
			else
				fprintf (out, "0");
			break;
		}
		case 0xD5:
		{
			if (val + 8 <= ctx->size)
			{
				unsigned long long uval
					= (unsigned long long)byml_u64 (ctx->data + val, ctx->is_le);
				fprintf (out, "%llu", uval);
			}
			else
				fprintf (out, "0");
			break;
		}
		case 0xD6:
		{
			if (val + 8 <= ctx->size)
			{
				double dval;
				u64 uv = byml_u64 (ctx->data + val, ctx->is_le);
				memcpy (&dval, &uv, 8);
				if (isnan (dval))
					fprintf (out, ".nan");
				else if (isinf (dval))
					fprintf (out, "%s.inf", dval < 0 ? "-" : "");
				else
				{
					char buf[64];
					snprintf (buf, sizeof (buf), "%.16g", dval);
					if (!strchr (buf, '.') && !strchr (buf, 'e') && !strchr (buf, 'E'))
						strcat (buf, ".0");
					fprintf (out, "%s", buf);
				}
			}
			else
				fprintf (out, "0.0");
			break;
		}
		case 0xFF:
		{
			fprintf (out, "null");
			break;
		}
		case 0xC0: // Array
		{
			u32 off = val;
			if (byml_is_visited (ctx, off))
			{
				fprintf (out, "*array_0x%x", off);
				break;
			}
			if (off + 4 > ctx->size)
			{
				fprintf (out, "[]");
				break;
			}
			const u8 *p = ctx->data + off;
			if (p[0] != 0xC0)
			{
				fprintf (out, "[]");
				break;
			}
			uint count = byml_u24 (p + 1, ctx->is_le);
			if (!count)
			{
				fprintf (out, "[]");
				break;
			}
			if (off + 4 + count > ctx->size)
			{
				fprintf (out, "[]");
				break;
			}

			const u8 *tags = p + 4;
			u32 val_start = off + 4 + ((count + 3) & ~3);
			if (val_start + count * 4 > ctx->size)
			{
				fprintf (out, "[]");
				break;
			}

			if (ctx->visited_depth < 256)
				((byml_ctx_t *)ctx)->visited_stack[((byml_ctx_t *)ctx)->visited_depth++] = off;

			for (uint i = 0; i < count; i++)
			{
				u8 elem_tag = tags[i];
				u32 elem_val = byml_u32 (ctx->data + val_start + i * 4, ctx->is_le);

				if (i > 0 || indent > 0)
				{
					for (int s = 0; s < indent; s++)
						fputc (' ', out);
				}
				fprintf (out, "- ");

				if (elem_tag == 0xC1)
				{
					u32 d_off = elem_val;
					if (byml_is_visited (ctx, d_off))
					{
						fprintf (out, "*dict_0x%x\n", d_off);
					}
					else if (d_off + 4 <= ctx->size && ctx->data[d_off] == 0xC1)
					{
						uint d_count = byml_u24 (ctx->data + d_off + 1, ctx->is_le);
						if (!d_count)
						{
							fprintf (out, "{}\n");
						}
						else
						{
							fprintf (out, "\n");
							byml_print_node (out, ctx, elem_tag, elem_val, indent + 2, depth + 1);
						}
					}
					else
						fprintf (out, "{}\n");
				}
				else if (elem_tag == 0xC0)
				{
					if (byml_is_visited (ctx, elem_val))
					{
						fprintf (out, "*array_0x%x\n", elem_val);
					}
					else
					{
						fprintf (out, "\n");
						byml_print_node (out, ctx, elem_tag, elem_val, indent + 2, depth + 1);
					}
				}
				else
				{
					byml_print_node (out, ctx, elem_tag, elem_val, indent + 2, depth + 1);
					fputc ('\n', out);
				}
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				((byml_ctx_t *)ctx)->visited_depth--;
			break;
		}
		case 0xC1: // Dictionary
		{
			u32 off = val;
			if (byml_is_visited (ctx, off))
			{
				fprintf (out, "*dict_0x%x", off);
				break;
			}
			if (off + 4 > ctx->size)
			{
				fprintf (out, "{}");
				break;
			}
			const u8 *p = ctx->data + off;
			if (p[0] != 0xC1)
			{
				fprintf (out, "{}");
				break;
			}
			uint count = byml_u24 (p + 1, ctx->is_le);
			if (!count)
			{
				fprintf (out, "{}");
				break;
			}
			if (off + 4 + count * 8 > ctx->size)
			{
				fprintf (out, "{}");
				break;
			}

			if (ctx->visited_depth < 256)
				((byml_ctx_t *)ctx)->visited_stack[((byml_ctx_t *)ctx)->visited_depth++] = off;

			for (uint i = 0; i < count; i++)
			{
				const u8 *entry = p + 4 + i * 8;
				uint key_idx = byml_u24 (entry, ctx->is_le);
				u8 val_type = entry[3];
				u32 child_val = byml_u32 (entry + 4, ctx->is_le);

				for (int s = 0; s < indent; s++)
					fputc (' ', out);
				if (key_idx < ctx->n_hash_keys && ctx->hash_keys[key_idx])
					yaml_print_string (out, ctx->hash_keys[key_idx]);
				else
					fprintf (out, "key_%u", key_idx);
				fprintf (out, ":");

				if (val_type == 0xC0 || val_type == 0xC1)
				{
					if (byml_is_visited (ctx, child_val))
					{
						fprintf (
							out, " *%s_0x%x\n", val_type == 0xC0 ? "array" : "dict", child_val);
					}
					else
					{
						bool is_empty = false;
						if (child_val + 4 <= ctx->size)
						{
							uint c_cnt = byml_u24 (ctx->data + child_val + 1, ctx->is_le);
							if (!c_cnt)
								is_empty = true;
						}
						if (is_empty)
						{
							fprintf (out, " %s\n", val_type == 0xC0 ? "[]" : "{}");
						}
						else
						{
							fprintf (out, "\n");
							byml_print_node (out, ctx, val_type, child_val, indent + 2, depth + 1);
						}
					}
				}
				else
				{
					fprintf (out, " ");
					byml_print_node (out, ctx, val_type, child_val, indent + 2, depth + 1);
					fputc ('\n', out);
				}
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				((byml_ctx_t *)ctx)->visited_depth--;
			break;
		}
		default:
			fprintf (out, "\"<unknown_0x%02x_%u>\"", type, val);
			break;
	}
	return ERR_OK;
}

// Build a YAML document and let LibYAML serialize it.  Keeping the document
// construction here (rather than formatting YAML ourselves) means strings,
// keys, escaping, and nested collections are always emitted as valid YAML.
static int byml_yaml_string (yaml_document_t *doc, const char *str)
{
	if (!str)
		str = "";
	if (is_valid_utf8(str))
		return yaml_document_add_scalar(doc,(yaml_char_t *)YAML_STR_TAG,
			(const yaml_char_t *)str,-1,YAML_DOUBLE_QUOTED_SCALAR_STYLE);

	// YAML requires UTF-8. Preserve invalid BYML bytes as the decoder's
	// reversible literal \\xNN representation before passing them to LibYAML.
	size_t len = strlen(str);
	char *escaped = CALLOC(1,len*4+1), *dest = escaped;
	for (const u8 *src = (const u8 *)str; *src; src++)
		if (*src < 0x20 || *src >= 0x80)
		{
			snprintf(dest,5,"\\x%02x",*src);
			dest += 4;
		}
		else
			*dest++ = *src;
	int node = yaml_document_add_scalar(doc,(yaml_char_t *)YAML_STR_TAG,
		(const yaml_char_t *)escaped,-1,YAML_DOUBLE_QUOTED_SCALAR_STYLE);
	FREE(escaped);
	return node;
}

static int byml_yaml_scalar (yaml_document_t *doc, const char *tag, const char *value)
{
	return yaml_document_add_scalar(doc,(yaml_char_t *)tag,
		(const yaml_char_t *)value,-1,YAML_PLAIN_SCALAR_STYLE);
}

static int byml_yaml_node (yaml_document_t *doc, byml_ctx_t *ctx, u8 type, u32 val, int depth)
{
	char buf[80];
	if (depth > 128)
		return byml_yaml_string(doc,"<BYML nesting limit>");
	switch (type)
	{
		case 0xa0: case 0x20:
			return byml_yaml_string(doc,val < ctx->n_strings && ctx->strings[val] ? ctx->strings[val] : "");
		case 0xa1: case 0x21:
			snprintf(buf,sizeof(buf),"<blob_idx_%u>",val);
			return byml_yaml_string(doc,buf);
		case 0xd0: return byml_yaml_scalar(doc,YAML_BOOL_TAG,val ? "true" : "false");
		case 0xd1:
			snprintf(buf,sizeof(buf),"%d",(int32_t)val);
			return byml_yaml_scalar(doc,YAML_INT_TAG,buf);
		case 0xd2:
			snprintf(buf,sizeof(buf),"%u",val);
			return byml_yaml_scalar(doc,YAML_INT_TAG,buf);
		case 0xd3:
		{
			float f;
			memcpy(&f,&val,sizeof(f));
			if (isnan(f)) return byml_yaml_scalar(doc,YAML_FLOAT_TAG,".nan");
			if (isinf(f)) return byml_yaml_scalar(doc,YAML_FLOAT_TAG,f < 0 ? "-.inf" : ".inf");
			snprintf(buf,sizeof(buf),"%.8g",f);
			if (!strchr(buf,'.') && !strchr(buf,'e') && !strchr(buf,'E')) strcat(buf,".0");
			return byml_yaml_scalar(doc,YAML_FLOAT_TAG,buf);
		}
		case 0xd4:
			snprintf(buf,sizeof(buf),"%lld",val+8 <= ctx->size ? (long long)byml_u64(ctx->data+val,ctx->is_le) : 0ll);
			return byml_yaml_scalar(doc,YAML_INT_TAG,buf);
		case 0xd5:
			snprintf(buf,sizeof(buf),"%llu",val+8 <= ctx->size ? (unsigned long long)byml_u64(ctx->data+val,ctx->is_le) : 0ull);
			return byml_yaml_scalar(doc,YAML_INT_TAG,buf);
		case 0xd6:
		{
			double d = 0.0;
			if (val+8 <= ctx->size)
			{
				u64 u = byml_u64(ctx->data+val,ctx->is_le);
				memcpy(&d,&u,sizeof(d));
			}
			if (isnan(d)) return byml_yaml_scalar(doc,YAML_FLOAT_TAG,".nan");
			if (isinf(d)) return byml_yaml_scalar(doc,YAML_FLOAT_TAG,d < 0 ? "-.inf" : ".inf");
			snprintf(buf,sizeof(buf),"%.16g",d);
			if (!strchr(buf,'.') && !strchr(buf,'e') && !strchr(buf,'E')) strcat(buf,".0");
			return byml_yaml_scalar(doc,YAML_FLOAT_TAG,buf);
		}
		case 0xff: return byml_yaml_scalar(doc,YAML_NULL_TAG,"null");
	}
	if (type != 0xc0 && type != 0xc1)
	{
		snprintf(buf,sizeof(buf),"<unknown_0x%02x_%u>",type,val);
		return byml_yaml_string(doc,buf);
	}

	int node = type == 0xc0
		? yaml_document_add_sequence(doc,(yaml_char_t *)YAML_SEQ_TAG,YAML_BLOCK_SEQUENCE_STYLE)
		: yaml_document_add_mapping(doc,(yaml_char_t *)YAML_MAP_TAG,YAML_BLOCK_MAPPING_STYLE);
	if (!node || val+4 > ctx->size || ctx->data[val] != type || byml_is_visited(ctx,val))
		return node;
	uint count = byml_u24(ctx->data+val+1,ctx->is_le);
	if (type == 0xc0 && (val+4+count > ctx->size || val+4+((count+3)&~3)+count*4 > ctx->size)) count=0;
	if (type == 0xc1 && val+4+count*8 > ctx->size) count=0;
	if (ctx->visited_depth >= sizeof(ctx->visited_stack)/sizeof(*ctx->visited_stack)) return node;
	ctx->visited_stack[ctx->visited_depth++] = val;
	if (type == 0xc0)
	{
		const u8 *tags = ctx->data+val+4;
		u32 vals = val+4+((count+3)&~3);
		for (uint i=0; i<count; i++)
		{
			int item=byml_yaml_node(doc,ctx,tags[i],byml_u32(ctx->data+vals+i*4,ctx->is_le),depth+1);
			if (!item || !yaml_document_append_sequence_item(doc,node,item)) { ctx->visited_depth--; return 0; }
		}
	}
	else for (uint i=0; i<count; i++)
	{
		const u8 *entry=ctx->data+val+4+i*8;
		uint key=byml_u24(entry,ctx->is_le);
		snprintf(buf,sizeof(buf),"key_%u",key);
		int k=byml_yaml_string(doc,key < ctx->n_hash_keys && ctx->hash_keys[key] ? ctx->hash_keys[key] : buf);
		int item=byml_yaml_node(doc,ctx,entry[3],byml_u32(entry+4,ctx->is_le),depth+1);
		if (!k || !item || !yaml_document_append_mapping_pair(doc,node,k,item)) { ctx->visited_depth--; return 0; }
	}
	ctx->visited_depth--;
	return node;
}

enumError DecodeBYML_YAML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;
	bool is_le = false;
	if (!memcmp (data, "YB", 2))
		is_le = true;
	else if (!memcmp (data, "BY", 2))
		is_le = false;
	else
		return ERR_INVALID_DATA;

	u16 version = byml_u16 (data + 2, is_le);
	if (version < 1 || version > 4)
		return ERR_INVALID_DATA;

	u32 hash_key_table_off = byml_u32 (data + 4, is_le);
	u32 str_table_off = byml_u32 (data + 8, is_le);
	u32 root_node_off = byml_u32 (data + 12, is_le);

	byml_ctx_t ctx = { 0 };
	ctx.data = data;
	ctx.size = size;
	ctx.is_le = is_le;
	ctx.version = version;

	enumError err
		= byml_parse_str_table (&ctx, hash_key_table_off, &ctx.hash_keys, &ctx.n_hash_keys);
	if (err)
		return err;
	err = byml_parse_str_table (&ctx, str_table_off, &ctx.strings, &ctx.n_strings);
	if (err)
	{
		FREE (ctx.hash_keys);
		return err;
	}

	yaml_document_t document;
	if (!yaml_document_initialize(&document,0,0,0,1,1))
		err = ERR_OUT_OF_MEMORY;
	else
	{
		int root = root_node_off < size
			? byml_yaml_node(&document,&ctx,data[root_node_off],root_node_off,0)
			: yaml_document_add_mapping(&document,(yaml_char_t *)YAML_MAP_TAG,YAML_BLOCK_MAPPING_STYLE);
		if (!root)
		{
			yaml_document_delete(&document);
			err = ERR_OUT_OF_MEMORY;
		}
		else
		{
			yaml_emitter_t emitter;
			if (!yaml_emitter_initialize(&emitter))
			{
				yaml_document_delete(&document);
				err = ERR_OUT_OF_MEMORY;
			}
			else
			{
				yaml_emitter_set_output_file(&emitter,out);
				if (!yaml_emitter_dump(&emitter,&document))
					err = ERR_WRITE_FAILED;
				yaml_emitter_delete(&emitter);
			}
		}
	}

	FREE (ctx.hash_keys);
	FREE (ctx.strings);
	return err;
}

typedef struct str_list_t
{
	char **items;
	uint count;
	uint cap;
} str_list_t;

static void str_list_init (str_list_t *l)
{
	l->items = 0;
	l->count = 0;
	l->cap = 0;
}

static void str_list_free (str_list_t *l)
{
	if (l->items)
	{
		for (uint i = 0; i < l->count; i++)
			FREE (l->items[i]);
		FREE (l->items);
	}
	memset (l, 0, sizeof (*l));
}

static int str_list_find (const str_list_t *l, const char *s)
{
	for (uint i = 0; i < l->count; i++)
		if (!strcmp (l->items[i], s))
			return (int)i;
	return -1;
}

static int str_list_add (str_list_t *l, const char *s)
{
	int idx = str_list_find (l, s);
	if (idx >= 0)
		return idx;
	if (l->count >= l->cap)
	{
		l->cap = l->cap ? l->cap * 2 : 16;
		l->items = REALLOC (l->items, l->cap * sizeof (char *));
	}
	l->items[l->count] = STRDUP (s);
	return (int)l->count++;
}

static int str_cmp_qsort (const void *a, const void *b)
{
	const char *const *sa = a;
	const char *const *sb = b;
	return strcmp (*sa, *sb);
}

static void collect_byml_symbols (const bf_val_t *val, str_list_t *keys, str_list_t *strs)
{
	if (!val)
		return;
	if (val->type == BF_T_NODE && val->u.node)
	{
		const bf_node_t *node = val->u.node;
		for (uint i = 0; i < node->n; i++)
		{
			if (node->kv[i].key)
				str_list_add (keys, node->kv[i].key);
			collect_byml_symbols (&node->kv[i].val, keys, strs);
		}
	}
	else if (val->type == BF_T_LIST && val->u.list)
	{
		const bf_list_t *list = val->u.list;
		for (uint i = 0; i < list->n; i++)
			collect_byml_symbols (&list->items[i], keys, strs);
	}
	else if (val->type == BF_T_STR && val->u.s)
	{
		str_list_add (strs, val->u.s);
	}
}

typedef struct byml_writer_t
{
	u8 *buf;
	uint len;
	uint cap;
	bool is_le;
} byml_writer_t;

static void bw_init (byml_writer_t *w, bool is_le)
{
	w->cap = 1024;
	w->buf = CALLOC (1, w->cap);
	w->len = 0;
	w->is_le = is_le;
}

static void bw_align (byml_writer_t *w, uint alignment)
{
	uint rem = w->len % alignment;
	if (rem)
	{
		uint pad = alignment - rem;
		while (w->len + pad > w->cap)
		{
			w->cap *= 2;
			w->buf = REALLOC (w->buf, w->cap);
		}
		memset (w->buf + w->len, 0, pad);
		w->len += pad;
	}
}

static void bw_append (byml_writer_t *w, const void *data, uint size)
{
	while (w->len + size > w->cap)
	{
		w->cap *= 2;
		w->buf = REALLOC (w->buf, w->cap);
	}
	if (data)
		memcpy (w->buf + w->len, data, size);
	else
		memset (w->buf + w->len, 0, size);
	w->len += size;
}

static void bw_u8 (byml_writer_t *w, u8 v)
{
	bw_append (w, &v, 1);
}

static void bw_u24 (byml_writer_t *w, u32 v)
{
	u8 b[4];
	if (w->is_le)
	{
		wr_le32 (b, v);
		bw_append (w, b, 3);
	}
	else
	{
		wr_be32 (b, v);
		bw_append (w, b + 1, 3);
	}
}

static void bw_put_u32 (byml_writer_t *w, uint pos, u32 v)
{
	if (pos + 4 <= w->len)
	{
		if (w->is_le)
			wr_le32 (w->buf + pos, v);
		else
			wr_be32 (w->buf + pos, v);
	}
}

static uint write_byml_str_table (byml_writer_t *w, const str_list_t *list)
{
	if (!list || !list->count)
		return 0;
	bw_align (w, 4);
	uint start = w->len;
	bw_u8 (w, 0xC2);
	bw_u24 (w, list->count);
	uint offsets_pos = w->len;
	bw_append (w, 0, (list->count + 1) * 4);

	for (uint i = 0; i < list->count; i++)
	{
		uint str_off = w->len - start;
		bw_put_u32 (w, offsets_pos + i * 4, str_off);
		const char *s = list->items[i];
		bw_append (w, s, (uint)strlen (s) + 1);
	}
	bw_put_u32 (w, offsets_pos + list->count * 4, w->len - start);
	return start;
}

typedef struct kv_sort_entry_t
{
	uint key_idx;
	uint orig_idx;
} kv_sort_entry_t;

static int kv_sort_cmp (const void *a, const void *b)
{
	const kv_sort_entry_t *ea = a;
	const kv_sort_entry_t *eb = b;
	return (ea->key_idx > eb->key_idx) - (ea->key_idx < eb->key_idx);
}

static uint write_byml_node (
	byml_writer_t *w, const bf_val_t *val, const str_list_t *keys, const str_list_t *strs)
{
	bw_align (w, 4);
	uint start = w->len;

	if (val->type == BF_T_NODE && val->u.node)
	{
		const bf_node_t *node = val->u.node;
		bw_u8 (w, 0xC1);
		bw_u24 (w, node->n);

		kv_sort_entry_t *sort_tab = CALLOC (node->n, sizeof (kv_sort_entry_t));
		for (uint i = 0; i < node->n; i++)
		{
			sort_tab[i].orig_idx = i;
			sort_tab[i].key_idx
				= (uint)str_list_find (keys, node->kv[i].key ? node->kv[i].key : "");
		}
		if (node->n > 1)
			qsort (sort_tab, node->n, sizeof (kv_sort_entry_t), kv_sort_cmp);

		uint entries_pos = w->len;
		bw_append (w, 0, node->n * 8);

		for (uint i = 0; i < node->n; i++)
		{
			uint oi = sort_tab[i].orig_idx;
			uint ki = sort_tab[i].key_idx;
			const bf_val_t *child = &node->kv[oi].val;
			u8 vtype = 0xFF;
			u32 vval = 0;

			if (child->type == BF_T_NODE)
			{
				vtype = 0xC1;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_LIST)
			{
				vtype = 0xC0;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_STR)
			{
				vtype = 0xA0;
				vval = (u32)str_list_find (strs, child->u.s ? child->u.s : "");
			}
			else if (child->type == BF_T_BOOL)
			{
				vtype = 0xD0;
				vval = child->u.b ? 1 : 0;
			}
			else if (child->type == BF_T_INT)
			{
				vtype = 0xD1;
				vval = (u32)child->u.i;
			}
			else if (child->type == BF_T_UINT)
			{
				vtype = 0xD2;
				vval = (u32)child->u.i;
			}
			else if (child->type == BF_T_FLOAT)
			{
				vtype = 0xD3;
				float f = (float)child->u.f;
				memcpy (&vval, &f, 4);
			}

			uint epos = entries_pos + i * 8;
			u8 b[4];
			if (w->is_le)
			{
				wr_le32 (b, ki);
				w->buf[epos] = b[0];
				w->buf[epos + 1] = b[1];
				w->buf[epos + 2] = b[2];
				w->buf[epos + 3] = vtype;
				wr_le32 (w->buf + epos + 4, vval);
			}
			else
			{
				wr_be32 (b, ki);
				w->buf[epos] = b[1];
				w->buf[epos + 1] = b[2];
				w->buf[epos + 2] = b[3];
				w->buf[epos + 3] = vtype;
				wr_be32 (w->buf + epos + 4, vval);
			}
		}
		FREE (sort_tab);
		return start;
	}
	else if (val->type == BF_T_LIST && val->u.list)
	{
		const bf_list_t *list = val->u.list;
		bw_u8 (w, 0xC0);
		bw_u24 (w, list->n);
		uint tags_pos = w->len;
		bw_append (w, 0, list->n);
		bw_align (w, 4);
		uint vals_pos = w->len;
		bw_append (w, 0, list->n * 4);

		for (uint i = 0; i < list->n; i++)
		{
			const bf_val_t *child = &list->items[i];
			u8 vtype = 0xFF;
			u32 vval = 0;

			if (child->type == BF_T_NODE)
			{
				vtype = 0xC1;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_LIST)
			{
				vtype = 0xC0;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_STR)
			{
				vtype = 0xA0;
				vval = (u32)str_list_find (strs, child->u.s ? child->u.s : "");
			}
			else if (child->type == BF_T_BOOL)
			{
				vtype = 0xD0;
				vval = child->u.b ? 1 : 0;
			}
			else if (child->type == BF_T_INT)
			{
				vtype = 0xD1;
				vval = (u32)child->u.i;
			}
			else if (child->type == BF_T_UINT)
			{
				vtype = 0xD2;
				vval = (u32)child->u.i;
			}
			else if (child->type == BF_T_FLOAT)
			{
				vtype = 0xD3;
				float f = (float)child->u.f;
				memcpy (&vval, &f, 4);
			}

			w->buf[tags_pos + i] = vtype;
			bw_put_u32 (w, vals_pos + i * 4, vval);
		}
		return start;
	}
	return start;
}

static void yaml_eval_scalar (const char *s, bf_val_t *val)
{
	memset (val, 0, sizeof (*val));
	if (!s || !*s)
	{
		val->type = BF_T_NONE;
		return;
	}
	if (!strcmp (s, "true") || !strcmp (s, "True") || !strcmp (s, "TRUE"))
	{
		val->type = BF_T_BOOL;
		val->u.b = true;
		return;
	}
	if (!strcmp (s, "false") || !strcmp (s, "False") || !strcmp (s, "FALSE"))
	{
		val->type = BF_T_BOOL;
		val->u.b = false;
		return;
	}
	if (!strcmp (s, "null") || !strcmp (s, "~") || !strcmp (s, "None"))
	{
		val->type = BF_T_NONE;
		return;
	}
	if (!strcmp (s, ".nan") || !strcmp (s, "nan") || !strcmp (s, "NaN"))
	{
		val->type = BF_T_FLOAT;
		val->u.f = 0.0f / 0.0f;
		return;
	}
	if (!strcmp (s, ".inf") || !strcmp (s, "inf") || !strcmp (s, "Infinity"))
	{
		val->type = BF_T_FLOAT;
		val->u.f = 1.0f / 0.0f;
		return;
	}
	if (!strcmp (s, "-.inf") || !strcmp (s, "-inf") || !strcmp (s, "-Infinity"))
	{
		val->type = BF_T_FLOAT;
		val->u.f = -1.0f / 0.0f;
		return;
	}
	if ((s[0] == '"' && s[strlen (s) - 1] == '"') || (s[0] == '\'' && s[strlen (s) - 1] == '\''))
	{
		uint len = (uint)strlen (s);
		char *str = CALLOC (1, len);
		uint out_pos = 0;
		for (uint i = 1; i < len - 1; i++)
		{
			if (s[i] == '\\' && i + 1 < len - 1)
			{
				i++;
				if (s[i] == 'n')
					str[out_pos++] = '\n';
				else if (s[i] == 'r')
					str[out_pos++] = '\r';
				else if (s[i] == 't')
					str[out_pos++] = '\t';
				else if (s[i] == '\\')
					str[out_pos++] = '\\';
				else if (s[i] == '"')
					str[out_pos++] = '"';
				else if (s[i] == '\'')
					str[out_pos++] = '\'';
				else if (s[i] == 'x' && i + 2 < len - 1 && isxdigit ((u8)s[i + 1])
					&& isxdigit ((u8)s[i + 2]))
				{
					char hex[3] = { s[i + 1], s[i + 2], 0 };
					str[out_pos++] = (char)(u8)strtoul (hex, 0, 16);
					i += 2;
				}
				else
					str[out_pos++] = s[i];
			}
			else
				str[out_pos++] = s[i];
		}
		val->type = BF_T_STR;
		val->u.s = str;
		return;
	}
	char *endp = 0;
	long long lval = strtoll (s, &endp, 0);
	if (endp && !*endp)
	{
		// A positive literal that doesn't fit in a signed 32-bit int can only
		// have come from our own TEXT dump of a BYML U32 (0xD2) value: S32
		// (0xD1) values are always printed with a leading '-' when negative,
		// so their magnitude as printed text never exceeds INT32_MAX. Tag it
		// as unsigned so the BYML encoder writes it back with type 0xD2
		// instead of silently reinterpreting the bits as a negative S32.
		if (s[0] != '-' && lval > INT32_MAX && lval <= UINT32_MAX)
		{
			val->type = BF_T_UINT;
			val->u.i = (int)(u32)lval;
		}
		else
		{
			val->type = BF_T_INT;
			val->u.i = (int)lval;
		}
		return;
	}
	double dval = strtod (s, &endp);
	if (endp && !*endp)
	{
		val->type = BF_T_FLOAT;
		val->u.f = dval;
		return;
	}
	val->type = BF_T_STR;
	val->u.s = STRDUP (s);
}

static void yaml_eval_node (const yaml_node_t *node, bf_val_t *val)
{
	const char *value = node && node->type == YAML_SCALAR_NODE
		? (const char *)node->data.scalar.value : "";
	// The parser has already resolved quotes and escapes.  Its tag is the
	// authoritative type information: a quoted "true" is a YAML string, not a
	// boolean to be guessed again from its text.
	if (node && node->tag && !strcmp((const char *)node->tag,YAML_STR_TAG))
	{
		val->type = BF_T_STR;
		val->u.s = STRDUP(value);
		return;
	}
	yaml_eval_scalar(value,val);
}

#include <yaml.h>

static enumError fill_bf_node_from_yaml (yaml_document_t *doc, int node_id, bf_node_t *out_dict);
static enumError fill_bf_list_from_yaml (yaml_document_t *doc, int node_id, bf_list_t *out_list);

static enumError fill_bf_list_from_yaml (yaml_document_t *doc, int node_id, bf_list_t *out_list)
{
	yaml_node_t *node = yaml_document_get_node (doc, node_id);
	if (!node || node->type != YAML_SEQUENCE_NODE)
		return ERR_SEMANTIC;

	for (yaml_node_item_t *i = node->data.sequence.items.start; i < node->data.sequence.items.top;
		i++)
	{
		yaml_node_t *item_node = yaml_document_get_node (doc, *i);
		if (!item_node)
			continue;

		if (item_node->type == YAML_SCALAR_NODE)
		{
			bf_val_t sval;
			yaml_eval_node (item_node, &sval);
			if (sval.type == BF_T_STR)
			{
				BFListAddStr (out_list, sval.u.s);
				FREE (sval.u.s);
			}
			else if (sval.type == BF_T_INT)
				BFListAddInt (out_list, sval.u.i);
			else if (sval.type == BF_T_UINT)
			{
				BFListAddInt (out_list, sval.u.i);
				if (out_list->n)
					out_list->items[out_list->n - 1].type = BF_T_UINT;
			}
			else if (sval.type == BF_T_FLOAT)
				BFListAddFloat (out_list, sval.u.f);
			else if (sval.type == BF_T_BOOL)
				BFListAddBool (out_list, sval.u.b);
		}
		else if (item_node->type == YAML_SEQUENCE_NODE)
		{
			bf_list_t *cl = BFListAddList (out_list);
			if (!cl)
				return ERR_OUT_OF_MEMORY;
			enumError err = fill_bf_list_from_yaml (doc, *i, cl);
			if (err)
				return err;
		}
		else if (item_node->type == YAML_MAPPING_NODE)
		{
			bf_node_t *cn = BFListAddNode (out_list);
			if (!cn)
				return ERR_OUT_OF_MEMORY;
			enumError err = fill_bf_node_from_yaml (doc, *i, cn);
			if (err)
				return err;
		}
	}
	return ERR_OK;
}

static enumError fill_bf_node_from_yaml (yaml_document_t *doc, int node_id, bf_node_t *out_dict)
{
	yaml_node_t *node = yaml_document_get_node (doc, node_id);
	if (!node || node->type != YAML_MAPPING_NODE)
		return ERR_SEMANTIC;

	for (yaml_node_pair_t *p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top;
		p++)
	{
		yaml_node_t *key_node = yaml_document_get_node (doc, p->key);
		yaml_node_t *val_node = yaml_document_get_node (doc, p->value);
		if (!key_node || key_node->type != YAML_SCALAR_NODE || !val_node)
			continue;
		const char *key_str = (const char *)key_node->data.scalar.value;

		if (val_node->type == YAML_SCALAR_NODE)
		{
			bf_val_t sval;
			yaml_eval_node (val_node, &sval);
			if (sval.type == BF_T_STR)
			{
				BFNodeSetStr (out_dict, key_str, sval.u.s);
				FREE (sval.u.s);
			}
			else if (sval.type == BF_T_INT)
				BFNodeSetInt (out_dict, key_str, sval.u.i);
			else if (sval.type == BF_T_UINT)
			{
				BFNodeSetInt (out_dict, key_str, sval.u.i);
				bf_val_t *v = BFNodeGet (out_dict, key_str);
				if (v)
					v->type = BF_T_UINT;
			}
			else if (sval.type == BF_T_FLOAT)
				BFNodeSetFloat (out_dict, key_str, sval.u.f);
			else if (sval.type == BF_T_BOOL)
				BFNodeSetBool (out_dict, key_str, sval.u.b);
			else if (sval.type == BF_T_NONE)
				BFNodeSetNone (out_dict, key_str);
		}
		else if (val_node->type == YAML_SEQUENCE_NODE)
		{
			bf_list_t *cl = BFNodeSetList (out_dict, key_str);
			if (!cl)
				return ERR_OUT_OF_MEMORY;
			enumError err = fill_bf_list_from_yaml (doc, p->value, cl);
			if (err)
				return err;
		}
		else if (val_node->type == YAML_MAPPING_NODE)
		{
			bf_node_t *cn = BFNodeSetNode (out_dict, key_str);
			if (!cn)
				return ERR_OUT_OF_MEMORY;
			enumError err = fill_bf_node_from_yaml (doc, p->value, cn);
			if (err)
				return err;
		}
	}
	return ERR_OK;
}

enumError EncodeBYML_Text (
	u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version)
{
	if (!dest || !dest_size || !text)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	yaml_parser_t parser;
	yaml_document_t document;
	if (!yaml_parser_initialize (&parser))
		return ERR_OUT_OF_MEMORY;
	yaml_parser_set_input_string (&parser, (const unsigned char *)text, text_len);
	if (!yaml_parser_load (&parser, &document))
	{
		yaml_parser_delete (&parser);
		return ERR_SEMANTIC;
	}

	bf_node_t root;
	BFNodeInit (&root);

	yaml_node_t *root_node = yaml_document_get_root_node (&document);
	if (root_node)
	{
		if (root_node->type == YAML_MAPPING_NODE)
		{
			fill_bf_node_from_yaml (&document,
				yaml_document_get_root_node (&document) - document.nodes.start + 1, &root);
		}
		else
		{
			// Not a mapping at root, technically BYML requires mapping at root but let's ignore or
			// error. (If BYML accepts lists at root, we'd have to restructure. Assuming dict at
			// root here.)
		}
	}

	yaml_document_delete (&document);
	yaml_parser_delete (&parser);

	str_list_t keys, strs;
	str_list_init (&keys);
	str_list_init (&strs);

	bf_val_t root_val;
	root_val.type = BF_T_NODE;
	root_val.u.node = &root;
	collect_byml_symbols (&root_val, &keys, &strs);

	if (keys.count > 1)
		qsort (keys.items, keys.count, sizeof (char *), str_cmp_qsort);
	if (strs.count > 1)
		qsort (strs.items, strs.count, sizeof (char *), str_cmp_qsort);

	byml_writer_t w;
	bw_init (&w, is_le);
	// Header placeholder: 16 bytes
	bw_append (&w, 0, 16);

	uint hash_key_off = write_byml_str_table (&w, &keys);
	uint str_table_off = write_byml_str_table (&w, &strs);
	uint root_off = write_byml_node (&w, &root_val, &keys, &strs);

	// Write header
	w.buf[0] = is_le ? 'Y' : 'B';
	w.buf[1] = is_le ? 'B' : 'Y';
	if (is_le)
	{
		wr_le16 (w.buf + 2, version ? version : 1);
		wr_le32 (w.buf + 4, hash_key_off);
		wr_le32 (w.buf + 8, str_table_off);
		wr_le32 (w.buf + 12, root_off);
	}
	else
	{
		wr_be16 (w.buf + 2, version ? version : 1);
		wr_be32 (w.buf + 4, hash_key_off);
		wr_be32 (w.buf + 8, str_table_off);
		wr_be32 (w.buf + 12, root_off);
	}

	str_list_free (&keys);
	str_list_free (&strs);
	BFNodeFree (&root);

	*dest = w.buf;
	*dest_size = w.len;
	return ERR_OK;
}
