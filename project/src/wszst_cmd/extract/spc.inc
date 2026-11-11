// SNES ".spc" sound state dump ("SNES-SPC700 Sound File Data v0.30"): 64 KiB of SPC700 RAM at 0x100 and the 128 DSP registers at 0x10100. DSP register
// 0x5D holds the page of the sample directory (4 byte entries {u16 start, u16 loop} per source); every sample is a chain of 9 byte BRR blocks (range/filter/
// loop/end header + 16 four bit samples) up to the block with the end flag. Each sample is decoded to "<file>.d/sample_NN.wav" (mono, 32 kHz; the real
// playback pitch is set by the game's music code, not stored with the sample).
static enumError extract_spc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".spc") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz >= 0x10180 && !memcmp (raw, "SNES-SPC700 Sound File Data", 27))
	{
		const u8 *ram = raw + 0x100, *dsp = raw + 0x10100;
		const uint dir = dsp[0x5d] << 8;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint done = 0;
		err = ERR_OK;
		for (uint n = 0; n < 256 && dir + n * 4 + 4 <= 0x10000 && err == ERR_OK; n++)
		{
			const uint start = ram[dir + n * 4] | ram[dir + n * 4 + 1] << 8;
			if (start < 0x200 || start + 9 > 0x10000)
			{
				if (done) // the directory ends at the first unused entry after the valid ones
					break;
				continue;
			}
			// count blocks
			uint blocks = 0;
			for (uint p = start; p + 9 <= 0x10000 && blocks < 8192;)
			{
				blocks++;
				const bool end = ram[p] & 1;
				p += 9;
				if (end)
					break;
			}
			if (!blocks)
				continue;
			u8 *wav = MALLOC (44 + (size_t)blocks * 32);
			if (!wav)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			int p1 = 0, p2 = 0;
			uint outn = 0;
			for (uint k = 0, p = start; k < blocks; k++, p += 9)
			{
				const uint hdr = ram[p], range = hdr >> 4, filt = hdr >> 2 & 3;
				for (uint i = 0; i < 16; i++)
				{
					int s = ram[p + 1 + i / 2];
					s = (i & 1) ? s & 15 : s >> 4;
					if (s >= 8)
						s -= 16;
					s = range <= 12 ? (s << range) >> 1 : (s < 0 ? -2048 : 0);
					switch (filt)
					{
						case 1: s += p1 + ((-p1) >> 4); break;
						case 2: s += p1 * 2 + ((-p1 * 3) >> 5) - p2 + (p2 >> 4); break;
						case 3: s += p1 * 2 + ((-p1 * 13) >> 6) - p2 + ((p2 * 3) >> 4); break;
					}
					if (s > 32767) s = 32767;
					if (s < -32768) s = -32768;
					s = (int16_t)(s * 2);
					p2 = p1;
					p1 = s >> 1;
					wav[44 + outn * 2] = s & 255;
					wav[44 + outn * 2 + 1] = (s >> 8) & 255;
					outn++;
				}
			}
			memcpy (wav, "RIFF", 4);
			nekogrp_put32 (wav + 4, 36 + outn * 2);
			memcpy (wav + 8, "WAVEfmt ", 8);
			nekogrp_put32 (wav + 16, 16);
			wav[20] = 1; wav[21] = 0; wav[22] = 1; wav[23] = 0;
			nekogrp_put32 (wav + 24, 32000);
			nekogrp_put32 (wav + 28, 64000);
			wav[32] = 2; wav[33] = 0; wav[34] = 16; wav[35] = 0;
			memcpy (wav + 36, "data", 4);
			nekogrp_put32 (wav + 40, outn * 2);
			char fn[64];
			snprintf (fn, sizeof (fn), "sample_%02u.wav", n);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT SPC:%s -> %s/%s%s (%u samples)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, outn);
			err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, wav, 44 + outn * 2);
			FREE (wav);
			done++;
		}
		if (!done && err == ERR_OK)
			err = ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	return err;
}
