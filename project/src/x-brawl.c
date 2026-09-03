/*
 * wit x-brawl.c - Super Smash Bros. Brawl Mod ISO Builder (BrawlBuilder engine)
 * 
 * Supports Gecko-based Brawl mods (Project M, Project+, Brawl-, PM Remix, etc.)
 * with automatic GCT patching, alternate stage handling, SFX DVD loading,
 * Subspace Emissary removal, custom IDs and titles, and DOL code injection.
 */

#include "x-brawl.h"
#include "x-brawl-data.h"
#include "ui-wit.h"
#include "lib-sf.h"
#include "iso-interface.h"
#include "wbfs-interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <libgen.h>

BrawlOptions_t brawl_options;

#define DOL_MAX_TEXT 7
#define DOL_MAX_DATA 11
#define DOL_MAX_SECT 18

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
} dol_mem_ctx_t;

static void dol_decode_header(dol_mem_ctx_t *ctx)
{
    const u8 *h = ctx->data;
    for (int i = 0; i < DOL_MAX_SECT; i++)
    {
        ctx->sect_off[i]  = be32(h + i * 4);
        ctx->sect_addr[i] = be32(h + 0x48 + i * 4);
        ctx->sect_size[i] = be32(h + 0x90 + i * 4);
    }
    ctx->bss_addr    = be32(h + 0xd8);
    ctx->bss_size    = be32(h + 0xdc);
    ctx->entry_point = be32(h + 0xe0);
}

static void dol_encode_header(dol_mem_ctx_t *ctx)
{
    u8 *h = ctx->data;
    for (int i = 0; i < DOL_MAX_SECT; i++)
    {
        write_be32(h + i * 4,        ctx->sect_off[i]);
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
            return (s64)ctx->sect_off[i] + (addr - ctx->sect_addr[i]);
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

// Stage mapping table
typedef struct BrawlStageMapping_t
{
    const char *pac_name;
    const char *rel_name;
} BrawlStageMapping_t;

static const BrawlStageMapping_t brawl_stage_mappings[] = {
    {"stgdonkey", "st_donkey"},         /* 75m               */
    {"stgkart", "st_kart"},             /* Mario Circuit     */
    {"stgbattlefield", "st_battle"},    /* Battlefield       */
    {"stgmariopast", "st_mariopast"},   /* Mushroomy Kingdom */
    {"stgdxbigblue", "st_dxbigblue"},   /* Big Blue          */
    {"stgnewpork", "st_newpork"},       /* New Pork City     */
    {"stgoldin", "st_oldin"},           /* Bridge of Eldin   */
    {"stgnorfair", "st_norfair"},       /* Norfair           */
    {"stgdxzebes", "st_dxzebes"},       /* Brinstar          */
    {"stgdxonett", "st_dxonett"},       /* Onett             */
    {"stgemblem", "st_emblem"},         /* Castle Siege      */
    {"stgonlinetraining", "st_otrain"}, /* Online Training   */
    {"stgconfigtest", "st_config"},     /* ConfigTest        */
    {"stgpictchat", "st_pictchat"},     /* PictoChat         */
    {"stgdxcorneria", "st_dxcorneria"}, /* Corneria          */
    {"stgpirates", "st_pirates"},       /* Pirate Ship       */
    {"stgdolpic", "st_dolpic"},         /* Delfino Plaza     */
    {"stgdxpstadium", "st_dxpstadium"}, /* Pokémon Stadium   */
    {"stgearth", "st_earth"},           /* Distant Planet    */
    {"stgstadium", "st_stadium"},       /* Pokémon Stadium 2 */
    {"stgedit", "st_stageedit"},        /* Edit              */
    {"stgfzero", "st_fzero"},           /* Port Town Aero Dive */
    {"stgfinal", "st_final"},           /* Final Destination */
    {"stgdxrcruise", "st_dxrcruise"},   /* Rainbow Cruise    */
    {"stggw", "st_gw"},                 /* Flat Zone 2       */
    {"stgjungle", "st_jungle"},         /* Rumble Falls      */
    {"stgorpheon", "st_orpheon"},       /* Frigate Orpheon   */
    {"stgmetalgear", "st_metalgear"},   /* Shadow Moses Island */
    {"stgdxgreens", "st_dxgreens"},     /* Green Greens      */
    {"stgpalutena", "st_palutena"},     /* Skyworld          */
    {"stggreenhill", "st_greenhill"},   /* Green Hill Zone   */
    {"stgvillage", "st_village"},       /* Smashville        */
    {"stghalberd", "st_halberd"},       /* Halberd           */
    {"stgtengan", "st_tengan"},         /* Spear Pillar      */
    {"stgplankton", "st_plankton"},     /* Hanenbow          */
    {"stgice", "st_ice"},               /* Summit            */
    {"stgheal", "st_heal"},             /* Heal              */
    {"stgtarget", "st_tbreak"},         /* Target Break      */
    {"stghomerun", "st_homerun"},       /* Homerun           */
    {"stgdxshrine", "st_dxshrine"},     /* Temple            */
    {"stgdxgarden", "st_dxgarden"},     /* Jungle Japes      */
    {"stgmadein", "st_madein"},         /* WarioWare Inc.    */
    {"stgmansion", "st_mansion"},       /* Luigi's Mansion   */
    {"stgcrayon", "st_crayon"},         /* Yoshi's Island (Brawl) */
    {"stgstarfox", "st_starfox"},       /* Lylat Cruise      */
    {"stgdxyorster", "st_dxyorster"},   /* Yoshi's Island (Melee) */
    {"stgfamicom", "st_famicom"},       /* Mario Bros.       */
    {NULL, NULL}
};

static const char *lookup_rel_name(const char *pac_prefix)
{
    for (int i = 0; brawl_stage_mappings[i].pac_name; i++)
    {
        if (!strcasecmp(brawl_stage_mappings[i].pac_name, pac_prefix))
            return brawl_stage_mappings[i].rel_name;
    }
    return NULL;
}

// Utility byte searching & replacement
static int search_bytes(const u8 *haystack, size_t haystack_len, const u8 *needle, size_t needle_len)
{
    if (!haystack || !needle || needle_len > haystack_len)
        return -1;
    size_t limit = haystack_len - needle_len;
    for (size_t i = 0; i <= limit; i++)
    {
        if (memcmp(haystack + i, needle, needle_len) == 0)
            return (int)i;
    }
    return -1;
}

static int remove_bytes(u8 *buf, size_t *len, const u8 *find, size_t find_len)
{
    int count = 0;
    int idx;
    while ((idx = search_bytes(buf, *len, find, find_len)) >= 0)
    {
        memmove(buf + idx, buf + idx + find_len, *len - (idx + find_len));
        *len -= find_len;
        count++;
    }
    return count;
}

static int replace_bytes(u8 **buf_ptr, size_t *len_ptr, const u8 *find, size_t find_len, const u8 *repl, size_t repl_len)
{
    int count = 0;
    int idx;
    while ((idx = search_bytes(*buf_ptr, *len_ptr, find, find_len)) >= 0)
    {
        if (repl_len == find_len)
        {
            memcpy(*buf_ptr + idx, repl, repl_len);
        }
        else if (repl_len < find_len)
        {
            memcpy(*buf_ptr + idx, repl, repl_len);
            memmove(*buf_ptr + idx + repl_len, *buf_ptr + idx + find_len, *len_ptr - (idx + find_len));
            *len_ptr -= (find_len - repl_len);
        }
        else
        {
            size_t diff = repl_len - find_len;
            *buf_ptr = REALLOC(*buf_ptr, *len_ptr + diff);
            memmove(*buf_ptr + idx + repl_len, *buf_ptr + idx + find_len, *len_ptr - (idx + find_len));
            memcpy(*buf_ptr + idx, repl, repl_len);
            *len_ptr += diff;
        }
        count++;
    }
    return count;
}

// Byte patterns from BrawlBuilder CodePatches.txt
static const u8 pat_stock_remove[40] = {
    0x04, 0x42, 0x18, 0xEC,  0x00, 0x09, 0x5F, 0x00,
    0x04, 0x42, 0x19, 0x0C,  0x00, 0x18, 0x00, 0x00,
    0x04, 0x49, 0x49, 0x90,  0x00, 0x09, 0x5F, 0x00,
    0x04, 0x49, 0x49, 0xEC,  0x80, 0xC2, 0x3A, 0x60,
    0x04, 0x49, 0x49, 0xF0,  0x00, 0x18, 0x00, 0x00
};

static const u8 pat_stock_patch[16] = {
    0x7F, 0xFC, 0xFB, 0x78,  0x3D, 0x80, 0x80, 0xC2,
    0xA1, 0x8C, 0x43, 0x14,  0x3B, 0xA0, 0x5F, 0x65
};

static const u8 pat_stock_to[16] = {
    0x7F, 0xFC, 0xFB, 0x78,  0x3D, 0x80, 0x80, 0xC3,
    0xA1, 0x8C, 0xD1, 0x14,  0x3B, 0xA0, 0x5F, 0x65
};

static const u8 pat_altstage_if[32] = {
    0x91, 0x62, 0x00, 0x00,  0x90, 0xE2, 0x00, 0x04,
    0x91, 0x82, 0x00, 0x08,  0x90, 0x62, 0x00, 0x10,
    0x91, 0x22, 0x00, 0x14,  0x3C, 0x60, 0x70, 0x72,
    0x60, 0x63, 0x6F, 0x6A,  0x80, 0xE1, 0x00, 0x20
};

static const u8 pat_altstage_patch1[8] = { 0x06, 0x5A, 0x7E, 0x00,  0x00, 0x00, 0x00, 0x70 };
static const u8 pat_altstage_to1[8]    = { 0x06, 0x5A, 0x7E, 0x00,  0x00, 0x00, 0x00, 0x88 };

static const u8 pat_altstage_patch2[56] = {
    0x38, 0x61, 0x00, 0x88,  0x4B, 0xA7, 0x4D, 0xB9,
    0x7C, 0x7C, 0x1B, 0x78,  0x2C, 0x03, 0x00, 0x00,
    0x40, 0x82, 0x00, 0x0C,  0x38, 0x21, 0x00, 0x80,
    0x48, 0x00, 0x00, 0x1C,  0xB8, 0x41, 0x00, 0x08,
    0x38, 0x21, 0x00, 0x80,  0x4B, 0xE5, 0x24, 0xE5,
    0x38, 0x61, 0x00, 0x08,  0x4B, 0xA7, 0x42, 0xE1,
    0x7C, 0x7C, 0x1B, 0x78,  0x4B, 0xA7, 0x41, 0xE8
};

static const u8 pat_altstage_to2[80] = {
    0x38, 0x61, 0x00, 0x88,  0x80, 0x83, 0x00, 0x00,
    0x38, 0x84, 0x00, 0x0C,  0x90, 0x83, 0x00, 0x00,
    0x4B, 0xA7, 0x42, 0xFD,  0x7C, 0x7C, 0x1B, 0x78,
    0x2C, 0x03, 0x00, 0x00,  0x40, 0x82, 0x00, 0x0C,
    0x38, 0x21, 0x00, 0x80,  0x48, 0x00, 0x00, 0x28,
    0xB8, 0x41, 0x00, 0x08,  0x38, 0x21, 0x00, 0x80,
    0x4B, 0xE5, 0x24, 0xD9,  0x38, 0x61, 0x00, 0x08,
    0x80, 0x83, 0x00, 0x00,  0x38, 0x84, 0xFF, 0xF4,
    0x90, 0x83, 0x00, 0x00,  0x4B, 0xA7, 0x42, 0xC9,
    0x7C, 0x7C, 0x1B, 0x78,  0x4B, 0xA7, 0x41, 0xD0
};

static const u8 pat_sfx_pm_if[16] = {
    0x2F, 0x70, 0x72, 0x6F,  0x6A, 0x65, 0x63, 0x74,
    0x6D, 0x2F, 0x70, 0x66,  0x2F, 0x73, 0x66, 0x78
};

static const u8 pat_sfx_rsbe_if[12] = {
    0x2F, 0x52, 0x53, 0x42,  0x45, 0x2F, 0x70, 0x66,
    0x2F, 0x73, 0x66, 0x78
};

static const u8 pat_sfx_patch1[40] = {
    0x90, 0x81, 0x00, 0x18,  0x38, 0x80, 0xFF, 0xFF,
    0x90, 0x81, 0x00, 0x1C,  0x38, 0x61, 0x00, 0x20,
    0x90, 0x61, 0x00, 0x08,  0x7C, 0xE4, 0x3B, 0x78,
    0x38, 0xA0, 0x00, 0x80,  0x4B, 0xE5, 0x29, 0xF5,
    0x38, 0x61, 0x00, 0x08,  0x4B, 0xA7, 0x52, 0xA1
};

static const u8 pat_sfx_to1[40] = {
    0x90, 0x81, 0x00, 0x18,  0x38, 0x80, 0x00, 0x00,
    0x90, 0x81, 0x00, 0x1C,  0x38, 0x61, 0x00, 0x20,
    0x90, 0x61, 0x00, 0x08,  0x7C, 0xE4, 0x3B, 0x78,
    0x38, 0xA0, 0x00, 0x80,  0x4B, 0xE5, 0x29, 0xF5,
    0x38, 0x61, 0x00, 0x08,  0x4B, 0xA7, 0x47, 0xF1
};

static const u8 pat_sfx_patch2[8]   = { 0x60, 0x84, 0x7D, 0x18,  0x7F, 0x45, 0xD3, 0x78 };
static const u8 pat_sfx_pm_to2[8]   = { 0x60, 0x84, 0x7D, 0x24,  0x7F, 0x45, 0xD3, 0x78 };
static const u8 pat_sfx_rsbe_to2[8] = { 0x60, 0x84, 0x7D, 0x30,  0x7F, 0x45, 0xD3, 0x78 };

static void patch_gct_for_brawl(u8 **gct_buf_ptr, size_t *gct_len_ptr, bool *remove_en_ptr)
{
    *remove_en_ptr = false;

    // 1. Stock icons fix
    remove_bytes(*gct_buf_ptr, gct_len_ptr, pat_stock_remove, sizeof(pat_stock_remove));
    replace_bytes(gct_buf_ptr, gct_len_ptr, pat_stock_patch, sizeof(pat_stock_patch), pat_stock_to, sizeof(pat_stock_to));

    // 2. Alt stage & disc loading
    if (search_bytes(*gct_buf_ptr, *gct_len_ptr, pat_altstage_if, sizeof(pat_altstage_if)) >= 0)
    {
        *remove_en_ptr = true;
        replace_bytes(gct_buf_ptr, gct_len_ptr, pat_altstage_patch1, sizeof(pat_altstage_patch1), pat_altstage_to1, sizeof(pat_altstage_to1));
        replace_bytes(gct_buf_ptr, gct_len_ptr, pat_altstage_patch2, sizeof(pat_altstage_patch2), pat_altstage_to2, sizeof(pat_altstage_to2));
    }

    // 3. SFX DVD loading
    if (search_bytes(*gct_buf_ptr, *gct_len_ptr, pat_sfx_pm_if, sizeof(pat_sfx_pm_if)) >= 0)
    {
        replace_bytes(gct_buf_ptr, gct_len_ptr, pat_sfx_patch1, sizeof(pat_sfx_patch1), pat_sfx_to1, sizeof(pat_sfx_to1));
        replace_bytes(gct_buf_ptr, gct_len_ptr, pat_sfx_patch2, sizeof(pat_sfx_patch2), pat_sfx_pm_to2, sizeof(pat_sfx_pm_to2));
    }
    else if (search_bytes(*gct_buf_ptr, *gct_len_ptr, pat_sfx_rsbe_if, sizeof(pat_sfx_rsbe_if)) >= 0)
    {
        replace_bytes(gct_buf_ptr, gct_len_ptr, pat_sfx_patch1, sizeof(pat_sfx_patch1), pat_sfx_to1, sizeof(pat_sfx_to1));
        replace_bytes(gct_buf_ptr, gct_len_ptr, pat_sfx_patch2, sizeof(pat_sfx_patch2), pat_sfx_rsbe_to2, sizeof(pat_sfx_rsbe_to2));
    }
}

// File copying and stage processing helpers
static void create_parent_dirs(const char *file_path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", file_path);
    char *dir = dirname(tmp);
    if (dir && strcmp(dir, ".") != 0 && strcmp(dir, "/") != 0)
    {
        char cmd[PATH_MAX + 16];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", dir);
        system(cmd);
    }
}

static bool copy_file_exact(const char *src, const char *dst)
{
    create_parent_dirs(dst);
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

static bool files_identical(const char *p1, const char *p2)
{
    struct stat s1, s2;
    if (stat(p1, &s1) != 0 || stat(p2, &s2) != 0)
        return false;
    if (s1.st_size != s2.st_size)
        return false;
    FILE *f1 = fopen(p1, "rb");
    FILE *f2 = fopen(p2, "rb");
    if (!f1 || !f2)
    {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return false;
    }
    char b1[4096], b2[4096];
    bool same = true;
    while (same)
    {
        size_t n1 = fread(b1, 1, sizeof(b1), f1);
        size_t n2 = fread(b2, 1, sizeof(b2), f2);
        if (n1 != n2) { same = false; break; }
        if (n1 == 0) break;
        if (memcmp(b1, b2, n1) != 0) { same = false; break; }
    }
    fclose(f1);
    fclose(f2);
    return same;
}

static void pad_file(const char *path, long target_size)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size >= target_size)
        return;
    long padding = target_size - st.st_size;
    FILE *f = fopen(path, "ab");
    if (!f) return;
    char zero[4096] = {0};
    while (padding > 0)
    {
        size_t to_write = padding > (long)sizeof(zero) ? sizeof(zero) : (size_t)padding;
        fwrite(zero, 1, to_write, f);
        padding -= to_write;
    }
    fclose(f);
}

// Stage rel duplication
static void duplicate_stage_rels(const char *mod_stage_dir, const char *dest_files_dir)
{
    DIR *d = opendir(mod_stage_dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len > 6 && !strcasecmp(ent->d_name + len - 4, ".pac"))
        {
            // Check for _[A-Z].pac pattern
            if (ent->d_name[len - 6] == '_' && isalpha((int)ent->d_name[len - 5]))
            {
                char alt_code[3] = { ent->d_name[len - 6], ent->d_name[len - 5], '\0' };
                // Extract base prefix (before any _ or LV[0-9])
                char pac_prefix[64];
                snprintf(pac_prefix, sizeof(pac_prefix), "%s", ent->d_name);
                char *underscore = strchr(pac_prefix, '_');
                if (underscore) *underscore = '\0';

                const char *rel_name = lookup_rel_name(pac_prefix);
                if (rel_name)
                {
                    char base_rel[PATH_MAX];
                    char alt_rel[PATH_MAX];
                    snprintf(base_rel, sizeof(base_rel), "%s/module/%s.rel", dest_files_dir, rel_name);
                    snprintf(alt_rel, sizeof(alt_rel), "%s/module/%s%s.rel", dest_files_dir, rel_name, alt_code);

                    struct stat st;
                    if (stat(base_rel, &st) == 0 && stat(alt_rel, &st) != 0)
                    {
                        copy_file_exact(base_rel, alt_rel);
                        if (verbose >= 1)
                            printf("  + Created alternate stage module: %s%s.rel\n", rel_name, alt_code);
                    }
                }
            }
        }
    }
    closedir(d);
}

// Stage padding
static void pad_alternate_stages(const char *dest_stage_dir)
{
    DIR *d = opendir(dest_stage_dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len > 4 && !strcasecmp(ent->d_name + len - 4, ".pac"))
        {
            // If base stage (not _[A-Z].pac)
            if (!(len > 6 && ent->d_name[len - 6] == '_' && isalpha((int)ent->d_name[len - 5])))
            {
                char base_name[128];
                snprintf(base_name, sizeof(base_name), "%s", ent->d_name);
                base_name[len - 4] = '\0';

                char base_path[PATH_MAX];
                snprintf(base_path, sizeof(base_path), "%s/%s", dest_stage_dir, ent->d_name);
                struct stat st_base;
                if (stat(base_path, &st_base) != 0) continue;

                // Find largest alt stage
                long largest_size = st_base.st_size;
                DIR *d2 = opendir(dest_stage_dir);
                if (d2)
                {
                    struct dirent *ent2;
                    while ((ent2 = readdir(d2)) != NULL)
                    {
                        size_t l2 = strlen(ent2->d_name);
                        if (l2 > 6 && !strcasecmp(ent2->d_name + l2 - 4, ".pac"))
                        {
                            if (!strncasecmp(ent2->d_name, base_name, strlen(base_name)) &&
                                ent2->d_name[l2 - 6] == '_' && isalpha((int)ent2->d_name[l2 - 5]))
                            {
                                char alt_path[PATH_MAX];
                                snprintf(alt_path, sizeof(alt_path), "%s/%s", dest_stage_dir, ent2->d_name);
                                struct stat st_alt;
                                if (stat(alt_path, &st_alt) == 0 && st_alt.st_size > largest_size)
                                    largest_size = st_alt.st_size;
                            }
                        }
                    }
                    closedir(d2);
                }

                if (largest_size > st_base.st_size)
                {
                    if (verbose >= 1)
                        printf("  + Padded %s from %lld to %ld bytes\n", ent->d_name, (long long)st_base.st_size, largest_size);
                    pad_file(base_path, largest_size);
                }
            }
        }
    }
    closedir(d);
}

// Clean identical alternate modules
static void cleanup_identical_stage_rels(const char *dest_module_dir)
{
    DIR *d = opendir(dest_module_dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len > 6 && !strcasecmp(ent->d_name + len - 4, ".rel"))
        {
            if (ent->d_name[len - 6] == '_' && isalpha((int)ent->d_name[len - 5]))
            {
                char alt_path[PATH_MAX];
                char base_path[PATH_MAX];
                snprintf(alt_path, sizeof(alt_path), "%s/%s", dest_module_dir, ent->d_name);
                snprintf(base_path, sizeof(base_path), "%s/%s", dest_module_dir, ent->d_name);
                // Remove _[A-Z]
                base_path[strlen(dest_module_dir) + 1 + len - 6] = '\0';
                strcat(base_path, ".rel");

                if (files_identical(alt_path, base_path))
                {
                    unlink(alt_path);
                    if (verbose >= 2)
                        printf("  - Removed redundant alternate module: %s\n", ent->d_name);
                }
            }
        }
    }
    closedir(d);
}

// Copy mod directory recursively
static int copy_mod_dir_recursive(const char *src_base, const char *rel_sub, const char *dest_files, bool remove_en)
{
    char full_src[PATH_MAX];
    if (rel_sub && *rel_sub)
        snprintf(full_src, sizeof(full_src), "%s/%s", src_base, rel_sub);
    else
        snprintf(full_src, sizeof(full_src), "%s", src_base);

    DIR *d = opendir(full_src);
    if (!d) return 0;

    int copied = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;

        char item_rel[PATH_MAX];
        if (rel_sub && *rel_sub)
            snprintf(item_rel, sizeof(item_rel), "%s/%s", rel_sub, ent->d_name);
        else
            snprintf(item_rel, sizeof(item_rel), "%s", ent->d_name);

        char item_full[PATH_MAX];
        snprintf(item_full, sizeof(item_full), "%s/%s", src_base, item_rel);

        struct stat st;
        if (stat(item_full, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                copied += copy_mod_dir_recursive(src_base, item_rel, dest_files, remove_en);
            }
            else if (S_ISREG(st.st_mode))
            {
                char target[PATH_MAX];
                char target_en[PATH_MAX];

                snprintf(target, sizeof(target), "%s/%s", dest_files, item_rel);

                // Check for _en variant in destination
                char *dot = strrchr(item_rel, '.');
                if (dot)
                {
                    char base_without_ext[PATH_MAX];
                    size_t blen = dot - item_rel;
                    snprintf(base_without_ext, sizeof(base_without_ext), "%.*s", (int)blen, item_rel);
                    snprintf(target_en, sizeof(target_en), "%s/%s_en%s", dest_files, base_without_ext, dot);
                }
                else
                {
                    snprintf(target_en, sizeof(target_en), "%s/%s_en", dest_files, item_rel);
                }

                struct stat st_en;
                bool en_exists = (stat(target_en, &st_en) == 0);

                if (remove_en)
                {
                    copy_file_exact(item_full, target);
                    if (en_exists) unlink(target_en);
                }
                else
                {
                    if (en_exists)
                        copy_file_exact(item_full, target_en);
                    else
                        copy_file_exact(item_full, target);
                }
                copied++;
            }
        }
    }
    closedir(d);
    return copied;
}

// DOL patcher for BrawlBuilder
static enumError patch_brawl_dol(const char *dol_path, const u8 *gct_data, size_t gct_len, u32 gct_offset, const char *custom_id)
{
    FILE *f = fopen(dol_path, "rb");
    if (!f)
        return ERROR0(ERR_CANT_OPEN, "Cannot open main.dol for patching: %s\n", dol_path);

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8 *buf = MALLOC(fsize + sizeof(brawl_codehandler_bin) + gct_len + 128);
    if (!buf || fread(buf, 1, fsize, f) != (size_t)fsize)
    {
        fclose(f);
        if (buf) FREE(buf);
        return ERROR0(ERR_READ_FAILED, "Failed to read main.dol: %s\n", dol_path);
    }
    fclose(f);

    dol_mem_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.data = buf;
    ctx.size = fsize;
    ctx.capacity = fsize + sizeof(brawl_codehandler_bin) + gct_len + 128;
    dol_decode_header(&ctx);

    // 1. Inject CodeHandler text section at 0x80001800
    int ch_sec = dol_find_empty_section(&ctx, false);
    if (ch_sec < 0)
    {
        FREE(buf);
        return ERROR0(ERR_WRONG_FILE_TYPE, "No empty DOL section available for CodeHandler\n");
    }

    u32 ch_off = (ctx.size + 31) & ~31;
    u32 ch_size = (sizeof(brawl_codehandler_bin) + 3) & ~3;
    ctx.size = ch_off + ch_size;

    memcpy(ctx.data + ch_off, brawl_codehandler_bin, sizeof(brawl_codehandler_bin));
    ctx.sect_off[ch_sec]  = ch_off;
    ctx.sect_addr[ch_sec] = 0x80001800;
    ctx.sect_size[ch_sec] = ch_size;

    if (verbose >= 1)
        printf("  + Injected Gecko CodeHandler section #%d at 0x80001800 (%zu bytes)\n", ch_sec, sizeof(brawl_codehandler_bin));

    // 2. Inject GCT data section at gct_offset
    int gct_sec = dol_find_empty_section(&ctx, true);
    if (gct_sec < 0)
    {
        FREE(buf);
        return ERROR0(ERR_WRONG_FILE_TYPE, "No empty DOL section available for GCT codes\n");
    }

    u32 gct_file_off = (ctx.size + 31) & ~31;
    u32 gct_padded_size = (gct_len + 31) & ~31;
    ctx.size = gct_file_off + gct_padded_size;

    memset(ctx.data + gct_file_off, 0, gct_padded_size);
    memcpy(ctx.data + gct_file_off, gct_data, gct_len);
    ctx.sect_off[gct_sec]  = gct_file_off;
    ctx.sect_addr[gct_sec] = gct_offset;
    ctx.sect_size[gct_sec] = gct_padded_size;

    if (verbose >= 1)
        printf("  + Injected GCT section #%d at 0x%08X (%zu bytes)\n", gct_sec, gct_offset, gct_len);

    // 3. Apply PatchCommon memory patches
    // 0x80200984 -> 0x4BE00F24 (branch to 0x800018A8)
    s64 off_hook = dol_addr_to_file_offset(&ctx, 0x80200984);
    if (off_hook >= 0) write_be32(ctx.data + off_hook, 0x4BE00F24);

    // 0x80002778 -> 0x80200B84 (return hook in codehandler)
    s64 off_ret = dol_addr_to_file_offset(&ctx, 0x80002778);
    if (off_ret >= 0) write_be32(ctx.data + off_ret, 0x80200B84);

    // GCT address pointers inside codehandler
    s64 off_gct1_hi = dol_addr_to_file_offset(&ctx, 0x80001CDE);
    if (off_gct1_hi >= 0) write_be16(ctx.data + off_gct1_hi, (gct_offset >> 16) & 0xFFFF);

    s64 off_gct1_lo = dol_addr_to_file_offset(&ctx, 0x80001CE2);
    if (off_gct1_lo >= 0) write_be16(ctx.data + off_gct1_lo, gct_offset & 0xFFFF);

    s64 off_gct2_hi = dol_addr_to_file_offset(&ctx, 0x80001F5A);
    if (off_gct2_hi >= 0) write_be16(ctx.data + off_gct2_hi, (gct_offset >> 16) & 0xFFFF);

    s64 off_gct2_lo = dol_addr_to_file_offset(&ctx, 0x80001F5E);
    if (off_gct2_lo >= 0) write_be16(ctx.data + off_gct2_lo, gct_offset & 0xFFFF);

    // Bypass checks
    s64 off_b1 = dol_addr_to_file_offset(&ctx, 0x800042B8);
    if (off_b1 >= 0) write_be32(ctx.data + off_b1, 0x60000000);

    s64 off_b2 = dol_addr_to_file_offset(&ctx, 0x803E9930);
    if (off_b2 >= 0) write_be32(ctx.data + off_b2, 0x60000000);

    // 4. Custom Game ID patches in DOL (avoids "Please insert disc" error)
    if (custom_id && strlen(custom_id) >= 6)
    {
        s64 off_id_first = dol_addr_to_file_offset(&ctx, 0x805A14B0);
        if (off_id_first >= 0)
            memcpy(ctx.data + off_id_first, custom_id, 4);

        s64 off_id_last = dol_addr_to_file_offset(&ctx, 0x805A14B8);
        if (off_id_last >= 0)
            memcpy(ctx.data + off_id_last, custom_id + 4, 2);

        if (verbose >= 1)
            printf("  + Patched DOL internal disc check to ID '%.6s'\n", custom_id);
    }

    dol_encode_header(&ctx);

    f = fopen(dol_path, "wb");
    if (!f || fwrite(ctx.data, 1, ctx.size, f) != ctx.size)
    {
        if (f) fclose(f);
        FREE(buf);
        return ERROR0(ERR_WRITE_FAILED, "Failed to write patched main.dol: %s\n", dol_path);
    }
    fclose(f);
    FREE(buf);
    return ERR_OK;
}

// Interactive configuration prompt
static void prompt_brawl_interactive(BrawlOptions_t *opt, char *custom_id_buf, size_t id_buf_size, char *custom_title_buf, size_t title_buf_size)
{
    printf("\n===================================================\n");
    printf("        BrawlBuilder Interactive Configuration       \n");
    printf("===================================================\n");

    // 1. Custom 6-character ID
    char input_line[256];
    printf("\nEnter 6-character Game ID [%s]: ", opt->custom_id ? opt->custom_id : "RSBE02");
    fflush(stdout);
    if (fgets(input_line, sizeof(input_line), stdin))
    {
        char *nl = strchr(input_line, '\n');
        if (nl) *nl = '\0';
        while (*input_line && isspace((int)*input_line)) memmove(input_line, input_line + 1, strlen(input_line));
        char *end = input_line + strlen(input_line) - 1;
        while (end >= input_line && isspace((int)*end)) { *end = '\0'; end--; }

        if (*input_line)
        {
            snprintf(custom_id_buf, id_buf_size, "%.6s", input_line);
            opt->custom_id = custom_id_buf;
        }
        else if (!opt->custom_id)
        {
            snprintf(custom_id_buf, id_buf_size, "RSBE02");
            opt->custom_id = custom_id_buf;
        }
    }

    // 2. Custom Disc Title
    printf("Enter Game Disc Title [%s]: ", opt->custom_name ? opt->custom_name : "Project M");
    fflush(stdout);
    if (fgets(input_line, sizeof(input_line), stdin))
    {
        char *nl = strchr(input_line, '\n');
        if (nl) *nl = '\0';
        while (*input_line && isspace((int)*input_line)) memmove(input_line, input_line + 1, strlen(input_line));
        char *end = input_line + strlen(input_line) - 1;
        while (end >= input_line && isspace((int)*end)) { *end = '\0'; end--; }

        if (*input_line)
        {
            snprintf(custom_title_buf, title_buf_size, "%s", input_line);
            opt->custom_name = custom_title_buf;
        }
        else if (!opt->custom_name)
        {
            snprintf(custom_title_buf, title_buf_size, "Project M");
            opt->custom_name = custom_title_buf;
        }
    }

    // 3. Remove Subspace Emissary
    printf("Remove Subspace Emissary to save ~5 GB? (Y/n) [Y]: ");
    fflush(stdout);
    if (fgets(input_line, sizeof(input_line), stdin))
    {
        char *nl = strchr(input_line, '\n');
        if (nl) *nl = '\0';
        if (!strcasecmp(input_line, "n") || !strcasecmp(input_line, "no"))
            opt->remove_sse = false;
        else
            opt->remove_sse = true;
    }

    printf("\nSelected Configuration:\n");
    printf("  Game ID:     %s\n", opt->custom_id);
    printf("  Game Title:  %s\n", opt->custom_name);
    printf("  Remove SSE:  %s\n\n", opt->remove_sse ? "Yes (shrunk ~2.5 GB)" : "No (full ~7.5 GB)");
}

// Main BrawlCommand implementation
enumError BrawlCommand(BrawlOptions_t *opt)
{
    if (!opt || !opt->source_image)
        return ERROR0(ERR_SYNTAX, "wit BRAWLBUILDER requires a source Brawl disc image / extracted FST.\n"
                                  "Usage: wit BRAWLBUILDER [options] source [mod_folder] [dest]\n");

    // Locate mod files directory
    char detected_mod_root[PATH_MAX] = {0};
    if (opt->mod_folder)
    {
        // Check if pf/ exists within mod_folder
        char pf_try[PATH_MAX];
        snprintf(pf_try, sizeof(pf_try), "%s/pf", opt->mod_folder);
        struct stat st_pf;
        if (stat(pf_try, &st_pf) == 0 && S_ISDIR(st_pf.st_mode))
            snprintf(detected_mod_root, sizeof(detected_mod_root), "%s", pf_try);
        else
        {
            snprintf(pf_try, sizeof(pf_try), "%s/projectm/pf", opt->mod_folder);
            if (stat(pf_try, &st_pf) == 0 && S_ISDIR(st_pf.st_mode))
                snprintf(detected_mod_root, sizeof(detected_mod_root), "%s", pf_try);
            else
                snprintf(detected_mod_root, sizeof(detected_mod_root), "%s", opt->mod_folder);
        }
    }

    // Locate GCT cheat file if not specified
    char detected_gct[PATH_MAX] = {0};
    if (opt->gct_file)
    {
        snprintf(detected_gct, sizeof(detected_gct), "%s", opt->gct_file);
    }
    else if (opt->mod_folder)
    {
        const char *candidates[] = {
            "%s/codes/RSBE01.gct",
            "%s/RSBE01.gct",
            "%s/projectm/codes/RSBE01.gct",
            "%s/projectm/RSBE01.gct",
            "%s/codes/RSBE.gct",
            "%s/RSBE.gct",
            NULL
        };
        for (int i = 0; candidates[i]; i++)
        {
            char gtry[PATH_MAX];
            snprintf(gtry, sizeof(gtry), candidates[i], opt->mod_folder);
            struct stat st;
            if (stat(gtry, &st) == 0 && S_ISREG(st.st_mode))
            {
                snprintf(detected_gct, sizeof(detected_gct), "%s", gtry);
                break;
            }
        }
    }

    if (opt->gct_offset == 0)
        opt->gct_offset = 0x80570000;

    // Interactive prompt
    char custom_id_buf[16] = {0};
    char custom_title_buf[64] = {0};
    if (opt->interactive)
        prompt_brawl_interactive(opt, custom_id_buf, sizeof(custom_id_buf), custom_title_buf, sizeof(custom_title_buf));

    if (verbose >= 0)
    {
        printf("\nBrawlBuilder Mod ISO Engine\n");
        printf("  Source Image: %s\n", opt->source_image);
        if (*detected_mod_root)
            printf("  Mod Files:    %s\n", detected_mod_root);
        if (*detected_gct)
            printf("  GCT Codes:    %s\n", detected_gct);
        if (opt->custom_id)
            printf("  Target ID:    %s\n", opt->custom_id);
        if (opt->custom_name)
            printf("  Target Title: %s\n", opt->custom_name);
        printf("  Remove SSE:   %s\n", opt->remove_sse ? "yes" : "no");
        if (opt->dest_path)
            printf("  Output Dest:  %s\n", opt->dest_path);
        printf("\n");
    }

    if (opt->test_mode)
    {
        printf("TEST MODE: BrawlBuilder configuration verified successfully.\n");
        return ERR_OK;
    }

    // Check if source is already an extracted FST directory or disc image
    struct stat st_src;
    if (stat(opt->source_image, &st_src) != 0)
        return ERROR0(ERR_CANT_OPEN, "Source file not found: %s\n", opt->source_image);

    bool source_is_dir = S_ISDIR(st_src.st_mode);
    char work_dir[PATH_MAX];
    bool temp_workdir = false;

    if (source_is_dir)
    {
        snprintf(work_dir, sizeof(work_dir), "%s", opt->source_image);
    }
    else
    {
        temp_workdir = true;
        ccp tmpenv = getenv("TMPDIR");
        if (!tmpenv || !*tmpenv)
            tmpenv = "/tmp";
        snprintf(work_dir, sizeof(work_dir), "%s/wit-brawl-XXXXXX", tmpenv);
        if (!mkdtemp(work_dir))
            return ERROR0(ERR_CANT_CREATE, "Failed to create temporary directory: %s\n", work_dir);

        if (verbose >= 0)
            printf("Extracting Brawl partition DATA to: %s\n", work_dir);

        char extract_cmd[PATH_MAX * 2 + 128];
        snprintf(extract_cmd, sizeof(extract_cmd),
                 "./bin/wit EXTRACT \"%s\" --psel=DATA --dest \"%s\" %s",
                 opt->source_image, work_dir, verbose >= 1 ? "-vv" : "-q");

        int ret = system(extract_cmd);
        if (ret != 0)
        {
            if (!opt->keep_temp) { char r[PATH_MAX + 16]; snprintf(r, sizeof(r), "rm -rf \"%s\"", work_dir); system(r); }
            return ERROR0(ERR_CANT_CREATE, "Failed to extract Brawl source disc image\n");
        }
    }

    // Locate DATA directory within work_dir
    char data_dir[PATH_MAX];
    char files_dir[PATH_MAX];
    char sys_dir[PATH_MAX];

    snprintf(data_dir, sizeof(data_dir), "%s/DATA", work_dir);
    struct stat st_data;
    if (stat(data_dir, &st_data) == 0 && S_ISDIR(st_data.st_mode))
    {
        snprintf(files_dir, sizeof(files_dir), "%s/DATA/files", work_dir);
        snprintf(sys_dir, sizeof(sys_dir), "%s/DATA/sys", work_dir);
    }
    else
    {
        snprintf(files_dir, sizeof(files_dir), "%s/files", work_dir);
        snprintf(sys_dir, sizeof(sys_dir), "%s/sys", work_dir);
    }

    // 1. Remove Subspace Emissary files if requested
    if (opt->remove_sse)
    {
        if (verbose >= 0)
            printf("Removing Subspace Emissary files to reduce image size...\n");
        int removed_count = 0;
        for (int i = 0; i < BRAWL_SUBPACE_FILE_COUNT; i++)
        {
            char sse_file[PATH_MAX];
            snprintf(sse_file, sizeof(sse_file), "%s/%s", files_dir, brawl_subspace_files[i]);
            if (unlink(sse_file) == 0)
                removed_count++;
        }
        if (verbose >= 1)
            printf("  - Removed %d Subspace Emissary assets\n", removed_count);
    }

    // 2. Read and patch GCT file
    u8 *gct_buf = NULL;
    size_t gct_len = 0;
    bool remove_en = false;

    if (*detected_gct)
    {
        FILE *fg = fopen(detected_gct, "rb");
        if (fg)
        {
            fseek(fg, 0, SEEK_END);
            gct_len = ftell(fg);
            fseek(fg, 0, SEEK_SET);
            gct_buf = MALLOC(gct_len + 512);
            if (gct_buf && fread(gct_buf, 1, gct_len, fg) == gct_len)
            {
                if (!opt->no_gct_patch)
                {
                    if (verbose >= 0)
                        printf("Applying GCT compatibility patches...\n");
                    patch_gct_for_brawl(&gct_buf, &gct_len, &remove_en);
                }
            }
            fclose(fg);
        }
    }

    // 3. Stage module preparation (duplicate base .rel for alternate stages)
    if (*detected_mod_root)
    {
        char mod_stage_dir[PATH_MAX];
        snprintf(mod_stage_dir, sizeof(mod_stage_dir), "%s/stage/melee", detected_mod_root);
        struct stat st_ms;
        if (stat(mod_stage_dir, &st_ms) == 0 && S_ISDIR(st_ms.st_mode))
        {
            if (verbose >= 0)
                printf("Preparing alternate stage modules...\n");
            duplicate_stage_rels(mod_stage_dir, files_dir);
        }

        // 4. Copy mod files
        if (verbose >= 0)
            printf("Copying mod files from %s...\n", detected_mod_root);
        int copied = copy_mod_dir_recursive(detected_mod_root, "", files_dir, remove_en);
        if (verbose >= 0)
            printf("  + Copied %d mod files\n", copied);

        // 5. Pad alternate stages
        if (!opt->no_alt_pad)
        {
            char dest_stage_dir[PATH_MAX];
            snprintf(dest_stage_dir, sizeof(dest_stage_dir), "%s/stage/melee", files_dir);
            struct stat st_ds;
            if (stat(dest_stage_dir, &st_ds) == 0 && S_ISDIR(st_ds.st_mode))
            {
                if (verbose >= 0)
                    printf("Padding base stages to match alternate stages...\n");
                pad_alternate_stages(dest_stage_dir);
            }
        }

        // 6. Clean up identical stage modules
        char dest_mod_dir[PATH_MAX];
        snprintf(dest_mod_dir, sizeof(dest_mod_dir), "%s/module", files_dir);
        cleanup_identical_stage_rels(dest_mod_dir);
    }

    // 7. Custom banner
    if (opt->banner_file)
    {
        char bnr_dest[PATH_MAX];
        snprintf(bnr_dest, sizeof(bnr_dest), "%s/opening.bnr", files_dir);
        copy_file_exact(opt->banner_file, bnr_dest);
        if (verbose >= 1)
            printf("  + Installed custom opening banner: %s\n", opt->banner_file);
    }

    // 8. Patch main.dol
    char main_dol_path[PATH_MAX];
    snprintf(main_dol_path, sizeof(main_dol_path), "%s/main.dol", sys_dir);
    struct stat st_md;
    if (stat(main_dol_path, &st_md) == 0 && gct_buf && gct_len > 0)
    {
        if (verbose >= 0)
            printf("Injecting CodeHandler and GCT cheats into main.dol...\n");
        enumError err = patch_brawl_dol(main_dol_path, gct_buf, gct_len, opt->gct_offset, opt->custom_id);
        if (err != ERR_OK)
        {
            FREE(gct_buf);
            if (temp_workdir && !opt->keep_temp) { char r[PATH_MAX + 16]; snprintf(r, sizeof(r), "rm -rf \"%s\"", work_dir); system(r); }
            return err;
        }
    }
    if (gct_buf) FREE(gct_buf);

    // 9. Pack final disc image if destination specified
    if (opt->dest_path)
    {
        if (verbose >= 0)
            printf("Packing disc image: %s...\n", opt->dest_path);

        char pack_cmd[PATH_MAX * 2 + 256];
        char extra_args[256] = {0};

        if (opt->custom_id)
            snprintf(extra_args + strlen(extra_args), sizeof(extra_args) - strlen(extra_args), " --id=\"%.6s\"", opt->custom_id);
        if (opt->custom_name)
            snprintf(extra_args + strlen(extra_args), sizeof(extra_args) - strlen(extra_args), " --name=\"%s\"", opt->custom_name);
        if (opt->overwrite)
            strcat(extra_args, " --overwrite");

        snprintf(pack_cmd, sizeof(pack_cmd),
                 "./bin/wit COPY \"%s\" \"%s\" %s %s",
                 work_dir, opt->dest_path, extra_args, verbose >= 1 ? "-vv" : "-q");

        int ret = system(pack_cmd);
        if (ret != 0)
        {
            if (temp_workdir && !opt->keep_temp) { char r[PATH_MAX + 16]; snprintf(r, sizeof(r), "rm -rf \"%s\"", work_dir); system(r); }
            return ERROR0(ERR_CANT_CREATE, "Failed to pack final disc image: %s\n", opt->dest_path);
        }

        if (verbose >= 0)
            printf("\nSuccessfully created: %s\n\n", opt->dest_path);
    }
    else
    {
        if (verbose >= 0)
            printf("\nBrawl FST filesystem prepared at: %s\n\n", work_dir);
    }

    if (temp_workdir && !opt->keep_temp && opt->dest_path)
    {
        char r[PATH_MAX + 16];
        snprintf(r, sizeof(r), "rm -rf \"%s\"", work_dir);
        system(r);
    }

    return ERR_OK;
}
