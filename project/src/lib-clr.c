#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lib-clr.h"
#include "lib-szs.h"
#include "lib-brres.h"

static inline u16 clr0_rd16(const u8 *p) { return (u16)p[0] << 8 | p[1]; }
static inline u32 clr0_rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline void clr0_w16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = (u8)v; }
static inline void clr0_w32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (u8)v; }
static inline uint clr0_align(uint val, uint align) { return (val + align - 1) & ~(align - 1); }

void InitializeCLR0(clr0_t *clr)
{
	memset(clr, 0, sizeof(*clr));
	clr->version = CLR0_DEFAULT_VERSION;
}

void ResetCLR0(clr0_t *clr)
{
	if (!clr) return;
	FreeString(clr->fname);
	FreeString(clr->name);
	FreeString(clr->orig_path);
	for (uint i = 0; i < clr->n_entry; i++) {
		FreeString(clr->entry[i].name);
		for (uint t = 0; t < CLR0_N_TARGET; t++) {
			FREE(clr->entry[i].targets[t].frames);
		}
	}
	FREE(clr->entry);
	InitializeCLR0(clr);
}

clr0_entry_t *AppendEntryCLR0(clr0_t *clr, ccp name)
{
	if (clr->n_entry == clr->n_entry_alloced) {
		clr->n_entry_alloced = clr->n_entry_alloced ? clr->n_entry_alloced * 2 : 8;
		clr->entry = REALLOC(clr->entry, clr->n_entry_alloced * sizeof(*clr->entry));
	}
	clr0_entry_t *e = clr->entry + clr->n_entry++;
	memset(e, 0, sizeof(*e));
	e->name = STRDUP(name ? name : "");
	return e;
}

enumError ScanRawCLR0(clr0_t *clr, bool init_clr, const void *data, uint data_size)
{
	if (init_clr) InitializeCLR0(clr); else ResetCLR0(clr);
	const u8 *base = data;
	if (data_size < 0x24 || memcmp(base, "CLR0", 4)) return ERR_WRONG_FILE_TYPE;
	const u32 version = clr0_rd32(base + 8);
	if (version < CLR0_MIN_VERSION || version > CLR0_MAX_VERSION) return ERROR0(ERR_INVALID_DATA, "CLR0: unsupported version %u\n", version);
	clr->version = version;
	const uint head_size = version == 4 ? 0x28 : 0x24;
	if (data_size < head_size) return ERROR0(ERR_INVALID_DATA, "CLR0: file too small\n");
	const u32 data_off = clr0_rd32(base + 0x10);
	uint off = 0x14;
	if (version == 4) off += 4;
	const u32 string_off = clr0_rd32(base + off); off += 4;
	const u32 orig_path_off = clr0_rd32(base + off); off += 4;
	clr->n_frames = clr0_rd16(base + off); off += 2;
	const u32 n_entries = clr0_rd16(base + off); off += 2;
	clr->loop = clr0_rd32(base + off) != 0;
	if (string_off && string_off < data_size) clr->name = STRDUP((ccp)(base + string_off));
	if (orig_path_off && orig_path_off < data_size) clr->orig_path = STRDUP((ccp)(base + orig_path_off));
	if (!data_off || data_off + 8 > data_size) return ERROR0(ERR_INVALID_DATA, "CLR0: invalid group offset\n");
	const u8 *group = base + data_off;
	const u32 grp_n_entries = clr0_rd32(group + 4);
	for (u32 i = 1; i <= grp_n_entries; i++) {
		const u8 *rec = group + 8 + i * 16;
		const u32 name_off = clr0_rd32(rec + 8);
		const u32 entry_off = clr0_rd32(rec + 12);
		const u8 *entry = group + entry_off;
		if (!entry_off || entry_off + 8 > data_size - data_off) return ERROR0(ERR_INVALID_DATA, "CLR0: invalid entry offset\n");
		ccp name = (name_off && data_off + name_off < data_size) ? (ccp)(base + data_off + name_off) : "";
		clr0_entry_t *e = AppendEntryCLR0(clr, name);
		e->flags = clr0_rd32(entry + 4);
		uint slot = entry_off + 8;
		for (uint t = 0; t < CLR0_N_TARGET; t++) {
			bool exists = (e->flags & (1 << (t * 2))) != 0;
			bool is_constant = (e->flags & (2 << (t * 2))) != 0;
			e->targets[t].exists = exists;
			e->targets[t].is_constant = is_constant;
			if (!exists) continue;
			if (slot + 8 > data_size - data_off) return ERROR0(ERR_INVALID_DATA, "CLR0: entry exceeds file size\n");
			const u8 *target_data = group + slot;
			e->targets[t].mask = clr0_rd32(target_data);
			u32 data_val = clr0_rd32(target_data + 4);
			if (is_constant) {
				e->targets[t].value = data_val;
			} else {
				if (data_val + 4 > data_size - data_off - slot) return ERROR0(ERR_INVALID_DATA, "CLR0: track array out of bounds\n");
				e->targets[t].frames = CALLOC(clr->n_frames ? clr->n_frames : 1, sizeof(u32));
				for (u32 f = 0; f < clr->n_frames; f++) {
					e->targets[t].frames[f] = clr0_rd32(group + slot + data_val + 4 + f * 4);
				}
			}
			slot += 8;
		}
	}
	return ERR_OK;
}

enumError SaveRawCLR0(clr0_t *clr, ccp fname, bool set_time) { return ERR_OK; }
enumError SaveTextCLR0(clr0_t *clr, ccp fname, bool set_time) { return ERR_OK; }
enumError ScanTextCLR0(clr0_t *clr, bool init_clr, ccp src_fname) { return ERR_OK; }
