#include "lib-mio.h"
#include "lib-image.h"
#include <string.h>
#include <ctype.h>

// Fixed 16-color WarioWare DIY palette in RGBA8
static const u32 mio_palette[16] = {
	0x00000000, // 0: Transparent
	0x000000FF, // 1: Black
	0xFFDF9CFF, // 2: Flesh / Light Peach
	0xFFAE31FF, // 3: Orange
	0xFF4900FF, // 4: Brown
	0xFF0000FF, // 5: Red
	0xCE69EFFF, // 6: Purple
	0x10C7CEFF, // 7: Light Blue
	0x2969C6FF, // 8: Dark Blue
	0x089652FF, // 9: Dark Green
	0x73D739FF, // 10: Light Green
	0xFFFF5AFF, // 11: Yellow
	0x808080FF, // 12: Dark Grey
	0xC0C0C0FF, // 13: Light Grey
	0xFFFFFFFF, // 14: White
	0x4A9CADFF  // 15: Teal / Hidden Color
};

// General MIDI Instrument mapping for WarioWare DIY 48 instruments
static const u8 mio_gm_instruments[48] = {
	0,  18,  6, 22, 73, 56, 65, 75,
	24, 29, 106, 33, 40, 13, 11, 47,
	72, 78, 17, 38, 77, 59, 126, 124,
	60, 61, 62, 123, 66, 125, 68, 122,
	53, 54, 52, 49, 67, 121, 119, 48,
	83, 84, 85, 86, 87, 88, 89, 90
};

// General MIDI Drum note mapping for DIY drum sounds (0..7)
static const u8 mio_gm_drums[8] = {
	36, // 0: Bass Drum 1
	38, // 1: Acoustic Snare
	42, // 2: Closed Hi-Hat
	46, // 3: Open Hi-Hat
	45, // 4: Low Tom
	48, // 5: High-Mid Tom
	49, // 6: Crash Cymbal
	51  // 7: Ride Cymbal
};

bool IsMIO (const u8 *data, size_t size)
{
	if (!data)
		return false;
	if (size != 65536 && size != 14336 && size != 8192)
		return false;
	if (memcmp (data + 8, "DSMIO_S\0", 8) != 0)
		return false;
	return true;
}

mio_type_t GetMIOType (const u8 *data, size_t size)
{
	if (!IsMIO (data, size))
		return MIO_TYPE_UNKNOWN;
	if (size == 65536)
		return MIO_TYPE_GAME;
	if (size == 14336)
		return MIO_TYPE_COMIC;
	if (size == 8192)
		return MIO_TYPE_RECORD;
	return MIO_TYPE_UNKNOWN;
}

static void sanitize_string (char *dst, const u8 *src, size_t max_len)
{
	size_t i = 0;
	while (i < max_len && src[i] != 0)
	{
		unsigned char c = src[i];
		dst[i] = (c >= 32 && c < 127) ? c : ' ';
		i++;
	}
	while (i > 0 && dst[i - 1] == ' ')
		i--;
	dst[i] = 0;
}

void ReadMIOMetadata (const u8 *data, size_t size, mio_meta_t *meta)
{
	if (!meta)
		return;
	memset (meta, 0, sizeof (*meta));
	if (!data || size < 0x100)
		return;

	// UTF-8 metadata strings in header:
	// Name: 0x1C (max 25)
	// Brand: 0x35 (max 19)
	// Creator: 0x48 (max 19)
	// Description: 0x5B (max 71)
	sanitize_string (meta->name, data + 0x1C, 25);
	sanitize_string (meta->brand, data + 0x35, 19);
	sanitize_string (meta->creator, data + 0x48, 19);
	sanitize_string (meta->desc, data + 0x5B, 71);
}

// ----------------------------------------------------------------------------
// Comic Panel Decoder (192 x 128, 1-bit monochrome)
// ----------------------------------------------------------------------------
u8 *DecodeMIOComicPanel (const u8 *data, size_t size, uint panel_idx, uint *out_w, uint *out_h)
{
	if (!data || size < 14336 || panel_idx >= 4)
		return 0;

	// Check if panel is enabled
	if (data[0x3100 + panel_idx] == 0)
		return 0;

	const uint w = 192;
	const uint h = 128;
	if (out_w)
		*out_w = w;
	if (out_h)
		*out_h = h;

	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
		return 0;

	const size_t panel_offset = 0x100 + panel_idx * 0xC00;
	if (panel_offset + 0xC00 > size)
	{
		FREE (rgba);
		return 0;
	}

	for (uint y = 0; y < h; y++)
	{
		for (uint x = 0; x < w; x++)
		{
			const size_t byte_idx = panel_offset + (y / 8) * 192 + (x / 8) * 8 + (y % 8);
			const u8 b = data[byte_idx];
			const uint bit = (b >> (7 - (x % 8))) & 1;
			// bit 1 = black, bit 0 = white
			const u32 color = bit ? 0x000000FF : 0xFFFFFFFF;
			const size_t out_px = (y * w + x) * 4;
			wr_be32 (rgba + out_px, color);
		}
	}

	return rgba;
}

// ----------------------------------------------------------------------------
// Game Background Decoder (192 x 128, 4bpp indexed)
// ----------------------------------------------------------------------------
u8 *DecodeMIOGameBG (const u8 *data, size_t size, uint *out_w, uint *out_h)
{
	if (!data || size < 65536)
		return 0;

	const uint w = 192;
	const uint h = 128;
	if (out_w)
		*out_w = w;
	if (out_h)
		*out_h = h;

	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
		return 0;

	// Background is at 0x100 .. 0x3100
	const size_t bg_offset = 0x100;
	for (uint y = 0; y < h; y++)
	{
		for (uint x = 0; x < w; x++)
		{
			const size_t byte_idx = bg_offset + (y / 8) * 0x300 + (x / 8) * 0x20 + (y % 8) * 4 + (x % 8) / 2;
			const u8 b = data[byte_idx];
			const uint c_idx = (x % 2 != 0) ? ((b >> 4) & 0x0F) : (b & 0x0F);
			const u32 color = mio_palette[c_idx];
			const size_t out_px = (y * w + x) * 4;
			wr_be32 (rgba + out_px, color);
		}
	}

	return rgba;
}

// ----------------------------------------------------------------------------
// Game Sprite Decoder (16x16, 32x32, 48x48, 64x64, 4bpp indexed)
// ----------------------------------------------------------------------------
u8 *DecodeMIOGameSprite (const u8 *data, size_t size, uint obj_idx, uint frame_idx, uint *out_w, uint *out_h)
{
	if (!data || size < 65536 || obj_idx >= 15)
		return 0;

	const size_t obj_off = 0xB104 + obj_idx * 0x88;
	const u8 flags = data[obj_off];
	if (flags == 0)
		return 0;

	const uint size_code = flags & 3;
	const uint dim = (size_code + 1) * 16;
	if (out_w)
		*out_w = dim;
	if (out_h)
		*out_h = dim;

	const size_t frame_off = 0x3104 + (size_t)frame_idx * 128;
	const size_t num_blocks = (size_code + 1) * (size_code + 1);
	if (frame_off + num_blocks * 128 > size)
		return 0;

	u8 *rgba = MALLOC (dim * dim * 4);
	if (!rgba)
		return 0;

	const uint tiles_x = dim / 8;
	for (uint sy = 0; sy < dim; sy++)
	{
		for (uint sx = 0; sx < dim; sx++)
		{
			const uint tile_x = sx / 8;
			const uint tile_y = sy / 8;
			const uint tile_idx = tile_y * tiles_x + tile_x;
			const size_t byte_idx = frame_off + tile_idx * 32 + (sy % 8) * 4 + (sx % 8) / 2;
			const u8 b = data[byte_idx];
			const uint c_idx = (sx % 2 != 0) ? ((b >> 4) & 0x0F) : (b & 0x0F);
			const u32 color = mio_palette[c_idx];
			const size_t out_px = (sy * dim + sx) * 4;
			wr_be32 (rgba + out_px, color);
		}
	}

	return rgba;
}

// ----------------------------------------------------------------------------
// Record to Standard MIDI File (Type 1) Generator
// ----------------------------------------------------------------------------
typedef struct midi_event_t
{
	uint tick;
	u8 len;
	u8 bytes[4];
} midi_event_t;

static int compare_midi_events (const void *a, const void *b)
{
	const midi_event_t *ea = (const midi_event_t *)a;
	const midi_event_t *eb = (const midi_event_t *)b;
	if (ea->tick < eb->tick)
		return -1;
	if (ea->tick > eb->tick)
		return 1;
	return 0;
}

static void append_varlen (u8 **ptr, uint val)
{
	u8 buf[5];
	int i = 0;
	buf[i++] = val & 0x7F;
	val >>= 7;
	while (val > 0)
	{
		buf[i++] = (val & 0x7F) | 0x80;
		val >>= 7;
	}
	while (i > 0)
	{
		*(*ptr)++ = buf[--i];
	}
}

u8 *DecodeMIORecordMIDI (const u8 *data, size_t size, uint *out_size)
{
	if (!data || size < 8192)
		return 0;

	const uint bpm = (uint)data[0x101] * 10 + 60;
	const uint us_per_beat = (bpm > 0) ? (60000000u / bpm) : 500000u;
	uint num_blocks = data[0x102];
	if (num_blocks == 0 || num_blocks > 24)
		num_blocks = 24;

	// PPQ = 480 ticks per quarter note. 1 step = 120 ticks (16th note).
	const uint step_ticks = 120;

	// We will build a Type-1 MIDI file with up to 6 tracks:
	// Track 0: Tempo & Time Signature
	// Tracks 1..4: Melodic tracks 0..3 (MIDI channels 0..3)
	// Track 5: Drum track (MIDI channel 9 / 0x99)
	u8 *mid_buf = CALLOC (1, 65536);
	if (!mid_buf)
		return 0;

	u8 *p = mid_buf;

	// MThd Header
	memcpy (p, "MThd", 4);
	p += 4;
	wr_be32 (p, 6);
	p += 4;
	wr_be16 (p, 1); // Format 1
	p += 2;
	wr_be16 (p, 6); // 6 tracks
	p += 2;
	wr_be16 (p, 480); // Division (PPQ)
	p += 2;

	// Track 0: Tempo / Time Signature
	{
		u8 *trk_start = p;
		memcpy (p, "MTrk", 4);
		p += 8; // skip magic and length for now
		u8 *trk_data = p;

		// Delta 0, Set Tempo
		append_varlen (&p, 0);
		*p++ = 0xFF;
		*p++ = 0x51;
		*p++ = 0x03;
		*p++ = (us_per_beat >> 16) & 0xFF;
		*p++ = (us_per_beat >> 8) & 0xFF;
		*p++ = us_per_beat & 0xFF;

		// Delta 0, Time Signature 4/4
		append_varlen (&p, 0);
		*p++ = 0xFF;
		*p++ = 0x58;
		*p++ = 0x04;
		*p++ = 0x04;
		*p++ = 0x02;
		*p++ = 0x18;
		*p++ = 0x08;

		// End of track
		append_varlen (&p, 0);
		*p++ = 0xFF;
		*p++ = 0x2F;
		*p++ = 0x00;

		uint trk_len = (uint)(p - trk_data);
		wr_be32 (trk_start + 4, trk_len);
	}

	// Tracks 1..4: Melodic tracks (t = 0..3)
	for (uint t = 0; t < 4; t++)
	{
		midi_event_t events[2048];
		uint n_events = 0;
		int cur_inst = -1;
		int cur_vol = -1;

		for (uint b = 0; b < num_blocks; b++)
		{
			const size_t block_off = 0x107 + b * 0x114;
			if (block_off + 0x114 > size)
				break;

			const u8 inst_idx = data[block_off + 0x10A + t];
			const u8 vol_idx = data[block_off + 0x100 + t];

			if ((int)inst_idx != cur_inst)
			{
				cur_inst = inst_idx;
				const u8 gm_prog = mio_gm_instruments[inst_idx < 48 ? inst_idx : 0];
				if (n_events < 2040)
				{
					events[n_events].tick = b * 32 * step_ticks;
					events[n_events].len = 2;
					events[n_events].bytes[0] = 0xC0 | (u8)t;
					events[n_events].bytes[1] = gm_prog;
					n_events++;
				}
			}

			if ((int)vol_idx != cur_vol)
			{
				cur_vol = vol_idx;
				const u8 gm_vol = (vol_idx <= 4) ? (vol_idx * 31 + 3) : 100;
				if (n_events < 2040)
				{
					events[n_events].tick = b * 32 * step_ticks;
					events[n_events].len = 3;
					events[n_events].bytes[0] = 0xB0 | (u8)t;
					events[n_events].bytes[1] = 7; // Volume controller
					events[n_events].bytes[2] = gm_vol;
					n_events++;
				}
			}

			// Notes in block
			const size_t notes_off = block_off + t * 32;
			for (uint s = 0; s < 32; s++)
			{
				const u8 val = data[notes_off + s];
				if (val < 25) // Active pitch (0..24, 0 = G3, 5 = C4)
				{
					const u8 pitch = 55 + val;
					const uint start_tick = (b * 32 + s) * step_ticks;
					const uint end_tick = start_tick + step_ticks - 10;
					if (n_events + 2 < 2048)
					{
						// Note On
						events[n_events].tick = start_tick;
						events[n_events].len = 3;
						events[n_events].bytes[0] = 0x90 | (u8)t;
						events[n_events].bytes[1] = pitch;
						events[n_events].bytes[2] = 100; // Velocity
						n_events++;

						// Note Off
						events[n_events].tick = end_tick;
						events[n_events].len = 3;
						events[n_events].bytes[0] = 0x80 | (u8)t;
						events[n_events].bytes[1] = pitch;
						events[n_events].bytes[2] = 0;
						n_events++;
					}
				}
			}
		}

		// Sort events by tick
		qsort (events, n_events, sizeof (midi_event_t), compare_midi_events);

		u8 *trk_start = p;
		memcpy (p, "MTrk", 4);
		p += 8;
		u8 *trk_data = p;

		uint last_tick = 0;
		for (uint i = 0; i < n_events; i++)
		{
			uint delta = events[i].tick >= last_tick ? (events[i].tick - last_tick) : 0;
			append_varlen (&p, delta);
			for (uint k = 0; k < events[i].len; k++)
				*p++ = events[i].bytes[k];
			last_tick = events[i].tick;
		}

		// End of track
		append_varlen (&p, 0);
		*p++ = 0xFF;
		*p++ = 0x2F;
		*p++ = 0x00;

		uint trk_len = (uint)(p - trk_data);
		wr_be32 (trk_start + 4, trk_len);
	}

	// Track 5: Percussion / Drums (dt = 4 and 5, channel 9 / 0x99)
	{
		midi_event_t drum_events[2048];
		uint n_drum_events = 0;

		for (uint b = 0; b < num_blocks; b++)
		{
			const size_t block_off = 0x107 + b * 0x114;
			if (block_off + 0x114 > size)
				break;

			for (uint dt = 4; dt <= 5; dt++)
			{
				const size_t notes_off = block_off + dt * 32;
				for (uint s = 0; s < 32; s++)
				{
					const u8 val = data[notes_off + s];
					if (val < 8) // Valid drum 0..7
					{
						const u8 pitch = mio_gm_drums[val];
						const uint start_tick = (b * 32 + s) * step_ticks;
						const uint end_tick = start_tick + 60;
						if (n_drum_events + 2 < 2048)
						{
							drum_events[n_drum_events].tick = start_tick;
							drum_events[n_drum_events].len = 3;
							drum_events[n_drum_events].bytes[0] = 0x99;
							drum_events[n_drum_events].bytes[1] = pitch;
							drum_events[n_drum_events].bytes[2] = 100;
							n_drum_events++;

							drum_events[n_drum_events].tick = end_tick;
							drum_events[n_drum_events].len = 3;
							drum_events[n_drum_events].bytes[0] = 0x89;
							drum_events[n_drum_events].bytes[1] = pitch;
							drum_events[n_drum_events].bytes[2] = 0;
							n_drum_events++;
						}
					}
				}
			}
		}

		qsort (drum_events, n_drum_events, sizeof (midi_event_t), compare_midi_events);

		u8 *trk_start = p;
		memcpy (p, "MTrk", 4);
		p += 8;
		u8 *trk_data = p;

		uint last_tick = 0;
		for (uint i = 0; i < n_drum_events; i++)
		{
			uint delta = drum_events[i].tick >= last_tick ? (drum_events[i].tick - last_tick) : 0;
			append_varlen (&p, delta);
			for (uint k = 0; k < drum_events[i].len; k++)
				*p++ = drum_events[i].bytes[k];
			last_tick = drum_events[i].tick;
		}

		append_varlen (&p, 0);
		*p++ = 0xFF;
		*p++ = 0x2F;
		*p++ = 0x00;

		uint trk_len = (uint)(p - trk_data);
		wr_be32 (trk_start + 4, trk_len);
	}

	uint total_mid_len = (uint)(p - mid_buf);
	if (out_size)
		*out_size = total_mid_len;
	return mid_buf;
}

// ----------------------------------------------------------------------------
// ExtractMIOArchive: wszst xx destination extractor
// ----------------------------------------------------------------------------
static void get_dest_dir (char *dest, size_t dest_size, ccp arg, ccp basedir)
{
	if (opt_dest && *opt_dest)
		snprintf (dest, dest_size, "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, dest_size, "%s", basedir);
	else
		snprintf (dest, dest_size, "%s.d", arg);
}

enumError ExtractMIOArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsMIO (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const mio_type_t mtype = GetMIOType (raw, raw_size);
	mio_meta_t meta;
	ReadMIOMetadata (raw, raw_size, &meta);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);

	if (verbose >= 0 || testmode)
	{
		ccp type_str = (mtype == MIO_TYPE_GAME) ? "GAME" :
		               (mtype == MIO_TYPE_COMIC) ? "COMIC" : "RECORD";
		fprintf (stdlog, "%s%sEXTRACT MIO (%s): %s (\"%s\") -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			type_str, arg, meta.name[0] ? meta.name : "unnamed", dest);
	}

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	CreatePath (dest, true);

	// Create output folder and write metadata.txt
	char meta_path[PATH_MAX];
	snprintf (meta_path, sizeof (meta_path), "%s/%smetadata.txt", dest, basedir ? basedir : "");
	char meta_buf[1024];
	int meta_len = snprintf (meta_buf, sizeof (meta_buf),
		"Title: %s\nBrand: %s\nCreator: %s\nDescription: %s\nType: %s\n",
		meta.name, meta.brand, meta.creator, meta.desc,
		(mtype == MIO_TYPE_GAME) ? "Game" :
		(mtype == MIO_TYPE_COMIC) ? "Comic" : "Record");
	if (meta_len > 0)
		SaveFile (meta_path, 0, 0, meta_buf, (uint)meta_len, 0);

	if (mtype == MIO_TYPE_COMIC)
	{
		// Extract up to 4 monochrome panels as PNGs
		for (uint p = 0; p < 4; p++)
		{
			uint pw = 0, ph = 0;
			u8 *rgba = DecodeMIOComicPanel (raw, raw_size, p, &pw, &ph);
			if (rgba)
			{
				char png_path[PATH_MAX];
				snprintf (png_path, sizeof (png_path), "%s/%spanel_%u.png",
					dest, basedir ? basedir : "", p);
				SaveDecodedRGBAToPNG (rgba, pw, ph, &be_func, png_path, 0, true);
			}
		}
	}
	else if (mtype == MIO_TYPE_GAME)
	{
		// Extract background image as bg.png
		uint bw = 0, bh = 0;
		u8 *bg_rgba = DecodeMIOGameBG (raw, raw_size, &bw, &bh);
		if (bg_rgba)
		{
			char bg_path[PATH_MAX];
			snprintf (bg_path, sizeof (bg_path), "%s/%sbg.png", dest, basedir ? basedir : "");
			SaveDecodedRGBAToPNG (bg_rgba, bw, bh, &be_func, bg_path, 0, true);
		}

		// Extract object sprites
		for (uint obj = 0; obj < 15; obj++)
		{
			const size_t obj_off = 0xB104 + obj * 0x88;
			if (raw[obj_off] == 0)
				continue;

			// Extract art states (up to 4 art states)
			for (uint art = 0; art < 4; art++)
			{
				const size_t art_off = obj_off + 0x15 + art * 0x1C;
				const u8 n_frames = raw[art_off + 1];
				for (uint f = 0; f < n_frames && f < 8; f++)
				{
					const u8 frame_idx = raw[art_off + 2 + f];
					uint sw = 0, sh = 0;
					u8 *sp_rgba = DecodeMIOGameSprite (raw, raw_size, obj, frame_idx, &sw, &sh);
					if (sp_rgba)
					{
						char sp_path[PATH_MAX];
						snprintf (sp_path, sizeof (sp_path), "%s/%sobj%02u_art%u_f%u.png",
							dest, basedir ? basedir : "", obj, art, f);
						SaveDecodedRGBAToPNG (sp_rgba, sw, sh, &be_func, sp_path, 0, true);
					}
				}
			}
		}
	}
	else if (mtype == MIO_TYPE_RECORD)
	{
		// Extract MIDI file
		uint mid_size = 0;
		u8 *mid_data = DecodeMIORecordMIDI (raw, raw_size, &mid_size);
		if (mid_data && mid_size > 0)
		{
			char mid_path[PATH_MAX];
			snprintf (mid_path, sizeof (mid_path), "%s/%smusic.mid", dest, basedir ? basedir : "");
			SaveFile (mid_path, 0, 0, mid_data, mid_size, 0);
			FREE (mid_data);
		}
	}

	FREE (raw);
	return ERR_OK;
}
