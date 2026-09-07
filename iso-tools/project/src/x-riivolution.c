// x-riivolution.c - Comprehensive Riivolution ISO Builder implementation for WIT
// Supports full Riivolution XML specification, option selection, variable substitution,
// folder/file replacements, and DOL memory patching with dynamic section allocation.

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "x-riivolution.h"
#include "iso-interface.h"
#include "patch.h"
#include "dclib/dclib-debug.h"

//=============================================================================
// 1. Lightweight XML Parser for Riivolution XML
//=============================================================================

typedef struct xml_attr_t
{
    char *name;
    char *value;
    struct xml_attr_t *next;
} xml_attr_t;

typedef struct xml_node_t
{
    char *name;
    char *text;
    xml_attr_t *attrs;
    struct xml_node_t *first_child;
    struct xml_node_t *last_child;
    struct xml_node_t *next_sibling;
} xml_node_t;

static xml_node_t *xml_node_create(const char *name)
{
    xml_node_t *node = CALLOC(1, sizeof(xml_node_t));
    if (name)
        node->name = STRDUP(name);
    return node;
}

static void xml_node_add_attr(xml_node_t *node, const char *name, const char *value)
{
    if (!node || !name)
        return;
    xml_attr_t *attr = CALLOC(1, sizeof(xml_attr_t));
    attr->name = STRDUP(name);
    attr->value = value ? STRDUP(value) : STRDUP("");
    
    if (!node->attrs)
    {
        node->attrs = attr;
    }
    else
    {
        xml_attr_t *cur = node->attrs;
        while (cur->next)
            cur = cur->next;
        cur->next = attr;
    }
}

static void xml_node_add_child(xml_node_t *parent, xml_node_t *child)
{
    if (!parent || !child)
        return;
    if (!parent->first_child)
    {
        parent->first_child = child;
        parent->last_child = child;
    }
    else
    {
        parent->last_child->next_sibling = child;
        parent->last_child = child;
    }
}

static void xml_free_node(xml_node_t *node)
{
    if (!node)
        return;
    FREE(node->name);
    FREE(node->text);
    xml_attr_t *a = node->attrs;
    while (a)
    {
        xml_attr_t *next = a->next;
        FREE(a->name);
        FREE(a->value);
        FREE(a);
        a = next;
    }
    xml_node_t *child = node->first_child;
    while (child)
    {
        xml_node_t *next = child->next_sibling;
        xml_free_node(child);
        child = next;
    }
    FREE(node);
}

static const char *xml_get_attr(const xml_node_t *node, const char *attr_name)
{
    if (!node || !attr_name)
        return NULL;
    for (const xml_attr_t *a = node->attrs; a; a = a->next)
    {
        if (!strcasecmp(a->name, attr_name))
            return a->value;
    }
    return NULL;
}

static bool xml_get_attr_bool(const xml_node_t *node, const char *attr_name, bool default_val)
{
    const char *val = xml_get_attr(node, attr_name);
    if (!val || !*val)
        return default_val;
    if (!strcasecmp(val, "true") || !strcasecmp(val, "1") || !strcasecmp(val, "yes"))
        return true;
    if (!strcasecmp(val, "false") || !strcasecmp(val, "0") || !strcasecmp(val, "no"))
        return false;
    return default_val;
}

static u32 xml_get_attr_u32(const xml_node_t *node, const char *attr_name, u32 default_val)
{
    const char *val = xml_get_attr(node, attr_name);
    if (!val || !*val)
        return default_val;
    while (*val && *val <= ' ')
        val++;
    if (*val == '0' && (val[1] == 'x' || val[1] == 'X'))
        return (u32)strtoul(val + 2, NULL, 16);
    for (const char *p = val; *p; p++)
    {
        if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
            return (u32)strtoul(val, NULL, 16);
    }
    return (u32)strtoul(val, NULL, 10);
}

static u8 *xml_parse_hex_bytes(const char *hex_str, uint *out_len)
{
    if (out_len)
        *out_len = 0;
    if (!hex_str)
        return NULL;
    while (*hex_str && *hex_str <= ' ')
        hex_str++;
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X'))
        hex_str += 2;
    
    uint hex_count = 0;
    for (const char *p = hex_str; *p; p++)
    {
        if (isxdigit((int)*p))
            hex_count++;
        else if (*p > ' ')
            break;
    }
    
    uint n_bytes = hex_count / 2;
    if (!n_bytes)
        return NULL;
    
    u8 *buf = MALLOC(n_bytes);
    uint b_idx = 0;
    for (const char *p = hex_str; *p && b_idx < n_bytes; )
    {
        while (*p && *p <= ' ')
            p++;
        if (!isxdigit((int)*p) || !isxdigit((int)*(p + 1)))
            break;
        char hex_byte[3] = { p[0], p[1], 0 };
        buf[b_idx++] = (u8)strtoul(hex_byte, NULL, 16);
        p += 2;
    }
    if (out_len)
        *out_len = b_idx;
    return buf;
}

static void xml_unescape_entities(char *str)
{
    if (!str)
        return;
    char *src = str;
    char *dst = str;
    while (*src)
    {
        if (*src == '&')
        {
            if (!strncmp(src, "&amp;", 5))
            {
                *dst++ = '&';
                src += 5;
            }
            else if (!strncmp(src, "&lt;", 4))
            {
                *dst++ = '<';
                src += 4;
            }
            else if (!strncmp(src, "&gt;", 4))
            {
                *dst++ = '>';
                src += 4;
            }
            else if (!strncmp(src, "&quot;", 6))
            {
                *dst++ = '"';
                src += 6;
            }
            else if (!strncmp(src, "&apos;", 6))
            {
                *dst++ = '\'';
                src += 6;
            }
            else if (src[1] == '#' && (src[2] == 'x' || src[2] == 'X'))
            {
                char *end = NULL;
                long code = strtol(src + 3, &end, 16);
                if (end && *end == ';')
                {
                    *dst++ = (char)code;
                    src = end + 1;
                }
                else
                {
                    *dst++ = *src++;
                }
            }
            else if (src[1] == '#')
            {
                char *end = NULL;
                long code = strtol(src + 2, &end, 10);
                if (end && *end == ';')
                {
                    *dst++ = (char)code;
                    src = end + 1;
                }
                else
                {
                    *dst++ = *src++;
                }
            }
            else
            {
                *dst++ = *src++;
            }
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static xml_node_t *xml_parse_buffer(char *buf, size_t len)
{
    char *ptr = buf;
    char *end = buf + len;
    xml_node_t *root = NULL;
    
    #define MAX_XML_DEPTH 64
    xml_node_t *stack[MAX_XML_DEPTH];
    int depth = 0;

    while (ptr < end)
    {
        while (ptr < end && (uchar)*ptr <= ' ')
            ptr++;
        if (ptr >= end)
            break;

        if (*ptr != '<')
        {
            while (ptr < end && *ptr != '<')
                ptr++;
            continue;
        }

        ptr++;

        if (ptr < end && *ptr == '?')
        {
            while (ptr + 1 < end && !(ptr[0] == '?' && ptr[1] == '>'))
                ptr++;
            if (ptr + 1 < end)
                ptr += 2;
            continue;
        }
        if (ptr + 2 < end && ptr[0] == '!' && ptr[1] == '-' && ptr[2] == '-')
        {
            ptr += 3;
            while (ptr + 2 < end && !(ptr[0] == '-' && ptr[1] == '-' && ptr[2] == '>'))
                ptr++;
            if (ptr + 2 < end)
                ptr += 3;
            continue;
        }
        if (ptr < end && *ptr == '!')
        {
            while (ptr < end && *ptr != '>')
                ptr++;
            if (ptr < end)
                ptr++;
            continue;
        }

        if (ptr < end && *ptr == '/')
        {
            ptr++;
            while (ptr < end && (uchar)*ptr <= ' ')
                ptr++;
            while (ptr < end && *ptr != '>')
                ptr++;
            if (ptr < end)
                ptr++;
            if (depth > 0)
                depth--;
            continue;
        }

        char *tag_start = ptr;
        while (ptr < end && !isspace((int)*ptr) && *ptr != '>' && *ptr != '/')
            ptr++;
        size_t tag_len = ptr - tag_start;
        if (!tag_len)
            continue;

        char tag_name[128];
        if (tag_len >= sizeof(tag_name))
            tag_len = sizeof(tag_name) - 1;
        memcpy(tag_name, tag_start, tag_len);
        tag_name[tag_len] = '\0';

        xml_node_t *node = xml_node_create(tag_name);
        if (depth == 0)
        {
            if (!root)
                root = node;
        }
        else
        {
            xml_node_add_child(stack[depth - 1], node);
        }

        bool is_self_closing = false;
        while (ptr < end)
        {
            while (ptr < end && isspace((int)*ptr))
                ptr++;
            if (ptr >= end || *ptr == '>' || *ptr == '/')
                break;

            char *attr_name_start = ptr;
            while (ptr < end && !isspace((int)*ptr) && *ptr != '=' && *ptr != '>' && *ptr != '/')
                ptr++;
            size_t a_name_len = ptr - attr_name_start;
            char a_name[128];
            if (a_name_len >= sizeof(a_name))
                a_name_len = sizeof(a_name) - 1;
            memcpy(a_name, attr_name_start, a_name_len);
            a_name[a_name_len] = '\0';

            while (ptr < end && isspace((int)*ptr))
                ptr++;

            char *a_val = "";
            char a_val_buf[1024];
            a_val_buf[0] = '\0';

            if (ptr < end && *ptr == '=')
            {
                ptr++;
                while (ptr < end && isspace((int)*ptr))
                    ptr++;
                if (ptr < end && (*ptr == '"' || *ptr == '\''))
                {
                    char quote = *ptr++;
                    char *val_start = ptr;
                    while (ptr < end && *ptr != quote)
                        ptr++;
                    size_t v_len = ptr - val_start;
                    if (v_len >= sizeof(a_val_buf))
                        v_len = sizeof(a_val_buf) - 1;
                    memcpy(a_val_buf, val_start, v_len);
                    a_val_buf[v_len] = '\0';
                    if (ptr < end && *ptr == quote)
                        ptr++;
                }
                else
                {
                    char *val_start = ptr;
                    while (ptr < end && !isspace((int)*ptr) && *ptr != '>')
                        ptr++;
                    size_t v_len = ptr - val_start;
                    if (v_len >= sizeof(a_val_buf))
                        v_len = sizeof(a_val_buf) - 1;
                    memcpy(a_val_buf, val_start, v_len);
                    a_val_buf[v_len] = '\0';
                }
                xml_unescape_entities(a_val_buf);
                a_val = a_val_buf;
            }
            xml_node_add_attr(node, a_name, a_val);
        }

        while (ptr < end && isspace((int)*ptr))
            ptr++;

        if (ptr < end && *ptr == '/')
        {
            is_self_closing = true;
            ptr++;
        }
        if (ptr < end && *ptr == '>')
            ptr++;

        if (!is_self_closing)
        {
            if (depth < MAX_XML_DEPTH)
                stack[depth++] = node;
        }
    }

    return root;
}

//=============================================================================
// 2. Riivolution Document Model & XML Parsing
//=============================================================================

static void riiv_param_set(riiv_param_t **head, const char *name, const char *value)
{
    if (!head || !name)
        return;
    for (riiv_param_t *p = *head; p; p = p->next)
    {
        if (!strcasecmp(p->name, name))
        {
            FREE(p->value);
            p->value = value ? STRDUP(value) : STRDUP("");
            return;
        }
    }
    riiv_param_t *p = CALLOC(1, sizeof(riiv_param_t));
    p->name = STRDUP(name);
    p->value = value ? STRDUP(value) : STRDUP("");
    p->next = *head;
    *head = p;
}

static const char *riiv_param_get(const riiv_param_t *head, const char *name)
{
    for (const riiv_param_t *p = head; p; p = p->next)
    {
        if (!strcasecmp(p->name, name))
            return p->value;
    }
    return NULL;
}

static riiv_param_t *riiv_param_clone(const riiv_param_t *src)
{
    riiv_param_t *dst = NULL;
    for (const riiv_param_t *p = src; p; p = p->next)
        riiv_param_set(&dst, p->name, p->value);
    return dst;
}

static void riiv_param_free(riiv_param_t *head)
{
    while (head)
    {
        riiv_param_t *next = head->next;
        FREE(head->name);
        FREE(head->value);
        FREE(head);
        head = next;
    }
}

static char *subst_params(const char *src, const riiv_param_t *params)
{
    if (!src)
        return STRDUP("");
    if (!strstr(src, "{$"))
        return STRDUP(src);

    char buf[PATH_MAX * 2];
    buf[0] = '\0';
    size_t out_len = 0;
    const char *p = src;

    while (*p && out_len < sizeof(buf) - 1)
    {
        if (p[0] == '{' && p[1] == '$')
        {
            const char *var_start = p + 2;
            const char *var_end = strchr(var_start, '}');
            if (var_end)
            {
                size_t var_name_len = var_end - var_start;
                char var_name[128];
                if (var_name_len >= sizeof(var_name))
                    var_name_len = sizeof(var_name) - 1;
                memcpy(var_name, var_start, var_name_len);
                var_name[var_name_len] = '\0';

                const char *val = riiv_param_get(params, var_name);
                if (val)
                {
                    size_t v_len = strlen(val);
                    if (out_len + v_len < sizeof(buf) - 1)
                    {
                        memcpy(buf + out_len, val, v_len);
                        out_len += v_len;
                    }
                }
                p = var_end + 1;
                continue;
            }
        }
        buf[out_len++] = *p++;
    }
    buf[out_len] = '\0';
    return STRDUP(buf);
}

void RiivolutionFreeDoc(riiv_doc_t *doc)
{
    if (!doc)
        return;
    FREE(doc->xml_path);
    FREE(doc->root);
    FREE(doc->filter_game);
    FREE(doc->filter_developer);
    ResetStringField(&doc->filter_regions);

    riiv_section_t *sec = doc->sections;
    while (sec)
    {
        riiv_section_t *next_sec = sec->next;
        FREE(sec->name);
        riiv_option_t *opt = sec->options;
        while (opt)
        {
            riiv_option_t *next_opt = opt->next;
            FREE(opt->id);
            FREE(opt->name);
            riiv_param_free(opt->params);
            riiv_choice_t *ch = opt->choices;
            while (ch)
            {
                riiv_choice_t *next_ch = ch->next;
                FREE(ch->name);
                riiv_param_free(ch->params);
                riiv_patch_ref_t *pref = ch->patch_refs;
                while (pref)
                {
                    riiv_patch_ref_t *next_pref = pref->next;
                    FREE(pref->id);
                    riiv_param_free(pref->params);
                    FREE(pref);
                    pref = next_pref;
                }
                FREE(ch);
                ch = next_ch;
            }
            FREE(opt);
            opt = next_opt;
        }
        FREE(sec);
        sec = next_sec;
    }

    riiv_patch_t *patch = doc->patches;
    while (patch)
    {
        riiv_patch_t *next_patch = patch->next;
        FREE(patch->id);
        FREE(patch->root);

        riiv_file_t *f = patch->files;
        while (f)
        {
            riiv_file_t *next_f = f->next;
            FREE(f->disc);
            FREE(f->external);
            FREE(f);
            f = next_f;
        }

        riiv_folder_t *fold = patch->folders;
        while (fold)
        {
            riiv_folder_t *next_fold = fold->next;
            FREE(fold->disc);
            FREE(fold->external);
            FREE(fold);
            fold = next_fold;
        }

        riiv_savegame_t *sg = patch->savegames;
        while (sg)
        {
            riiv_savegame_t *next_sg = sg->next;
            FREE(sg->external);
            FREE(sg);
            sg = next_sg;
        }

        riiv_memory_t *m = patch->memories;
        while (m)
        {
            riiv_memory_t *next_m = m->next;
            FREE(m->value);
            FREE(m->valuefile);
            FREE(m->original);
            FREE(m);
            m = next_m;
        }

        FREE(patch);
        patch = next_patch;
    }

    FREE(doc);
}

static void parse_params_node(const xml_node_t *node, riiv_param_t **params)
{
    for (const xml_node_t *child = node->first_child; child; child = child->next_sibling)
    {
        if (!strcasecmp(child->name, "param"))
        {
            const char *name = xml_get_attr(child, "name");
            const char *val = xml_get_attr(child, "value");
            if (name)
                riiv_param_set(params, name, val);
        }
    }
}

riiv_doc_t *RiivolutionParseXmlFile(ccp xml_path)
{
    if (!xml_path || !*xml_path)
        return NULL;

    u8 *xml_data = NULL;
    size_t xml_size = 0;
    enumError err = LoadFileAlloc(xml_path, 0, 0, &xml_data, &xml_size, 32 * MiB, 0, 0, false);
    if (err || !xml_data)
        return NULL;

    xml_node_t *root_node = xml_parse_buffer((char *)xml_data, xml_size);
    FREE(xml_data);

    if (!root_node)
        return NULL;

    const xml_node_t *wiidisc = NULL;
    if (!strcasecmp(root_node->name, "wiidisc"))
    {
        wiidisc = root_node;
    }
    else
    {
        for (const xml_node_t *c = root_node->first_child; c; c = c->next_sibling)
        {
            if (!strcasecmp(c->name, "wiidisc"))
            {
                wiidisc = c;
                break;
            }
        }
    }

    if (!wiidisc)
        wiidisc = root_node;

    riiv_doc_t *doc = CALLOC(1, sizeof(riiv_doc_t));
    doc->xml_path = STRDUP(xml_path);
    InitializeStringField(&doc->filter_regions);
    doc->version = (int)xml_get_attr_u32(wiidisc, "version", 1);
    const char *doc_root = xml_get_attr(wiidisc, "root");
    if (doc_root)
        doc->root = STRDUP(doc_root);

    for (const xml_node_t *n = wiidisc->first_child; n; n = n->next_sibling)
    {
        if (!strcasecmp(n->name, "id"))
        {
            const char *game = xml_get_attr(n, "game");
            const char *dev = xml_get_attr(n, "developer");
            if (game)
                doc->filter_game = STRDUP(game);
            if (dev)
                doc->filter_developer = STRDUP(dev);
            doc->filter_disc = (int)xml_get_attr_u32(n, "disc", (u32)-1);
            doc->filter_version = (int)xml_get_attr_u32(n, "version", (u32)-1);

            for (const xml_node_t *reg = n->first_child; reg; reg = reg->next_sibling)
            {
                if (!strcasecmp(reg->name, "region"))
                {
                    const char *rtype = xml_get_attr(reg, "type");
                    if (rtype)
                        AppendStringField(&doc->filter_regions, rtype, false);
                }
            }
        }
        else if (!strcasecmp(n->name, "options"))
        {
            for (const xml_node_t *snode = n->first_child; snode; snode = snode->next_sibling)
            {
                if (!strcasecmp(snode->name, "section"))
                {
                    riiv_section_t *sec = CALLOC(1, sizeof(riiv_section_t));
                    const char *sname = xml_get_attr(snode, "name");
                    sec->name = STRDUP(sname ? sname : "General");

                    if (!doc->sections)
                        doc->sections = sec;
                    else
                    {
                        riiv_section_t *cur = doc->sections;
                        while (cur->next)
                            cur = cur->next;
                        cur->next = sec;
                    }

                    for (const xml_node_t *onode = snode->first_child; onode; onode = onode->next_sibling)
                    {
                        if (!strcasecmp(onode->name, "option"))
                        {
                            riiv_option_t *opt = CALLOC(1, sizeof(riiv_option_t));
                            const char *oname = xml_get_attr(onode, "name");
                            const char *oid = xml_get_attr(onode, "id");
                            opt->name = STRDUP(oname ? oname : "Option");
                            if (oid)
                                opt->id = STRDUP(oid);

                            opt->default_choice = xml_get_attr_u32(onode, "default", 0);
                            if (opt->default_choice == 0)
                                opt->default_choice = xml_get_attr_u32(onode, "index", 0);

                            parse_params_node(onode, &opt->params);

                            if (!sec->options)
                                sec->options = opt;
                            else
                            {
                                riiv_option_t *cur = sec->options;
                                while (cur->next)
                                    cur = cur->next;
                                cur->next = opt;
                            }

                            for (const xml_node_t *cnode = onode->first_child; cnode; cnode = cnode->next_sibling)
                            {
                                if (!strcasecmp(cnode->name, "choice"))
                                {
                                    riiv_choice_t *ch = CALLOC(1, sizeof(riiv_choice_t));
                                    const char *cname = xml_get_attr(cnode, "name");
                                    ch->name = STRDUP(cname ? cname : "Choice");
                                    opt->n_choices++;

                                    parse_params_node(cnode, &ch->params);

                                    if (!opt->choices)
                                        opt->choices = ch;
                                    else
                                    {
                                        riiv_choice_t *cur = opt->choices;
                                        while (cur->next)
                                            cur = cur->next;
                                        cur->next = ch;
                                    }

                                    for (const xml_node_t *pnode = cnode->first_child; pnode; pnode = pnode->next_sibling)
                                    {
                                        if (!strcasecmp(pnode->name, "patch"))
                                        {
                                            riiv_patch_ref_t *pref = CALLOC(1, sizeof(riiv_patch_ref_t));
                                            const char *pid = xml_get_attr(pnode, "id");
                                            pref->id = STRDUP(pid ? pid : "");
                                            parse_params_node(pnode, &pref->params);

                                            if (!ch->patch_refs)
                                                ch->patch_refs = pref;
                                            else
                                            {
                                                riiv_patch_ref_t *cur = ch->patch_refs;
                                                while (cur->next)
                                                    cur = cur->next;
                                                cur->next = pref;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else if (!strcasecmp(snode->name, "macros"))
                {
                    const char *macro_id = xml_get_attr(snode, "id");
                    const char *macro_name = xml_get_attr(snode, "name");
                    if (macro_id && doc->sections)
                    {
                        for (riiv_section_t *sec = doc->sections; sec; sec = sec->next)
                        {
                            for (riiv_option_t *opt = sec->options; opt; opt = opt->next)
                            {
                                if (opt->id && !strcasecmp(opt->id, macro_id))
                                {
                                    riiv_option_t *cloned = CALLOC(1, sizeof(riiv_option_t));
                                    cloned->name = STRDUP(macro_name ? macro_name : opt->name);
                                    cloned->id = STRDUP(opt->id);
                                    cloned->default_choice = opt->default_choice;
                                    cloned->n_choices = opt->n_choices;
                                    cloned->params = riiv_param_clone(opt->params);

                                    riiv_choice_t **ch_tail = &cloned->choices;
                                    for (riiv_choice_t *src_ch = opt->choices; src_ch; src_ch = src_ch->next)
                                    {
                                        riiv_choice_t *new_ch = CALLOC(1, sizeof(riiv_choice_t));
                                        new_ch->name = STRDUP(src_ch->name);
                                        new_ch->params = riiv_param_clone(src_ch->params);

                                        riiv_patch_ref_t **pref_tail = &new_ch->patch_refs;
                                        for (riiv_patch_ref_t *src_pref = src_ch->patch_refs; src_pref; src_pref = src_pref->next)
                                        {
                                            riiv_patch_ref_t *new_pref = CALLOC(1, sizeof(riiv_patch_ref_t));
                                            new_pref->id = STRDUP(src_pref->id);
                                            new_pref->params = riiv_param_clone(src_pref->params);
                                            parse_params_node(snode, &new_pref->params);
                                            *pref_tail = new_pref;
                                            pref_tail = &new_pref->next;
                                        }

                                        *ch_tail = new_ch;
                                        ch_tail = &new_ch->next;
                                    }

                                    riiv_option_t *cur = sec->options;
                                    while (cur->next)
                                        cur = cur->next;
                                    cur->next = cloned;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (!strcasecmp(n->name, "patch"))
        {
            riiv_patch_t *patch = CALLOC(1, sizeof(riiv_patch_t));
            const char *pid = xml_get_attr(n, "id");
            const char *proot = xml_get_attr(n, "root");
            patch->id = STRDUP(pid ? pid : "default");
            if (proot && *proot)
                patch->root = STRDUP(proot);
            else if (doc->root)
                patch->root = STRDUP(doc->root);
            else
                patch->root = STRDUP("");

            if (!doc->patches)
                doc->patches = patch;
            else
            {
                riiv_patch_t *cur = doc->patches;
                while (cur->next)
                    cur = cur->next;
                cur->next = patch;
            }

            for (const xml_node_t *sub = n->first_child; sub; sub = sub->next_sibling)
            {
                if (!strcasecmp(sub->name, "file"))
                {
                    const char *disc = xml_get_attr(sub, "disc");
                    const char *ext = xml_get_attr(sub, "external");
                    if (ext)
                    {
                        riiv_file_t *f = CALLOC(1, sizeof(riiv_file_t));
                        f->disc = STRDUP(disc ? disc : "");
                        f->external = STRDUP(ext);
                        f->resize = xml_get_attr_bool(sub, "resize", true);
                        f->create = xml_get_attr_bool(sub, "create", false);
                        f->offset = xml_get_attr_u32(sub, "offset", 0);
                        f->fileoffset = xml_get_attr_u32(sub, "fileoffset", 0);
                        f->length = xml_get_attr_u32(sub, "length", 0);

                        if (!patch->files)
                            patch->files = f;
                        else
                        {
                            riiv_file_t *cur = patch->files;
                            while (cur->next)
                                cur = cur->next;
                            cur->next = f;
                        }
                    }
                }
                else if (!strcasecmp(sub->name, "folder"))
                {
                    const char *disc = xml_get_attr(sub, "disc");
                    const char *ext = xml_get_attr(sub, "external");
                    if (ext)
                    {
                        riiv_folder_t *fold = CALLOC(1, sizeof(riiv_folder_t));
                        fold->disc = STRDUP(disc ? disc : "");
                        fold->external = STRDUP(ext);
                        fold->resize = xml_get_attr_bool(sub, "resize", true);
                        fold->create = xml_get_attr_bool(sub, "create", false);
                        fold->recursive = xml_get_attr_bool(sub, "recursive", true);
                        fold->length = xml_get_attr_u32(sub, "length", 0);

                        if (!patch->folders)
                            patch->folders = fold;
                        else
                        {
                            riiv_folder_t *cur = patch->folders;
                            while (cur->next)
                                cur = cur->next;
                            cur->next = fold;
                        }
                    }
                }
                else if (!strcasecmp(sub->name, "savegame"))
                {
                    const char *ext = xml_get_attr(sub, "external");
                    if (ext)
                    {
                        riiv_savegame_t *sg = CALLOC(1, sizeof(riiv_savegame_t));
                        sg->external = STRDUP(ext);
                        sg->clone = xml_get_attr_bool(sub, "clone", true);

                        if (!patch->savegames)
                            patch->savegames = sg;
                        else
                        {
                            riiv_savegame_t *cur = patch->savegames;
                            while (cur->next)
                                cur = cur->next;
                            cur->next = sg;
                        }
                    }
                }
                else if (!strcasecmp(sub->name, "memory"))
                {
                    riiv_memory_t *m = CALLOC(1, sizeof(riiv_memory_t));
                    m->offset = xml_get_attr_u32(sub, "offset", 0);
                    const char *val = xml_get_attr(sub, "value");
                    if (val)
                        m->value = xml_parse_hex_bytes(val, &m->value_len);
                    const char *vfile = xml_get_attr(sub, "valuefile");
                    if (vfile)
                        m->valuefile = STRDUP(vfile);
                    const char *orig = xml_get_attr(sub, "original");
                    if (orig)
                        m->original = xml_parse_hex_bytes(orig, &m->original_len);
                    m->ocarina = xml_get_attr_bool(sub, "ocarina", false);
                    m->search = xml_get_attr_bool(sub, "search", false);
                    m->align = xml_get_attr_u32(sub, "align", m->search ? 4 : 1);

                    if (!patch->memories)
                        patch->memories = m;
                    else
                    {
                        riiv_memory_t *cur = patch->memories;
                        while (cur->next)
                            cur = cur->next;
                        cur->next = m;
                    }
                }
            }
        }
    }

    xml_free_node(root_node);
    return doc;
}

//=============================================================================
// 3. Choice Selection & Active Patch Resolution
//=============================================================================

static void select_option_choice(riiv_option_t *opt, const char *choice_str)
{
    if (!opt || !choice_str || !*choice_str)
        return;
    while (*choice_str && *choice_str <= ' ')
        choice_str++;
    if (!strcasecmp(choice_str, "none") || !strcasecmp(choice_str, "disabled") || !strcmp(choice_str, "0"))
    {
        opt->selected_choice = 0;
        return;
    }
    if (isdigit((int)*choice_str))
    {
        uint idx = (uint)strtoul(choice_str, NULL, 10);
        if (idx <= opt->n_choices)
            opt->selected_choice = idx;
        return;
    }
    uint idx = 1;
    for (riiv_choice_t *ch = opt->choices; ch; ch = ch->next, idx++)
    {
        if (!strcasecmp(ch->name, choice_str))
        {
            opt->selected_choice = idx;
            return;
        }
    }
}

static void apply_choice_specs(riiv_doc_t *doc, const RiivolutionOptions_t *opt)
{
    for (riiv_section_t *sec = doc->sections; sec; sec = sec->next)
    {
        for (riiv_option_t *o = sec->options; o; o = o->next)
        {
            if (opt->all_choices && o->n_choices > 0)
                o->selected_choice = 1;
            else if (o->default_choice > 0 && o->default_choice <= o->n_choices)
                o->selected_choice = o->default_choice;
            else
                o->selected_choice = 0;
        }
    }

    for (uint i = 0; i < opt->choices.used; i++)
    {
        const char *spec = opt->choices.field[i];
        if (!spec || !*spec)
            continue;

        char buf[1024];
        StringCopyS(buf, sizeof(buf), spec);
        char *token = strtok(buf, ",;");
        uint pos_opt_idx = 0;

        while (token)
        {
            while (*token && *token <= ' ')
                token++;
            char *eq = strchr(token, '=');
            if (eq)
            {
                *eq = '\0';
                char *opt_name = token;
                char *ch_name = eq + 1;
                while (*ch_name && *ch_name <= ' ')
                    ch_name++;

                char *colon = strchr(opt_name, ':');
                char *sec_filter = NULL;
                if (colon)
                {
                    *colon = '\0';
                    sec_filter = opt_name;
                    opt_name = colon + 1;
                }

                for (riiv_section_t *s = doc->sections; s; s = s->next)
                {
                    if (sec_filter && strcasecmp(s->name, sec_filter))
                        continue;
                    for (riiv_option_t *o = s->options; o; o = o->next)
                    {
                        if ((o->id && !strcasecmp(o->id, opt_name)) || !strcasecmp(o->name, opt_name))
                        {
                            select_option_choice(o, ch_name);
                        }
                    }
                }
            }
            else
            {
                uint cur_idx = 0;
                for (riiv_section_t *s = doc->sections; s; s = s->next)
                {
                    for (riiv_option_t *o = s->options; o; o = o->next)
                    {
                        if (cur_idx == pos_opt_idx)
                        {
                            select_option_choice(o, token);
                            goto next_token;
                        }
                        cur_idx++;
                    }
                }
            next_token:
                pos_opt_idx++;
            }
            token = strtok(NULL, ",;");
        }
    }
}

static void prompt_interactive_choices(riiv_doc_t *doc, RiivolutionOptions_t *opt, char *id6, char *game_title, size_t title_size)
{
    if (!doc)
        return;

    printf("\n========================================================\n");
    printf("     Riivolution Mod Interactive Configuration          \n");
    printf("========================================================\n");

    if (doc->sections)
    {
        for (riiv_section_t *sec = doc->sections; sec; sec = sec->next)
        {
            if (*sec->name)
                printf("\n--- Section: %s ---\n", sec->name);

            for (riiv_option_t *op = sec->options; op; op = op->next)
            {
                if (op->n_choices == 0)
                    continue;

                printf("\nOption: %s\n", op->name);
                printf("  [0] Disabled / None%s\n", (op->selected_choice == 0) ? " (Current)" : "");
                uint idx = 1;
                for (riiv_choice_t *ch = op->choices; ch; ch = ch->next, idx++)
                {
                    printf("  [%u] %s%s\n", idx, ch->name, (idx == (uint)op->selected_choice) ? " (Current)" : "");
                }

                printf("Select choice (0-%u) [default: %d]: ", op->n_choices, op->selected_choice);
                fflush(stdout);

                char line[256];
                if (fgets(line, sizeof(line), stdin))
                {
                    char *p = line;
                    while (*p && (unsigned char)*p <= ' ') p++;
                    char *end = p + strlen(p);
                    while (end > p && (unsigned char)*(end - 1) <= ' ') *(--end) = '\0';

                    if (*p)
                    {
                        int val = atoi(p);
                        if (val >= 0 && (uint)val <= op->n_choices)
                            op->selected_choice = val;
                    }
                }
            }
        }
    }

    printf("\n--- Game Metadata ---\n");
    ccp cur_id = opt->custom_id ? opt->custom_id : (id6[0] ? id6 : "CUSTOM");
    printf("Enter 6-character Game ID [default: %s]: ", cur_id);
    fflush(stdout);
    char line[256];
    if (fgets(line, sizeof(line), stdin))
    {
        char *p = line;
        while (*p && (unsigned char)*p <= ' ') p++;
        char *end = p + strlen(p);
        while (end > p && (unsigned char)*(end - 1) <= ' ') *(--end) = '\0';
        if (strlen(p) == 6)
        {
            opt->custom_id = STRDUP(p);
            StringCopyS(id6, 7, p);
        }
    }

    ccp cur_title = opt->custom_name ? opt->custom_name : (game_title[0] ? game_title : "Modded Game");
    printf("Enter Game Title [default: %s]: ", cur_title);
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin))
    {
        char *p = line;
        while (*p && (unsigned char)*p <= ' ') p++;
        char *end = p + strlen(p);
        while (end > p && (unsigned char)*(end - 1) <= ' ') *(--end) = '\0';
        if (*p)
        {
            opt->custom_name = STRDUP(p);
            StringCopyS(game_title, title_size, p);
        }
    }
    printf("\n");
}

typedef struct active_patch_t
{
    riiv_patch_t *patch;
    riiv_param_t *params;
    char *resolved_root;
    struct active_patch_t *next;
} active_patch_t;

static active_patch_t *resolve_active_patches(riiv_doc_t *doc, const RiivolutionOptions_t *opt, const char *game_id6)
{
    (void)opt;
    active_patch_t *head = NULL;
    active_patch_t *tail = NULL;

    char gameid3[4] = "";
    char region1[2] = "";
    char maker2[3] = "";
    if (game_id6 && strlen(game_id6) >= 4)
    {
        strncpy(gameid3, game_id6, 3);
        region1[0] = game_id6[3];
        if (strlen(game_id6) >= 6)
            strncpy(maker2, game_id6 + 4, 2);
    }

    bool any_choice_activated = false;

    for (riiv_section_t *sec = doc->sections; sec; sec = sec->next)
    {
        for (riiv_option_t *o = sec->options; o; o = o->next)
        {
            if (o->selected_choice == 0 || o->selected_choice > o->n_choices)
                continue;

            uint c_idx = 1;
            riiv_choice_t *sel_choice = NULL;
            for (riiv_choice_t *ch = o->choices; ch; ch = ch->next, c_idx++)
            {
                if (c_idx == o->selected_choice)
                {
                    sel_choice = ch;
                    break;
                }
            }
            if (!sel_choice)
                continue;

            for (riiv_patch_ref_t *pref = sel_choice->patch_refs; pref; pref = pref->next)
            {
                for (riiv_patch_t *p = doc->patches; p; p = p->next)
                {
                    if (!strcasecmp(p->id, pref->id))
                    {
                        any_choice_activated = true;
                        active_patch_t *ap = CALLOC(1, sizeof(active_patch_t));
                        ap->patch = p;

                        riiv_param_set(&ap->params, "__gameid", gameid3);
                        riiv_param_set(&ap->params, "__region", region1);
                        riiv_param_set(&ap->params, "__maker", maker2);

                        for (riiv_param_t *param = o->params; param; param = param->next)
                            riiv_param_set(&ap->params, param->name, param->value);
                        for (riiv_param_t *param = sel_choice->params; param; param = param->next)
                            riiv_param_set(&ap->params, param->name, param->value);
                        for (riiv_param_t *param = pref->params; param; param = param->next)
                            riiv_param_set(&ap->params, param->name, param->value);

                        ap->resolved_root = subst_params(p->root, ap->params);

                        if (!head)
                            head = tail = ap;
                        else
                        {
                            tail->next = ap;
                            tail = ap;
                        }
                    }
                }
            }
        }
    }

    if (!any_choice_activated)
    {
        for (riiv_patch_t *p = doc->patches; p; p = p->next)
        {
            active_patch_t *ap = CALLOC(1, sizeof(active_patch_t));
            ap->patch = p;
            riiv_param_set(&ap->params, "__gameid", gameid3);
            riiv_param_set(&ap->params, "__region", region1);
            riiv_param_set(&ap->params, "__maker", maker2);
            ap->resolved_root = subst_params(p->root, ap->params);

            if (!head)
                head = tail = ap;
            else
            {
                tail->next = ap;
                tail = ap;
            }
        }
    }

    return head;
}

static void free_active_patches(active_patch_t *head)
{
    while (head)
    {
        active_patch_t *next = head->next;
        riiv_param_free(head->params);
        FREE(head->resolved_root);
        FREE(head);
        head = next;
    }
}

enumError RiivolutionInspect(riiv_doc_t *doc, int verbose)
{
    if (!doc)
        return ERROR0(ERR_SYNTAX, "Invalid Riivolution document.\n");

    printf("\nRiivolution XML: %s (v%d)\n", doc->xml_path ? doc->xml_path : "<unknown>", doc->version);
    if (doc->root && *doc->root)
        printf("  Default Root:  %s\n", doc->root);

    printf("  Game Filter:   Game=%s, Dev=%s, Disc=%d, Ver=%d\n",
           doc->filter_game ? doc->filter_game : "Any",
           doc->filter_developer ? doc->filter_developer : "Any",
           doc->filter_disc, doc->filter_version);

    if (doc->filter_regions.used)
    {
        printf("  Regions:       ");
        for (uint i = 0; i < doc->filter_regions.used; i++)
            printf("%s%s", doc->filter_regions.field[i], (i + 1 < doc->filter_regions.used) ? ", " : "");
        putchar('\n');
    }

    if (doc->sections)
    {
        printf("\n--- Sections & Options ---\n");
        for (riiv_section_t *sec = doc->sections; sec; sec = sec->next)
        {
            printf("  Section: \"%s\"\n", sec->name);
            for (riiv_option_t *o = sec->options; o; o = o->next)
            {
                printf("    Option: \"%s\"%s%s%s (Default: %u)\n",
                       o->name,
                       o->id ? " [id=" : "",
                       o->id ? o->id : "",
                       o->id ? "]" : "",
                       o->default_choice);
                uint c_idx = 1;
                for (riiv_choice_t *ch = o->choices; ch; ch = ch->next, c_idx++)
                {
                    printf("      [%u] \"%s\"", c_idx, ch->name);
                    if (ch->patch_refs)
                    {
                        printf(" -> Patches:");
                        for (riiv_patch_ref_t *pr = ch->patch_refs; pr; pr = pr->next)
                            printf(" %s", pr->id);
                    }
                    putchar('\n');
                }
            }
        }
    }

    if (doc->patches)
    {
        printf("\n--- Defined Patches ---\n");
        for (riiv_patch_t *p = doc->patches; p; p = p->next)
        {
            uint n_files = 0, n_folders = 0, n_mem = 0, n_saves = 0;
            for (riiv_file_t *f = p->files; f; f = f->next) n_files++;
            for (riiv_folder_t *fo = p->folders; fo; fo = fo->next) n_folders++;
            for (riiv_memory_t *m = p->memories; m; m = m->next) n_mem++;
            for (riiv_savegame_t *s = p->savegames; s; s = s->next) n_saves++;

            printf("  Patch: \"%s\" (root=\"%s\") -> Files: %u, Folders: %u, Memory: %u, Savegame: %u\n",
                   p->id, p->root ? p->root : "", n_files, n_folders, n_mem, n_saves);

            if (verbose > 0)
            {
                for (riiv_file_t *f = p->files; f; f = f->next)
                    printf("    FILE:   ext=\"%s\" -> disc=\"%s\" (resize=%d, create=%d, off=0x%x, foff=0x%x, len=%u)\n",
                           f->external, f->disc, f->resize, f->create, f->offset, f->fileoffset, f->length);
                for (riiv_folder_t *fo = p->folders; fo; fo = fo->next)
                    printf("    FOLDER: ext=\"%s\" -> disc=\"%s\" (resize=%d, create=%d, rec=%d)\n",
                           fo->external, fo->disc, fo->resize, fo->create, fo->recursive);
                for (riiv_memory_t *m = p->memories; m; m = m->next)
                    printf("    MEMORY: addr=0x%08X (val_bytes=%u, vfile=%s, orig_bytes=%u, search=%d)\n",
                           m->offset, m->value_len, m->valuefile ? m->valuefile : "none", m->original_len, m->search);
                for (riiv_savegame_t *s = p->savegames; s; s = s->next)
                    printf("    SAVE:   ext=\"%s\" (clone=%d)\n", s->external, s->clone);
            }
        }
    }
    putchar('\n');
    return ERR_OK;
}

//=============================================================================
// 4. Filesystem Helpers & Disc File/Folder Patching
//=============================================================================

static enumError mkdir_p(const char *dir)
{
    if (!dir || !*dir)
        return ERR_OK;
    char tmp[PATH_MAX];
    StringCopyS(tmp, sizeof(tmp), dir);
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return ERR_CANT_CREATE;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return ERR_CANT_CREATE;
    return ERR_OK;
}

static enumError remove_dir_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
    {
        if (errno == ENOENT)
            return ERR_OK;
        return unlink(path) == 0 ? ERR_OK : ERR_REMOVE_FAILED;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(subpath, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
                remove_dir_recursive(subpath);
            else
                unlink(subpath);
        }
    }
    closedir(d);
    rmdir(path);
    return ERR_OK;
}

static enumError copy_file_contents(const char *src_path, const char *dst_path)
{
    FILE *in = fopen(src_path, "rb");
    if (!in)
        return ERR_CANT_OPEN;

    char dirbuf[PATH_MAX];
    StringCopyS(dirbuf, sizeof(dirbuf), dst_path);
    char *slash = strrchr(dirbuf, '/');
    if (slash)
    {
        *slash = '\0';
        mkdir_p(dirbuf);
    }

    FILE *out = fopen(dst_path, "wb");
    if (!out)
    {
        fclose(in);
        return ERR_CANT_CREATE;
    }

    char buf[64 * 1024];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, bytes, out) != bytes)
        {
            fclose(in);
            fclose(out);
            return ERR_WRITE_FAILED;
        }
    }

    fclose(in);
    fclose(out);
    return ERR_OK;
}

static bool resolve_case_insensitive_path(const char *base, const char *rel, char *out, size_t out_size)
{
    if (!base || !rel || !out || out_size == 0)
        return false;

    snprintf(out, out_size, "%s", base);

    char rel_copy[PATH_MAX];
    StringCopyS(rel_copy, sizeof(rel_copy), rel);
    for (char *p = rel_copy; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }

    char *token = strtok(rel_copy, "/");
    while (token)
    {
        if (!strcmp(token, "."))
        {
            token = strtok(NULL, "/");
            continue;
        }
        if (!strcmp(token, ".."))
        {
            char *last_slash = strrchr(out, '/');
            if (last_slash && last_slash > out)
                *last_slash = '\0';
            token = strtok(NULL, "/");
            continue;
        }

        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", out, token);
        struct stat st;
        if (stat(candidate, &st) == 0)
        {
            snprintf(out, out_size, "%s", candidate);
            token = strtok(NULL, "/");
            continue;
        }

        DIR *d = opendir(out);
        if (!d)
            return false;

        bool found = false;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL)
        {
            if (!strcasecmp(ent->d_name, token))
            {
                snprintf(out, out_size, "%s/%s", out, ent->d_name);
                found = true;
                break;
            }
        }
        closedir(d);

        if (!found)
        {
            size_t cur_len = strlen(out);
            snprintf(out + cur_len, out_size - cur_len, "/%s", token);
        }

        token = strtok(NULL, "/");
    }

    return true;
}

static bool find_file_in_tree(const char *dir, const char *target_filename, char *out, size_t out_size)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(subpath, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                if (find_file_in_tree(subpath, target_filename, out, out_size))
                {
                    closedir(d);
                    return true;
                }
            }
            else if (!strcasecmp(ent->d_name, target_filename))
            {
                StringCopyS(out, out_size, subpath);
                closedir(d);
                return true;
            }
        }
    }
    closedir(d);
    return false;
}

static bool resolve_external_path(const active_patch_t *ap, const char *sd_root, const char *xml_dir,
                                  const char *external_raw, char *out_path, size_t out_size)
{
    char *subst_ext = subst_params(external_raw, ap->params);
    char *subst_root = ap->resolved_root;

    char combined[PATH_MAX * 2];
    combined[0] = '\0';

    if (subst_ext[0] == '/' || subst_ext[0] == '\\')
    {
        snprintf(combined, sizeof(combined), "%s/%s", sd_root, subst_ext + 1);
    }
    else if (subst_root && (subst_root[0] == '/' || subst_root[0] == '\\'))
    {
        snprintf(combined, sizeof(combined), "%s/%s/%s", sd_root, subst_root + 1, subst_ext);
    }
    else if (subst_root && *subst_root)
    {
        snprintf(combined, sizeof(combined), "%s/%s/%s", xml_dir, subst_root, subst_ext);
    }
    else
    {
        snprintf(combined, sizeof(combined), "%s/%s", xml_dir, subst_ext);
    }

    FREE(subst_ext);

    for (char *p = combined; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }

    struct stat st;
    if (stat(combined, &st) == 0)
    {
        StringCopyS(out_path, out_size, combined);
        return true;
    }

    return resolve_case_insensitive_path(sd_root, combined + strlen(sd_root), out_path, out_size);
}

static enumError apply_file_patch_direct(const char *src_ext_file, const char *dst_disc_file,
                                        bool create, bool resize, u32 offset, u32 fileoffset, u32 length, int verbose)
{
    struct stat st_src;
    if (stat(src_ext_file, &st_src) != 0 || !S_ISREG(st_src.st_mode))
    {
        if (verbose >= 1)
            printf("  - External file not found: %s\n", src_ext_file);
        return ERR_NOT_EXISTS;
    }

    struct stat st_dst;
    bool dst_exists = (stat(dst_disc_file, &st_dst) == 0 && S_ISREG(st_dst.st_mode));

    if (!dst_exists)
    {
        if (!create)
        {
            if (verbose >= 2)
                printf("  - Skipping non-existing disc file (create=false): %s\n", dst_disc_file);
            return ERR_OK;
        }

        char dirbuf[PATH_MAX];
        StringCopyS(dirbuf, sizeof(dirbuf), dst_disc_file);
        char *slash = strrchr(dirbuf, '/');
        if (slash)
        {
            *slash = '\0';
            mkdir_p(dirbuf);
        }
    }

    if (offset == 0 && fileoffset == 0 && length == 0 && resize)
    {
        if (verbose >= 1)
            printf("  + Replacing file: %s\n", dst_disc_file);
        return copy_file_contents(src_ext_file, dst_disc_file);
    }

    FILE *in = fopen(src_ext_file, "rb");
    if (!in)
        return ERR_CANT_OPEN;

    FILE *out = fopen(dst_disc_file, dst_exists ? "r+b" : "w+b");
    if (!out)
    {
        fclose(in);
        return ERR_CANT_CREATE;
    }

    if (fileoffset > 0)
        fseek(in, fileoffset, SEEK_SET);

    u64 cur_dst_size = dst_exists ? (u64)st_dst.st_size : 0;
    if (offset > cur_dst_size)
    {
        fseek(out, cur_dst_size, SEEK_SET);
        size_t pad_size = offset - cur_dst_size;
        char zero[4096] = {0};
        while (pad_size > 0)
        {
            size_t chunk = pad_size > sizeof(zero) ? sizeof(zero) : pad_size;
            fwrite(zero, 1, chunk, out);
            pad_size -= chunk;
        }
    }
    fseek(out, offset, SEEK_SET);

    u64 to_copy = length;
    if (to_copy == 0)
    {
        u64 src_avail = (u64)st_src.st_size > fileoffset ? (u64)st_src.st_size - fileoffset : 0;
        to_copy = src_avail;
    }

    char buf[64 * 1024];
    u64 copied = 0;
    while (copied < to_copy)
    {
        size_t chunk = (to_copy - copied) > sizeof(buf) ? sizeof(buf) : (size_t)(to_copy - copied);
        size_t read_bytes = fread(buf, 1, chunk, in);
        if (read_bytes == 0)
            break;
        fwrite(buf, 1, read_bytes, out);
        copied += read_bytes;
    }

    if (resize)
    {
        fflush(out);
        int fd = fileno(out);
        if (fd >= 0)
            ftruncate(fd, offset + copied);
    }

    fclose(in);
    fclose(out);

    if (verbose >= 1)
        printf("  + Patched %llu bytes into %s at offset 0x%x\n", (unsigned long long)copied, dst_disc_file, offset);
    return ERR_OK;
}

static enumError apply_folder_patch_recursive(const char *src_dir, const char *dst_dir, const char *files_dir,
                                             bool create, bool resize, bool recursive, u32 length, int verbose)
{
    DIR *d = opendir(src_dir);
    if (!d)
        return ERR_OK;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        char child_src[PATH_MAX];
        snprintf(child_src, sizeof(child_src), "%s/%s", src_dir, ent->d_name);

        struct stat st;
        if (stat(child_src, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            if (recursive)
            {
                char child_dst[PATH_MAX];
                snprintf(child_dst, sizeof(child_dst), "%s/%s", dst_dir, ent->d_name);
                apply_folder_patch_recursive(child_src, child_dst, files_dir, create, resize, recursive, length, verbose);
            }
        }
        else if (S_ISREG(st.st_mode))
        {
            char child_dst[PATH_MAX];
            if (!strcasecmp(ent->d_name, "main.dol"))
            {
                // Direct replacement of main executable in sys directory
                char sys_calc[PATH_MAX];
                StringCopyS(sys_calc, sizeof(sys_calc), files_dir);
                char *f_sl = strrchr(sys_calc, '/');
                if (f_sl)
                {
                    *f_sl = '\0';
                    char sys_dol[PATH_MAX];
                    snprintf(sys_dol, sizeof(sys_dol), "%s/sys/main.dol", sys_calc);
                    apply_file_patch_direct(child_src, sys_dol, true, true, 0, 0, length, verbose);
                }
            }

            if (!strcmp(dst_dir, files_dir))
            {
                if (!find_file_in_tree(files_dir, ent->d_name, child_dst, sizeof(child_dst)))
                {
                    if (create)
                        snprintf(child_dst, sizeof(child_dst), "%s/%s", files_dir, ent->d_name);
                    else
                        continue;
                }
            }
            else
            {
                snprintf(child_dst, sizeof(child_dst), "%s/%s", dst_dir, ent->d_name);
            }

            apply_file_patch_direct(child_src, child_dst, create, resize, 0, 0, length, verbose);
        }
    }
    closedir(d);
    return ERR_OK;
}

//=============================================================================
// 5. DOL Executable Memory Patching & Dynamic Section Allocation
//=============================================================================

#define DOL_MAX_TEXT 7
#define DOL_MAX_DATA 11
#define DOL_MAX_SECT (DOL_MAX_TEXT + DOL_MAX_DATA)

typedef struct dol_mem_ctx_t
{
    u8 *data;
    size_t size;
    size_t capacity;

    u32 sect_off[DOL_MAX_SECT];
    u32 sect_addr[DOL_MAX_SECT];
    u32 sect_size[DOL_MAX_SECT];
    u32 bss_addr;
    u32 bss_size;
    u32 entry_point;

    int new_sections[DOL_MAX_SECT];
    int n_new_sections;
} dol_mem_ctx_t;

static void dol_decode_header(dol_mem_ctx_t *ctx)
{
    const u8 *h = ctx->data;
    for (int i = 0; i < DOL_MAX_SECT; i++)
    {
        ctx->sect_off[i] = be32(h + i * 4);
        ctx->sect_addr[i] = be32(h + 0x48 + i * 4);
        ctx->sect_size[i] = be32(h + 0x90 + i * 4);
    }
    ctx->bss_addr = be32(h + 0xd8);
    ctx->bss_size = be32(h + 0xdc);
    ctx->entry_point = be32(h + 0xe0);
}

static void dol_encode_header(dol_mem_ctx_t *ctx)
{
    u8 *h = ctx->data;
    for (int i = 0; i < DOL_MAX_SECT; i++)
    {
        write_be32(h + i * 4, ctx->sect_off[i]);
        write_be32(h + 0x48 + i * 4, ctx->sect_addr[i]);
        write_be32(h + 0x90 + i * 4, ctx->sect_size[i]);
    }
    write_be32(h + 0xd8, ctx->bss_addr);
    write_be32(h + 0xdc, ctx->bss_size);
    write_be32(h + 0xe0, ctx->entry_point);
}

static s64 dol_addr_to_file_offset(const dol_mem_ctx_t *ctx, u32 addr)
{
    for (int i = 0; i < DOL_MAX_SECT; i++)
    {
        if (ctx->sect_size[i] > 0 && addr >= ctx->sect_addr[i] && addr < ctx->sect_addr[i] + ctx->sect_size[i])
        {
            return (s64)ctx->sect_off[i] + (addr - ctx->sect_addr[i]);
        }
    }
    return -1;
}

static int dol_find_empty_section(const dol_mem_ctx_t *ctx, bool prefer_data)
{
    if (prefer_data)
    {
        for (int i = DOL_MAX_TEXT; i < DOL_MAX_SECT; i++)
            if (ctx->sect_size[i] == 0 || ctx->sect_off[i] == 0)
                return i;
        for (int i = 0; i < DOL_MAX_TEXT; i++)
            if (ctx->sect_size[i] == 0 || ctx->sect_off[i] == 0)
                return i;
    }
    else
    {
        for (int i = 0; i < DOL_MAX_TEXT; i++)
            if (ctx->sect_size[i] == 0 || ctx->sect_off[i] == 0)
                return i;
        for (int i = DOL_MAX_TEXT; i < DOL_MAX_SECT; i++)
            if (ctx->sect_size[i] == 0 || ctx->sect_off[i] == 0)
                return i;
    }
    return -1;
}

static void dol_merge_adjacent_sections(dol_mem_ctx_t *ctx)
{
    for (int i = 0; i < ctx->n_new_sections; i++)
    {
        int s1 = ctx->new_sections[i];
        if (s1 < 0 || ctx->sect_size[s1] == 0)
            continue;

        for (int j = 0; j < ctx->n_new_sections; j++)
        {
            if (i == j)
                continue;
            int s2 = ctx->new_sections[j];
            if (s2 < 0 || ctx->sect_size[s2] == 0)
                continue;

            if (ctx->sect_addr[s1] + ctx->sect_size[s1] == ctx->sect_addr[s2] &&
                ctx->sect_off[s1] + ctx->sect_size[s1] == ctx->sect_off[s2])
            {
                ctx->sect_size[s1] += ctx->sect_size[s2];
                ctx->sect_off[s2] = 0;
                ctx->sect_addr[s2] = 0;
                ctx->sect_size[s2] = 0;
                ctx->new_sections[j] = -1;
            }
        }
    }
    dol_encode_header(ctx);
}

static enumError apply_memory_patches_to_dol(const char *dol_path, active_patch_t *active_patches,
                                            const char *sd_root, const char *xml_dir, int verbose)
{
    u8 *dol_data = NULL;
    size_t dol_size = 0;
    enumError err = LoadFileAlloc(dol_path, 0, 0, &dol_data, &dol_size, 64 * MiB, 0, 0, false);
    if (err || !dol_data || dol_size < 0x100)
    {
        if (verbose >= 1)
            printf("  ! Failed to load main.dol: %s\n", dol_path);
        FREE(dol_data);
        return err ? err : ERR_CANT_OPEN;
    }

    dol_mem_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.data = dol_data;
    ctx.size = dol_size;
    ctx.capacity = dol_size;
    dol_decode_header(&ctx);

    uint applied_count = 0;
    uint skipped_count = 0;

    for (active_patch_t *ap = active_patches; ap; ap = ap->next)
    {
        for (riiv_memory_t *m = ap->patch->memories; m; m = m->next)
        {
            u8 *payload = NULL;
            uint payload_len = 0;
            bool free_payload = false;

            if (m->valuefile && *m->valuefile)
            {
                char vpath[PATH_MAX];
                if (resolve_external_path(ap, sd_root, xml_dir, m->valuefile, vpath, sizeof(vpath)))
                {
                    size_t v_size = 0;
                    if (LoadFileAlloc(vpath, 0, 0, &payload, &v_size, 32 * MiB, 0, 0, false) == ERR_OK)
                    {
                        payload_len = (uint)v_size;
                        free_payload = true;
                    }
                }
                if (!payload)
                {
                    if (verbose >= 1)
                        printf("  ! Memory valuefile not found: %s\n", m->valuefile);
                    skipped_count++;
                    continue;
                }
            }
            else if (m->value && m->value_len > 0)
            {
                payload = m->value;
                payload_len = m->value_len;
            }

            if (!payload || payload_len == 0)
            {
                skipped_count++;
                continue;
            }

            // Ignore Return-to-Riivolution channel launcher hooks (Title ID 00010001-RIIV / 0x52494956)
            // which break standalone ISO/WBFS execution by hijacking _OSBootDOL
            if (m->offset == 0x80048C10 || (m->offset >= 0x80003400 && m->offset <= 0x80003430))
            {
                if (verbose >= 1)
                    printf("  - Skipping Return-to-Riivolution channel hook at 0x%08X (standalone disc)\n", m->offset);
                skipped_count++;
                if (free_payload) FREE(payload);
                continue;
            }

            if (m->search && m->original && m->original_len > 0)
            {
                bool match_found = false;
                u32 align = m->align > 0 ? m->align : 4;

                for (int s = 0; s < DOL_MAX_SECT; s++)
                {
                    if (ctx.sect_size[s] < m->original_len)
                        continue;

                    const u8 *sect_data = ctx.data + ctx.sect_off[s];
                    u32 sect_limit = ctx.sect_size[s] - m->original_len;

                    for (u32 off = 0; off <= sect_limit; off += align)
                    {
                        if (!memcmp(sect_data + off, m->original, m->original_len))
                        {
                            u32 match_addr = ctx.sect_addr[s] + off;
                            s64 foff = (s64)ctx.sect_off[s] + off;
                            memcpy(ctx.data + foff, payload, payload_len);
                            applied_count++;
                            match_found = true;

                            if (verbose >= 1)
                                printf("  + Matched search pattern at 0x%08X (DOL off 0x%llX), wrote %u bytes\n",
                                       match_addr, (unsigned long long)foff, payload_len);

                            if (m->ocarina && m->offset > 0)
                            {
                                for (u32 b_off = off + payload_len; b_off + 4 <= ctx.sect_size[s]; b_off += 4)
                                {
                                    if (be32(sect_data + b_off) == 0x4e800020)
                                    {
                                        u32 blr_addr = ctx.sect_addr[s] + b_off;
                                        s32 delta = (s32)m->offset - (s32)blr_addr;
                                        u32 branch = 0x48000000 | (delta & 0x03fffffc);
                                        write_be32(ctx.data + ctx.sect_off[s] + b_off, branch);
                                        if (verbose >= 1)
                                            printf("  + Patched Ocarina hook at 0x%08X -> branch to 0x%08X (0x%08X)\n",
                                                   blr_addr, m->offset, branch);
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                    if (match_found)
                        break;
                }

                if (!match_found)
                {
                    if (verbose >= 2)
                        printf("  - Search pattern not found for memory patch\n");
                    skipped_count++;
                }
            }
            else
            {
                u32 addr = m->offset;
                s64 foff = dol_addr_to_file_offset(&ctx, addr);

                if (foff >= 0)
                {
                    if (m->original && m->original_len > 0)
                    {
                        if (foff + m->original_len > (s64)ctx.size ||
                            memcmp(ctx.data + foff, m->original, m->original_len) != 0)
                        {
                            if (verbose >= 2)
                                printf("  - Original bytes mismatch at 0x%08X (DOL off 0x%llX), skipping region-specific patch\n",
                                       addr, (unsigned long long)foff);
                            skipped_count++;
                            if (free_payload) FREE(payload);
                            continue;
                        }
                    }

                    if (foff + payload_len <= (s64)ctx.size)
                    {
                        memcpy(ctx.data + foff, payload, payload_len);
                        applied_count++;
                        if (verbose >= 1)
                            printf("  + Patched 0x%08X (DOL off 0x%llX) with %u bytes\n", addr, (unsigned long long)foff, payload_len);
                    }
                    else
                    {
                        skipped_count++;
                    }
                }
                else
                {
                    if (m->original && m->original_len > 0)
                    {
                        skipped_count++;
                        if (free_payload) FREE(payload);
                        continue;
                    }

                    // Check if addr can extend the most recently created dynamic section
                    int extend_idx = -1;
                    if (ctx.n_new_sections > 0)
                    {
                        int last_s = ctx.new_sections[ctx.n_new_sections - 1];
                        u32 s_start = ctx.sect_addr[last_s];
                        u32 s_end = s_start + ctx.sect_size[last_s];
                        if (addr >= s_start && addr <= s_end + 256)
                            extend_idx = last_s;
                    }

                    if (extend_idx >= 0)
                    {
                        u32 s_start = ctx.sect_addr[extend_idx];
                        u32 s_end = s_start + ctx.sect_size[extend_idx];
                        u32 new_end = addr + payload_len;

                        if (new_end > s_end)
                        {
                            u32 new_size = ((new_end - s_start) + 3) & ~3;
                            u32 growth = new_size - ctx.sect_size[extend_idx];

                            ctx.data = REALLOC(ctx.data, ctx.size + growth);
                            memset(ctx.data + ctx.size, 0, growth);
                            ctx.capacity = ctx.size + growth;
                            ctx.size += growth;
                            ctx.sect_size[extend_idx] = new_size;
                            dol_encode_header(&ctx);
                        }

                        u32 target_off = ctx.sect_off[extend_idx] + (addr - s_start);
                        memcpy(ctx.data + target_off, payload, payload_len);
                        applied_count++;
                        if (verbose >= 1)
                            printf("  + Extended DOL section #%d to 0x%08X (wrote %u bytes at off 0x%X)\n",
                                   extend_idx, addr, payload_len, target_off);
                        if (free_payload) FREE(payload);
                        continue;
                    }

                    int empty_idx = dol_find_empty_section(&ctx, (addr & 0x80000000) != 0);
                    if (empty_idx < 0)
                    {
                        if (verbose >= 1)
                            printf("  ! No empty DOL section available for address 0x%08X\n", addr);
                        skipped_count++;
                        if (free_payload) FREE(payload);
                        continue;
                    }

                    u32 new_off = (ctx.size + 31) & ~31;
                    u32 new_size = (payload_len + 3) & ~3;
                    size_t new_total = new_off + new_size;

                    ctx.data = REALLOC(ctx.data, new_total);
                    memset(ctx.data + ctx.size, 0, new_total - ctx.size);
                    ctx.capacity = new_total;

                    memcpy(ctx.data + new_off, payload, payload_len);

                    ctx.sect_off[empty_idx] = new_off;
                    ctx.sect_addr[empty_idx] = addr & ~3;
                    ctx.sect_size[empty_idx] = new_size;
                    ctx.size = new_total;

                    if (ctx.n_new_sections < DOL_MAX_SECT)
                        ctx.new_sections[ctx.n_new_sections++] = empty_idx;

                    dol_encode_header(&ctx);
                    applied_count++;

                    if (verbose >= 1)
                        printf("  + Created new DOL section #%d at 0x%08X (file off 0x%X, size %u bytes)\n",
                               empty_idx, addr, new_off, new_size);
                }
            }

            if (free_payload)
                FREE(payload);
        }
    }

    if (ctx.n_new_sections > 1)
        dol_merge_adjacent_sections(&ctx);

    dol_encode_header(&ctx);

    err = SaveFile(dol_path, 0, FM_OVERWRITE, ctx.data, ctx.size, 0);
    FREE(ctx.data);

    if (verbose >= 1)
        printf("  DOL patching complete: %u applied, %u skipped/ignored.\n", applied_count, skipped_count);

    return err;
}

//=============================================================================
// 6. High-Level Orchestrator (RiivolutionBuild & RiivolutionCommand)
//=============================================================================

static enumError inject_gct_into_dol(const char *dol_path, const char *gct_path, int verbose)
{
    struct stat st;
    if (stat(gct_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        if (verbose >= 1)
            printf("  ! GCT file not found: %s\n", gct_path);
        return ERR_NOT_EXISTS;
    }

    char cmd[PATH_MAX * 3];
    snprintf(cmd, sizeof(cmd), "wstrt patch \"%s\" --add-section \"%s\" %s 2>&1",
             dol_path, gct_path, verbose >= 2 ? "-vv" : (verbose >= 1 ? "-v" : "-q"));

    if (verbose >= 0)
        printf("  Injecting GCT code handler: %s -> main.dol\n", gct_path);

    int ret = system(cmd);
    if (ret != 0)
    {
        const char *wstrt_paths[] = {
            "/Users/larsen/wiimms-szs-tools-nintendo/project/bin/wstrt",
            "/usr/local/bin/wstrt",
            "/opt/homebrew/bin/wstrt",
            NULL
        };
        for (int i = 0; wstrt_paths[i]; i++)
        {
            if (stat(wstrt_paths[i], &st) == 0 && (st.st_mode & S_IXUSR))
            {
                snprintf(cmd, sizeof(cmd), "\"%s\" patch \"%s\" --add-section \"%s\" %s 2>&1",
                         wstrt_paths[i], dol_path, gct_path, verbose >= 2 ? "-vv" : (verbose >= 1 ? "-v" : "-q"));
                ret = system(cmd);
                if (ret == 0)
                    break;
            }
        }
    }

    if (ret == 0)
    {
        if (verbose >= 0)
            printf("  + Successfully injected GCT section into main.dol\n");
        return ERR_OK;
    }
    else
    {
        printf("  ! Warning: Failed to inject GCT using wstrt (status %d).\n"
               "    Ensure 'wstrt' is installed and in PATH.\n", ret);
        return ERR_WARNING;
    }
}

void InitRiivolutionOptions(RiivolutionOptions_t *opt)
{
    if (!opt)
        return;
    memset(opt, 0, sizeof(*opt));
    InitializeStringField(&opt->choices);
    InitializeStringField(&opt->gct_files);
    opt->output_oft = OFT_UNKNOWN;
    opt->verbose = 0;
}

static void get_xml_directory(const char *xml_path, char *out_dir, size_t out_size)
{
    StringCopyS(out_dir, out_size, xml_path);
    char *slash = strrchr(out_dir, '/');
    if (slash)
        *slash = '\0';
    else
        StringCopyS(out_dir, out_size, ".");
}

static void determine_sd_root(const RiivolutionOptions_t *opt, const char *xml_dir, char *out_root, size_t out_size)
{
    if (opt->root_dir && *opt->root_dir)
    {
        StringCopyS(out_root, out_size, opt->root_dir);
        return;
    }

    char tmp[PATH_MAX];
    StringCopyS(tmp, sizeof(tmp), xml_dir);
    for (char *p = tmp; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }

    char *riiv_pos = strcasestr(tmp, "/riivolution");
    if (riiv_pos && (riiv_pos[12] == '/' || riiv_pos[12] == '\0'))
    {
        *riiv_pos = '\0';
        StringCopyS(out_root, out_size, tmp);
        return;
    }

    StringCopyS(out_root, out_size, xml_dir);
}

enumError RiivolutionBuild(RiivolutionOptions_t *opt)
{
    if (!opt || !opt->xml_file)
        return ERROR0(ERR_MISSING_PARAM, "Missing Riivolution XML file.\n");

    riiv_doc_t *doc = RiivolutionParseXmlFile(opt->xml_file);
    if (!doc)
        return ERROR0(ERR_SYNTAX, "Failed to parse Riivolution XML: %s\n", opt->xml_file);

    if (opt->info_only || !opt->source_image)
    {
        RiivolutionInspect(doc, opt->verbose);
        RiivolutionFreeDoc(doc);
        return ERR_OK;
    }

    char xml_dir[PATH_MAX];
    get_xml_directory(opt->xml_file, xml_dir, sizeof(xml_dir));

    char sd_root[PATH_MAX];
    determine_sd_root(opt, xml_dir, sd_root, sizeof(sd_root));

    if (opt->verbose >= 0)
    {
        printf("Riivolution ISO Builder\n");
        printf("  XML Path:     %s\n", opt->xml_file);
        printf("  SD Root:      %s\n", sd_root);
        printf("  Source Game:  %s\n", opt->source_image);
    }

    char id6[8] = "";
    char game_title[64] = "";
    bool source_is_dir = IsDirectory(opt->source_image, false);
    SuperFile_t sfi;
    InitializeSF(&sfi);

    if (source_is_dir)
    {
        char hpath[PATH_MAX];
        snprintf(hpath, sizeof(hpath), "%s/disc/header.bin", opt->source_image);
        FILE *hf = fopen(hpath, "rb");
        if (!hf)
        {
            snprintf(hpath, sizeof(hpath), "%s/DATA/sys/boot.bin", opt->source_image);
            hf = fopen(hpath, "rb");
        }
        if (!hf)
        {
            snprintf(hpath, sizeof(hpath), "%s/sys/boot.bin", opt->source_image);
            hf = fopen(hpath, "rb");
        }
        if (hf)
        {
            fread(id6, 1, 6, hf);
            id6[6] = '\0';
            fseek(hf, 0x20, SEEK_SET);
            fread(game_title, 1, sizeof(game_title) - 1, hf);
            game_title[sizeof(game_title) - 1] = '\0';
            fclose(hf);
        }
    }
    else
    {
        SetupIOD(&sfi, OFT_UNKNOWN, OFT_UNKNOWN);
        enumError err = OpenSF(&sfi, opt->source_image, true, false);
        if (err)
        {
            ResetSF(&sfi, 0);
            RiivolutionFreeDoc(doc);
            return ERROR0(err, "Cannot open source disc image: %s\n", opt->source_image);
        }
        wd_disc_t *disc = OpenDiscSF(&sfi, true, true);
        if (!disc)
        {
            ResetSF(&sfi, 0);
            RiivolutionFreeDoc(doc);
            return ERROR0(ERR_CANT_OPEN, "Cannot read disc header from image: %s\n", opt->source_image);
        }
        memcpy(id6, disc->dhead.id6.id6, 6);
        id6[6] = '\0';
        memcpy(game_title, disc->dhead.disc_title, sizeof(disc->dhead.disc_title));
        game_title[sizeof(disc->dhead.disc_title) - 1] = '\0';
    }

    if (opt->verbose >= 0)
        printf("  Base Game ID: %s\n", id6[0] ? id6 : "<unknown>");

    if (!opt->ignore_regions && id6[0])
    {
        if (doc->filter_game && strncasecmp(id6, doc->filter_game, strlen(doc->filter_game)) != 0)
        {
            ResetSF(&sfi, 0);
            RiivolutionFreeDoc(doc);
            return ERROR0(ERR_WRONG_FILE_TYPE,
                          "Game ID mismatch: base game '%s' does not match XML filter '%s' (use --ignore-regions to override).\n",
                          id6, doc->filter_game);
        }

        if (doc->filter_regions.used > 0)
        {
            bool reg_matched = false;
            char game_reg[2] = { id6[3], '\0' };
            for (uint i = 0; i < doc->filter_regions.used; i++)
            {
                if (!strcasecmp(doc->filter_regions.field[i], game_reg))
                {
                    reg_matched = true;
                    break;
                }
            }
            if (!reg_matched)
            {
                ResetSF(&sfi, 0);
                RiivolutionFreeDoc(doc);
                return ERROR0(ERR_WRONG_FILE_TYPE,
                              "Region mismatch: game region '%c' not in XML supported regions (use --ignore-regions to override).\n",
                              id6[3]);
            }
        }
    }

    apply_choice_specs(doc, opt);
    if (opt->interactive)
        prompt_interactive_choices(doc, opt, id6, game_title, sizeof(game_title));

    active_patch_t *active_patches = resolve_active_patches(doc, opt, id6);

    uint act_count = 0;
    for (active_patch_t *ap = active_patches; ap; ap = ap->next)
        act_count++;

    if (opt->testmode)
    {
        printf("\nTEST MODE: Successfully resolved %u active patch(es) for game %s:\n", act_count, id6);
        for (active_patch_t *ap = active_patches; ap; ap = ap->next)
        {
            printf("  Patch ID: \"%s\" (root=\"%s\")\n", ap->patch->id, ap->resolved_root);
            for (riiv_file_t *f = ap->patch->files; f; f = f->next)
                printf("    FILE:   %s -> %s\n", f->external, f->disc);
            for (riiv_folder_t *fo = ap->patch->folders; fo; fo = fo->next)
                printf("    FOLDER: %s -> %s\n", fo->external, fo->disc);
            for (riiv_memory_t *m = ap->patch->memories; m; m = m->next)
                printf("    MEMORY: 0x%08X (%u bytes)\n", m->offset, m->value_len);
        }

        if (opt->custom_dol)
            printf("  CUSTOM DOL: %s\n", opt->custom_dol);
        if (opt->gct_files.used > 0)
        {
            printf("  GCT CODE INJECTION:\n");
            for (uint i = 0; i < opt->gct_files.used; i++)
                printf("    GCT:    %s\n", opt->gct_files.field[i]);
        }
        printf("\nTest mode passed: mod XML, game ID, and patches verified without making modifications.\n");
        ResetSF(&sfi, 0);
        free_active_patches(active_patches);
        RiivolutionFreeDoc(doc);
        return ERR_OK;
    }

    char work_dir[PATH_MAX];
    char temp_dir[PATH_MAX];
    temp_dir[0] = '\0';
    bool is_temp = false;

    if (source_is_dir)
    {
        if (!opt->dest_path || !strcmp(opt->source_image, opt->dest_path))
        {
            StringCopyS(work_dir, sizeof(work_dir), opt->source_image);
        }
        else if (opt->output_oft == OFT_FST || IsDirectory(opt->dest_path, false) ||
                 opt->dest_path[strlen(opt->dest_path) - 1] == '/')
        {
            mkdir_p(opt->dest_path);
            StringCopyS(work_dir, sizeof(work_dir), opt->dest_path);
            apply_folder_patch_recursive(opt->source_image, work_dir, work_dir, true, true, true, 0, opt->verbose);
        }
        else
        {
            StringCopyS(work_dir, sizeof(work_dir), opt->source_image);
        }
    }
    else
    {
        const char *tmp_root = getenv("TMPDIR");
        if (!tmp_root) tmp_root = getenv("TEMP");
        if (!tmp_root) tmp_root = "/tmp";

        snprintf(temp_dir, sizeof(temp_dir), "%s/wit-riiv-XXXXXX", tmp_root);
        if (!mkdtemp(temp_dir))
        {
            ResetSF(&sfi, 0);
            free_active_patches(active_patches);
            RiivolutionFreeDoc(doc);
            return ERROR0(ERR_CANT_CREATE, "Failed to create temporary directory for extraction.\n");
        }
        is_temp = true;
        StringCopyS(work_dir, sizeof(work_dir), temp_dir);

        if (opt->verbose >= 0)
            printf("  Extracting game filesystem to: %s\n", work_dir);

        char dest_dir_slash[PATH_MAX + 2];
        snprintf(dest_dir_slash, sizeof(dest_dir_slash), "%s/", work_dir);
        enumError err = ExtractImage(&sfi, dest_dir_slash, 1, false);
        ResetSF(&sfi, 0);

        if (err)
        {
            remove_dir_recursive(temp_dir);
            free_active_patches(active_patches);
            RiivolutionFreeDoc(doc);
            return ERROR0(err, "Failed to extract source image filesystem.\n");
        }
    }

    char files_dir[PATH_MAX];
    char sys_dir[PATH_MAX];

    snprintf(files_dir, sizeof(files_dir), "%s/DATA/files", work_dir);
    if (IsDirectory(files_dir, false))
    {
        snprintf(sys_dir, sizeof(sys_dir), "%s/DATA/sys", work_dir);
    }
    else
    {
        snprintf(files_dir, sizeof(files_dir), "%s/files", work_dir);
        snprintf(sys_dir, sizeof(sys_dir), "%s/sys", work_dir);
    }

    if (opt->verbose >= 0)
        printf("  Applying %u active patches...\n", act_count);

    bool has_savegame = false;
    bool dol_replaced = false;

    char dol_file[PATH_MAX];
    snprintf(dol_file, sizeof(dol_file), "%s/main.dol", sys_dir);
    struct stat st_dol;

    if (opt->custom_dol && *opt->custom_dol)
    {
        if (copy_file_contents(opt->custom_dol, dol_file) == ERR_OK)
        {
            if (opt->verbose >= 0)
                printf("  + Replaced main.dol with custom executable: %s\n", opt->custom_dol);
            dol_replaced = true;
        }
        else
        {
            printf("  ! Warning: Failed to copy custom DOL file: %s\n", opt->custom_dol);
        }
    }

    for (active_patch_t *ap = active_patches; ap; ap = ap->next)
    {
        if (ap->patch->savegames)
            has_savegame = true;

        for (riiv_file_t *f = ap->patch->files; f; f = f->next)
        {
            char src_path[PATH_MAX];
            if (!resolve_external_path(ap, sd_root, xml_dir, f->external, src_path, sizeof(src_path)))
            {
                if (opt->verbose >= 1)
                    printf("  ! Cannot resolve external file: %s\n", f->external);
                continue;
            }

            char dst_path[PATH_MAX];
            char *subst_disc = subst_params(f->disc, ap->params);
            size_t slen = strlen(subst_disc);

            bool is_dol = (!strcasecmp(subst_disc, "main.dol") ||
                           !strcasecmp(subst_disc, "/main.dol") ||
                           !strcasecmp(subst_disc, "sys/main.dol") ||
                           !strcasecmp(subst_disc, "/sys/main.dol") ||
                           !strcasecmp(subst_disc, "DATA/sys/main.dol") ||
                           !strcasecmp(subst_disc, "/DATA/sys/main.dol") ||
                           (slen >= 8 && !strcasecmp(subst_disc + slen - 8, "main.dol") &&
                            (subst_disc[slen - 9] == '/' || subst_disc[slen - 9] == '\\')));

            if (!is_dol && (!*subst_disc || !strcmp(subst_disc, "/")))
            {
                size_t elen = strlen(f->external);
                if (elen > 4 && !strcasecmp(f->external + elen - 4, ".dol"))
                    is_dol = true;
            }

            if (is_dol)
            {
                snprintf(dst_path, sizeof(dst_path), "%s/main.dol", sys_dir);
                dol_replaced = true;
                if (opt->verbose >= 0)
                    printf("  + Replacing executable main.dol from external file: %s\n", src_path);
            }
            else if (subst_disc[0] == '/' || subst_disc[0] == '\\')
            {
                snprintf(dst_path, sizeof(dst_path), "%s/%s", files_dir, subst_disc + 1);
            }
            else if (*subst_disc)
            {
                if (!find_file_in_tree(files_dir, subst_disc, dst_path, sizeof(dst_path)))
                {
                    if (f->create)
                        snprintf(dst_path, sizeof(dst_path), "%s/%s", files_dir, subst_disc);
                    else
                    {
                        FREE(subst_disc);
                        continue;
                    }
                }
            }
            else
            {
                FREE(subst_disc);
                continue;
            }
            FREE(subst_disc);

            apply_file_patch_direct(src_path, dst_path, f->create, f->resize, f->offset, f->fileoffset, f->length, opt->verbose);
        }

        for (riiv_folder_t *fold = ap->patch->folders; fold; fold = fold->next)
        {
            char src_dir[PATH_MAX];
            if (!resolve_external_path(ap, sd_root, xml_dir, fold->external, src_dir, sizeof(src_dir)))
            {
                if (opt->verbose >= 1)
                    printf("  ! Cannot resolve external folder: %s\n", fold->external);
                continue;
            }

            char dst_dir[PATH_MAX];
            char *subst_disc = subst_params(fold->disc, ap->params);

            if (!strcasecmp(subst_disc, "sys") || !strcasecmp(subst_disc, "/sys") ||
                !strcasecmp(subst_disc, "DATA/sys") || !strcasecmp(subst_disc, "/DATA/sys"))
            {
                snprintf(dst_dir, sizeof(dst_dir), "%s", sys_dir);
            }
            else if (!strcasecmp(subst_disc, "/") || !strcasecmp(subst_disc, "root"))
            {
                snprintf(dst_dir, sizeof(dst_dir), "%s", files_dir);
            }
            else if (subst_disc[0] == '/' || subst_disc[0] == '\\')
            {
                snprintf(dst_dir, sizeof(dst_dir), "%s/%s", files_dir, subst_disc + 1);
            }
            else
            {
                snprintf(dst_dir, sizeof(dst_dir), "%s/%s", files_dir, subst_disc);
            }

            FREE(subst_disc);
            apply_folder_patch_recursive(src_dir, dst_dir, files_dir, fold->create, fold->resize, fold->recursive, fold->length, opt->verbose);
        }
    }

    if (!dol_replaced)
    {
        char cand[PATH_MAX];
        snprintf(cand, sizeof(cand), "%s/main.dol", sd_root);
        if (stat(cand, &st_dol) != 0)
            snprintf(cand, sizeof(cand), "%s/sys/main.dol", sd_root);
        if (stat(cand, &st_dol) != 0)
            snprintf(cand, sizeof(cand), "%s/main.dol", xml_dir);
        if (stat(cand, &st_dol) != 0)
            snprintf(cand, sizeof(cand), "%s/sys/main.dol", xml_dir);

        if (stat(cand, &st_dol) == 0 && S_ISREG(st_dol.st_mode))
        {
            if (copy_file_contents(cand, dol_file) == ERR_OK)
            {
                if (opt->verbose >= 0)
                    printf("  + Imported patched main.dol from mod directory: %s\n", cand);
                dol_replaced = true;
            }
        }
    }

    if (stat(dol_file, &st_dol) == 0)
    {
        apply_memory_patches_to_dol(dol_file, active_patches, sd_root, xml_dir, opt->verbose);

        // Inject GCT cheat codes via wstrt if specified or found
        StringField_t all_gcts;
        InitializeStringField(&all_gcts);

        for (uint i = 0; i < opt->gct_files.used; i++)
            AppendStringField(&all_gcts, opt->gct_files.field[i], false);

        if (all_gcts.used == 0)
        {
            char gct_candidate[PATH_MAX];
            snprintf(gct_candidate, sizeof(gct_candidate), "%s/%s.gct", sd_root, id6);
            struct stat st_gct;
            if (stat(gct_candidate, &st_gct) == 0)
                AppendStringField(&all_gcts, gct_candidate, false);
            else
            {
                snprintf(gct_candidate, sizeof(gct_candidate), "%s/codes/%s.gct", sd_root, id6);
                if (stat(gct_candidate, &st_gct) == 0)
                    AppendStringField(&all_gcts, gct_candidate, false);
                else
                {
                    snprintf(gct_candidate, sizeof(gct_candidate), "%s/%s.gct", xml_dir, id6);
                    if (stat(gct_candidate, &st_gct) == 0)
                        AppendStringField(&all_gcts, gct_candidate, false);
                }
            }
        }

        for (uint i = 0; i < all_gcts.used; i++)
        {
            inject_gct_into_dol(dol_file, all_gcts.field[i], opt->verbose);
        }
        ResetStringField(&all_gcts);
    }

    if (has_savegame && !opt->custom_id && opt->verbose >= 0)
    {
        printf("  Note: Mod specifies <savegame> redirection. Consider specifying --id=<ID6>\n"
               "        to give the mod an independent save file slot.\n");
    }

    char final_dest[PATH_MAX];
    if (opt->dest_path && *opt->dest_path)
    {
        StringCopyS(final_dest, sizeof(final_dest), opt->dest_path);
    }
    else
    {
        if (source_is_dir)
        {
            if (opt->verbose >= 0)
                printf("\nPatched FST directory in place: %s\n", work_dir);
            free_active_patches(active_patches);
            RiivolutionFreeDoc(doc);
            return ERR_OK;
        }
        snprintf(final_dest, sizeof(final_dest), "%s-mod.iso", id6[0] ? id6 : "game");
    }

    enumOFT out_oft = opt->output_oft;
    if (out_oft == OFT_UNKNOWN)
    {
        size_t dlen = strlen(final_dest);
        if (final_dest[dlen - 1] == '/' || IsDirectory(final_dest, false))
            out_oft = OFT_FST;
        else
            out_oft = OFT_UNKNOWN;
    }

    if (out_oft == OFT_FST)
    {
        if (strcmp(work_dir, final_dest))
        {
            mkdir_p(final_dest);
            apply_folder_patch_recursive(work_dir, final_dest, final_dest, true, true, true, 0, opt->verbose);
        }
        if (opt->verbose >= 0)
            printf("\nSuccessfully exported FST directory: %s\n", final_dest);
    }
    else
    {
        if (opt->verbose >= 0)
            printf("  Packing disc image: %s (%s)...\n", final_dest, oft_info[out_oft].name);

        SuperFile_t fo_fst;
        InitializeSF(&fo_fst);
        fo_fst.allow_fst = true;
        enumError err = OpenSF(&fo_fst, work_dir, true, false);
        if (err)
        {
            if (is_temp && !opt->keep_temp)
                remove_dir_recursive(temp_dir);
            free_active_patches(active_patches);
            RiivolutionFreeDoc(doc);
            return ERROR0(err, "Failed to compose FST filesystem from: %s\n", work_dir);
        }

        if (opt->custom_id)
            ScanOptId(opt->custom_id);
        if (opt->custom_name)
            ScanOptName(opt->custom_name);

        err = CopyImageName(&fo_fst, final_dest, NULL, out_oft, opt->overwrite, true, false);
        ResetSF(&fo_fst, 0);

        if (err)
        {
            if (is_temp && !opt->keep_temp)
                remove_dir_recursive(temp_dir);
            free_active_patches(active_patches);
            RiivolutionFreeDoc(doc);
            return ERROR0(err, "Failed to create output disc image: %s\n", final_dest);
        }

        if (opt->verbose >= 0)
            printf("\nSuccessfully created: %s\n", final_dest);
    }

    if (is_temp && !opt->keep_temp)
        remove_dir_recursive(temp_dir);

    free_active_patches(active_patches);
    RiivolutionFreeDoc(doc);
    return ERR_OK;
}

enumError RiivolutionCommand(RiivolutionOptions_t *opt)
{
    return RiivolutionBuild(opt);
}
