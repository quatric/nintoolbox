// Native NintendoWare sequence renderer. See lib-nwseq.h.
//
// The engine model follows the behaviour of the NintendoWare sound library: the MML command set
// and its prefixes, track parameter handling, the envelope generator, LFO, sweep / portamento,
// volume and pitch arithmetic, the 3 ms update cadence of the sound thread and the voice mixing
// (pan curves, pan modes, volume ramps). Deliberately free of lib-std.h.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "lib-nwseq.h"

typedef int8_t s8;
typedef int16_t s16;
typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;

#define FRAME_MS 3
#define FRAME_SAMPLES (NWS_RATE / 1000 * FRAME_MS) // 96
#define TRACK_NUM 16
#define CALL_DEPTH 3
#define MAX_CHANNELS 96
#define MAX_VOICES 64 // DSP voices; a stereo wave takes two
#define INVALID_ENV 0xff

///////////////////////////////////////////////////////////////////////////////
// arithmetic helpers (sound library Util)

static float clampf (float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static float g_note_table[12], g_pitch_table[256], g_db_table[1068], g_pan_table[3][257];
static bool g_tables_ready;

static void init_tables (void)
{
	if (g_tables_ready)
		return;
	for (int i = 0; i < 12; i++)
		g_note_table[i] = (float)pow (2.0, i / 12.0);
	for (int i = 0; i < 256; i++)
		g_pitch_table[i] = (float)pow (2.0, i / (256.0 * 12.0));
	g_db_table[0] = 0.0f; // -90.4 dB = -inf
	for (int i = 1; i < 1068; i++)
		g_db_table[i] = (float)pow (10.0, (i - 904) / 200.0);
	for (int i = 0; i <= 256; i++)
	{
		g_pan_table[0][i] = (float)sqrt ((256 - i) / 256.0);
		g_pan_table[1][i] = (float)cos (i / 256.0 * M_PI / 2);
		g_pan_table[2][i] = (256 - i) / 256.0f;
	}
	g_tables_ready = true;
}

static float calc_pitch_ratio (int pitch)
{
	int octave = 0;
	float of = 1.0f;
	while (pitch < 0)
	{
		octave--;
		pitch += 256 * 12;
	}
	while (pitch >= 256 * 12)
	{
		octave++;
		pitch -= 256 * 12;
	}
	for (; octave > 0; octave--)
		of *= 2.0f;
	for (; octave < 0; octave++)
		of /= 2.0f;
	const int note = pitch / 256;
	pitch %= 256;
	float r = of;
	if (note)
		r *= g_note_table[note];
	if (pitch)
		r *= g_pitch_table[pitch];
	return r;
}

static float calc_volume_ratio (float db)
{
	db = clampf (db, -90.4f, 6.0f);
	return g_db_table[clampi ((int)(db * 10) + 904, 0, 1067)];
}

typedef struct pan_info_t { int curve; bool center_zero, zero_clamp; } pan_info_t;

static pan_info_t pan_info (int pan_curve)
{
	pan_info_t pi = { 0, false, false };
	if (pan_curve < 0 || pan_curve > 8)
		pan_curve = 0;
	pi.curve = pan_curve / 3;
	pi.center_zero = pan_curve % 3 >= 1;
	pi.zero_clamp = pan_curve % 3 == 2;
	return pi;
}

static float calc_pan_ratio (float pan, pan_info_t pi)
{
	pan = (clampf (pan, -1.0f, 1.0f) + 1.0f) / 2.0f;
	const float *t = g_pan_table[pi.curve];
	float r = t[clampi ((int)(pan * 256 + 0.5f), 0, 256)];
	if (pi.center_zero)
		r /= t[128];
	return pi.zero_clamp ? clampf (r, 0.0f, 1.0f) : clampf (r, 0.0f, 2.0f);
}

///////////////////////////////////////////////////////////////////////////////
// envelope generator

enum { ENV_ATTACK, ENV_HOLD, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

static const float attack_table[128] = {
	0.9992175f, 0.9984326f, 0.9976452f, 0.9968553f, 0.9960629f, 0.9952679f, 0.9944704f, 0.9936704f,
	0.9928677f, 0.9920625f, 0.9912546f, 0.9904441f, 0.9896309f, 0.9888151f, 0.9879965f, 0.9871752f,
	0.9863512f, 0.9855244f, 0.9846949f, 0.9838625f, 0.9830273f, 0.9821893f, 0.9813483f, 0.9805045f,
	0.9796578f, 0.9788081f, 0.9779555f, 0.9770999f, 0.9762413f, 0.9753797f, 0.9745150f, 0.9736472f,
	0.9727763f, 0.9719023f, 0.9710251f, 0.9701448f, 0.9692612f, 0.9683744f, 0.9674844f, 0.9665910f,
	0.9656944f, 0.9647944f, 0.9638910f, 0.9629842f, 0.9620740f, 0.9611604f, 0.9602433f, 0.9593226f,
	0.9583984f, 0.9574706f, 0.9565392f, 0.9556042f, 0.9546655f, 0.9537231f, 0.9527769f, 0.9518270f,
	0.9508732f, 0.9499157f, 0.9489542f, 0.9479888f, 0.9470195f, 0.9460462f, 0.9450689f, 0.9440875f,
	0.9431020f, 0.9421124f, 0.9411186f, 0.9401206f, 0.9391184f, 0.9381118f, 0.9371009f, 0.9360856f,
	0.9350659f, 0.9340417f, 0.9330131f, 0.9319798f, 0.9309420f, 0.9298995f, 0.9288523f, 0.9278004f,
	0.9267436f, 0.9256821f, 0.9246156f, 0.9235442f, 0.9224678f, 0.9213864f, 0.9202998f, 0.9192081f,
	0.9181112f, 0.9170091f, 0.9159016f, 0.9147887f, 0.9136703f, 0.9125465f, 0.9114171f, 0.9102821f,
	0.9091414f, 0.9079949f, 0.9068427f, 0.9056845f, 0.9045204f, 0.9033502f, 0.9021740f, 0.9009916f,
	0.8998029f, 0.8986080f, 0.8974066f, 0.8961988f, 0.8949844f, 0.8900599f, 0.8824622f, 0.8759247f,
	0.8691861f, 0.8636406f, 0.8535788f, 0.8430189f, 0.8286135f, 0.8149099f, 0.8002172f, 0.7780663f,
	0.7554750f, 0.7242125f, 0.6828239f, 0.6329169f, 0.5592135f, 0.4551411f, 0.3298770f, 0.0000000f
};

// "decibel square" scale (dB * 10) used for the sustain level
static const s16 db_square_table[128] = {
	-723, -722, -721, -651, -601, -562, -530, -503, -480, -460, -442, -425, -410, -396, -383, -371,
	-360, -349, -339, -330, -321, -313, -305, -297, -289, -282, -276, -269, -263, -257, -251, -245,
	-239, -234, -229, -224, -219, -214, -210, -205, -201, -196, -192, -188, -184, -180, -176, -173,
	-169, -165, -162, -158, -155, -152, -149, -145, -142, -139, -136, -133, -130, -127, -125, -122,
	-119, -116, -114, -111, -109, -106, -103, -101, -99, -96, -94, -91, -89, -87, -85, -82,
	-80, -78, -76, -74, -72, -70, -68, -66, -64, -62, -60, -58, -56, -54, -52, -50,
	-49, -47, -45, -43, -42, -40, -38, -36, -35, -33, -31, -30, -28, -27, -25, -23,
	-22, -20, -19, -17, -16, -14, -13, -11, -10, -8, -7, -6, -4, -3, -1, 0
};

typedef struct env_t
{
	float value; // dB * 10
	int status;
	float attack, decay, release;
	int hold, hold_counter;
	u8 sustain;
} env_t;

static float env_calc_release (int r)
{
	if (r == 127)
		return 65535.0f;
	if (r == 126)
		return 120 / 5.0f;
	if (r < 50)
		return ((r << 1) + 1) / 128.0f / 5.0f;
	return (60.0f / (126 - r)) / 5.0f;
}

static void env_set_attack (env_t *e, int v) { e->attack = attack_table[v & 127]; }
static void env_set_hold (env_t *e, int v) { e->hold = (u16)((v + 1) * (v + 1) / 4); }
static void env_set_decay (env_t *e, int v) { e->decay = env_calc_release (v & 127); }
static void env_set_sustain (env_t *e, int v) { e->sustain = (u8)(v & 127); }
static void env_set_release (env_t *e, int v) { e->release = env_calc_release (v & 127); }

static void env_reset (env_t *e)
{
	e->value = -90.4f * 10.0f;
	e->status = ENV_ATTACK;
}

static void env_init (env_t *e)
{
	env_set_attack (e, 127);
	env_set_hold (e, 0);
	env_set_decay (e, 127);
	env_set_sustain (e, 127);
	env_set_release (e, 127);
	env_reset (e);
}

static float env_value (const env_t *e)
{
	if (e->status == ENV_ATTACK && e->attack == 0.0f)
		return 0.0f;
	return e->value / 10.0f;
}

static void env_update (env_t *e, int msec)
{
	switch (e->status)
	{
	case ENV_ATTACK:
		while (msec > 0)
		{
			e->value *= e->attack;
			msec--;
			if (e->value > -1.0f / 32.0f)
			{
				e->value = 0.0f;
				e->status = ENV_HOLD;
				e->hold_counter = e->hold;
				break;
			}
		}
		break;
	case ENV_HOLD:
		if (msec < e->hold_counter)
			e->hold_counter -= msec;
		else
		{
			msec -= e->hold_counter;
			e->hold_counter = 0;
			e->status = ENV_DECAY;
		}
		if (e->status != ENV_DECAY)
			break;
		// fall through
	case ENV_DECAY:
	{
		const float sus = db_square_table[e->sustain];
		e->value -= e->decay * msec;
		if (e->value < sus)
		{
			e->value = sus;
			e->status = ENV_SUSTAIN;
		}
		break;
	}
	case ENV_SUSTAIN:
		break;
	case ENV_RELEASE:
		e->value -= e->release * msec;
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////
// LFO

enum { LFO_PITCH, LFO_VOLUME, LFO_PAN };

typedef struct lfo_param_t
{
	float depth, speed;
	u32 delay;
	u8 range;
	u8 curve; // CSEQ/FSEQ: 0 sine, 1 triangle, 2 saw, 3 square, 4 random
	float phase; // CSEQ/FSEQ: start phase, 0..1
} lfo_param_t;

typedef struct lfo_t
{
	lfo_param_t p;
	float counter;
	u32 delay_counter;
} lfo_t;

static void lfo_param_init (lfo_param_t *p)
{
	p->depth = 0.0f;
	p->range = 1;
	p->speed = 6.25f;
	p->delay = 0;
	p->curve = 0;
	p->phase = 0.0f;
}

static void lfo_update (lfo_t *l, int msec)
{
	if (l->delay_counter < l->p.delay)
	{
		if (l->delay_counter + msec <= l->p.delay)
		{
			l->delay_counter += msec;
			return;
		}
		msec -= l->p.delay - l->delay_counter;
		l->delay_counter = l->p.delay;
	}
	l->counter += l->p.speed * msec / 1000.0f;
	l->counter -= (int)l->counter;
}

static int lfo_sin_idx (int i)
{
	static const signed char t[33] = { 0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85, 90,
		94, 98, 102, 106, 109, 112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127 };
	i &= 127;
	if (i < 32)
		return t[i];
	if (i < 64)
		return t[32 - (i - 32)];
	if (i < 96)
		return -t[i - 64];
	return -t[32 - (i - 96)];
}

static float lfo_value (const lfo_t *l)
{
	if (l->p.depth == 0.0f || l->delay_counter < l->p.delay)
		return 0.0f;
	float v, x = l->counter + l->p.phase;
	x -= (int)x;
	switch (l->p.curve)
	{
	case 1: v = x < 0.25f ? 4 * x : x < 0.75f ? 2 - 4 * x : 4 * x - 4; break;
	case 2: v = x < 0.5f ? 2 * x : 2 * x - 2; break;
	case 3: v = x < 0.5f ? 1.0f : -1.0f; break;
	case 4: v = ((int)(x * 16) * 2654435761u >> 16 & 0xffff) / 32767.5f - 1.0f; break;
	default: v = lfo_sin_idx ((int)(x * 32 * 4)) / 127.0f; break;
	}
	return v * l->p.depth * l->p.range;
}

///////////////////////////////////////////////////////////////////////////////
// player state

typedef struct mv_t { int origin, target, frame, counter; } mv_t; // MoveValue

static void mv_init (mv_t *m, int v) { m->origin = m->target = v; m->frame = m->counter = 0; }
static int mv_value (const mv_t *m)
{
	if (m->counter >= m->frame)
		return m->target;
	return m->origin + (m->target - m->origin) * m->counter / m->frame;
}
static void mv_set (mv_t *m, int target, int frames)
{
	m->origin = mv_value (m);
	m->target = target;
	m->frame = frames;
	m->counter = 0;
}
static void mv_update (mv_t *m) { if (m->counter < m->frame) m->counter++; }

typedef struct channel_t
{
	bool used, active, attached;
	int track; // owning track while attached
	int next;  // next channel of the same track (-1 = end)
	u32 serial;
	int priority;

	s32 length;
	u8 key, original_key;
	float init_volume, init_pan, tune;
	float user_volume, user_pitch, user_pitch_ratio, user_pan, main_send;
	bool release_ignore;
	u8 alternate_assign;
	env_t env;
	lfo_t lfo;
	int lfo_target;
	float sweep_pitch;
	int sweep_length, sweep_counter;
	bool auto_sweep;
	int pan_mode, pan_curve;

	// voice
	const nws_wave_t *wave;
	double pos;
	float ve_init, ve_target, volume, pitch, pan;
	float mix_l[2], mix_r[2], prev_l[2], prev_r[2];
	bool mix_valid;
} channel_t;

typedef struct call_t { u32 addr; u8 loop_count; bool loop_flag; } call_t;

typedef struct track_t
{
	bool allocated, opened;
	u32 cur;
	s32 wait;
	bool cmp_flag, note_wait, tie, mono, note_finish_wait, porta, damper, mute;
	int call_depth;
	call_t calls[CALL_DEPTH];
	int prg, bank;
	lfo_param_t lfo;
	int lfo_target;
	float sweep_pitch;
	mv_t volume, pan, pitch_bend;
	u8 volume2, velocity_range, bend_range, priority, porta_key, porta_time;
	s8 init_pan, transpose;
	u8 attack, decay, sustain, release, env_hold;
	u8 main_send;
	s16 var[16];
	int head; // channel list (most recent first), -1 = empty
	int loops; // backward jumps taken (loop passes)
} track_t;

typedef struct player_t
{
	const nws_params_t *p;
	const u8 *seq;
	size_t seq_size;
	track_t tr[TRACK_NUM];
	channel_t ch[MAX_CHANNELS];
	u32 serial;
	int tempo, timebase, main_volume, priority;
	float tick_fraction;
	s16 local_var[16], global_var[16];
	u32 rand_state;
	bool active;
	bool looped;
	bool debug; // NWSEQ_DEBUG set: trace to stderr
} player_t;

static u16 calc_random (player_t *pl)
{
	pl->rand_state = pl->rand_state * 1664525 + 1013904223;
	return (u16)(pl->rand_state >> 16);
}

static u8 rd8 (player_t *pl, track_t *t)
{
	if (t->cur >= pl->seq_size)
		return 0xff; // fin
	return pl->seq[t->cur++];
}
static u16 rd16 (player_t *pl, track_t *t) { u16 v = rd8 (pl, t) << 8; return v | rd8 (pl, t); }
static u32 rd24 (player_t *pl, track_t *t) { u32 v = rd8 (pl, t) << 16; v |= rd8 (pl, t) << 8; return v | rd8 (pl, t); }
static s32 rdvar (player_t *pl, track_t *t)
{
	s32 v = 0;
	for (int i = 0; i < 4; i++)
	{
		const u8 b = rd8 (pl, t);
		v = v << 7 | (b & 0x7f);
		if (!(b & 0x80))
			break;
	}
	return v;
}

static s16 *var_ptr (player_t *pl, track_t *t, int n)
{
	if (n < 0)
		return 0;
	if (n < 16)
		return pl->local_var + n;
	if (n < 32)
		return pl->global_var + n - 16;
	if (n < 48)
		return t->var + n - 32;
	return 0;
}

enum { ARG_NONE, ARG_U8, ARG_S16, ARG_VMIDI, ARG_RANDOM, ARG_VARIABLE };

static s32 read_arg (player_t *pl, track_t *t, int type)
{
	switch (type)
	{
	case ARG_U8: return rd8 (pl, t);
	case ARG_S16: return rd16 (pl, t);
	case ARG_VMIDI: return rdvar (pl, t);
	case ARG_VARIABLE:
	{
		const s16 *v = var_ptr (pl, t, rd8 (pl, t));
		return v ? *v : 0;
	}
	case ARG_RANDOM:
	{
		const s16 mn = (s16)rd16 (pl, t), mx = (s16)rd16 (pl, t);
		s32 r = calc_random (pl);
		r *= (mx - mn) + 1;
		r >>= 16;
		return r + mn;
	}
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// channels

static int voices_in_use (const player_t *pl)
{
	int n = 0;
	for (int i = 0; i < MAX_CHANNELS; i++)
		if (pl->ch[i].active)
			n += pl->ch[i].wave && pl->ch[i].wave->channels == 2 ? 2 : 1;
	return n;
}

static void track_unlink (player_t *pl, int ci)
{
	channel_t *c = pl->ch + ci;
	if (!c->attached)
		return;
	track_t *t = pl->tr + c->track;
	if (t->head == ci)
		t->head = c->next;
	else
		for (int i = t->head; i >= 0; i = pl->ch[i].next)
			if (pl->ch[i].next == ci)
			{
				pl->ch[i].next = c->next;
				break;
			}
	c->attached = false;
	c->next = -1;
}

static void channel_stop (player_t *pl, int ci)
{
	channel_t *c = pl->ch + ci;
	track_unlink (pl, ci);
	c->active = false;
	c->used = false;
}

static void channel_release (channel_t *c)
{
	if (c->env.status != ENV_RELEASE)
	{
		c->priority = 1;
		c->env.status = ENV_RELEASE;
	}
}

static int channel_alloc (player_t *pl, int voices, int priority)
{
	// free slot and enough DSP voices; otherwise drop the weakest (released first, then oldest)
	for (int pass = 0; pass < 4; pass++)
	{
		int slot = -1;
		for (int i = 0; i < MAX_CHANNELS && slot < 0; i++)
			if (!pl->ch[i].used)
				slot = i;
		if (slot >= 0 && voices_in_use (pl) + voices <= MAX_VOICES)
			return slot;
		int victim = -1;
		for (int i = 0; i < MAX_CHANNELS; i++)
		{
			const channel_t *c = pl->ch + i;
			if (!c->used)
				continue;
			if (victim < 0 || c->priority < pl->ch[victim].priority
				|| (c->priority == pl->ch[victim].priority && c->serial < pl->ch[victim].serial))
				victim = i;
		}
		if (victim < 0 || pl->ch[victim].priority > priority)
			return -1;
		channel_stop (pl, victim);
	}
	return -1;
}

static void channel_update (player_t *pl, int ci)
{
	channel_t *c = pl->ch + ci;
	if (!c->active)
		return;
	const float lfo = lfo_value (&c->lfo);
	const float volume = c->init_volume * c->user_volume;
	float ve_init = calc_volume_ratio (env_value (&c->env));
	if (c->lfo_target == LFO_VOLUME)
		ve_init *= calc_volume_ratio (lfo * 6.0f);
	if (c->env.status == ENV_RELEASE)
	{
		if (c->attached ? ve_init == 0.0f : volume * ve_init == 0.0f)
		{
			channel_stop (pl, ci);
			return;
		}
	}
	float cent = (float)(c->key - c->original_key);
	if (c->sweep_pitch != 0.0f && c->sweep_counter < c->sweep_length)
		cent += c->sweep_pitch * (c->sweep_length - c->sweep_counter) / c->sweep_length;
	cent += c->user_pitch;
	if (c->lfo_target == LFO_PITCH)
		cent += lfo;
	const float pitch = calc_pitch_ratio ((s32)(cent * 256)) * c->tune * c->user_pitch_ratio;
	float pan = c->init_pan + c->user_pan;
	if (c->lfo_target == LFO_PAN)
		pan += lfo;

	if (c->auto_sweep)
	{
		c->sweep_counter += FRAME_MS;
		if (c->sweep_counter > c->sweep_length)
			c->sweep_counter = c->sweep_length;
	}
	lfo_update (&c->lfo, FRAME_MS);
	env_update (&c->env, FRAME_MS);

	float ve_target = calc_volume_ratio (env_value (&c->env));
	if (c->lfo_target == LFO_VOLUME)
		ve_target *= calc_volume_ratio (lfo_value (&c->lfo) * 6.0f);

	c->volume = volume < 0 ? 0 : volume;
	c->ve_init = ve_init;
	c->ve_target = ve_target;
	c->pitch = pitch;
	c->pan = pan;

	// mix (stereo output mode)
	const float send = clampf (c->main_send + 1.0f, 0.0f, 1.0f);
	const pan_info_t pi = pan_info (c->pan_curve);
	const int nch = c->wave ? c->wave->channels : 1;
	for (int k = 0; k < nch; k++)
	{
		float l, r;
		if (nch > 1 && c->pan_mode == 1)
		{
			l = k == 0 ? calc_pan_ratio (pan, pi) : 0.0f;
			r = k == 1 ? calc_pan_ratio (0.0f - pan, pi) : 0.0f;
		}
		else
		{
			const float vp = nch == 2 ? (k == 0 ? -1.0f : 1.0f) : 0.0f;
			l = calc_pan_ratio (pan + vp, pi);
			r = calc_pan_ratio (0.0f - (pan + vp), pi);
		}
		c->prev_l[k] = c->mix_valid ? c->mix_l[k] : send * l;
		c->prev_r[k] = c->mix_valid ? c->mix_r[k] : send * r;
		c->mix_l[k] = send * l;
		c->mix_r[k] = send * r;
	}
	c->mix_valid = true;
}

///////////////////////////////////////////////////////////////////////////////
// tracks

static void track_init (track_t *t)
{
	memset (t, 0, sizeof (*t));
	t->cmp_flag = true;
	t->note_wait = true;
	lfo_param_init (&t->lfo);
	t->lfo_target = LFO_PITCH;
	mv_init (&t->volume, 127);
	mv_init (&t->pan, 0);
	mv_init (&t->pitch_bend, 0);
	t->volume2 = 127;
	t->velocity_range = 127;
	t->bend_range = 2;
	t->priority = 64;
	t->porta_key = 60;
	t->attack = t->decay = t->sustain = t->release = t->env_hold = INVALID_ENV;
	t->main_send = 127;
	for (int i = 0; i < 16; i++)
		t->var[i] = -1;
	t->head = -1;
}

static void track_update_channel_param (player_t *pl, track_t *t)
{
	if (!t->opened || t->head < 0)
		return;
	const float v = mv_value (&t->volume) / 127.0f, v2 = t->volume2 / 127.0f, mv = pl->main_volume / 127.0f;
	const float volume = v * v * v2 * v2 * mv * mv * pl->p->volume;
	const float pitch = (mv_value (&t->pitch_bend) / 128.0f) * t->bend_range;
	const float pan = clampf (mv_value (&t->pan) / 63.0f, -1.0f, 1.0f);
	const float main_send = t->main_send / 127.0f - 1.0f;
	for (int i = t->head; i >= 0; i = pl->ch[i].next)
	{
		channel_t *c = pl->ch + i;
		c->user_volume = volume;
		c->user_pitch = pitch;
		c->user_pitch_ratio = 1.0f;
		c->user_pan = pan;
		c->main_send = main_send;
		c->lfo.p = t->lfo;
		c->lfo_target = t->lfo_target;
	}
}

static void track_release_all (player_t *pl, track_t *t, int release)
{
	track_update_channel_param (pl, t);
	for (int i = t->head; i >= 0; i = pl->ch[i].next)
	{
		channel_t *c = pl->ch + i;
		if (!c->active)
			continue;
		if (release >= 0)
			env_set_release (&c->env, release);
		channel_release (c);
	}
}

static void track_free_all (player_t *pl, track_t *t)
{
	for (int i = t->head; i >= 0;)
	{
		const int n = pl->ch[i].next;
		pl->ch[i].attached = false;
		pl->ch[i].next = -1;
		i = n;
	}
	t->head = -1;
}

static void track_stop_all (player_t *pl, track_t *t)
{
	for (int i = t->head; i >= 0;)
	{
		const int n = pl->ch[i].next;
		pl->ch[i].attached = false;
		pl->ch[i].next = -1;
		channel_stop (pl, i);
		i = n;
	}
	t->head = -1;
}

static void track_close (player_t *pl, track_t *t)
{
	track_release_all (pl, t, -1);
	track_free_all (pl, t);
	t->opened = false;
}

static void track_open (track_t *t)
{
	t->note_finish_wait = false;
	t->call_depth = 0;
	t->wait = 0;
	t->opened = true;
}

static void note_on (player_t *pl, int ti, int key, int velocity, s32 length)
{
	track_t *t = pl->tr + ti;
	velocity = velocity * t->velocity_range / 127;
	int ci = -1;
	if (t->tie && t->head >= 0)
	{
		ci = t->head;
		channel_t *c = pl->ch + ci;
		c->key = (u8)key;
		const float iv = velocity / 127.0f;
		c->init_volume = iv * iv;
	}
	if (t->mono)
	{
		ci = t->head;
		if (ci >= 0)
		{
			channel_t *c = pl->ch + ci;
			if (c->env.status == ENV_RELEASE)
			{
				channel_stop (pl, ci);
				ci = -1;
			}
			else
			{
				c->key = (u8)key;
				const float iv = velocity / 127.0f;
				c->init_volume = iv * iv;
				c->length = length;
			}
		}
	}
	if (ci < 0)
	{
		nws_inst_t in;
		memset (&in, 0, sizeof (in));
		if (!pl->p->inst || !pl->p->inst (pl->p->ctx, t->bank, t->prg, key, velocity, &in) || !in.wave || !in.wave->length)
		{
			if (pl->debug)
				fprintf (stderr, "nwseq: track %d: no region for prg %d key %d vel %d\n", ti, t->prg, key, velocity);
			return;
		}
		const int prio = clampi (pl->priority + t->priority, 0, 255);
		ci = channel_alloc (pl, in.wave->channels == 2 ? 2 : 1, prio);
		if (ci < 0)
			return;
		channel_t *c = pl->ch + ci;
		memset (c, 0, sizeof (*c));
		c->used = c->active = true;
		c->serial = ++pl->serial;
		c->priority = prio;
		c->next = -1;
		c->init_volume = 1.0f;
		c->tune = 1.0f;
		c->user_volume = 1.0f;
		c->user_pitch_ratio = 1.0f;
		c->auto_sweep = true;
		env_init (&c->env);
		lfo_param_init (&c->lfo.p);
		c->lfo_target = LFO_PITCH;

		// Bank::NoteOn
		c->key = (u8)key;
		c->original_key = in.original_key;
		float iv = velocity / 127.0f;
		iv *= iv;
		iv *= in.volume / 127.0f;
		c->init_volume = iv;
		c->tune = in.tune > 0 ? in.tune : 1.0f;
		env_set_attack (&c->env, in.attack);
		env_set_hold (&c->env, in.hold);
		env_set_decay (&c->env, in.decay);
		env_set_sustain (&c->env, in.sustain);
		env_set_release (&c->env, in.release);
		c->init_pan = (in.pan - 64) / 63.0f + t->init_pan / 63.0f;
		c->alternate_assign = in.alternate_assign;
		c->release_ignore = in.release_ignore;
		c->wave = in.wave;
		c->pos = 0;
		// Channel::Start
		c->length = t->tie ? -1 : length;
		c->lfo.counter = 0;
		c->lfo.delay_counter = 0;
		env_reset (&c->env);
		c->sweep_counter = 0;

		if (c->alternate_assign > 0)
			for (int i = t->head; i >= 0; i = pl->ch[i].next)
				if (pl->ch[i].alternate_assign == c->alternate_assign)
					channel_release (pl->ch + i);
		c->attached = true;
		c->track = ti;
		c->next = t->head;
		t->head = ci;
	}
	channel_t *c = pl->ch + ci;
	if (t->attack != INVALID_ENV)
		env_set_attack (&c->env, t->attack);
	if (t->decay != INVALID_ENV)
		env_set_decay (&c->env, t->decay);
	if (t->sustain != INVALID_ENV)
		env_set_sustain (&c->env, t->sustain);
	if (t->release != INVALID_ENV)
		env_set_release (&c->env, t->release);
	if (t->env_hold != INVALID_ENV)
		env_set_hold (&c->env, t->env_hold);
	float sweep = t->sweep_pitch;
	if (t->porta)
		sweep += t->porta_key - key;
	if (t->porta_time == 0)
	{
		c->sweep_pitch = sweep;
		c->sweep_length = length;
		c->auto_sweep = false;
	}
	else
	{
		int len = t->porta_time;
		len *= len;
		len = (int)(len * (sweep >= 0 ? sweep : -sweep));
		len >>= 5;
		len *= 5;
		c->sweep_pitch = sweep;
		c->sweep_length = len;
		c->auto_sweep = true;
	}
	c->sweep_counter = 0;
	t->porta_key = (u8)key;
	c->pan_mode = pl->p->pan_mode;
	c->pan_curve = pl->p->pan_curve;
}

enum
{
	MML_WAIT = 0x80, MML_PRG = 0x81, MML_OPEN_TRACK = 0x88, MML_JUMP = 0x89, MML_CALL = 0x8a,
	MML_RANDOM = 0xa0, MML_VARIABLE = 0xa1, MML_IF = 0xa2, MML_TIME = 0xa3, MML_TIME_RANDOM = 0xa4,
	MML_TIME_VARIABLE = 0xa5,
	MML_TIMEBASE = 0xb0, MML_ENV_HOLD = 0xb1, MML_MONOPHONIC = 0xb2, MML_VELOCITY_RANGE = 0xb3,
	MML_PAN = 0xc0, MML_VOLUME = 0xc1, MML_MAIN_VOLUME = 0xc2, MML_TRANSPOSE = 0xc3, MML_PITCH_BEND = 0xc4,
	MML_BEND_RANGE = 0xc5, MML_PRIO = 0xc6, MML_NOTE_WAIT = 0xc7, MML_TIE = 0xc8, MML_PORTA = 0xc9,
	MML_MOD_DEPTH = 0xca, MML_MOD_SPEED = 0xcb, MML_MOD_TYPE = 0xcc, MML_MOD_RANGE = 0xcd, MML_PORTA_SW = 0xce,
	MML_PORTA_TIME = 0xcf, MML_ATTACK = 0xd0, MML_DECAY = 0xd1, MML_SUSTAIN = 0xd2, MML_RELEASE = 0xd3,
	MML_LOOP_START = 0xd4, MML_VOLUME2 = 0xd5, MML_MAINSEND = 0xdb, MML_INIT_PAN = 0xdc, MML_MUTE = 0xdd,
	MML_DAMPER = 0xdf,
	MML_MOD_DELAY = 0xe0, MML_TEMPO = 0xe1, MML_SWEEP_PITCH = 0xe3,
	MML_EX_COMMAND = 0xf0, MML_ENV_RESET = 0xfb, MML_LOOP_END = 0xfc, MML_RET = 0xfd, MML_ALLOC_TRACK = 0xfe,
	MML_FIN = 0xff
};

static void command (player_t *pl, int ti, u32 cmd, s32 a1, s32 a2)
{
	track_t *t = pl->tr + ti;
	if (cmd <= 0xff)
	{
		switch (cmd)
		{
		case MML_TEMPO: pl->tempo = clampi (a1, 0, 1023); break;
		case MML_TIMEBASE: pl->timebase = (u8)a1; break;
		case MML_PRG: if (a1 < 0x10000) t->prg = a1; break;
		case MML_MUTE:
			switch (a1)
			{
			case 0: t->mute = false; break;
			case 1: t->mute = true; break; // no stop
			case 2: track_release_all (pl, t, -1); track_free_all (pl, t); t->mute = true; break;
			case 3: track_stop_all (pl, t); t->mute = true; break;
			}
			break;
		case MML_VOLUME: mv_set (&t->volume, (u8)a1, (s16)a2); break;
		case MML_VOLUME2: t->volume2 = (u8)a1; break;
		case MML_VELOCITY_RANGE: t->velocity_range = (u8)a1; break;
		case MML_MAIN_VOLUME: pl->main_volume = (u8)a1; break;
		case MML_TRANSPOSE: t->transpose = (s8)a1; break;
		case MML_PITCH_BEND: mv_set (&t->pitch_bend, (s8)a1, (s16)a2); break;
		case MML_BEND_RANGE: t->bend_range = (u8)a1; break;
		case MML_PAN: mv_set (&t->pan, (s8)(a1 - 64), (s16)a2); break;
		case MML_INIT_PAN: t->init_pan = (s8)(a1 - 64); break;
		case MML_PRIO: t->priority = (u8)a1; break;
		case MML_NOTE_WAIT: t->note_wait = a1 != 0; break;
		case MML_PORTA_TIME: t->porta_time = (u8)a1; break;
		case MML_MOD_DEPTH: t->lfo.depth = (u8)a1 / 128.0f; break;
		case MML_MOD_SPEED: t->lfo.speed = (u8)a1 * 0.390625f; break;
		case MML_MOD_TYPE: t->lfo_target = (u8)a1; break;
		case MML_MOD_RANGE: t->lfo.range = (u8)a1; break;
		case MML_MOD_DELAY: t->lfo.delay = (u32)(a1 * 5); break;
		case MML_SWEEP_PITCH: t->sweep_pitch = a1 / 64.0f; break;
		case MML_ATTACK: t->attack = (u8)a1; break;
		case MML_DECAY: t->decay = (u8)a1; break;
		case MML_SUSTAIN: t->sustain = (u8)a1; break;
		case MML_RELEASE: t->release = (u8)a1; break;
		case MML_ENV_HOLD: t->env_hold = (u8)a1; break;
		case MML_ENV_RESET: t->attack = t->decay = t->sustain = t->release = t->env_hold = INVALID_ENV; break;
		case MML_DAMPER: t->damper = (u8)a1 >= 64; break;
		case MML_TIE:
			t->tie = a1 != 0;
			track_release_all (pl, t, -1);
			track_free_all (pl, t);
			break;
		case MML_MONOPHONIC:
			t->mono = a1 != 0;
			if (t->mono)
			{
				track_release_all (pl, t, -1);
				track_free_all (pl, t);
			}
			break;
		case MML_PORTA: t->porta_key = (u8)(a1 + t->transpose); t->porta = true; break;
		case MML_PORTA_SW: t->porta = a1 != 0; break;
		case MML_MAINSEND: t->main_send = (u8)a1; break;
		case 0xb6: t->bank = clampi ((s8)a1, 0, 3); break; // bank select (CSEQ/FSEQ)
		case 0xbd: t->lfo.phase = (s8)a1 / 128.0f; if (t->lfo.phase < 0) t->lfo.phase += 1.0f; break; // mod phase
		case 0xbe: t->lfo.curve = (u8)a1; break; // mod curve
		case 0xe4: if ((u16)a1) t->lfo.speed = 1000.0f / (u16)a1; break; // mod period (ms)
		case MML_OPEN_TRACK:
		{
			if (a1 < 0 || a1 >= TRACK_NUM || !pl->tr[a1].allocated || a1 == ti)
				break;
			track_t *nt = pl->tr + a1;
			track_close (pl, nt);
			nt->cur = (u32)a2;
			track_open (nt);
			break;
		}
		case MML_JUMP:
			if ((u32)a1 <= t->cur)
			{
				t->loops++;
				pl->looped = true;
			}
			t->cur = (u32)a1;
			break;
		case MML_CALL:
			if (t->call_depth >= CALL_DEPTH)
				break;
			t->calls[t->call_depth].addr = t->cur;
			t->calls[t->call_depth].loop_flag = false;
			t->call_depth++;
			t->cur = (u32)a1;
			break;
		case MML_RET:
		{
			call_t *cs = 0;
			while (t->call_depth > 0)
			{
				t->call_depth--;
				if (!t->calls[t->call_depth].loop_flag)
				{
					cs = t->calls + t->call_depth;
					break;
				}
			}
			if (cs)
				t->cur = cs->addr;
			break;
		}
		case MML_LOOP_START:
			if (t->call_depth >= CALL_DEPTH)
				break;
			t->calls[t->call_depth].addr = t->cur;
			t->calls[t->call_depth].loop_count = (u8)a1;
			t->calls[t->call_depth].loop_flag = true;
			t->call_depth++;
			break;
		case MML_LOOP_END:
		{
			if (t->call_depth == 0)
				break;
			call_t *cs = t->calls + t->call_depth - 1;
			if (!cs->loop_flag)
				break;
			u8 n = cs->loop_count;
			if (n > 0)
			{
				n--;
				if (n == 0)
				{
					t->call_depth--;
					break;
				}
			}
			else
			{
				t->loops++; // endless loop: one pass
				pl->looped = true;
			}
			cs->loop_count = n;
			t->cur = cs->addr;
			break;
		}
		}
		return;
	}
	if (cmd <= 0xffff)
	{
		const u32 ex = cmd & 0xff;
		s16 *v = 0;
		if ((ex & 0xf0) == 0x80 || (ex & 0xf0) == 0x90)
		{
			v = var_ptr (pl, t, a1);
			if (!v)
				return;
		}
		switch (ex)
		{
		case 0x80: *v = (s16)a2; break;
		case 0x81: *v += a2; break;
		case 0x82: *v -= a2; break;
		case 0x83: *v *= a2; break;
		case 0x84: if (a2) *v /= a2; break;
		case 0x85: if (a2 >= 0) *v <<= a2; else *v >>= -a2; break;
		case 0x86:
		{
			bool neg = false;
			if (a2 < 0)
			{
				neg = true;
				a2 = (s16)-a2;
			}
			s32 r = calc_random (pl);
			r *= a2 + 1;
			r >>= 16;
			*v = (s16)(neg ? -r : r);
			break;
		}
		case 0x87: *v &= a2; break;
		case 0x88: *v |= a2; break;
		case 0x89: *v ^= a2; break;
		case 0x8a: *v = (s16)~(u16)a2; break;
		case 0x8b: if (a2) *v %= a2; break;
		case 0x90: t->cmp_flag = *v == a2; break;
		case 0x91: t->cmp_flag = *v >= a2; break;
		case 0x92: t->cmp_flag = *v > a2; break;
		case 0x93: t->cmp_flag = *v <= a2; break;
		case 0x94: t->cmp_flag = *v < a2; break;
		case 0x95: t->cmp_flag = *v != a2; break;
		}
	}
}

// returns false on "fin"
static bool parse (player_t *pl, int ti)
{
	track_t *t = pl->tr + ti;
	int arg_type = ARG_NONE, arg_type2 = ARG_NONE;
	bool use_arg = false, exec = true;
	u32 cmd = rd8 (pl, t);
	if (cmd == MML_IF)
	{
		cmd = rd8 (pl, t);
		exec = t->cmp_flag;
	}
	if (cmd == MML_TIME)
	{
		cmd = rd8 (pl, t);
		arg_type2 = ARG_S16;
	}
	else if (cmd == MML_TIME_RANDOM)
	{
		cmd = rd8 (pl, t);
		arg_type2 = ARG_RANDOM;
	}
	else if (cmd == MML_TIME_VARIABLE)
	{
		cmd = rd8 (pl, t);
		arg_type2 = ARG_VARIABLE;
	}
	if (cmd == MML_RANDOM)
	{
		cmd = rd8 (pl, t);
		arg_type = ARG_RANDOM;
		use_arg = true;
	}
	else if (cmd == MML_VARIABLE)
	{
		cmd = rd8 (pl, t);
		arg_type = ARG_VARIABLE;
		use_arg = true;
	}

	if (!(cmd & 0x80))
	{
		const u8 vel = rd8 (pl, t);
		const s32 len = read_arg (pl, t, use_arg ? arg_type : ARG_VMIDI);
		int key = (int)cmd + t->transpose;
		if (!exec)
			return true;
		key = clampi (key, 0, 127);
		if (!t->mute)
			note_on (pl, ti, key, vel, len > 0 ? len : -1);
		if (t->note_wait)
		{
			t->wait = len;
			if (len == 0)
				t->note_finish_wait = true;
		}
		return true;
	}

	s32 a1 = 0, a2 = 0;
	switch (cmd & 0xf0)
	{
	case 0x80:
		switch (cmd)
		{
		case MML_WAIT:
		{
			const s32 a = read_arg (pl, t, use_arg ? arg_type : ARG_VMIDI);
			if (exec)
				t->wait = a;
			break;
		}
		case MML_PRG:
			a1 = read_arg (pl, t, use_arg ? arg_type : ARG_VMIDI);
			if (exec)
				command (pl, ti, cmd, a1, a2);
			break;
		case MML_OPEN_TRACK:
		{
			const u8 no = rd8 (pl, t);
			const u32 off = rd24 (pl, t);
			if (exec)
				command (pl, ti, cmd, no, (s32)off);
			break;
		}
		case MML_JUMP:
		case MML_CALL:
		{
			const u32 off = rd24 (pl, t);
			if (exec)
				command (pl, ti, cmd, (s32)off, 0);
			break;
		}
		}
		break;
	case 0xb0:
	case 0xc0:
	case 0xd0:
	{
		const u8 a = (u8)read_arg (pl, t, use_arg ? arg_type : ARG_U8);
		if (arg_type2 != ARG_NONE)
			a2 = read_arg (pl, t, arg_type2);
		if (exec)
		{
			a1 = cmd == MML_TRANSPOSE || cmd == MML_PITCH_BEND ? (s8)a : a;
			command (pl, ti, cmd, a1, a2);
		}
		break;
	}
	case 0x90:
		if (exec)
			command (pl, ti, cmd, a1, a2);
		break;
	case 0xe0:
		a1 = (s16)read_arg (pl, t, use_arg ? arg_type : ARG_S16);
		if (exec)
			command (pl, ti, cmd, a1, a2);
		break;
	case 0xf0:
		switch (cmd)
		{
		case MML_ALLOC_TRACK:
			rd16 (pl, t);
			break;
		case MML_FIN:
			if (exec)
				return false;
			break;
		case MML_EX_COMMAND:
		{
			const u32 ex = rd8 (pl, t);
			switch (ex & 0xf0)
			{
			case 0xe0:
				a1 = (u16)read_arg (pl, t, use_arg ? arg_type : ARG_S16);
				if (exec)
					command (pl, ti, (cmd << 8) + ex, a1, a2);
				break;
			case 0x80:
			case 0x90:
				a1 = rd8 (pl, t);
				a2 = (s16)read_arg (pl, t, use_arg ? arg_type : ARG_S16);
				if (exec)
					command (pl, ti, (cmd << 8) + ex, a1, a2);
				break;
			case 0xa0:
			case 0xb0:
				// CSEQ/FSEQ modulators 2..4 (curve/phase/depth/speed/type/range): u8, not modelled
				read_arg (pl, t, use_arg ? arg_type : ARG_U8);
				break;
			}
			break;
		}
		default:
			if (exec)
				command (pl, ti, cmd, a1, a2);
			break;
		}
		break;
	default:
		break; // 0xa0 prefix without command: ignored
	}
	return true;
}

// SeqTrack::ParseNextTick; returns -1 on fin
static int track_parse_tick (player_t *pl, int ti)
{
	track_t *t = pl->tr + ti;
	if (!t->opened)
		return 0;
	mv_update (&t->volume);
	mv_update (&t->pan);
	mv_update (&t->pitch_bend);
	if (t->note_finish_wait)
	{
		if (t->head >= 0)
			return 1;
		t->note_finish_wait = false;
	}
	if (t->wait > 0)
	{
		t->wait--;
		if (t->wait > 0)
			return 1;
	}
	for (int guard = 0; t->wait == 0 && !t->note_finish_wait; guard++)
	{
		if (guard > 100000)
			return -1; // command loop without waits
		if (!parse (pl, ti))
			return -1;
	}
	return 1;
}

static void track_update_length (player_t *pl, track_t *t)
{
	if (!t->opened)
		return;
	for (int i = t->head; i >= 0;)
	{
		channel_t *c = pl->ch + i;
		const int next = c->next;
		if (c->length > 0)
			c->length--;
		if (c->length == 0 && c->env.status != ENV_RELEASE && !t->damper && !c->release_ignore)
			channel_release (c);
		if (!c->auto_sweep)
		{
			c->sweep_counter++;
			if (c->sweep_counter > c->sweep_length)
				c->sweep_counter = c->sweep_length;
		}
		i = next;
	}
}

static void player_finish (player_t *pl)
{
	for (int i = 0; i < TRACK_NUM; i++)
		if (pl->tr[i].allocated)
		{
			track_close (pl, pl->tr + i);
			pl->tr[i].allocated = false;
		}
	pl->active = false;
}

// SeqPlayer::ParseNextTick: returns true when no track is open any more
static bool player_parse_tick (player_t *pl)
{
	bool any = false;
	for (int i = 0; i < TRACK_NUM; i++)
	{
		track_t *t = pl->tr + i;
		if (!t->allocated)
			continue;
		track_update_length (pl, t);
		if (track_parse_tick (pl, i) < 0)
		{
			track_close (pl, t);
			t->allocated = false;
		}
		if (t->allocated && t->opened)
			any = true;
	}
	return !any;
}

static void player_update (player_t *pl)
{
	if (!pl->active)
		return;
	float tpm = pl->timebase * pl->tempo / (60 * 1000.0f);
	if (tpm > 0.0f)
	{
		float rest = (float)FRAME_MS;
		float next = pl->tick_fraction / tpm;
		while (next < rest)
		{
			rest -= next;
			if (player_parse_tick (pl))
			{
				player_finish (pl);
				return;
			}
			tpm = pl->timebase * pl->tempo / (60 * 1000.0f);
			if (tpm == 0.0f)
				break;
			next = 1.0f / tpm;
		}
		if (tpm != 0.0f)
		{
			next -= rest;
			pl->tick_fraction = next * tpm;
		}
	}
	for (int i = 0; i < TRACK_NUM; i++)
		if (pl->tr[i].allocated)
			track_update_channel_param (pl, pl->tr + i);
}

///////////////////////////////////////////////////////////////////////////////
// voice rendering

static inline float wave_at (const nws_wave_t *w, int ch, int64_t i)
{
	if (i < 0)
		return 0.0f;
	if (i >= (int64_t)w->length)
	{
		if (!w->loop || w->loop_start >= w->length)
			return 0.0f;
		i = w->loop_start + (i - w->loop_start) % (w->length - w->loop_start);
	}
	return w->pcm[ch][i];
}

static void render_frame (player_t *pl, float *mix)
{
	for (int ci = 0; ci < MAX_CHANNELS; ci++)
	{
		channel_t *c = pl->ch + ci;
		if (!c->active || !c->wave)
			continue;
		const nws_wave_t *w = c->wave;
		const double step = (double)c->pitch * w->rate / NWS_RATE;
		const float v0 = clampf (c->volume * c->ve_init, 0.0f, 2.0f);
		const float v1 = clampf (c->volume * c->ve_target, 0.0f, 2.0f);
		const int nch = w->channels == 2 ? 2 : 1;
		double pos = c->pos;
		bool ended = false;
		for (int s = 0; s < FRAME_SAMPLES; s++)
		{
			if (!w->loop && pos >= w->length)
			{
				ended = true;
				break;
			}
			const float f = (float)s / FRAME_SAMPLES;
			const float ve = v0 + (v1 - v0) * f;
			const int64_t i = (int64_t)pos;
			const float x = (float)(pos - (double)i);
			for (int k = 0; k < nch; k++)
			{
				// 4 point, 3rd order Hermite
				const float ym1 = wave_at (w, k, i - 1), y0 = wave_at (w, k, i), y1 = wave_at (w, k, i + 1),
				            y2 = wave_at (w, k, i + 2);
				const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2,
				            c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
				const float smp = ((c3 * x + c2) * x + c1) * x + y0;
				const float l = c->prev_l[k] + (c->mix_l[k] - c->prev_l[k]) * f;
				const float r = c->prev_r[k] + (c->mix_r[k] - c->prev_r[k]) * f;
				mix[2 * s] += smp * ve * l;
				mix[2 * s + 1] += smp * ve * r;
			}
			pos += step;
			if (w->loop && pos >= w->length)
			{
				const double ll = (double)w->length - w->loop_start;
				if (ll <= 0)
				{
					ended = true;
					break;
				}
				while (pos >= w->length)
					pos -= ll;
			}
		}
		c->pos = pos;
		if (ended)
			channel_stop (pl, ci); // end of a one-shot wave
	}
}

///////////////////////////////////////////////////////////////////////////////

static int render_pass (const nws_params_t *p, float gain, int16_t **out, size_t *frames, int *looped, float *peak);

int NWSeqRender (const nws_params_t *p, int16_t **out, size_t *frames, int *looped)
{
	// rendering is deterministic: when the mix would clip, render again with just enough headroom
	float peak = 0.0f;
	int rc = render_pass (p, 1.0f, out, frames, looped, &peak);
	if (!rc && peak > 32767.0f)
	{
		NWSeqFree (*out);
		rc = render_pass (p, 32440.0f / peak, out, frames, looped, &peak);
	}
	return rc;
}

static int render_pass (const nws_params_t *p, float gain, int16_t **out, size_t *frames, int *looped, float *peak)
{
	*out = 0;
	*frames = 0;
	if (looped)
		*looped = 0;
	if (!p || !p->seq || p->start >= p->seq_size)
		return -1;
	init_tables ();
	player_t *pl = calloc (1, sizeof (*pl));
	if (!pl)
		return -1;
	pl->p = p;
	pl->seq = p->seq;
	pl->seq_size = p->seq_size;
	pl->tempo = 120;
	pl->timebase = 48;
	pl->main_volume = 127;
	pl->priority = 64;
	pl->rand_state = 0x12345678;
	pl->debug = getenv ("NWSEQ_DEBUG") != 0;
	for (int i = 0; i < 16; i++)
		pl->local_var[i] = pl->global_var[i] = -1;
	for (int i = 0; i < MAX_CHANNELS; i++)
		pl->ch[i].next = -1;

	u32 start = p->start, alloc = p->alloc_track;
	if (pl->seq[start] == MML_ALLOC_TRACK && start + 3 <= p->seq_size)
	{
		const u32 a = (u32)pl->seq[start + 1] << 8 | pl->seq[start + 2];
		if (!alloc)
			alloc = a;
		start += 3;
	}
	if (!alloc)
		alloc = 1;
	for (int i = 0; i < TRACK_NUM; i++)
	{
		track_init (pl->tr + i);
		pl->tr[i].allocated = (alloc >> i & 1) != 0;
	}
	pl->tr[0].allocated = true;
	pl->tr[0].cur = start;
	track_open (pl->tr);
	pl->active = true;

	const int loops = p->loops > 0 ? p->loops : 2;
	const double fade_sec = p->fade_sec > 0 ? p->fade_sec : 10.0;
	const double max_sec = p->max_sec > 0 ? p->max_sec : 900.0;
	const double tail_sec = p->tail_sec > 0 ? p->tail_sec : 15.0;
	const size_t max_frames = (size_t)(max_sec * 1000 / FRAME_MS);
	size_t cap = 0, n = 0; // samples (stereo frames)
	int16_t *buf = 0;
	float mix[FRAME_SAMPLES * 2];
	long fade_total = 0, fade_left = -1;
	size_t tail_frames = 0;
	int rc = 0;

	for (size_t f = 0; f < max_frames; f++)
	{
		player_update (pl);
		for (int ci = 0; ci < MAX_CHANNELS; ci++)
			channel_update (pl, ci);

		bool any_ch = false;
		for (int ci = 0; ci < MAX_CHANNELS && !any_ch; ci++)
			any_ch = pl->ch[ci].active;
		if (!pl->active && !any_ch)
			break;
		if (!pl->active)
		{
			// release tail; stop runaway sustains (looping waves with ignored note-off)
			if (++tail_frames * FRAME_MS > tail_sec * 1000 && fade_left < 0)
			{
				fade_total = fade_left = (long)(1.0 * NWS_RATE);
			}
		}

		// loop detection: every track that loops has completed "loops" passes
		if (fade_left < 0 && pl->active)
		{
			int mn = -1;
			for (int i = 0; i < TRACK_NUM; i++)
				if (pl->tr[i].allocated && pl->tr[i].opened && pl->tr[i].loops > 0)
					if (mn < 0 || pl->tr[i].loops < mn)
						mn = pl->tr[i].loops;
			if (mn >= loops)
				fade_total = fade_left = (long)(fade_sec * NWS_RATE);
		}
		if (fade_left < 0 && f + (size_t)(fade_sec * 1000 / FRAME_MS) >= max_frames)
			fade_total = fade_left = (long)(fade_sec * NWS_RATE);

		memset (mix, 0, sizeof (mix));
		render_frame (pl, mix);

		if (n + FRAME_SAMPLES > cap)
		{
			cap = cap ? cap * 2 : (size_t)NWS_RATE * 60;
			int16_t *nb = realloc (buf, cap * 2 * sizeof (int16_t));
			if (!nb)
			{
				rc = -1;
				break;
			}
			buf = nb;
		}
		bool done = false;
		for (int s = 0; s < FRAME_SAMPLES; s++)
		{
			float g = 1.0f;
			if (fade_left >= 0)
			{
				g = fade_total ? (float)fade_left / fade_total : 0.0f;
				if (fade_left == 0)
				{
					done = true;
					break;
				}
				fade_left--;
			}
			for (int k = 0; k < 2; k++)
			{
				float v = mix[2 * s + k] * g * gain;
				if (fabsf (v) > *peak)
					*peak = fabsf (v);
				v = v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v;
				buf[2 * n + k] = (int16_t)lrintf (v);
			}
			n++;
		}
		if (done)
			break;
	}
	if (looped)
		*looped = pl->looped;
	if (pl->debug)
		fprintf (stderr, "nwseq: %zu samples (%.2f s), %s\n", n, (double)n / NWS_RATE, pl->looped ? "looped" : "one-shot");
	free (pl);
	if (rc)
	{
		free (buf);
		return rc;
	}
	*out = buf;
	*frames = n;
	return 0;
}

void NWSeqFree (void *p) { free (p); }

int NWSeqWriteWav (const char *path, const int16_t *pcm, size_t frames, unsigned rate)
{
	FILE *fp = fopen (path, "wb");
	if (!fp)
		return -1;
	const u32 data = (u32)(frames * 4);
	u8 h[44];
	memcpy (h, "RIFF", 4);
	const u32 riff = 36 + data;
#define LE32(p, v) ((p)[0] = (u8)(v), (p)[1] = (u8)((v) >> 8), (p)[2] = (u8)((v) >> 16), (p)[3] = (u8)((v) >> 24))
#define LE16(p, v) ((p)[0] = (u8)(v), (p)[1] = (u8)((v) >> 8))
	LE32 (h + 4, riff);
	memcpy (h + 8, "WAVEfmt ", 8);
	LE32 (h + 16, 16);
	LE16 (h + 20, 1);
	LE16 (h + 22, 2);
	LE32 (h + 24, rate);
	LE32 (h + 28, rate * 4);
	LE16 (h + 32, 4);
	LE16 (h + 34, 16);
	memcpy (h + 36, "data", 4);
	LE32 (h + 40, data);
	bool ok = fwrite (h, 1, 44, fp) == 44;
	for (size_t i = 0; ok && i < frames * 2; i += 4096)
	{
		u8 tmp[4096 * 2];
		const size_t m = frames * 2 - i < 4096 ? frames * 2 - i : 4096;
		for (size_t k = 0; k < m; k++)
			LE16 (tmp + 2 * k, (u16)pcm[i + k]);
		ok = fwrite (tmp, 2, m, fp) == m;
	}
#undef LE32
#undef LE16
	if (fclose (fp))
		ok = false;
	return ok ? 0 : -1;
}
