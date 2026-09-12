#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "types.h"
#include "lib-nintendo.h"
#include "lib-nitro.h"
#include "lib-smdh.h"
#include "lib-gtx.h"
#include "lib-nsbanim.h"
#ifdef __cplusplus
}
#endif

extern void trace_free (const char *func, const char *file, unsigned int line, void *ptr);
extern void *trace_calloc (
	const char *func, const char *file, unsigned int line, size_t nmemb, size_t size);
extern void *trace_malloc (const char *func, const char *file, unsigned int line, size_t size);
#define free(p) trace_free (__FUNCTION__, __FILE__, __LINE__, (p))
#define calloc(n, s) trace_calloc (__FUNCTION__, __FILE__, __LINE__, (n), (s))
#define malloc(s) trace_malloc (__FUNCTION__, __FILE__, __LINE__, (s))

int main (void)
{
	int fail = 0;
	printf ("=== Testing NitroPaint additions ===\n");

	// 1. Test Diff8 / Diff16
	{
		const u8 test_data[]
			= "Hello World! This is a differential compression test sequence 1234567890.";
		const uint len = sizeof (test_data);
		u8 *enc8 = 0, *dec8 = 0;
		uint enc8_sz = 0, dec8_sz = 0;

		enumError e1 = EncodeDiff8 (&enc8, &enc8_sz, test_data, len);
		enumError e2 = DecodeDiff8 (&dec8, &dec8_sz, enc8, enc8_sz);
		if (e1 || e2 || dec8_sz != len || memcmp (dec8, test_data, len))
		{
			printf ("  FAIL: Diff8 encode/decode mismatch\n");
			fail++;
		}
		else
		{
			printf ("  PASS: Diff8 encode/decode roundtrip\n");
		}
		free (enc8);
		free (dec8);

		u8 *enc16 = 0, *dec16 = 0;
		uint enc16_sz = 0, dec16_sz = 0;
		e1 = EncodeDiff16 (&enc16, &enc16_sz, test_data, len);
		e2 = DecodeDiff16 (&dec16, &dec16_sz, enc16, enc16_sz);
		if (e1 || e2 || dec16_sz < len || memcmp (dec16, test_data, len))
		{
			printf ("  FAIL: Diff16 encode/decode mismatch\n");
			fail++;
		}
		else
		{
			printf ("  PASS: Diff16 encode/decode roundtrip\n");
		}
		free (enc16);
		free (dec16);
	}

	// 2. Test PuCrunch
	{
		const u8 test_data[] = "The quick brown fox jumps over the lazy dog. "
							   "AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVVWWXXYYZZ";
		const uint len = sizeof (test_data);
		u8 *enc_pc = 0, *dec_pc = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodePuCrunch (&enc_pc, &enc_sz, test_data, len);
		if (e1 || !CxIsCompressedPuCrunch (enc_pc, enc_sz))
		{
			printf ("  FAIL: PuCrunch encode failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodePuCrunch (&dec_pc, &dec_sz, enc_pc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_pc, test_data, len))
			{
				printf ("  FAIL: PuCrunch decode mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: PuCrunch encode/decode roundtrip\n");
			}
			free (dec_pc);
		}
		free (enc_pc);
	}

	// 3. Test LZX
	{
		const u8 test_data[] = "LZX compression format test: repeating patterns repeating patterns "
							   "repeating patterns 12345 12345!";
		const uint len = sizeof (test_data);
		u8 *enc_lzx = 0, *dec_lzx = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeLZX (&enc_lzx, &enc_sz, test_data, len);
		if (e1 || !CxIsCompressedLZX (enc_lzx, enc_sz))
		{
			printf ("  FAIL: LZX encode failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeLZX (&dec_lzx, &dec_sz, enc_lzx, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_lzx, test_data, len))
			{
				printf ("  FAIL: LZX decode mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: LZX encode/decode roundtrip\n");
			}
			free (dec_lzx);
		}
		free (enc_lzx);
	}

	// 4. Test VLX
	{
		const u8 test_data[] = "VLX format test: Pac-Man World DS namco compression literal and "
							   "repeat stream test abcdef";
		const uint len = sizeof (test_data);
		u8 *enc_vlx = 0, *dec_vlx = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeVLX (&enc_vlx, &enc_sz, test_data, len);
		if (e1 || !CxIsCompressedVlx (enc_vlx, enc_sz))
		{
			printf ("  FAIL: VLX encode failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeVLX (&dec_vlx, &dec_sz, enc_vlx, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_vlx, test_data, len))
			{
				printf ("  FAIL: VLX decode mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: VLX encode/decode roundtrip\n");
			}
			free (dec_vlx);
		}
		free (enc_vlx);
	}

	// 5. Test NSBTX Texture Archive creation & decode
	{
		const uint w = 32, h = 32;
		u8 rgba_in[32 * 32 * 4];
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				rgba_in[(y * w + x) * 4 + 0] = (u8)(x * 8);
				rgba_in[(y * w + x) * 4 + 1] = (u8)(y * 8);
				rgba_in[(y * w + x) * 4 + 2] = 128;
				rgba_in[(y * w + x) * 4 + 3] = 255;
			}

		u8 *btx = 0;
		uint btx_sz = 0;
		enumError e1
			= CreateNSBTX (&btx, &btx_sz, rgba_in, w, h, NITRO_TEXFMT_DIRECT, "test_tex", 0);
		if (e1 || !btx || btx_sz < 0x20)
		{
			printf ("  FAIL: CreateNSBTX failed\n");
			fail++;
		}
		else
		{
			nfmt_info_t nfmt = DetectNintendoFormat (btx, btx_sz, "test.nsbtx");
			if (nfmt.type != NFMT_NSBTX)
			{
				printf ("  FAIL: DetectNintendoFormat failed for NSBTX\n");
				fail++;
			}
			else
			{
				u8 *rgba_out = 0;
				uint out_w = 0, out_h = 0;
				enumError e2 = DecodeNSBTX_RGBA (&rgba_out, &out_w, &out_h, btx, btx_sz);
				if (e2 || out_w != w || out_h != h || !rgba_out)
				{
					printf ("  FAIL: DecodeNSBTX_RGBA failed\n");
					fail++;
				}
				else
				{
					printf ("  PASS: NSBTX create -> detect -> decode roundtrip (%ux%u)\n", out_w,
						out_h);
				}
				free (rgba_out);
			}
			free (btx);
		}
	}

	// 6. Test NFTR / BNFR Font Atlas encoding & decoding
	{
		const uint aw = 128, ah = 64;
		u8 *atlas_in = (u8 *)calloc (1, aw * ah * 4);
		for (uint y = 0; y < ah; y++)
			for (uint x = 0; x < aw; x++)
				if ((x % 8 >= 2 && x % 8 <= 5) && (y % 8 >= 2 && y % 8 <= 5))
					atlas_in[(y * aw + x) * 4 + 3] = 255;

		u8 *nftr = 0;
		uint nftr_sz = 0;
		enumError e1 = EncodeNFTR_Atlas (&nftr, &nftr_sz, atlas_in, aw, ah, 0, false);
		if (e1 || !nftr || nftr_sz < 0x20)
		{
			printf ("  FAIL: EncodeNFTR_Atlas failed\n");
			fail++;
		}
		else
		{
			nfmt_info_t nfmt = DetectNintendoFormat (nftr, nftr_sz, "font.nftr");
			if (nfmt.type != NFMT_NFTR)
			{
				printf ("  FAIL: DetectNintendoFormat failed for NFTR\n");
				fail++;
			}
			else
			{
				u8 *atlas_out = 0;
				uint out_w = 0, out_h = 0;
				char *xml_out = 0;
				enumError e2
					= DecodeNFTR_Atlas (&atlas_out, &out_w, &out_h, &xml_out, nftr, nftr_sz);
				if (e2 || out_w != aw || out_h != ah || !atlas_out || !xml_out)
				{
					printf ("  FAIL: DecodeNFTR_Atlas failed\n");
					fail++;
				}
				else
				{
					printf ("  PASS: NFTR font encode -> detect -> decode atlas (%ux%u)\n", out_w,
						out_h);
				}
				free (atlas_out);
				free (xml_out);
			}
			free (nftr);
		}
		free (atlas_in);
	}

	// 7. Test 5TX Single Texture format
	{
		const uint w = 16, h = 16;
		u8 rgba_in[16 * 16 * 4];
		for (uint i = 0; i < w * h; i++)
		{
			rgba_in[i * 4 + 0] = 200;
			rgba_in[i * 4 + 1] = 100;
			rgba_in[i * 4 + 2] = 50;
			rgba_in[i * 4 + 3] = 255;
		}

		u8 *fivetx = 0;
		uint fivetx_sz = 0;
		enumError e1 = Encode5TX_RGBA (&fivetx, &fivetx_sz, rgba_in, w, h);
		if (e1 || !fivetx || fivetx_sz < 16)
		{
			printf ("  FAIL: Encode5TX_RGBA failed\n");
			fail++;
		}
		else
		{
			u8 *rgba_out = 0;
			uint out_w = 0, out_h = 0;
			enumError e2 = Decode5TX_RGBA (&rgba_out, &out_w, &out_h, fivetx, fivetx_sz);
			if (e2 || out_w != w || out_h != h || !rgba_out)
			{
				printf ("  FAIL: Decode5TX_RGBA failed\n");
				fail++;
			}
			else
			{
				printf ("  PASS: 5TX image encode -> decode (%ux%u)\n", out_w, out_h);
			}
			free (rgba_out);
			free (fivetx);
		}
	}

	// 8. Test BNLL 2D Layout disassemble / assemble
	{
		u8 *bnll = 0;
		uint bnll_sz = 0;
		enumError e1 = EncodeBNLL_Text (&bnll, &bnll_sz, "layout text");
		if (e1 || !bnll || bnll_sz < 0x10)
		{
			printf ("  FAIL: EncodeBNLL_Text failed\n");
			fail++;
		}
		else
		{
			char *txt = 0;
			enumError e2 = DecodeBNLL_Text (&txt, bnll, bnll_sz);
			if (e2 || !txt || !strstr (txt, "BNLL"))
			{
				printf ("  FAIL: DecodeBNLL_Text failed\n");
				fail++;
			}
			else
			{
				printf ("  PASS: BNLL layout encode -> disassemble text\n");
			}
			free (txt);
			free (bnll);
		}
	}

	// 9. Test LZOvl (NDS Overlay reverse compression)
	{
		const u8 test_data[]
			= "Reverse LZ overlay compression test payload for ARM9 overlay 0123456789";
		const uint len = sizeof (test_data);
		u8 *enc_ovl = 0, *dec_ovl = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeLZOvl (&enc_ovl, &enc_sz, test_data, len);
		if (e1 || !enc_ovl || enc_sz < 8)
		{
			printf ("  FAIL: EncodeLZOvl failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeLZOvl (&dec_ovl, &dec_sz, enc_ovl, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_ovl, test_data, len))
			{
				printf ("  FAIL: DecodeLZOvl roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: LZOvl reverse compression roundtrip\n");
			}
			free (dec_ovl);
			free (enc_ovl);
		}
	}

	// 10. Test ALAR (Jump Ultimate Stars Archive)
	{
		u8 alar_buf[64] = { 0 };
		memcpy (alar_buf, "ALAR", 4);
		alar_buf[4] = 2; // Type 2
		alar_buf[6] = 1; // 1 file
		// File 0 entry at 16: ofs=32, sz=12
		alar_buf[16 + 4] = 32;
		alar_buf[16 + 8] = 12;
		memcpy (alar_buf + 32, "hello alar!", 12);

		u8 *out = 0;
		uint out_sz = 0;
		enumError e1 = DecodeALAR (&out, &out_sz, alar_buf, sizeof (alar_buf));
		if (e1 || out_sz != 12 || memcmp (out, "hello alar!", 12))
		{
			printf ("  FAIL: DecodeALAR failed\n");
			fail++;
		}
		else
		{
			printf ("  PASS: ALAR archive unpack\n");
		}
		free (out);
	}

	// 11. Test DARC (Level-5 Layton Archive)
	{
		u8 darc_buf[64] = { 0 };
		memcpy (darc_buf, "DARC", 4);
		darc_buf[4] = 1; // 1 file
		// rel_ofs = 8 (abs_ofs = 8 + 4 + 8 = 20)
		darc_buf[8] = 8;
		// sz at abs_ofs - 4 = 16
		darc_buf[16] = 12;
		memcpy (darc_buf + 20, "hello darc!", 12);

		u8 *out = 0;
		uint out_sz = 0;
		enumError e1 = DecodeDARC (&out, &out_sz, darc_buf, sizeof (darc_buf));
		if (e1 || out_sz != 12 || memcmp (out, "hello darc!", 12))
		{
			printf ("  FAIL: DecodeDARC failed\n");
			fail++;
		}
		else
		{
			printf ("  PASS: DARC archive unpack\n");
		}
		free (out);
	}

	// 12. Test SADL (Level-5 Audio -> WAV)
	{
		u8 sadl_buf[0x100 + 32] = { 0 };
		memcpy (sadl_buf, "SADL", 4);
		sadl_buf[0x32] = 1; // 1 channel
		sadl_buf[0x33] = 4; // 32728 Hz
		sadl_buf[0x40] = (u8)(sizeof (sadl_buf) & 0xFF);
		sadl_buf[0x41] = (u8)(sizeof (sadl_buf) >> 8);

		u8 *wav = 0;
		uint wav_sz = 0;
		enumError e1 = DecodeSADL_WAV (&wav, &wav_sz, sadl_buf, sizeof (sadl_buf));
		if (e1 || wav_sz < 44 || memcmp (wav, "RIFF", 4))
		{
			printf ("  FAIL: DecodeSADL_WAV failed\n");
			fail++;
		}
		else
		{
			printf ("  PASS: SADL audio -> WAV decode\n");
		}
		free (wav);
	}

	// 13. Test NCER & NANR 2D Graphics text codecs
	{
		u8 *ncer = 0;
		uint ncer_sz = 0;
		enumError e1 = EncodeNCER_Text (&ncer, &ncer_sz, "cell text");
		if (e1 || !ncer || ncer_sz < 0x20)
		{
			printf ("  FAIL: EncodeNCER_Text failed\n");
			fail++;
		}
		else
		{
			char *txt = 0;
			enumError e2 = DecodeNCER_Text (&txt, ncer, ncer_sz);
			if (e2 || !txt || !strstr (txt, "NCER"))
			{
				printf ("  FAIL: DecodeNCER_Text failed\n");
				fail++;
			}
			else
			{
				printf ("  PASS: NCER 2D cell encode -> disassemble text\n");
			}
			free (txt);
			free (ncer);
		}

		u8 *nanr = 0;
		uint nanr_sz = 0;
		e1 = EncodeNANR_Text (&nanr, &nanr_sz, "anim text");
		if (e1 || !nanr || nanr_sz < 0x20)
		{
			printf ("  FAIL: EncodeNANR_Text failed\n");
			fail++;
		}
		else
		{
			char *txt = 0;
			enumError e2 = DecodeNANR_Text (&txt, nanr, nanr_sz);
			if (e2 || !txt || !strstr (txt, "NANR"))
			{
				printf ("  FAIL: DecodeNANR_Text failed\n");
				fail++;
			}
			else
			{
				printf ("  PASS: NANR 2D animation encode -> disassemble text\n");
			}
			free (txt);
			free (nanr);
		}
	}

	// 14. Test PSDK (Prosonic SDK LZ)
	{
		const u8 psdk_test[] = "PSDK Prosonic SDK compression test payload with some repeating "
							   "repeating text 123456789";
		const uint len = sizeof (psdk_test);
		u8 *enc = 0, *dec = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodePSDK (&enc, &enc_sz, psdk_test, len);
		if (e1 || !enc || enc_sz < 8)
		{
			printf ("  FAIL: EncodePSDK failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodePSDK (&dec, &dec_sz, enc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec, psdk_test, len))
			{
				printf ("  FAIL: DecodePSDK roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: PSDK encode -> decode roundtrip\n");
			}
			free (dec);
			free (enc);
		}
	}

	// 15. Test MVDK (Mario vs. Donkey Kong compression)
	{
		const u8 mvdk_test[]
			= "Mario vs Donkey Kong custom LZ compression test payload string 0123456789";
		const uint len = sizeof (mvdk_test);
		u8 *enc = 0, *dec = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeMVDK (&enc, &enc_sz, mvdk_test, len);
		if (e1 || !enc || enc_sz < 4)
		{
			printf ("  FAIL: EncodeMVDK failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeMVDK (&dec, &dec_sz, enc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec, mvdk_test, len))
			{
				printf ("  FAIL: DecodeMVDK roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: MVDK encode -> decode roundtrip\n");
			}
			free (dec);
			free (enc);
		}
	}

	// 16. Test SSZL (Namco Museum LZSS0 compression)
	{
		const u8 sszl_test[] = "Namco Museum SSZL LZSS0 custom compression test payload 0123456789";
		const uint len = sizeof (sszl_test);
		u8 *enc = 0, *dec = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeSSZL (&enc, &enc_sz, sszl_test, len);
		if (e1 || !enc || enc_sz < 16)
		{
			printf ("  FAIL: EncodeSSZL failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeSSZL (&dec, &dec_sz, enc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec, sszl_test, len))
			{
				printf ("  FAIL: DecodeSSZL roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: SSZL encode -> decode roundtrip\n");
			}
			free (dec);
			free (enc);
		}
	}

	// 17. Test SMDH (3DS System Menu Data Header)
	{
		smdh_t smdh;
		memset (&smdh, 0, sizeof (smdh));
		smdh.version = 0;
		smdh.title[SMDH_LANG_ENGLISH].short_desc = "Test Title";
		smdh.title[SMDH_LANG_ENGLISH].long_desc = "Long Test Description";
		smdh.title[SMDH_LANG_ENGLISH].publisher = "Nintendo";
		smdh.region_lock = 0x7fffffff;
		smdh.flags = 0x01;

		u8 *enc = 0;
		uint enc_sz = 0;
		enumError e1 = EncodeSMDH (&enc, &enc_sz, &smdh);
		if (e1 || !enc || enc_sz != SMDH_SIZE)
		{
			printf ("  FAIL: EncodeSMDH failed\n");
			fail++;
		}
		else
		{
			smdh_t parsed;
			enumError e2 = ScanSMDH (&parsed, enc, enc_sz);
			if (e2 || strcmp (parsed.title[SMDH_LANG_ENGLISH].short_desc, "Test Title")
				|| strcmp (parsed.title[SMDH_LANG_ENGLISH].long_desc, "Long Test Description")
				|| strcmp (parsed.title[SMDH_LANG_ENGLISH].publisher, "Nintendo")
				|| parsed.region_lock != 0x7fffffff)
			{
				printf ("  FAIL: ScanSMDH roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: SMDH encode -> scan roundtrip\n");
			}
			ResetSMDH (&parsed);
			free (enc);
		}
	}

	// 18. Test NUTEXB (Switch texture wrapper)
	{
		const uint w = 16, h = 16;
		u8 rgba_in[16 * 16 * 4];
		for (uint i = 0; i < sizeof (rgba_in); i++)
			rgba_in[i] = (u8)(i ^ 0x5a);

		u8 *enc = 0, *dec = 0;
		uint enc_sz = 0, dec_w = 0, dec_h = 0;

		enumError e1 = EncodeNUTEXB_RGBA (&enc, &enc_sz, rgba_in, w, h, "test_tex");
		if (e1 || !enc || enc_sz < 0x70)
		{
			printf ("  FAIL: EncodeNUTEXB_RGBA failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeNUTEXB_RGBA (&dec, &dec_w, &dec_h, enc, enc_sz);
			if (e2 || dec_w != w || dec_h != h || memcmp (dec, rgba_in, sizeof (rgba_in)))
			{
				printf ("  FAIL: DecodeNUTEXB_RGBA roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: NUTEXB encode -> decode roundtrip (16x16)\n");
			}
			free (dec);
			free (enc);
		}
	}

	// 19. Test BG4 (Mario & Luigi: Paper Jam 3DS archive)
	{
		nintendo_sarc_entry_t entries[2];
		memset (entries, 0, sizeof (entries));
		entries[0].name = "file1.txt";
		entries[0].data = (const u8 *)"Hello from BG4 test member 1!";
		entries[0].size = (uint)strlen ((const char *)entries[0].data);
		entries[1].name = "sub/file2.bin";
		entries[1].data = (const u8 *)"\x01\x02\x03\x04\x05\x06\x07\x08";
		entries[1].size = 8;

		u8 *bg4_blob = 0;
		uint bg4_sz = 0;
		enumError e1 = CreateBG4 (&bg4_blob, &bg4_sz, entries, 2);
		if (e1 || !bg4_blob || bg4_sz < 16)
		{
			printf ("  FAIL: CreateBG4 failed\n");
			fail++;
		}
		else
		{
			nintendo_sarc_entry_t *scanned = 0;
			uint n_scanned = 0;
			enumError e2 = ScanBG4 (&scanned, &n_scanned, bg4_blob, bg4_sz);
			if (e2 || n_scanned != 2 || strcmp (scanned[0].name, "file1.txt")
				|| scanned[0].size != entries[0].size
				|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
				|| strcmp (scanned[1].name, "sub/file2.bin") || scanned[1].size != entries[1].size
				|| memcmp (scanned[1].data, entries[1].data, entries[1].size))
			{
				printf ("  FAIL: ScanBG4 roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: BG4 create -> scan roundtrip (2 members)\n");
			}
			ResetOwnedEntries (scanned, n_scanned);
			free (bg4_blob);
		}
	}

	// 20. Test SA01 (Mii Maker Wii U archive)
	{
		nintendo_sarc_entry_t entries[2];
		memset (entries, 0, sizeof (entries));
		entries[0].name = "mii_head.dat";
		entries[0].data = (const u8 *)"MII_HEAD_DATA_PAYLOAD";
		entries[0].size = (uint)strlen ((const char *)entries[0].data);
		entries[1].name = "mii_body.dat";
		entries[1].data = (const u8 *)"MII_BODY_DATA_PAYLOAD_123456";
		entries[1].size = (uint)strlen ((const char *)entries[1].data);

		u8 *sa_blob = 0;
		uint sa_sz = 0;
		enumError e1 = CreateSA01 (&sa_blob, &sa_sz, entries, 2, true, true);
		if (e1 || !sa_blob || sa_sz < 8)
		{
			printf ("  FAIL: CreateSA01 failed\n");
			fail++;
		}
		else
		{
			u8 *inner = 0;
			uint inner_sz = 0;
			enumError e_dec = DecodeSA01Container (&inner, &inner_sz, sa_blob, sa_sz);
			if (e_dec || !inner || inner_sz < 12)
			{
				printf ("  FAIL: DecodeSA01Container failed on created SA01\n");
				fail++;
			}
			else
			{
				nintendo_sarc_entry_t *scanned = 0;
				uint n_scanned = 0;
				enumError e2 = ScanSA01 (&scanned, &n_scanned, inner, inner_sz);
				if (e2 || n_scanned != 2 || strcmp (scanned[0].name, "mii_head.dat")
					|| scanned[0].size != entries[0].size
					|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
					|| strcmp (scanned[1].name, "mii_body.dat")
					|| scanned[1].size != entries[1].size
					|| memcmp (scanned[1].data, entries[1].data, entries[1].size))
				{
					printf ("  FAIL: ScanSA01 roundtrip mismatch\n");
					fail++;
				}
				else
				{
					printf ("  PASS: SA01 create -> decode -> scan roundtrip (2 members)\n");
				}
				ResetOwnedEntries (scanned, n_scanned);
				free (inner);
			}
			free (sa_blob);
		}
	}

	// 21. Test CA01 (amiibo Settings 3DS archive)
	{
		nintendo_sarc_entry_t entries[2];
		memset (entries, 0, sizeof (entries));
		entries[0].data = (const u8 *)"AMIIBO_DATA_CHUNK_0";
		entries[0].size = (uint)strlen ((const char *)entries[0].data);
		entries[1].data = (const u8 *)"AMIIBO_DATA_CHUNK_1_5678";
		entries[1].size = (uint)strlen ((const char *)entries[1].data);

		u8 *ca_blob = 0;
		uint ca_sz = 0;
		enumError e1 = CreateCA01 (&ca_blob, &ca_sz, entries, 2, true, false);
		if (e1 || !ca_blob || ca_sz < 0x80)
		{
			printf ("  FAIL: CreateCA01 failed\n");
			fail++;
		}
		else
		{
			u8 *inner = 0;
			uint inner_sz = 0;
			enumError e_dec = DecodeSA01Container (&inner, &inner_sz, ca_blob, ca_sz);
			if (e_dec || !inner || inner_sz < 12)
			{
				printf ("  FAIL: DecodeSA01Container failed on created CA01\n");
				fail++;
			}
			else
			{
				nintendo_sarc_entry_t *scanned = 0;
				uint n_scanned = 0;
				enumError e2 = ScanSA01 (&scanned, &n_scanned, inner, inner_sz);
				if (e2 || n_scanned != 2 || scanned[0].size != entries[0].size
					|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
					|| scanned[1].size != entries[1].size
					|| memcmp (scanned[1].data, entries[1].data, entries[1].size))
				{
					printf ("  FAIL: ScanCA01 roundtrip mismatch\n");
					fail++;
				}
				else
				{
					printf ("  PASS: CA01 create -> decode -> scan roundtrip (2 members)\n");
				}
				ResetOwnedEntries (scanned, n_scanned);
				free (inner);
			}
			free (ca_blob);
		}
	}

	// 22. Test cram (.arc, Xenoblade Chronicles 3D)
	{
		nintendo_sarc_entry_t entries[2];
		memset (entries, 0, sizeof (entries));
		entries[0].name = "model.bcmdl";
		entries[0].data = (const u8 *)"BCMDL_SAMPLE_CHUNK_01234567";
		entries[0].size = (uint)strlen ((const char *)entries[0].data);
		entries[1].name = "texture.bctex";
		entries[1].data = (const u8 *)"BCTEX_SAMPLE_CHUNK_ABCDEFGH";
		entries[1].size = (uint)strlen ((const char *)entries[1].data);

		u8 *cram_blob = 0;
		uint cram_sz = 0;
		enumError e1 = CreateCramARC (&cram_blob, &cram_sz, entries, 2);
		if (e1 || !cram_blob || cram_sz < 16)
		{
			printf ("  FAIL: CreateCramARC failed\n");
			fail++;
		}
		else
		{
			nintendo_sarc_entry_t *scanned = 0;
			uint n_scanned = 0;
			enumError e2 = ScanCramARC (&scanned, &n_scanned, cram_blob, cram_sz);
			if (e2 || n_scanned != 2 || strcmp (scanned[0].name, "model.bcmdl")
				|| scanned[0].size != entries[0].size
				|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
				|| strcmp (scanned[1].name, "texture.bctex") || scanned[1].size != entries[1].size
				|| memcmp (scanned[1].data, entries[1].data, entries[1].size))
			{
				printf ("  FAIL: ScanCramARC roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: cram ARC create -> scan roundtrip (2 members)\n");
			}
			ResetOwnedEntries (scanned, n_scanned);
			free (cram_blob);
		}
	}

	// 23. Test FSYS (Genius Sonority Pokémon archive)
	{
		nintendo_sarc_entry_t entries[2];
		memset (entries, 0, sizeof (entries));
		entries[0].name = "file_0000.bin";
		entries[0].data = (const u8 *)"POKEMON_COLOSSEUM_FSYS_TEST_DATA_12345678901234567890";
		entries[0].size = (uint)strlen ((const char *)entries[0].data);
		entries[1].name = "file_0001.bin";
		entries[1].data = (const u8 *)"POKEMON_BATTLE_REVOLUTION_FSYS_SAMPLE_ABCDEF";
		entries[1].size = (uint)strlen ((const char *)entries[1].data);

		u8 *fsys_blob = 0;
		uint fsys_sz = 0;
		enumError e1 = CreateFSYS (&fsys_blob, &fsys_sz, entries, 2, true);
		if (e1 || !fsys_blob || fsys_sz < 0x40)
		{
			printf ("  FAIL: CreateFSYS failed\n");
			fail++;
		}
		else
		{
			nintendo_sarc_entry_t *scanned = 0;
			uint n_scanned = 0;
			enumError e2 = ScanFSYS (&scanned, &n_scanned, fsys_blob, fsys_sz);
			if (e2 || n_scanned != 2 || scanned[0].size != entries[0].size
				|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
				|| scanned[1].size != entries[1].size
				|| memcmp (scanned[1].data, entries[1].data, entries[1].size))
			{
				printf ("  FAIL: ScanFSYS roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: FSYS create -> scan roundtrip (2 members)\n");
			}
			ResetOwnedEntries (scanned, n_scanned);
			free (fsys_blob);
		}
	}

	// 24. Test GSH (Gfx2 shader container encode -> scan)
	{
		const char *latte_src = "RAW[0000] word0=0x00000000 word1=0x00000000\n"
								"RAW[0001] word0=0x12345678 word1=0x9abcdef0\n";
		u8 *gsh_blob = 0;
		uint gsh_sz = 0;
		enumError e1 = EncodeGSHFromLatte (&gsh_blob, &gsh_sz, latte_src, GTX_SHADER_VERTEX);
		if (e1 || !gsh_blob || gsh_sz < 64)
		{
			printf ("  FAIL: EncodeGSHFromLatte failed\n");
			fail++;
		}
		else
		{
			gtx_t gtx;
			enumError e2 = ScanGTX (&gtx, gsh_blob, gsh_sz);
			if (e2 || gtx.n_shaders != 1 || !gtx.shaders[0].program
				|| gtx.shaders[0].program->data_size != 16
				|| gtx.shaders[0].stage != GTX_SHADER_VERTEX)
			{
				printf ("  FAIL: ScanGTX on generated GSH failed\n");
				fail++;
			}
			else
			{
				printf ("  PASS: GSH shader encode -> scan roundtrip\n");
			}
			ResetGTX (&gtx);
			free (gsh_blob);
		}
	}

	// 25. Test Hyrule Warriors Legends (.idx / .bin pair)
	{
		nintendo_sarc_entry_t entries[2];
		memset (entries, 0, sizeof (entries));
		entries[0].name = "00000.bin";
		entries[0].data = (const u8 *)"HYRULE_WARRIORS_MEMBER_0";
		entries[0].size = (uint)strlen ((const char *)entries[0].data);
		entries[1].name = "00001.bin";
		entries[1].data = (const u8 *)"HYRULE_WARRIORS_MEMBER_1_DATA";
		entries[1].size = (uint)strlen ((const char *)entries[1].data);

		u8 *idx_blob = 0, *bin_blob = 0;
		uint idx_sz = 0, bin_sz = 0;
		enumError e1 = CreateHWLegends (&idx_blob, &idx_sz, &bin_blob, &bin_sz, entries, 2);
		if (e1 || !idx_blob || !bin_blob || idx_sz != 16)
		{
			printf ("  FAIL: CreateHWLegends failed\n");
			fail++;
		}
		else
		{
			nintendo_sarc_entry_t *scanned = 0;
			uint n_scanned = 0;
			enumError e2 = ScanHWLegends (&scanned, &n_scanned, idx_blob, idx_sz, bin_blob, bin_sz);
			if (e2 || n_scanned != 2 || scanned[0].size != entries[0].size
				|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
				|| scanned[1].size != entries[1].size
				|| memcmp (scanned[1].data, entries[1].data, entries[1].size))
			{
				printf ("  FAIL: ScanHWLegends roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: HWLegends .idx/.bin create -> scan roundtrip (2 members)\n");
			}
			ResetOwnedEntries (scanned, n_scanned);
			free (idx_blob);
			free (bin_blob);
		}
	}

	// 26. Test BPE (Good-Feel Byte Pair Encoding)
	{
		const u8 test_data[] = "BPE_TEST_DATA_Good_Feel_Kirby_Epic_Yarn_and_Yoshis_Woolly_World_"
							   "GFCP_Mode_1_1234567890!_Roundtrip_Verify";
		const uint len = sizeof (test_data);
		u8 *enc = 0;
		uint enc_sz = 0;

		enumError e1 = EncodeBPE (&enc, &enc_sz, test_data, len);
		if (e1 || !enc || !enc_sz)
		{
			printf ("  FAIL: EncodeBPE failed\n");
			fail++;
		}
		else
		{
			u8 *dec = (u8 *)calloc (1, len);
			enumError e2 = DecodeBPE (dec, len, enc, enc_sz);
			if (e2 || memcmp (dec, test_data, len))
			{
				printf ("  FAIL: DecodeBPE roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf ("  PASS: BPE / GFCP encode -> decode roundtrip\n");
			}
			free (dec);
			free (enc);
		}
	}

	// 28. Test RFL_Res.dat (Revolution Face Library)
	{
		nintendo_sarc_entry_t entries[3] = { { "beard/000.bin", (const u8 *)"RFL_BEARD_DATA", 14 },
			{ "faceline/000.bin", (const u8 *)"RFL_FACELINE_GEOMETRY_MODEL_DATA", 32 },
			{ "eye/000.bin", (const u8 *)"RFL_EYE_TEXTURE_RESOURCE", 24 } };
		u8 *rfl_data = 0;
		uint rfl_size = 0;
		enumError e1 = CreateRFLRes (&rfl_data, &rfl_size, entries, 3);
		if (e1 || !rfl_data || rfl_size < 32)
		{
			printf ("  FAIL: CreateRFLRes failed\n");
			fail++;
		}
		else
		{
			nintendo_sarc_entry_t *scanned = 0;
			uint n_scanned = 0;
			enumError e2 = ScanRFLRes (&scanned, &n_scanned, rfl_data, rfl_size);
			if (e2 || n_scanned != 3 || strcmp (scanned[0].name, entries[0].name)
				|| scanned[0].size != entries[0].size
				|| memcmp (scanned[0].data, entries[0].data, entries[0].size)
				|| strcmp (scanned[1].name, entries[2].name) // eye is arc index 1
				|| scanned[1].size != entries[2].size
				|| memcmp (scanned[1].data, entries[2].data, entries[2].size)
				|| strcmp (scanned[2].name, entries[1].name) // faceline is arc index 3
				|| scanned[2].size != entries[1].size
				|| memcmp (scanned[2].data, entries[1].data, entries[1].size))
			{
				printf ("  FAIL: ScanRFLRes roundtrip mismatch (n_scanned=%u)\n", n_scanned);
				fail++;
			}
			else
			{
				u8 *rfl_re = 0;
				uint rfl_re_size = 0;
				enumError e3 = CreateRFLRes (&rfl_re, &rfl_re_size, scanned, n_scanned);
				if (e3 || rfl_re_size != rfl_size || memcmp (rfl_re, rfl_data, rfl_size))
				{
					printf ("  FAIL: RFL_Res.dat re-create byte-exact mismatch\n");
					fail++;
				}
				else
				{
					printf ("  PASS: RFL_Res.dat create -> scan -> byte-identical re-create (3 "
							"members)\n");
				}
				free (rfl_re);
			}
			ResetOwnedEntries (scanned, n_scanned);
			free (rfl_data);
		}
	}

	// 29. Test retail Nintendo DS Nitro 2D Graphics (NCGR, NCLR, NCER, NANR)
	{
		FILE *f_ncgr = fopen ("../tests/fixtures/nitro_samples/retail_sample.ncgr", "rb");
		FILE *f_nclr = fopen ("../tests/fixtures/nitro_samples/retail_kart_std_color.nclr", "rb");
		if (f_ncgr && f_nclr)
		{
			fseek (f_ncgr, 0, SEEK_END);
			uint ncgr_sz = (uint)ftell (f_ncgr);
			fseek (f_ncgr, 0, SEEK_SET);
			u8 *ncgr_buf = (u8 *)malloc (ncgr_sz);
			fread (ncgr_buf, 1, ncgr_sz, f_ncgr);
			fclose (f_ncgr);

			fseek (f_nclr, 0, SEEK_END);
			uint nclr_sz = (uint)ftell (f_nclr);
			fseek (f_nclr, 0, SEEK_SET);
			u8 *nclr_buf = (u8 *)malloc (nclr_sz);
			fread (nclr_buf, 1, nclr_sz, f_nclr);
			fclose (f_nclr);

			nitro_ncgr_t ncgr;
			nitro_nclr_t nclr;
			enumError e_ncgr = ScanNitroNCGR (&ncgr, ncgr_buf, ncgr_sz);
			enumError e_nclr = ScanNitroNCLR (&nclr, nclr_buf, nclr_sz);
			if (e_ncgr || e_nclr || ncgr.n_tiles == 0 || nclr.n_entries == 0)
			{
				printf ("  FAIL: ScanNitroNCGR / ScanNitroNCLR on retail samples failed\n");
				fail++;
			}
			else
			{
				printf ("  PASS: Retail NCGR (%u tiles, %ubpp) and NCLR (%u colors) scanned "
						"successfully\n",
					ncgr.n_tiles, ncgr.bpp, nclr.n_entries);
			}
			ResetNitroNCLR (&nclr);
			free (ncgr_buf);
			free (nclr_buf);
		}

		FILE *f_ncer = fopen ("../tests/fixtures/nitro_samples/retail_sample.ncer", "rb");
		if (f_ncer)
		{
			fseek (f_ncer, 0, SEEK_END);
			uint ncer_sz = (uint)ftell (f_ncer);
			fseek (f_ncer, 0, SEEK_SET);
			u8 *ncer_buf = (u8 *)malloc (ncer_sz);
			fread (ncer_buf, 1, ncer_sz, f_ncer);
			fclose (f_ncer);

			char *txt = 0;
			enumError e_ncer = DecodeNCER_Text (&txt, ncer_buf, ncer_sz);
			if (e_ncer || !txt || !strstr (txt, "NCER"))
			{
				printf ("  FAIL: DecodeNCER_Text on retail sample failed\n");
				fail++;
			}
			else
			{
				u8 *re_ncer = 0;
				uint re_sz = 0;
				enumError e_enc = EncodeNCER_Text (&re_ncer, &re_sz, txt);
				if (e_enc || !re_ncer || re_sz == 0)
				{
					printf ("  FAIL: EncodeNCER_Text from retail text failed\n");
					fail++;
				}
				else
				{
					printf ("  PASS: Retail NCER decode -> disassemble -> re-encode roundtrip\n");
				}
				free (re_ncer);
			}
			free (txt);
			free (ncer_buf);
		}

		FILE *f_nanr = fopen ("../tests/fixtures/nitro_samples/retail_sample.nanr", "rb");
		if (f_nanr)
		{
			fseek (f_nanr, 0, SEEK_END);
			uint nanr_sz = (uint)ftell (f_nanr);
			fseek (f_nanr, 0, SEEK_SET);
			u8 *nanr_buf = (u8 *)malloc (nanr_sz);
			fread (nanr_buf, 1, nanr_sz, f_nanr);
			fclose (f_nanr);

			char *txt = 0;
			enumError e_nanr = DecodeNANR_Text (&txt, nanr_buf, nanr_sz);
			if (e_nanr || !txt || !strstr (txt, "NANR"))
			{
				printf ("  FAIL: DecodeNANR_Text on retail sample failed\n");
				fail++;
			}
			else
			{
				u8 *re_nanr = 0;
				uint re_sz = 0;
				enumError e_enc = EncodeNANR_Text (&re_nanr, &re_sz, txt);
				if (e_enc || !re_nanr || re_sz == 0)
				{
					printf ("  FAIL: EncodeNANR_Text from retail text failed\n");
					fail++;
				}
				else
				{
					printf ("  PASS: Retail NANR decode -> disassemble -> re-encode roundtrip\n");
				}
				free (re_nanr);
			}
			free (txt);
			free (nanr_buf);
		}
	}

	// BVA0 (VIS0) visibility-animation decode + build.  Build a synthetic
	// bitfield, serialize it, decode it back and compare every bit, then push
	// it through the parse-into-model -> encode raw pass-through.
	{
		const uint nfr = 60, nn = 5;
		const uint words_b = (nfr * nn + 31) / 32;
		nsb_vis_t src = { 0 };
		src.num_frame = nfr;
		src.num_node = nn;
		src.words = words_b;
		u8 *bits = (u8 *)calloc (words_b, 4);
		if (!bits)
		{
			printf ("  FAIL: BVA0 test OOM\n");
			fail++;
		}
		else
		{
			for (uint f = 0; f < nfr; f++)
				for (uint n = 0; n < nn; n++)
					if ((f * 7 + n * 3) % 5 < 2)
						bits[(f * nn + n) >> 3] |= (u8)(1u << ((f * nn + n) & 7));
			src.bits = bits;

			size_t bva_sz = 0;
			u8 *bva = BuildNSBVA (&src, "VisibilityOne", &bva_sz);
			if (!bva || bva_sz == 0)
			{
				printf ("  FAIL: BuildNSBVA failed\n");
				fail++;
			}
			else
			{
				int bad = 0;
				nsb_vis_t vis;
				if (!DecodeNSBVA_Clip (&vis, bva, bva_sz, 0))
				{
					printf ("  FAIL: DecodeNSBVA_Clip failed\n");
					fail++;
					bad = 1;
				}
				else if (vis.num_frame != nfr || vis.num_node != nn || vis.words != words_b)
				{
					printf ("  FAIL: DecodeNSBVA_Clip dims (%u,%u,%u) != (%u,%u,%u)\n",
						vis.num_frame, vis.num_node, vis.words, nfr, nn, words_b);
					fail++;
					bad = 1;
				}
				for (uint f = 0; f < nfr && !bad; f++)
					for (uint n = 0; n < nn; n++)
					{
						const int expect = ((f * 7 + n * 3) % 5 < 2) ? 1 : 0;
						if (NSBVA_Visible (&vis, f, n) != expect)
						{
							printf ("  FAIL: BVA0 bit mismatch frame %u node %u\n", f, n);
							fail++;
							bad = 1;
							break;
						}
					}

				model_t m;
				memset (&m, 0, sizeof (m));
				if (!bad && !ParseNSBVAIntoModel (&m, bva, bva_sz, "VisibilityOne"))
				{
					printf ("  FAIL: ParseNSBVAIntoModel failed\n");
					fail++;
					bad = 1;
				}
				else if (!bad)
				{
					size_t re_sz = 0;
					u8 *re_bva = EncodeNSBVA (&m, &re_sz);
					if (!re_bva || re_sz != bva_sz || memcmp (re_bva, bva, bva_sz))
					{
						printf ("  FAIL: BVA0 model round-trip not byte-exact\n");
						fail++;
					}
					else
						printf ("  PASS: BVA0 build -> decode -> model raw round-trip\n");
					free (re_bva);
				}
				for (size_t i = 0; i < m.num_nsb_raw; i++)
					free (m.nsb_raw[i].data);
				free (m.nsb_raw);
				free (bva);
			}
		}
		free (bits);
	}

	// BTP0 (PAT0) texture-pattern animation: build two keyframe clips, decode
	// them back and exercise NSBTP_Lookup at sample frames, then push the clip
	// through the parse-into-model -> encode raw pass-through (byte-exact).
	{
		const uint nfr_tp = 40, ntex = 2, npltt = 3;
		nsb_tp_key_t k0[4] = { { 0, 0, 0 }, { 10, 1, 1 }, { 25, 0, 0xff }, { 39, 1, 2 } };
		nsb_tp_key_t k1[1] = { { 0, 0, 0 } };
		nsb_tp_clip_spec_t specs[2] = {
			{ "Mat0", k0, 4 },
			{ "Mat1", k1, 1 }
		};
		size_t btp_sz = 0;
		u8 *btp = BuildNSBTP (nfr_tp, ntex, npltt, specs, 2, &btp_sz);
		if (!btp || btp_sz == 0)
		{
			printf ("  FAIL: BuildNSBTP failed\n");
			fail++;
		}
		else
		{
			const uint32_t exp_ratio = (uint32_t)(((uint64_t)4 << 16) / nfr_tp);
			int bad = 0;
			nsb_tp_clip_t clip;
			if (!DecodeNSBTP_Clip (&clip, btp, btp_sz, 0))
			{
				printf ("  FAIL: DecodeNSBTP_Clip(0) failed\n");
				fail++;
				bad = 1;
			}
			else if (clip.num_frame != nfr_tp || clip.num_tex != ntex || clip.num_pltt != npltt
				|| clip.num_keys != 4 || clip.ratio_fx16 != exp_ratio)
			{
				printf ("  FAIL: BTP0 clip dims/ratio wrong\n");
				fail++;
				bad = 1;
			}
			static const uint chks[7][3] = {
				{ 0, 0, 0 }, { 9, 0, 0 }, { 10, 1, 1 }, { 24, 1, 1 },
				{ 25, 0, 0xff }, { 38, 0, 0xff }, { 39, 1, 2 }
			};
			for (uint i = 0; i < 7 && !bad; i++)
			{
				uint tex = 99, pltt = 99;
				if (!NSBTP_Lookup (&clip, chks[i][0], &tex, &pltt)
					|| tex != chks[i][1] || pltt != chks[i][2])
				{
					printf ("  FAIL: BTP0 lookup frame %u -> %u,%u (want %u,%u)\n",
						chks[i][0], tex, pltt, chks[i][1], chks[i][2]);
					fail++;
					bad = 1;
				}
			}
			nsb_tp_clip_t clip1;
			if (!bad && !DecodeNSBTP_Clip (&clip1, btp, btp_sz, 1))
			{
				printf ("  FAIL: DecodeNSBTP_Clip(1) failed\n");
				fail++;
				bad = 1;
			}
			else if (!bad)
			{
				uint tex = 99, pltt = 99;
				if (!NSBTP_Lookup (&clip1, 37, &tex, &pltt) || tex != 0 || pltt != 0)
				{
					printf ("  FAIL: BTP0 clip1 lookup failed\n");
					fail++;
					bad = 1;
				}
			}
			if (!bad && DecodeNSBTP_Clip (&clip, btp, btp_sz, 2))
			{
				printf ("  FAIL: DecodeNSBTP_Clip out-of-range should fail\n");
				fail++;
				bad = 1;
			}
			model_t m;
			memset (&m, 0, sizeof (m));
			if (!bad && !ParseNSBTPIntoModel (&m, btp, btp_sz, "Patterns"))
			{
				printf ("  FAIL: ParseNSBTPIntoModel failed\n");
				fail++;
				bad = 1;
			}
			else if (!bad)
			{
				size_t re_sz = 0;
				u8 *re_btp = EncodeNSBTP (&m, &re_sz);
				if (!re_btp || re_sz != btp_sz || memcmp (re_btp, btp, btp_sz))
				{
					printf ("  FAIL: BTP0 model round-trip not byte-exact\n");
					fail++;
				}
				else
					printf ("  PASS: BTP0 build -> decode -> model raw round-trip\n");
				free (re_btp);
			}
			for (size_t i = 0; i < m.num_nsb_raw; i++)
				free (m.nsb_raw[i].data);
			free (m.nsb_raw);
			free (btp);
		}
	}

	// BMA0 (MAT0) material-colour animation: two clips, interpolated lookup
	// per colour channel and a byte-exact model raw pass-through.
	{
		const uint nfr_ma = 30;
		nsb_ma_key_t k0[3] = {
			{ 0,  { 0xffffffff, 0xff808080, 0xff222222, 0xff000000, 0x000000ff } },
			{ 15, { 0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff, 0x00800000 } },
			{ 29, { 0xff123456, 0xffffffff, 0xffabcdef, 0xff102030, 0x0000ff00 } }
		};
		nsb_ma_key_t k1[1] = {
			{ 0, { 0x00123456, 0x00112233, 0x00445566, 0x00778899, 0x00aabbcc } }
		};
		nsb_ma_clip_spec_t specs[2] = { { "MatA", k0, 3 }, { "MatB", k1, 1 } };
		size_t bma_sz = 0;
		u8 *bma = BuildNSBMA (nfr_ma, specs, 2, &bma_sz);
		if (!bma || bma_sz == 0)
		{
			printf ("  FAIL: BuildNSBMA failed\n");
			fail++;
		}
		else
		{
			const uint32_t exp_ratio = (uint32_t)(((uint64_t)3 << 16) / nfr_ma);
			int bad = 0;
			nsb_ma_clip_t clip;
			if (!DecodeNSBMA_Clip (&clip, bma, bma_sz, 0))
			{
				printf ("  FAIL: DecodeNSBMA_Clip(0) failed\n");
				fail++;
				bad = 1;
			}
			else if (clip.num_frame != nfr_ma || clip.num_channels != 5
				|| clip.num_keys != 3 || clip.ratio_fx16 != exp_ratio)
			{
				printf ("  FAIL: BMA0 clip dims/ratio wrong\n");
				fail++;
				bad = 1;
			}
			// exact keys and a midpoint channel blend (frame 7 sits halfway
			// between key 0 and key 1, so each byte averages the two)
			static const uint chk[4][2] = { { 0, 0 }, { 15, 2 }, { 29, 4 }, { 7, 0 } };
			static const uint32_t want[4] = {
				0xffffffff, 0xffff0000, 0x0000ff00, 0xff8888ff
			};
			for (uint i = 0; i < 4 && !bad; i++)
			{
				uint32_t col = 0;
				if (!NSBMA_Lookup (&clip, chk[i][1], chk[i][0], &col) || col != want[i])
				{
					printf ("  FAIL: BMA0 lookup f%u ch%u = %08x (want %08x)\n",
						chk[i][0], chk[i][1], col, want[i]);
					fail++;
					bad = 1;
				}
			}
			// single-key clip: any frame returns its key's colours
			nsb_ma_clip_t clip1;
			if (!bad && !DecodeNSBMA_Clip (&clip1, bma, bma_sz, 1))
			{
				printf ("  FAIL: DecodeNSBMA_Clip(1) failed\n");
				fail++;
				bad = 1;
			}
			else if (!bad)
			{
				uint32_t col = 0;
				if (!NSBMA_Lookup (&clip1, 4, 29, &col) || col != 0x00aabbcc)
				{
					printf ("  FAIL: BMA0 clip1 lookup failed\n");
					fail++;
					bad = 1;
				}
			}
			uint32_t col = 0;
			if (!bad && DecodeNSBMA_Clip (&clip, bma, bma_sz, 2))
			{
				printf ("  FAIL: DecodeNSBMA_Clip out-of-range should fail\n");
				fail++;
				bad = 1;
			}
			if (!bad && NSBMA_Lookup (&clip, 0, nfr_ma, &col))
			{
				printf ("  FAIL: NSBMA_Lookup out-of-range frame should fail\n");
				fail++;
				bad = 1;
			}
			model_t m;
			memset (&m, 0, sizeof (m));
			if (!bad && !ParseNSBMAIntoModel (&m, bma, bma_sz, "Mats"))
			{
				printf ("  FAIL: ParseNSBMAIntoModel failed\n");
				fail++;
				bad = 1;
			}
			else if (!bad)
			{
				size_t re_sz = 0;
				u8 *re_bma = EncodeNSBMA (&m, &re_sz);
				if (!re_bma || re_sz != bma_sz || memcmp (re_bma, bma, bma_sz))
				{
					printf ("  FAIL: BMA0 model round-trip not byte-exact\n");
					fail++;
				}
				else
					printf ("  PASS: BMA0 build -> decode -> model raw round-trip\n");
				free (re_bma);
			}
			for (size_t i = 0; i < m.num_nsb_raw; i++)
				free (m.nsb_raw[i].data);
			free (m.nsb_raw);
			free (bma);
		}
	}

	// BTA0 (SRT0) texture-SRT animation: fx1.10.5 parameters, interpolated
	// lookup and a byte-exact model raw pass-through.
	{
		const uint nfr_ta = 24;
		nsb_ta_key_t k0[3] = {
			{ 0,  { 32, 32, 0, -64, 64 } },
			{ 12, { 32, 64, 16, 0, 32 } },
			{ 23, { 32, 32, -16, 64, 0 } }
		};
		nsb_ta_key_t k1[1] = { { 0, { 32, 32, 0, 0, 0 } } };
		nsb_ta_clip_spec_t specs[2] = { { "Tex0", k0, 3 }, { "Tex1", k1, 1 } };
		size_t bta_sz = 0;
		u8 *bta = BuildNSBTA (nfr_ta, specs, 2, &bta_sz);
		if (!bta || bta_sz == 0)
		{
			printf ("  FAIL: BuildNSBTA failed\n");
			fail++;
		}
		else
		{
			const uint32_t exp_ratio = (uint32_t)(((uint64_t)3 << 16) / nfr_ta);
			int bad = 0;
			nsb_ta_clip_t clip;
			if (!DecodeNSBTA_Clip (&clip, bta, bta_sz, 0))
			{
				printf ("  FAIL: DecodeNSBTA_Clip(0) failed\n");
				fail++;
				bad = 1;
			}
			else if (clip.num_frame != nfr_ta || clip.num_keys != 3
				|| clip.ratio_fx16 != exp_ratio)
			{
				printf ("  FAIL: BTA0 clip dims/ratio wrong\n");
				fail++;
				bad = 1;
			}
			// frame 0 and 12 are exact keys; frame 6 is the exact midpoint,
			// so each parameter averages the two surrounding keys
			static const uint chk[3][6] = {
				{ 0,  32, 32, 0, -64, 64 },
				{ 12, 32, 64, 16, 0, 32 },
				{ 6,  32, 48, 8, -32, 48 }
			};
			for (uint i = 0; i < 3 && !bad; i++)
			{
				int16_t v[5];
				if (!NSBTA_Lookup (&clip, chk[i][0], v)
					|| v[0] != (int16_t)chk[i][1] || v[1] != (int16_t)chk[i][2]
					|| v[2] != (int16_t)chk[i][3] || v[3] != (int16_t)chk[i][4]
					|| v[4] != (int16_t)chk[i][5])
				{
					printf ("  FAIL: BTA0 lookup frame %u got %d,%d,%d,%d,%d (want %u,%u,%u,%u,%u)\n",
						chk[i][0], v[0], v[1], v[2], v[3], v[4],
						chk[i][1], chk[i][2], chk[i][3], chk[i][4], chk[i][5]);
					fail++;
					bad = 1;
				}
			}
			// last key holds for frames 23..23 (frame 23 is the final key)
			int16_t v[5];
			if (!bad && !NSBTA_Lookup (&clip, 23, v))
			{
				printf ("  FAIL: BTA0 last-key lookup failed\n");
				fail++;
				bad = 1;
			}
			nsb_ta_clip_t clip1;
			if (!bad && !DecodeNSBTA_Clip (&clip1, bta, bta_sz, 1))
			{
				printf ("  FAIL: DecodeNSBTA_Clip(1) failed\n");
				fail++;
				bad = 1;
			}
			if (!bad && (DecodeNSBTA_Clip (&clip, bta, bta_sz, 2)
				|| NSBTA_Lookup (&clip, nfr_ta, v)))
			{
				printf ("  FAIL: BTA0 out-of-range should fail\n");
				fail++;
				bad = 1;
			}
			model_t m;
			memset (&m, 0, sizeof (m));
			if (!bad && !ParseNSBTAIntoModel (&m, bta, bta_sz, "SRTs"))
			{
				printf ("  FAIL: ParseNSBTAIntoModel failed\n");
				fail++;
				bad = 1;
			}
			else if (!bad)
			{
				size_t re_sz = 0;
				u8 *re_bta = EncodeNSBTA (&m, &re_sz);
				if (!re_bta || re_sz != bta_sz || memcmp (re_bta, bta, bta_sz))
				{
					printf ("  FAIL: BTA0 model round-trip not byte-exact\n");
					fail++;
				}
				else
					printf ("  PASS: BTA0 build -> decode -> model raw round-trip\n");
				free (re_bta);
			}
			for (size_t i = 0; i < m.num_nsb_raw; i++)
				free (m.nsb_raw[i].data);
			free (m.nsb_raw);
			free (bta);
		}
	}

	// 12. Test BCK0 (CHR0) character animation: build, sample, pass-through
	{
		const uint32_t nfr_ck = 16;
		int32_t arm_s1[16];
		for (uint i = 0; i < nfr_ck; i++)
			arm_s1[i] = (int32_t)(4096 + i * 32);
		static const int32_t arm_s2[16]
			= { 4096, 4096, 3000, 3000, 2000, 2000, 1000, 1000, 200, 200, 100, 100, 50, 50, 25, 25 };
		static const int32_t head_s4[16]
			= { 4096, 4096, 4096, 4096, 2048, 2048, 2048, 2048, 0, 0, 0, 0, -2048, -2048, -2048, -2048 };

		nsb_ck_node_spec_t arm_node, head_node;
		memset (&arm_node, 0, sizeof (arm_node));
		arm_node.name = "Arm";
		arm_node.mode[0] = NSB_CHR_AXIS_ANIM; arm_node.step[0] = 1; arm_node.keys[0] = arm_s1;
		arm_node.mode[4] = NSB_CHR_AXIS_ANIM; arm_node.step[4] = 2; arm_node.keys[4] = arm_s2;
		arm_node.mode[8] = NSB_CHR_AXIS_CONST; arm_node.row_major[8] = 1024;
		arm_node.mode[9] = NSB_CHR_AXIS_CONST; arm_node.pos[0] = 512;
		memset (&head_node, 0, sizeof (head_node));
		head_node.name = "Head";
		head_node.mode[0] = NSB_CHR_AXIS_ANIM; head_node.step[0] = 4; head_node.keys[0] = head_s4;
		head_node.mode[9] = NSB_CHR_AXIS_CONST; head_node.pos[0] = 128;

		nsb_ck_clip_spec_t clips[2];
		memset (clips, 0, sizeof (clips));
		clips[0].name = "Walk"; clips[0].nodes = &arm_node; clips[0].num_nodes = 1;
		clips[1].name = "Jump"; clips[1].nodes = &head_node; clips[1].num_nodes = 1;

		size_t ck_sz = 0;
		u8 *bck = BuildNSBCK (nfr_ck, clips, 2, &ck_sz);
		if (!bck || !ck_sz)
		{
			printf ("  FAIL: BuildNSBCK failed\n");
			fail++;
		}
		else
		{
			int bad = 0;
			nsb_ck_clip_t clip, clip1;
			if (!DecodeNSBCK_Clip (&clip, bck, ck_sz, 0)
				|| clip.num_frame != nfr_ck || clip.num_node != 1)
			{
				printf ("  FAIL: DecodeNSBCK_Clip(0) failed\n");
				fail++;
				bad = 1;
			}
			float m[9], pos[3];
			// Arm f3: axis0 ramp 4192, axis4 pair (2,3)->3000, axis8 const,
			// axis9 const; off-diagonal basis cells stay identity/zero.
			if (!bad && (!NSBCK_SampleMatrix (&clip, 0, 3, m, pos)
				|| m[0] != 4192.0f / 4096.0f || m[4] != 3000.0f / 4096.0f
				|| m[8] != 1024.0f / 4096.0f || pos[0] != 512.0f / 4096.0f
				|| m[1] != 0.0f || m[3] != 0.0f || pos[1] != 0.0f || pos[2] != 0.0f))
			{
				printf ("  FAIL: NSBCK_SampleMatrix(Arm,f3) mismatch\n");
				fail++;
				bad = 1;
			}
			// step2 compression: f5 -> pair 2 (2000), f0 -> pair 0 (4096)
			if (!bad && (!NSBCK_SampleMatrix (&clip, 0, 5, m, pos)
				|| m[4] != 2000.0f / 4096.0f))
			{
				printf ("  FAIL: NSBCK_SampleMatrix(Arm,f5) mismatch\n");
				fail++;
				bad = 1;
			}
			// step4 compression: Head f12/f13 -> quad 3 (-2048), f4 -> quad 1
			if (!bad && (!DecodeNSBCK_Clip (&clip1, bck, ck_sz, 1)
				|| !NSBCK_SampleMatrix (&clip1, 0, 12, m, pos)
				|| m[0] != -2048.0f / 4096.0f || pos[0] != 128.0f / 4096.0f))
			{
				printf ("  FAIL: NSBCK_SampleMatrix(Head,f12) mismatch\n");
				fail++;
				bad = 1;
			}
			if (!bad && (!NSBCK_SampleMatrix (&clip1, 0, 13, m, pos)
				|| m[0] != -2048.0f / 4096.0f
				|| !NSBCK_SampleMatrix (&clip1, 0, 4, m, pos)
				|| m[0] != 2048.0f / 4096.0f))
			{
				printf ("  FAIL: NSBCK_SampleMatrix(Head,step4) mismatch\n");
				fail++;
				bad = 1;
			}
			if (!bad && (DecodeNSBCK_Clip (&clip, bck, ck_sz, 2)
				|| NSBCK_SampleMatrix (&clip, 5, 0, m, pos)
				|| NSBCK_SampleMatrix (&clip, 0, nfr_ck, m, pos)))
			{
				printf ("  FAIL: BCK0 out-of-range should fail\n");
				fail++;
				bad = 1;
			}
			if (!bad)
				printf ("  PASS: BCK0 build -> decode -> sample\n");
			model_t m2;
			memset (&m2, 0, sizeof (m2));
			if (!bad && !ParseNSBCKIntoModel (&m2, bck, ck_sz, "CHRs"))
			{
				printf ("  FAIL: ParseNSBCKIntoModel failed\n");
				fail++;
				bad = 1;
			}
			else if (!bad)
			{
				size_t re_sz = 0;
				u8 *re_bck = EncodeNSBCK (&m2, &re_sz);
				if (!re_bck || re_sz != ck_sz || memcmp (re_bck, bck, ck_sz))
				{
					printf ("  FAIL: BCK0 model round-trip not byte-exact\n");
					fail++;
				}
				else
					printf ("  PASS: BCK0 build -> decode -> model raw round-trip\n");
				free (re_bck);
			}
			for (size_t i = 0; i < m2.num_nsb_raw; i++)
				free (m2.nsb_raw[i].data);
			free (m2.nsb_raw);
			free (bck);
		}
	}

	printf ("=== Results: %s (failures: %d) ===\n", fail == 0 ? "ALL PASSED" : "SOME FAILED", fail);
	return fail;
}
