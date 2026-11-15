// JAudio1 ".jam" sequences (Pikmin 1 pikiseq.arc and other first generation JAudio games) -> Standard MIDI File.
// The byte code is interpreted like the game's sequencer (Jam_SeqmainNote / Cmd_Process in the decompilation): notes,
// waits, registers, conditions, calls, loops and child tracks (OpenTrack). Time is counted in time base ticks, so the
// MIDI division is the sequence's time base. A looping song is written for one pass (stops at the second backward
// jump of the root track, or after 10 minutes). Written as "<name>.mid".
#pragma push_macro("calloc")
#pragma push_macro("realloc")
#pragma push_macro("free")
#undef calloc
#undef realloc
#undef free
typedef struct jam_ev_t
{
	u32 tick;
	u32 seq;
	u8 track;
	u8 len;
	u8 b[3];
} jam_ev_t;

typedef struct jam_track_t
{
	bool active;
	u32 pc;
	int wait;
	int depth;
	u32 stk_pc[16];
	u32 stk_cnt[16];
	bool stk_loop[16];
	u16 reg[64];
	int transpose;
	int parent;
	int midi_ch;
	int note_active[8];
	u32 note_off_tick[8];
	int last_prog;
	u32 jumps[8];
	int njumps;
	bool looped;
} jam_track_t;

typedef struct jam_vm_t
{
	const u8 *d;
	u32 sz;
	jam_track_t t[17];
	jam_ev_t *ev;
	size_t nev, cap;
	u32 tick;
	u32 timebase;
	u32 bpm;
	u32 tempos[256][2];
	int ntempos;
	bool stop;
	bool bad;
	int sel;
	bool usedport;
	u32 opened[17];
	bool capped;
} jam_vm_t;

static const u8 jam_argc[64] = { 0, 2, 2, 1, 0, 0, 1, 1, 0, 1, 0, 2, 2, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 2, 5, 1, 1, 1, 1, 2, 1, 2,
	1, 0, 0, 0, 2, 0, 0, 1, 0, 0, 1, 5, 4, 1, 1, 3, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0 };
static const u16 jam_argt[64] = { 0, 8, 8, 2, 0, 0, 0, 2, 0, 1, 0, 0, 0xc, 0, 0, 3, 5, 0xc, 0xc, 0xf, 0, 0, 0, 8, 0x155, 0, 0, 0, 1,
	4, 0, 8, 0, 0, 0, 0, 4, 0, 0, 1, 0, 0, 2, 0, 0x55, 2, 2, 0, 0, 0, 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0 };

static void jam_event (jam_vm_t *v, int track, u8 st, u8 a, u8 b, u8 len)
{
	if (v->nev == v->cap)
	{
		v->cap = v->cap ? v->cap * 2 : 4096;
		jam_ev_t *n = (jam_ev_t *)realloc (v->ev, v->cap * sizeof (*n));
		if (!n)
		{
			v->stop = v->bad = true;
			return;
		}
		v->ev = n;
	}
	jam_ev_t *e = v->ev + v->nev;
	e->tick = v->tick;
	e->seq = (u32)v->nev++;
	e->track = (u8)track;
	e->len = len;
	e->b[0] = st;
	e->b[1] = a;
	e->b[2] = b;
}

static u32 jam_rd (jam_vm_t *v, jam_track_t *t, int n)
{
	u32 r = 0;
	for (int i = 0; i < n; i++)
	{
		if (t->pc >= v->sz)
		{
			t->active = false;
			v->bad = true;
			return 0;
		}
		r = r << 8 | v->d[t->pc++];
	}
	return r;
}

static u32 jam_regval (jam_track_t *t, u8 r)
{
	if (r >= 64)
		return 0;
	if (r == 0x30)
		return 0;
	return t->reg[r];
}

static void jam_note_off (jam_vm_t *v, int ti, int voice)
{
	jam_track_t *t = v->t + ti;
	if (t->note_active[voice] >= 0)
		jam_event (v, ti, 0x80 | t->midi_ch, (u8)t->note_active[voice], 0, 3);
	t->note_active[voice] = -1;
	t->note_off_tick[voice] = 0xffffffffu;
}

static void jam_close (jam_vm_t *v, int ti)
{
	for (int i = 0; i < 8; i++)
		jam_note_off (v, ti, i);
	v->t[ti].active = false;
}

static bool jam_cond (jam_track_t *t, u8 c)
{
	const u16 r = t->reg[3];
	switch (c & 15)
	{
	case 0: return true;
	case 1: return r == 0;
	case 2: return r != 0;
	case 3: return r == 1;
	case 4: return r >= 0x8000;
	case 5: return r < 0x8000;
	}
	return false;
}

static void jam_set_prog (jam_vm_t *v, int ti)
{
	jam_track_t *t = v->t + ti;
	const int p = t->reg[6] & 0xff;
	if (p != t->last_prog)
	{
		t->last_prog = p;
		jam_event (v, ti, 0xc0 | t->midi_ch, (u8)(p & 0x7f), 0, 2);
	}
}

static void jam_backjump (jam_vm_t *v, int ti, u32 from, u32 to)
{
	if (to > from)
		return;
	jam_track_t *t = v->t + ti;
	for (int i = 0; i < t->njumps; i++)
		if (t->jumps[i] == from)
		{
			t->looped = true;
			return;
		}
	if (t->njumps < 8)
		t->jumps[t->njumps++] = from;
}

static void jam_cmd (jam_vm_t *v, int ti, u8 cmd, u16 mask)
{
	jam_track_t *t = v->t + ti;
	if (cmd < 0xc0)
		return;
	const int ci = cmd - 0xc0;
	u32 arg[5] = { 0, 0, 0, 0, 0 };
	u16 ty = (u16)(jam_argt[ci] | mask);
	if (cmd == 0xc4 || cmd == 0xc8)
	{
		// CallF / JmpF: flags byte; bit7 target from register (bit6 table lookup), else a 24 bit address
		const u8 fl = (u8)jam_rd (v, t, 1);
		u32 target;
		if (fl & 0x80)
		{
			target = jam_regval (t, (u8)jam_rd (v, t, 1));
			if (fl & 0x40)
			{
				u32 base;
				if (fl & 0x20)
					base = jam_regval (t, (u8)jam_rd (v, t, 1));
				else
					base = jam_rd (v, t, 3);
				const u32 a = base + target * 3;
				target = a + 3 <= v->sz ? (u32)(v->d[a] << 16 | v->d[a + 1] << 8 | v->d[a + 2]) : 0;
			}
		}
		else
			target = jam_rd (v, t, 3);
		if (jam_cond (t, fl))
		{
			if (cmd == 0xc4)
			{
				if (t->depth >= 16)
				{
					jam_close (v, ti);
					return;
				}
				t->stk_pc[t->depth] = t->pc;
				t->stk_loop[t->depth++] = false;
			}
			else
				jam_backjump (v, ti, t->pc, target);
			t->pc = target;
		}
		return;
	}
	for (int i = 0; i < jam_argc[ci]; i++)
	{
		switch (ty & 3)
		{
		case 0: arg[i] = jam_rd (v, t, 1); break;
		case 1: arg[i] = jam_rd (v, t, 2); break;
		case 2: arg[i] = jam_rd (v, t, 3); break;
		default: arg[i] = jam_regval (t, (u8)jam_rd (v, t, 1)); break;
		}
		ty >>= 2;
	}
	switch (cmd)
	{
	case 0xc1:
	case 0xc2:
	{
		const int idx = (arg[0] & 15) + 1;
		const int pi = cmd == 0xc2 && t->parent >= 0 ? t->parent : ti;
		const int slot = (int)(arg[0] & 15) + 1;
		(void)idx;
		(void)pi;
		if (slot > 16 || arg[1] >= v->sz)
			break;
		jam_track_t *c = v->t + slot;
		if (v->opened[slot] == arg[1] + 1)
			v->stop = true; // the same child is started again: the song restarts
		v->opened[slot] = arg[1] + 1;
		if (c->active)
			jam_close (v, slot);
		memset (c, 0, sizeof (*c));
		c->active = true;
		c->pc = arg[1];
		c->parent = ti;
		c->midi_ch = slot < 9 ? slot : slot + 1;
		if (c->midi_ch > 15)
			c->midi_ch = 15;
		c->last_prog = -1;
		for (int i = 0; i < 8; i++)
		{
			c->note_active[i] = -1;
			c->note_off_tick[i] = 0xffffffffu;
		}
		c->transpose = 0;
		break;
	}
	case 0xc3:
		if (t->depth >= 16)
		{
			jam_close (v, ti);
			break;
		}
		t->stk_pc[t->depth] = t->pc;
		t->stk_loop[t->depth++] = false;
		t->pc = arg[0];
		break;
	case 0xc5:
		if (!t->depth)
		{
			jam_close (v, ti);
			break;
		}
		t->pc = t->stk_pc[--t->depth];
		break;
	case 0xc6:
		if (jam_cond (t, (u8)arg[0]))
		{
			if (!t->depth)
			{
				jam_close (v, ti);
				break;
			}
			t->pc = t->stk_pc[--t->depth];
		}
		break;
	case 0xc7:
		jam_backjump (v, ti, t->pc, arg[0]);
		t->pc = arg[0];
		break;
	case 0xc9:
		if (t->depth >= 16)
		{
			jam_close (v, ti);
			break;
		}
		t->stk_pc[t->depth] = t->pc;
		t->stk_cnt[t->depth] = arg[0];
		t->stk_loop[t->depth++] = true;
		break;
	case 0xca:
		if (!t->depth || !t->stk_loop[t->depth - 1])
		{
			jam_close (v, ti);
			break;
		}
		{
			u32 *c = t->stk_cnt + t->depth - 1;
			const bool infinite = *c == 0;
			if (*c)
				(*c)--;
			if (!infinite && !*c)
				t->depth--;
			else
			{
				if (infinite)
					jam_backjump (v, ti, t->pc, t->stk_pc[t->depth - 1]);
				t->pc = t->stk_pc[t->depth - 1];
			}
		}
		break;
	case 0xcb: // ReadPort: dispatcher sequences read their song number from a port, supply the selector
		v->usedport = true;
		if (arg[0] == 0 && arg[1] < 64)
			t->reg[arg[1]] = (u16)v->sel;
		break;
	case 0xcd: // CheckPortImport: pretend the host wrote port 0 so a dispatcher starts the song
		t->reg[3] = arg[0] == 0 ? 1 : 0;
		break;
	case 0xcf:
	case 0xea:
		t->wait = (int)arg[0];
		break;
	case 0xd9:
		t->transpose = (int)(s8)arg[0];
		break;
	case 0xda:
		jam_close (v, ti);
		break;
	case 0xfd:
		if (arg[0] > 0 && arg[0] < 2000)
		{
			v->bpm = arg[0];
			if (v->ntempos < 256)
			{
				v->tempos[v->ntempos][0] = v->tick;
				v->tempos[v->ntempos++][1] = arg[0];
			}
		}
		break;
	case 0xfe:
		if (arg[0] > 0 && arg[0] <= 0x7fff && !v->tick)
			v->timebase = arg[0];
		break;
	case 0xff:
		jam_close (v, ti);
		break;
	case 0xfb:
	{
		// Printf: NUL terminated string with backslash escapes
		int nparm = 0;
		for (int n = 0; n < 128; n++)
		{
			const u32 c = jam_rd (v, t, 1);
			if (!t->active || !c)
				break;
			if (c == '\\')
			{
				if (!jam_rd (v, t, 1))
					break;
			}
			else if (c == '%')
			{
				if (!jam_rd (v, t, 1))
					break;
				nparm++;
			}
		}
		for (int i = 0; i < nparm; i++)
			jam_rd (v, t, 1);
		break;
	}
	default:
		break;
	}
}

static void jam_reg_write (jam_vm_t *v, int ti, u8 cb)
{
	jam_track_t *t = v->t + ti;
	u32 sel = cb & 0xc, op = cb & 3, shift_n = 0;
	if ((cb & 15) == 0xb)
	{
		sel = 0;
		op = 0xb;
	}
	if ((cb & 15) == 0xa)
	{
		cb = (u8)jam_rd (v, t, 1);
		sel = cb & 0xc;
		op = 0xa;
		shift_n = (u32)(cb >> 4) + 4;
	}
	if ((cb & 15) == 9)
	{
		cb = (u8)jam_rd (v, t, 1);
		sel = cb & 0xc;
		op = cb & 0xf0;
		if (sel == 8)
			sel = 0x10;
	}
	const u8 ri = (u8)jam_rd (v, t, 1);
	(void)shift_n;
	u32 tbase = 0;
	if (op == 0xa)
	{
		// table base: 32 bit register pairs 0x28.. alias the register pairs 0x10:0x11, 0x12:0x13, ...
		const u8 br = (u8)jam_rd (v, t, 1);
		if (br >= 0x28 && br <= 0x2b)
		{
			const int q = 0x10 + (br - 0x28) * 2;
			tbase = (u32)t->reg[q] << 16 | t->reg[q + 1];
		}
		else
			tbase = jam_regval (t, br);
	}
	s32 nv = 0;
	switch (sel)
	{
	case 0: nv = (s16)jam_regval (t, (u8)jam_rd (v, t, 1)); break;
	case 4: nv = (s8)jam_rd (v, t, 1); break;
	case 8: nv = (s16)(jam_rd (v, t, 1) << 8); break;
	case 12: nv = (s16)jam_rd (v, t, 2); break;
	default: nv = -1; break;
	}
	const s16 old = (s16)(ri < 64 ? t->reg[ri] : 0);
	const bool imm8 = sel == 4;
	(void)imm8;
	switch (op)
	{
	case 0x00: break;
	case 0x01: nv = (s16)(old + nv); break;
	case 0x02: nv = (s16)(old * nv); break;
	case 0x03: t->reg[3] = (u16)(old - nv); return;
	case 0x0b: nv = (s16)(old - nv); break;
	case 0x10: nv = nv < 0 ? (u16)old >> -nv : (u16)old << nv; break;
	case 0x20: nv = nv < 0 ? old >> -nv : old << nv; break;
	case 0x30: nv = old & nv; break;
	case 0x40: nv = old | nv; break;
	case 0x50: nv = old ^ nv; break;
	case 0x60: nv = -old; break;
	case 0xa:
	{
		const u32 idx = (u32)(u16)nv;
		u32 a = 0, r = 0;
		switch (shift_n)
		{
		case 4: a = tbase + idx; if (a + 1 <= v->sz) r = v->d[a]; break;
		case 5: a = tbase + idx * 2; if (a + 2 <= v->sz) r = (u32)(v->d[a] << 8 | v->d[a + 1]); break;
		case 6: a = tbase + idx * 3; if (a + 3 <= v->sz) r = (u32)(v->d[a] << 16 | v->d[a + 1] << 8 | v->d[a + 2]); break;
		default: a = tbase + idx * 4; if (a + 4 <= v->sz) r = rd_be32 (v->d + a); break;
		}
		nv = (s16)r;
		break;
	}
	case 0x90: nv = (u16)nv ? (s32)(((u32)v->tick * 2654435761u >> 8) % (u16)nv) : 0; break;
	default: break;
	}
	u16 w = (u16)nv;
	if (ri <= 2)
		w &= 0xff;
	if (ri == 0x21)
	{
		t->reg[6] = (u16)((t->reg[6] & 0xff00) | (w & 0xff));
		jam_set_prog (v, ti);
		return;
	}
	if (ri == 0x20)
	{
		t->reg[6] = (u16)((t->reg[6] & 0x00ff) | (w << 8));
		return;
	}
	if (ri == 0x22)
	{
		t->reg[0] = (u16)(w >> 8);
		t->reg[1] = w & 0xff;
		return;
	}
	if (ri < 64 && ri != 0x30)
		t->reg[ri] = w;
	if (ri == 6)
		jam_set_prog (v, ti);
}

static void jam_time_param (jam_vm_t *v, int ti, u8 cb)
{
	jam_track_t *t = v->t + ti;
	const u8 pi = (u8)jam_rd (v, t, 1);
	s16 val = 0;
	switch (cb & 0xc)
	{
	case 0: val = (s16)jam_regval (t, (u8)jam_rd (v, t, 1)); break;
	case 4: val = (s16)jam_rd (v, t, 1); break;
	case 8: val = (s16)(jam_rd (v, t, 1) << 8); break;
	default: val = (s16)jam_rd (v, t, 2); break;
	}
	switch (cb & 3)
	{
	case 1: jam_rd (v, t, 1); break;
	case 2: jam_rd (v, t, 1); break;
	case 3: jam_rd (v, t, 2); break;
	}
	const int ch = t->midi_ch;
	if (pi == 0)
		jam_event (v, ti, 0xb0 | ch, 7, (u8)(val < 0 ? 0 : val >> 8 > 127 ? 127 : (val * 127) / 32768), 3);
	else if (pi == 2)
		jam_event (v, ti, 0xb0 | ch, 10, (u8)(val < 0 ? 0 : val > 32767 ? 127 : 64 + (val * 63) / 32768), 3);
	else if (pi == 1)
	{
		int bend = 8192 + (int)((s32)val * 8192 / 32768);
		if (bend < 0)
			bend = 0;
		if (bend > 16383)
			bend = 16383;
		jam_event (v, ti, 0xe0 | ch, (u8)(bend & 0x7f), (u8)(bend >> 7), 3);
	}
}

static void jam_step (jam_vm_t *v, int ti)
{
	jam_track_t *t = v->t + ti;
	for (int off = 0; off < 8; off++)
		if (t->note_off_tick[off] <= v->tick)
			jam_note_off (v, ti, off);
	if (t->wait > 0)
	{
		if (--t->wait)
			return;
	}
	for (int guard = 0; t->active && guard < 200000 && !v->stop; guard++)
	{
		const u32 op = jam_rd (v, t, 1);
		if (!t->active)
			return;
		if (!(op & 0x80))
		{
			int key = (int)op + t->transpose;
			if (t->parent >= 0)
				key += v->t[t->parent].transpose;
			const u8 fl = (u8)jam_rd (v, t, 1);
			if (fl & 0x80)
			{
				key = (int)jam_regval (t, (u8)op) + t->transpose;
			}
			int vel = (int)jam_rd (v, t, 1);
			if (vel >= 0x80)
				vel = (int)jam_regval (t, (u8)(vel - 0x80));
			u32 low = fl & 7;
			int voice = 0;
			int gate = 100;
			u32 dur = 0xffffffffu;
			if (!low)
			{
				gate = (int)jam_rd (v, t, 1);
				if (gate >= 0x80)
					gate = (int)jam_regval (t, (u8)(gate - 0x80));
				const int nb = (fl >> 3) & 3;
				dur = 0;
				for (int i = 0; i < nb; i++)
					dur = dur << 8 | jam_rd (v, t, 1);
				if (nb == 1 && dur >= 0x80)
					dur = jam_regval (t, (u8)(dur - 0x80));
			}
			else
			{
				voice = (int)low;
				if ((fl >> 3) & 3)
				{
					voice = (int)jam_regval (t, (u8)(voice - 1));
					if (voice >= 8)
						break;
				}
			}
			if (!t->active)
				return;
			if (key < 0)
				key = 0;
			if (key > 127)
				key = 127;
			if (vel > 127)
				vel = 127;
			if (!vel)
				vel = 1;
			const int vi = voice & 7;
			if (t->note_active[vi] >= 0)
				jam_note_off (v, ti, vi);
			jam_event (v, ti, 0x90 | t->midi_ch, (u8)key, (u8)vel, 3);
			t->note_active[vi] = key;
			if (dur != 0xffffffffu)
			{
				u32 g = (u32)((u64)dur * (u32)(gate > 0 ? gate : 1) / 100);
				if (!g)
					g = 1;
				if (g > dur && dur)
					g = dur;
				t->note_off_tick[vi] = v->tick + g;
				if (dur)
				{
					t->wait = (int)dur;
					return;
				}
				// duration 0: wait until the note is released
				t->wait = (int)g;
				return;
			}
			continue;
		}
		if ((op & 0xf0) == 0x80 || op == 0xf9)
		{
			u32 wbytes = 1;
			u32 o = op;
			if (op == 0xf9)
			{
				const u32 c = jam_rd (v, t, 1);
				const u32 vi = jam_regval (t, (u8)(c & 7));
				if (vi > 7 || !vi)
				{
					if (c & 0x80)
						jam_rd (v, t, 1);
					continue;
				}
				o = vi + 0x80;
				if (c & 0x80)
					o += 8;
			}
			u32 voice = o & 15;
			if (voice == 8)
			{
				wbytes = 2;
				voice = 0;
			}
			if (voice > 8)
			{
				voice -= 8;
				jam_rd (v, t, 1);
			}
			if (!voice)
			{
				u32 w = 0;
				for (u32 i = 0; i < wbytes; i++)
					w = w << 8 | jam_rd (v, t, 1);
				if (!w)
					continue;
				t->wait = (int)w;
				return;
			}
			jam_note_off (v, ti, (int)voice & 7);
			continue;
		}
		if ((op & 0xf0) == 0x90)
			jam_time_param (v, ti, op & 15);
		else if ((op & 0xf0) == 0xa0)
			jam_reg_write (v, ti, op & 15);
		else if ((op & 0xf0) == 0xb0)
		{
			const bool from_reg = (op & 8) != 0;
			const u32 nargs = op & 7;
			u8 cmd = (u8)jam_rd (v, t, 1);
			if (from_reg)
				cmd = (u8)jam_regval (t, cmd);
			u16 mask = 0;
			if (!from_reg || nargs)
			{
				u8 mb = (u8)jam_rd (v, t, 1);
				u16 bp = 3;
				for (u32 i = 0; i < nargs + 1; i++)
				{
					if (mb & 0x80)
						mask |= bp;
					mb <<= 1;
					bp <<= 2;
				}
			}
			jam_cmd (v, ti, cmd, mask);
		}
		else
			jam_cmd (v, ti, (u8)op, 0);
		if (t->wait > 0)
			return;
	}
}

static void jam_vlq (u8 **p, u32 v)
{
	u8 tmp[5];
	int n = 0;
	tmp[n++] = v & 0x7f;
	while (v >>= 7)
		tmp[n++] = (u8)(0x80 | (v & 0x7f));
	while (n)
		*(*p)++ = tmp[--n];
}

static int jam_ev_cmp (const void *a, const void *b)
{
	const jam_ev_t *x = (const jam_ev_t *)a, *y = (const jam_ev_t *)b;
	if (x->tick != y->tick)
		return x->tick < y->tick ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static u8 *jam_render (const u8 *raw, size_t sz, int sel, uint *notes_out, bool *usedport, size_t *outlen)
{
	jam_vm_t *v = (jam_vm_t *)calloc (1, sizeof (*v));
	if (!v || sz < 16)
	{
		free (v);
		return 0;
	}
	v->sel = sel;
	v->d = raw;
	v->sz = (u32)sz;
	v->timebase = 120;
	v->bpm = 120;
	v->t[0].active = true;
	v->t[0].parent = -1;
	v->t[0].last_prog = -1;
	for (int t = 0; t < 17; t++)
		for (int i = 0; i < 8; i++)
		{
			v->t[t].note_active[i] = -1;
			v->t[t].note_off_tick[i] = 0xffffffffu;
		}
	double secs = 0;
	for (; !v->stop && v->tick < 4000000 && secs < 600.0; v->tick++)
	{
		bool any = false;
		for (int i = 0; i < 17; i++)
			if (v->t[i].active)
			{
				any = true;
				jam_step (v, i);
			}
		if (!any)
			break;
		bool all_looped = true;
		for (int i = 0; i < 17; i++)
			if (v->t[i].active && !v->t[i].looped)
				all_looped = false;
		if (all_looped)
			break;
		secs += 60.0 / ((double)v->bpm * v->timebase);
	}
	v->capped = secs >= 600.0 || v->tick >= 4000000;
	for (int i = 0; i < 17; i++)
		jam_close (v, i);
	// build the MIDI file
	qsort (v->ev, v->nev, sizeof (*v->ev), jam_ev_cmp);
	int used[17] = { 0 };
	uint nnotes = 0;
	for (size_t i = 0; i < v->nev; i++)
	{
		used[v->ev[i].track] = 1;
		if ((v->ev[i].b[0] & 0xf0) == 0x90)
			nnotes++;
	}
	u8 *result = 0;
	*notes_out = nnotes;
	*usedport = v->usedport;
	if (nnotes && !v->bad && !(v->capped && sel))
	{
		size_t cap = v->nev * 8 + 4096;
		u8 *out = (u8 *)calloc (1, cap);
		if (out)
		{
			u8 *p = out;
			int ntr = 1;
			for (int i = 0; i < 17; i++)
				ntr += used[i];
			memcpy (p, "MThd\0\0\0\6\0\1", 10);
			p += 10;
			*p++ = (u8)(ntr >> 8);
			*p++ = (u8)ntr;
			*p++ = (u8)(v->timebase >> 8);
			*p++ = (u8)v->timebase;
			// tempo track
			u8 *hdr = p;
			memcpy (p, "MTrk", 4);
			p += 8;
			u32 last = 0;
			const int nt = v->ntempos ? v->ntempos : 0;
			if (!nt || v->tempos[0][0])
			{
				jam_vlq (&p, 0);
				const u32 us = 500000;
				*p++ = 0xff;
				*p++ = 0x51;
				*p++ = 3;
				*p++ = (u8)(us >> 16);
				*p++ = (u8)(us >> 8);
				*p++ = (u8)us;
			}
			for (int i = 0; i < nt; i++)
			{
				jam_vlq (&p, v->tempos[i][0] - last);
				last = v->tempos[i][0];
				const u32 us = 60000000u / v->tempos[i][1];
				*p++ = 0xff;
				*p++ = 0x51;
				*p++ = 3;
				*p++ = (u8)(us >> 16);
				*p++ = (u8)(us >> 8);
				*p++ = (u8)us;
			}
			jam_vlq (&p, 0);
			*p++ = 0xff;
			*p++ = 0x2f;
			*p++ = 0;
			u32 len = (u32)(p - hdr - 8);
			hdr[4] = (u8)(len >> 24);
			hdr[5] = (u8)(len >> 16);
			hdr[6] = (u8)(len >> 8);
			hdr[7] = (u8)len;
			for (int t = 0; t < 17; t++)
			{
				if (!used[t])
					continue;
				hdr = p;
				memcpy (p, "MTrk", 4);
				p += 8;
				last = 0;
				for (size_t i = 0; i < v->nev; i++)
				{
					const jam_ev_t *e = v->ev + i;
					if (e->track != t)
						continue;
					jam_vlq (&p, e->tick - last);
					last = e->tick;
					for (int k = 0; k < e->len; k++)
						*p++ = e->b[k];
				}
				jam_vlq (&p, 0);
				*p++ = 0xff;
				*p++ = 0x2f;
				*p++ = 0;
				len = (u32)(p - hdr - 8);
				hdr[4] = (u8)(len >> 24);
				hdr[5] = (u8)(len >> 16);
				hdr[6] = (u8)(len >> 8);
				hdr[7] = (u8)len;
			}
			*outlen = (size_t)(p - out);
			result = out;
		}
	}
	free (v->ev);
	free (v);
	return result;
}

static enumError jam_write (ccp arg, const char *dest, const u8 *out, size_t n, uint notes)
{
	enumError err = ERR_OK;
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT JAM:%s -> MIDI:%s\n", arg, dest);
		return ERR_OK;
	}
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (out, 1, n, F.f) != n)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)n, dest);
	ResetFile (&F, opt_preserve);
	if (verbose >= 0 && !err)
		fprintf (stdlog, "%sEXTRACT JAM:%s -> MIDI:%s (%u notes)\n", verbose > 0 ? "\n" : "", arg, dest, notes);
	return err;
}

static enumError extract_pikmin_jam_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".jam"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	uint notes = 0;
	bool port = false;
	size_t n = 0;
	u8 *out = jam_render (raw, sz, 0, &notes, &port, &n);
	char dest[PATH_MAX];
	if (out)
	{
		beside_source_dest_ext (dest, sizeof (dest), arg, ".mid");
		err = jam_write (arg, dest, out, n, notes);
		free (out);
	}
	else if (port)
	{
		// dispatcher: render the songs selected by port value 1..15
		char base[PATH_MAX];
		beside_source_dest_ext (base, sizeof (base), arg, "");
		err = ERR_OK;
		bool any = false;
		for (int sel = 1; sel < 16 && !err; sel++)
		{
			out = jam_render (raw, sz, sel, &notes, &port, &n);
			if (!out)
				continue;
			snprintf (dest, sizeof (dest), "%s.%d.mid", base, sel);
			err = jam_write (arg, dest, out, n, notes);
			any = true;
			free (out);
		}
		if (!any)
			err = ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	return err;
}

#pragma pop_macro("free")
#pragma pop_macro("realloc")
#pragma pop_macro("calloc")
