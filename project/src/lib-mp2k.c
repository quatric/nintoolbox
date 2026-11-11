// GBA MusicPlayer2000 ("Sappy" / m4a) songs -> standard MIDI.
//
// Port of the Nintendo logic of VGMTrans (MP2kScanner.cpp + MP2kSeq.cpp, itself based on
// GBAMusRiper by Bregalad and the Sappy documentation): the sound engine is found in a
// GBA ROM by its "SelectSong" code pattern, the song table it points to is walked, and
// every song's track byte-code is converted to one MIDI file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib-std.h"
#include "lib-mp2k.h"

static u32 gw (const u8 *d, size_t n, size_t o) // little endian word, 0 when out of range
{
	return o + 4 <= n ? (u32)d[o] | (u32)d[o + 1] << 8 | (u32)d[o + 2] << 16 | (u32)d[o + 3] << 24 : 0;
}

static u32 gs (const u8 *d, size_t n, size_t o) { return o + 2 <= n ? (u32)d[o] | (u32)d[o + 1] << 8 : 0; }
static u32 gb (const u8 *d, size_t n, size_t o) { return o < n ? d[o] : 0; }

static bool is_rom_addr (u32 a) { return ((a >> 24) & 0xfe) == 8; }

static bool pointer_valid (const u8 *d, size_t n, size_t o)
{
	if (o + 12 > n)
		return false;
	const u32 w = gw (d, n, o);
	const uint poly = (w & 0x000f00) >> 8, vol = (w & 0x00f000) >> 12, rate = (w & 0x0f0000) >> 16;
	const int dac = 17 - (int)((w & 0xf00000) >> 20);
	const u32 tbl = (gw (d, n, o + 8) & 0x3ffffff) + 12 * gw (d, n, o + 4);
	return vol != 0 && poly <= 12 && dac >= 6 && dac <= 9 && rate >= 1 && rate <= 12 && tbl < n && gw (d, n, o + 4) < 256
		&& (w & 0xff000000) == 0;
}

// Sappy sound engine detector (c) 2014 Bregalad, loveemu; returns the engine struct offset or -1
static long mp2k_detect (const u8 *d, size_t n)
{
	static const u8 sel[0x1e] = { 0x00, 0xB5, 0x00, 0x04, 0x07, 0x4A, 0x08, 0x49, 0x40, 0x0B, 0x40, 0x18, 0x83, 0x88, 0x59, 0x00,
		0xC9, 0x18, 0x89, 0x00, 0x89, 0x18, 0x0A, 0x68, 0x01, 0x68, 0x10, 0x1C, 0x00, 0xF0 };
	long from = 0, hit = -1;
	while (from >= 0)
	{
		hit = -1;
		for (size_t o = (size_t)from; o + sizeof (sel) <= n; o++)
			if (!memcmp (d + o, sel, sizeof (sel)))
			{
				hit = (long)o;
				break;
			}
		if (hit < 0)
			return -1;
		const u32 tbl_addr = gw (d, n, (size_t)hit + 40);
		bool ok = is_rom_addr (tbl_addr);
		if (ok)
		{
			const size_t to = tbl_addr & 0x1ffffff;
			ok = to + 3 < n;
			int valid = 0;
			for (int i = 0; ok && valid < 1; i++)
			{
				const size_t eo = to + (size_t)i * 8;
				if (eo + 3 >= n)
					break;
				const u32 a = gw (d, n, eo);
				if (!a)
					continue;
				if (!is_rom_addr (a) || (a & 0x1ffffff) + 3 >= n)
					break;
				valid++;
			}
			ok = ok && valid >= 1;
		}
		if (ok)
			break;
		from = hit + 1;
	}
	if (hit < 0)
		return -1;
	long main_off = -1;
	for (long t = hit; t > 0 && t > hit - 0x20; t--)
		if (n > (size_t)t + 1 && d[t] == 0x00 && d[t + 1] == 0xB5)
			main_off = t;
	if (main_off < 0)
		return -1;
	const bool m16 = main_off >= 16 && pointer_valid (d, n, (size_t)main_off - 16);
	const bool m32 = main_off >= 32 && pointer_valid (d, n, (size_t)main_off - 32);
	if (!m16 && !m32)
		return -1;
	return main_off - (m16 ? 16 : 32);
}

///////////////////////////////////////////////////////////////////////////////
// MIDI events

typedef struct ev_t
{
	u32 tick;
	u8 st, d1, d2;
	u32 seq; // stable order
	u32 tempo; // microseconds per quarter note for st == 0xff
} ev_t;

typedef struct trk_t
{
	ev_t *e;
	size_t n, cap;
	u32 seq;
} trk_t;

static void ev_add (trk_t *t, u32 tick, u8 st, u8 d1, u8 d2, u32 tempo)
{
	if (t->n == t->cap)
	{
		t->cap = t->cap ? t->cap * 2 : 256;
		t->e = REALLOC (t->e, t->cap * sizeof (ev_t));
		if (!t->e)
		{
			t->n = t->cap = 0;
			return;
		}
	}
	t->e[t->n++] = (ev_t){ tick, st, d1, d2, t->seq++, tempo };
}

static int ev_cmp (const void *a, const void *b)
{
	const ev_t *x = a, *y = b;
	if (x->tick != y->tick)
		return x->tick < y->tick ? -1 : 1;
	// note-offs before note-ons at the same tick
	const int xo = (x->st & 0xf0) == 0x80, yo = (y->st & 0xf0) == 0x80;
	if (xo != yo)
		return xo ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

typedef struct buf_t
{
	u8 *d;
	size_t n, cap;
} buf_t;

static void bput (buf_t *b, const void *s, size_t len)
{
	if (b->n + len > b->cap)
	{
		size_t c = b->cap ? b->cap : 1024;
		while (c < b->n + len)
			c *= 2;
		u8 *p = REALLOC (b->d, c);
		if (!p)
			return;
		b->d = p;
		b->cap = c;
	}
	memcpy (b->d + b->n, s, len);
	b->n += len;
}

static void bvlq (buf_t *b, u32 v)
{
	u8 t[5];
	int n = 0;
	t[n++] = v & 0x7f;
	while (v >>= 7)
		t[n++] = (v & 0x7f) | 0x80;
	while (n--)
		bput (b, &t[n], 1);
}

///////////////////////////////////////////////////////////////////////////////
// track byte-code

static const u8 length_table[0x31] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1C, 0x1E, 0x20, 0x24, 0x28, 0x2A, 0x2C, 0x30, 0x34, 0x36, 0x38, 0x3C, 0x40, 0x42, 0x44,
	0x48, 0x4C, 0x4E, 0x50, 0x54, 0x58, 0x5A, 0x5C, 0x60 };

enum { ST_NOTE, ST_TIE, ST_TIE_END, ST_VOL, ST_PAN, ST_BEND, ST_MOD };

static u8 clampk (int k) { return k < 0 ? 0 : k > 127 ? 127 : (u8)k; }

static void run_track (const u8 *d, size_t n, size_t pos, uint ch, trk_t *t, trk_t *conductor)
{
	u8 *used = CALLOC (n ? n : 1, 1);
	size_t stack[32];
	uint sp = 0;
	u32 time = 0, dur = 0;
	uint state = ST_NOTE;
	u8 vel = 0, prev_key = 0, prev_vel = 0;
	int transpose = 0;
	if (!used)
		return;
	const u8 c = ch & 15;
	for (uint guard = 0; guard < 2000000 && pos < n; guard++)
	{
		const size_t begin = pos;
		const u8 st = d[pos++];
		used[begin] = 1;
		if (st <= 0x7f)
		{
			switch (state)
			{
			case ST_NOTE:
			case ST_TIE:
				if (gb (d, n, pos) <= 0x7f)
				{
					vel = d[pos++];
					if (gb (d, n, pos) <= 0x7f)
						pos++;
				}
				prev_key = st;
				prev_vel = vel;
				if (state == ST_NOTE)
				{
					ev_add (t, time, 0x90 | c, clampk (st + transpose), vel, 0);
					ev_add (t, time + dur, 0x80 | c, clampk (st + transpose), 0, 0);
				}
				else
					ev_add (t, time, 0x90 | c, clampk (st + transpose), vel, 0);
				break;
			case ST_TIE_END:
				ev_add (t, time, 0x80 | c, clampk (st + transpose), 0, 0);
				break;
			case ST_VOL: ev_add (t, time, 0xb0 | c, 7, st, 0); break;
			case ST_PAN: ev_add (t, time, 0xb0 | c, 10, st, 0); break;
			case ST_BEND:
			{
				int v = 0x2000 + ((int)st - 0x40) * 128;
				v = v < 0 ? 0 : v > 0x3fff ? 0x3fff : v;
				ev_add (t, time, 0xe0 | c, v & 0x7f, v >> 7, 0);
				break;
			}
			case ST_MOD: ev_add (t, time, 0xb0 | c, 1, st, 0); break;
			}
		}
		else if (st <= 0xb0)
			time += length_table[st - 0x80] * 2;
		else if (st >= 0xd0)
		{
			state = ST_NOTE;
			dur = length_table[st - 0xcf] * 2;
			if (gb (d, n, pos) > 0x7f)
			{
				ev_add (t, time, 0x90 | c, clampk (prev_key + transpose), prev_vel, 0);
				ev_add (t, time + dur, 0x80 | c, clampk (prev_key + transpose), 0, 0);
			}
		}
		else
		{
			switch (st)
			{
			case 0xb1:
				goto done;
			case 0xb2: // goto
			{
				const u32 a = gw (d, n, pos) - 0x8000000u;
				pos += 4;
				if (used[a < n ? a : 0] && sp == 0)
					goto done; // loops forever: stop at the first repetition
				pos = a;
				break;
			}
			case 0xb3: // pattern play
			{
				const u32 a = gw (d, n, pos) - 0x8000000u;
				pos += 4;
				if (sp < 32)
					stack[sp++] = pos;
				pos = a;
				break;
			}
			case 0xb4: // pattern end
				if (sp)
					pos = stack[--sp];
				break;
			case 0xba: pos++; break;
			case 0xbb:
				if (conductor)
				{
					const u32 bpm = gb (d, n, pos) * 2;
					if (bpm)
						ev_add (conductor, time, 0xff, 0x51, 0, 60000000u / bpm);
				}
				pos++;
				break;
			case 0xbc: transpose = (int8_t)gb (d, n, pos++); break;
			case 0xbd: ev_add (t, time, 0xc0 | c, gb (d, n, pos++) & 0x7f, 0, 0); break;
			case 0xbe: state = ST_VOL; break;
			case 0xbf: state = ST_PAN; break;
			case 0xc0: state = ST_BEND; break;
			case 0xc1: case 0xc2: case 0xc3: case 0xc5: case 0xc8: pos++; break;
			case 0xc4: state = ST_MOD; break;
			case 0xcd:
			{
				const u32 sub = gb (d, n, pos++), par = gb (d, n, pos);
				if ((sub == 8 || sub == 9) && par <= 127)
					pos++;
				else
					while (gb (d, n, pos) <= 127 && pos < n)
						pos++;
				break;
			}
			case 0xce:
				state = ST_TIE_END;
				if (gb (d, n, pos) > 0x7f)
					ev_add (t, time, 0x80 | c, clampk (prev_key + transpose), 0, 0);
				break;
			case 0xcf:
				state = ST_TIE;
				if (gb (d, n, pos) > 0x7f)
					ev_add (t, time, 0x90 | c, clampk (prev_key + transpose), prev_vel, 0);
				break;
			default: break;
			}
		}
	}
done:
	FREE (used);
}

static bool song_to_midi (const u8 *d, size_t n, size_t song, buf_t *out)
{
	const uint ntr = gs (d, n, song);
	if (!ntr || ntr > 24 || song + 8 + (size_t)ntr * 4 > n)
		return false;
	trk_t tr[24] = { { 0 } }, cond = { 0 };
	for (uint i = 0; i < ntr; i++)
	{
		const u32 a = gw (d, n, song + 8 + 4 * (size_t)i) - 0x8000000u;
		if (a < n)
			run_track (d, n, a, i, tr + i, i == 0 ? &cond : 0);
	}
	const u16 ppq = 0x30;
	const uint nt = ntr + 1;
	u8 hdr[14] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, nt >> 8, nt, ppq >> 8, ppq & 255 };
	bput (out, hdr, 14);
	for (uint k = 0; k < nt; k++)
	{
		trk_t *t = k ? tr + k - 1 : &cond;
		if (t->n)
			qsort (t->e, t->n, sizeof (ev_t), ev_cmp);
		buf_t body = { 0 };
		u32 last = 0;
		if (k == 0)
		{
			static const u8 ts[] = { 0xff, 0x58, 4, 4, 2, 24, 8 };
			bvlq (&body, 0);
			bput (&body, ts, sizeof (ts));
			if (!t->n || t->e[0].tick)
			{
				static const u8 t0[] = { 0xff, 0x51, 3, 0x07, 0xa1, 0x20 }; // 120 bpm
				bvlq (&body, 0);
				bput (&body, t0, sizeof (t0));
			}
		}
		for (size_t i = 0; i < t->n; i++)
		{
			const ev_t *e = t->e + i;
			bvlq (&body, e->tick - last);
			last = e->tick;
			if (e->st == 0xff)
			{
				const u8 m[] = { 0xff, 0x51, 3, e->tempo >> 16, e->tempo >> 8, e->tempo };
				bput (&body, m, sizeof (m));
			}
			else
			{
				const u8 m[3] = { e->st, e->d1, e->d2 };
				bput (&body, m, (e->st & 0xf0) == 0xc0 ? 2 : 3);
			}
		}
		bvlq (&body, 0);
		static const u8 eot[] = { 0xff, 0x2f, 0 };
		bput (&body, eot, 3);
		u8 th[8] = { 'M', 'T', 'r', 'k', body.n >> 24, body.n >> 16, body.n >> 8, body.n };
		bput (out, th, 8);
		bput (out, body.d, body.n);
		FREE (body.d);
	}
	for (uint i = 0; i < ntr; i++)
		FREE (tr[i].e);
	FREE (cond.e);
	return out->d != 0;
}

enumError ConvertMP2kROM (const u8 *data, size_t size, ccp out_dir, uint *n_songs)
{
	if (n_songs)
		*n_songs = 0;
	if (size < 0x200)
		return ERR_NOTHING_TO_DO;
	const long eng = mp2k_detect (data, size);
	if (eng < 0)
		return ERR_NOTHING_TO_DO;
	size_t tbl = (gw (data, size, (size_t)eng + 8) & 0x1ffffff) + 12 * (size_t)gw (data, size, (size_t)eng + 4);
	for (;;)
	{
		if (tbl + 4 > size)
			return ERR_NOTHING_TO_DO;
		if (gw (data, size, tbl))
			break;
		tbl += 4;
	}
	CreatePath (out_dir, true);
	uint count = 0;
	for (uint i = 0;; i++)
	{
		const u32 p = gw (data, size, tbl + (size_t)i * 8) & 0x1ffffff;
		if (!p || p >= size)
			break;
		buf_t mid = { 0 };
		if (song_to_midi (data, size, p, &mid))
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/song_%03u.mid", out_dir, i);
			FILE *fp = fopen (path, "wb");
			if (fp)
			{
				fwrite (mid.d, 1, mid.n, fp);
				fclose (fp);
				count++;
			}
		}
		FREE (mid.d);
	}
	if (n_songs)
		*n_songs = count;
	return count ? ERR_OK : ERR_NOTHING_TO_DO;
}
