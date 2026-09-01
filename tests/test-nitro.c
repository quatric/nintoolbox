#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "lib-nintendo.h"
#include "lib-nitro.h"
#include "lib-smdh.h"
#ifdef __cplusplus
}
#endif

int main (void)
{
	int fail = 0;
	printf("=== Testing NitroPaint additions ===\n");

	// 1. Test Diff8 / Diff16
	{
		const u8 test_data[] = "Hello World! This is a differential compression test sequence 1234567890.";
		const uint len = sizeof (test_data);
		u8 *enc8 = 0, *dec8 = 0;
		uint enc8_sz = 0, dec8_sz = 0;

		enumError e1 = EncodeDiff8 (&enc8, &enc8_sz, test_data, len);
		enumError e2 = DecodeDiff8 (&dec8, &dec8_sz, enc8, enc8_sz);
		if (e1 || e2 || dec8_sz != len || memcmp (dec8, test_data, len))
		{
			printf("  FAIL: Diff8 encode/decode mismatch\n");
			fail++;
		}
		else
		{
			printf("  PASS: Diff8 encode/decode roundtrip\n");
		}
		free (enc8); free (dec8);

		u8 *enc16 = 0, *dec16 = 0;
		uint enc16_sz = 0, dec16_sz = 0;
		e1 = EncodeDiff16 (&enc16, &enc16_sz, test_data, len);
		e2 = DecodeDiff16 (&dec16, &dec16_sz, enc16, enc16_sz);
		if (e1 || e2 || dec16_sz < len || memcmp (dec16, test_data, len))
		{
			printf("  FAIL: Diff16 encode/decode mismatch\n");
			fail++;
		}
		else
		{
			printf("  PASS: Diff16 encode/decode roundtrip\n");
		}
		free (enc16); free (dec16);
	}

	// 2. Test PuCrunch
	{
		const u8 test_data[] = "The quick brown fox jumps over the lazy dog. AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVVWWXXYYZZ";
		const uint len = sizeof (test_data);
		u8 *enc_pc = 0, *dec_pc = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodePuCrunch (&enc_pc, &enc_sz, test_data, len);
		if (e1 || !CxIsCompressedPuCrunch (enc_pc, enc_sz))
		{
			printf("  FAIL: PuCrunch encode failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodePuCrunch (&dec_pc, &dec_sz, enc_pc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_pc, test_data, len))
			{
				printf("  FAIL: PuCrunch decode mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: PuCrunch encode/decode roundtrip\n");
			}
			free (dec_pc);
		}
		free (enc_pc);
	}

	// 3. Test LZX
	{
		const u8 test_data[] = "LZX compression format test: repeating patterns repeating patterns repeating patterns 12345 12345!";
		const uint len = sizeof (test_data);
		u8 *enc_lzx = 0, *dec_lzx = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeLZX (&enc_lzx, &enc_sz, test_data, len);
		if (e1 || !CxIsCompressedLZX (enc_lzx, enc_sz))
		{
			printf("  FAIL: LZX encode failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeLZX (&dec_lzx, &dec_sz, enc_lzx, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_lzx, test_data, len))
			{
				printf("  FAIL: LZX decode mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: LZX encode/decode roundtrip\n");
			}
			free (dec_lzx);
		}
		free (enc_lzx);
	}

	// 4. Test VLX
	{
		const u8 test_data[] = "VLX format test: Pac-Man World DS namco compression literal and repeat stream test abcdef";
		const uint len = sizeof (test_data);
		u8 *enc_vlx = 0, *dec_vlx = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeVLX (&enc_vlx, &enc_sz, test_data, len);
		if (e1 || !CxIsCompressedVlx (enc_vlx, enc_sz))
		{
			printf("  FAIL: VLX encode failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeVLX (&dec_vlx, &dec_sz, enc_vlx, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_vlx, test_data, len))
			{
				printf("  FAIL: VLX decode mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: VLX encode/decode roundtrip\n");
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
		enumError e1 = CreateNSBTX (&btx, &btx_sz, rgba_in, w, h, NITRO_TEXFMT_DIRECT, "test_tex", 0);
		if (e1 || !btx || btx_sz < 0x20)
		{
			printf("  FAIL: CreateNSBTX failed\n");
			fail++;
		}
		else
		{
			nfmt_info_t nfmt = DetectNintendoFormat (btx, btx_sz, "test.nsbtx");
			if (nfmt.type != NFMT_NSBTX)
			{
				printf("  FAIL: DetectNintendoFormat failed for NSBTX\n");
				fail++;
			}
			else
			{
				u8 *rgba_out = 0;
				uint out_w = 0, out_h = 0;
				enumError e2 = DecodeNSBTX_RGBA (&rgba_out, &out_w, &out_h, btx, btx_sz);
				if (e2 || out_w != w || out_h != h || !rgba_out)
				{
					printf("  FAIL: DecodeNSBTX_RGBA failed\n");
					fail++;
				}
				else
				{
					printf("  PASS: NSBTX create -> detect -> decode roundtrip (%ux%u)\n", out_w, out_h);
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
			printf("  FAIL: EncodeNFTR_Atlas failed\n");
			fail++;
		}
		else
		{
			nfmt_info_t nfmt = DetectNintendoFormat (nftr, nftr_sz, "font.nftr");
			if (nfmt.type != NFMT_NFTR)
			{
				printf("  FAIL: DetectNintendoFormat failed for NFTR\n");
				fail++;
			}
			else
			{
				u8 *atlas_out = 0;
				uint out_w = 0, out_h = 0;
				char *xml_out = 0;
				enumError e2 = DecodeNFTR_Atlas (&atlas_out, &out_w, &out_h, &xml_out, nftr, nftr_sz);
				if (e2 || out_w != aw || out_h != ah || !atlas_out || !xml_out)
				{
					printf("  FAIL: DecodeNFTR_Atlas failed\n");
					fail++;
				}
				else
				{
					printf("  PASS: NFTR font encode -> detect -> decode atlas (%ux%u)\n", out_w, out_h);
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
			printf("  FAIL: Encode5TX_RGBA failed\n");
			fail++;
		}
		else
		{
			u8 *rgba_out = 0;
			uint out_w = 0, out_h = 0;
			enumError e2 = Decode5TX_RGBA (&rgba_out, &out_w, &out_h, fivetx, fivetx_sz);
			if (e2 || out_w != w || out_h != h || !rgba_out)
			{
				printf("  FAIL: Decode5TX_RGBA failed\n");
				fail++;
			}
			else
			{
				printf("  PASS: 5TX image encode -> decode (%ux%u)\n", out_w, out_h);
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
			printf("  FAIL: EncodeBNLL_Text failed\n");
			fail++;
		}
		else
		{
			char *txt = 0;
			enumError e2 = DecodeBNLL_Text (&txt, bnll, bnll_sz);
			if (e2 || !txt || !strstr (txt, "BNLL"))
			{
				printf("  FAIL: DecodeBNLL_Text failed\n");
				fail++;
			}
			else
			{
				printf("  PASS: BNLL layout encode -> disassemble text\n");
			}
			free (txt);
			free (bnll);
		}
	}

	// 9. Test LZOvl (NDS Overlay reverse compression)
	{
		const u8 test_data[] = "Reverse LZ overlay compression test payload for ARM9 overlay 0123456789";
		const uint len = sizeof (test_data);
		u8 *enc_ovl = 0, *dec_ovl = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeLZOvl (&enc_ovl, &enc_sz, test_data, len);
		if (e1 || !enc_ovl || enc_sz < 8)
		{
			printf("  FAIL: EncodeLZOvl failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeLZOvl (&dec_ovl, &dec_sz, enc_ovl, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec_ovl, test_data, len))
			{
				printf("  FAIL: DecodeLZOvl roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: LZOvl reverse compression roundtrip\n");
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
			printf("  FAIL: DecodeALAR failed\n");
			fail++;
		}
		else
		{
			printf("  PASS: ALAR archive unpack\n");
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
			printf("  FAIL: DecodeDARC failed\n");
			fail++;
		}
		else
		{
			printf("  PASS: DARC archive unpack\n");
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
			printf("  FAIL: DecodeSADL_WAV failed\n");
			fail++;
		}
		else
		{
			printf("  PASS: SADL audio -> WAV decode\n");
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
			printf("  FAIL: EncodeNCER_Text failed\n");
			fail++;
		}
		else
		{
			char *txt = 0;
			enumError e2 = DecodeNCER_Text (&txt, ncer, ncer_sz);
			if (e2 || !txt || !strstr (txt, "NCER"))
			{
				printf("  FAIL: DecodeNCER_Text failed\n");
				fail++;
			}
			else
			{
				printf("  PASS: NCER 2D cell encode -> disassemble text\n");
			}
			free (txt);
			free (ncer);
		}

		u8 *nanr = 0;
		uint nanr_sz = 0;
		e1 = EncodeNANR_Text (&nanr, &nanr_sz, "anim text");
		if (e1 || !nanr || nanr_sz < 0x20)
		{
			printf("  FAIL: EncodeNANR_Text failed\n");
			fail++;
		}
		else
		{
			char *txt = 0;
			enumError e2 = DecodeNANR_Text (&txt, nanr, nanr_sz);
			if (e2 || !txt || !strstr (txt, "NANR"))
			{
				printf("  FAIL: DecodeNANR_Text failed\n");
				fail++;
			}
			else
			{
				printf("  PASS: NANR 2D animation encode -> disassemble text\n");
			}
			free (txt);
			free (nanr);
		}
	}

	// 14. Test PSDK (Prosonic SDK LZ)
	{
		const u8 psdk_test[] = "PSDK Prosonic SDK compression test payload with some repeating repeating text 123456789";
		const uint len = sizeof (psdk_test);
		u8 *enc = 0, *dec = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodePSDK (&enc, &enc_sz, psdk_test, len);
		if (e1 || !enc || enc_sz < 8)
		{
			printf("  FAIL: EncodePSDK failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodePSDK (&dec, &dec_sz, enc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec, psdk_test, len))
			{
				printf("  FAIL: DecodePSDK roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: PSDK encode -> decode roundtrip\n");
			}
			free (dec);
			free (enc);
		}
	}

	// 15. Test MVDK (Mario vs. Donkey Kong compression)
	{
		const u8 mvdk_test[] = "Mario vs Donkey Kong custom LZ compression test payload string 0123456789";
		const uint len = sizeof (mvdk_test);
		u8 *enc = 0, *dec = 0;
		uint enc_sz = 0, dec_sz = 0;

		enumError e1 = EncodeMVDK (&enc, &enc_sz, mvdk_test, len);
		if (e1 || !enc || enc_sz < 4)
		{
			printf("  FAIL: EncodeMVDK failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeMVDK (&dec, &dec_sz, enc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec, mvdk_test, len))
			{
				printf("  FAIL: DecodeMVDK roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: MVDK encode -> decode roundtrip\n");
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
			printf("  FAIL: EncodeSSZL failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeSSZL (&dec, &dec_sz, enc, enc_sz);
			if (e2 || dec_sz != len || memcmp (dec, sszl_test, len))
			{
				printf("  FAIL: DecodeSSZL roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: SSZL encode -> decode roundtrip\n");
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
			printf("  FAIL: EncodeSMDH failed\n");
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
				printf("  FAIL: ScanSMDH roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: SMDH encode -> scan roundtrip\n");
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
			printf("  FAIL: EncodeNUTEXB_RGBA failed\n");
			fail++;
		}
		else
		{
			enumError e2 = DecodeNUTEXB_RGBA (&dec, &dec_w, &dec_h, enc, enc_sz);
			if (e2 || dec_w != w || dec_h != h || memcmp (dec, rgba_in, sizeof (rgba_in)))
			{
				printf("  FAIL: DecodeNUTEXB_RGBA roundtrip mismatch\n");
				fail++;
			}
			else
			{
				printf("  PASS: NUTEXB encode -> decode roundtrip (16x16)\n");
			}
			free (dec);
			free (enc);
		}
	}

	printf("=== Results: %s (failures: %d) ===\n", fail == 0 ? "ALL PASSED" : "SOME FAILED", fail);
	return fail;
}