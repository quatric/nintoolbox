// Ogg Speex (".spx") speech files (Let's Dance and other games): the Ogg pages are re-assembled into packets, packet 0 is the 80-byte "Speex   " header (mode, rate, channels, frames per packet), packet 1 the Vorbis-style
// comment header (plus any extra headers) and the rest are Speex frame groups decoded with the bundled libspeex (src/speex, BSD, floating point, enhancement on). Mono and (via the Speex intensity stereo callback) stereo
// streams are written as 16-bit PCM to "<file>.wav".
#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>
#include <speex/speex_callbacks.h>

// Writes interleaved 16-bit PCM as a canonical WAV file.
static enumError spx_write_wav (ccp arg, ccp path, const s16 *pcm, uint ns, uint nch, uint rate)
{
	const size_t bytes = (size_t)ns * nch * 2;
	u8 h[44];
	memcpy (h, "RIFF", 4);
	wr_le32 (h + 4, (u32)(36 + bytes));
	memcpy (h + 8, "WAVEfmt ", 8);
	wr_le32 (h + 16, 16);
	h[20] = 1; h[21] = 0; h[22] = nch; h[23] = 0;
	wr_le32 (h + 24, rate);
	wr_le32 (h + 28, rate * nch * 2);
	h[32] = nch * 2; h[33] = 0; h[34] = 16; h[35] = 0;
	memcpy (h + 36, "data", 4);
	wr_le32 (h + 40, (u32)bytes);
	u8 *le = MALLOC (bytes ? bytes : 1);
	if (!le)
		return ERR_OUT_OF_MEMORY;
	for (size_t q = 0; q < (size_t)ns * nch; q++)
	{
		le[q * 2] = (u8)pcm[q];
		le[q * 2 + 1] = (u8)((u16)pcm[q] >> 8);
	}
	File_t F;
	enumError err = CreateFileOpt (&F, true, path, false, arg);
	if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (le, 1, bytes, F.f) != bytes))
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", bytes, path);
	ResetFile (&F, opt_preserve);
	FREE (le);
	return err;
}

// Splits an Ogg stream into packets. *pk / *pl receive malloc'ed arrays, returns the number of packets.
static uint spx_ogg_packets (const u8 *d, size_t sz, u8 ***pk, uint **pl)
{
	uint n = 0, cap = 0;
	u8 **P = 0;
	uint *L = 0;
	u8 *cur = 0;
	size_t curlen = 0, curcap = 0;
	size_t o = 0;
	while (o + 27 <= sz && !memcmp (d + o, "OggS", 4))
	{
		const uint ns = d[o + 26];
		if (o + 27 + ns > sz)
			break;
		size_t dp = o + 27 + ns;
		for (uint i = 0; i < ns; i++)
		{
			const uint seg = d[o + 27 + i];
			if (dp + seg > sz)
				goto out;
			if (curlen + seg > curcap)
			{
				curcap = (curlen + seg) * 2 + 256;
				u8 *nb = (u8 *)MALLOC (curcap);
				if (!nb)
					goto out;
				if (curlen)
					memcpy (nb, cur, curlen);
				FREE (cur);
				cur = nb;
			}
			memcpy (cur + curlen, d + dp, seg);
			curlen += seg;
			dp += seg;
			if (seg < 255)
			{
				if (n == cap)
				{
					const uint nc = cap ? cap * 2 : 64;
					u8 **np = (u8 **)CALLOC (nc, sizeof (*np));
					uint *nl = (uint *)CALLOC (nc, sizeof (*nl));
					if (!np || !nl)
					{
						FREE (np);
						FREE (nl);
						goto out;
					}
					if (n)
					{
						memcpy (np, P, n * sizeof (*np));
						memcpy (nl, L, n * sizeof (*nl));
					}
					FREE (P);
					FREE (L);
					P = np;
					L = nl;
					cap = nc;
				}
				P[n] = (u8 *)MALLOC (curlen ? curlen : 1);
				if (!P[n])
					goto out;
				memcpy (P[n], cur, curlen);
				L[n] = (uint)curlen;
				n++;
				curlen = 0;
			}
		}
		o = dp;
	}
out:
	FREE (cur);
	*pk = P;
	*pl = L;
	return n;
}

static enumError extract_spxogg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0, **pk = 0;
	uint *pl = 0, np = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".spx") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 100 || memcmp (raw, "OggS", 4))
		goto done;
	np = spx_ogg_packets (raw, sz, &pk, &pl);
	if (np < 3 || pl[0] < 80 || memcmp (pk[0], "Speex   ", 8))
		goto done;
	{
		SpeexHeader *hdr = speex_packet_to_header ((char *)pk[0], (int)pl[0]);
		if (!hdr)
			goto done;
		const SpeexMode *mode = speex_lib_get_mode (hdr->mode);
		void *st = mode ? speex_decoder_init (mode) : 0;
		if (!st || hdr->rate < 4000 || hdr->rate > 96000 || hdr->nb_channels < 1 || hdr->nb_channels > 2 || hdr->frames_per_packet < 1)
		{
			if (st)
				speex_decoder_destroy (st);
			speex_header_free (hdr);
			goto done;
		}
		int enh = 1, frame = 0;
		speex_decoder_ctl (st, SPEEX_SET_ENH, &enh);
		speex_decoder_ctl (st, SPEEX_GET_FRAME_SIZE, &frame);
		const uint nch = hdr->nb_channels, rate = hdr->rate, skip = 2 + (hdr->extra_headers > 0 ? hdr->extra_headers : 0);
		SpeexStereoState stereo = SPEEX_STEREO_STATE_INIT;
		if (nch == 2)
		{
			SpeexCallback cb;
			cb.callback_id = SPEEX_INBAND_STEREO;
			cb.func = speex_std_stereo_request_handler;
			cb.data = &stereo;
			speex_decoder_ctl (st, SPEEX_SET_HANDLER, &cb);
		}
		SpeexBits bits;
		speex_bits_init (&bits);
		const uint maxs = (np > skip ? np - skip : 0) * hdr->frames_per_packet * frame + 1;
		s16 *pcm = (s16 *)CALLOC (maxs * nch, sizeof (s16));
		s16 *tmp = (s16 *)CALLOC ((size_t)frame * nch + 16, sizeof (s16));
		uint ns = 0;
		for (uint p = skip; p < np && pcm && tmp; p++)
		{
			speex_bits_read_from (&bits, (char *)pk[p], (int)pl[p]);
			for (uint f = 0; f < (uint)hdr->frames_per_packet; f++)
			{
				if (speex_decode_int (st, &bits, tmp) < 0)
					break;
				if (nch == 2)
					speex_decode_stereo_int (tmp, frame, &stereo);
				if (ns + frame <= maxs)
				{
					memcpy (pcm + (size_t)ns * nch, tmp, (size_t)frame * nch * sizeof (s16));
					ns += frame;
				}
			}
		}
		speex_bits_destroy (&bits);
		speex_decoder_destroy (st);
		speex_header_free (hdr);
		if (pcm && tmp && ns)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s.wav", arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE SPEEX:%s (%u samples, %u Hz, %u ch) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns, rate, nch, path);
			err = ERR_OK;
			if (!testmode)
				err = spx_write_wav (arg, path, pcm, ns, nch, rate);
		}
		FREE (pcm);
		FREE (tmp);
	}
done:
	for (uint i = 0; i < np; i++)
		FREE (pk[i]);
	FREE (pk);
	FREE (pl);
	FREE (raw);
	return err;
}
