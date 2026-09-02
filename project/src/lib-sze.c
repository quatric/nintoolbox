#include "lib-std.h"
#include "lib-aes.h"
#include "lib-sze.h"
#include <string.h>
#include <errno.h>

static const u8 default_sze_key[16] = {
	0x46, 0x5a, 0x45, 0x52, 0x4f, 0x39, 0x39, 0x5f, // "FZERO99_"
	0x4e, 0x53, 0x54, 0x5f, 0x53, 0x5a, 0x45, 0x31  // "NST_SZE1"
};

enumError DecodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16])
{
	if (!dest || !dest_size || !data || size < 32)
		return EINVAL;

	*dest = 0;
	*dest_size = 0;

	if (memcmp (data, "SZE\0", 4) && memcmp (data, "SZE1", 4))
		return EINVAL;

	const u32 dec_size = rd_le32 (data + 4);
	const u32 mode = rd_le32 (data + 8);
	const u8 *iv = data + 16;
	const u8 *payload = data + 32;
	const uint payload_sz = size - 32;

	const u8 *k = key ? key : default_sze_key;

	u8 *dec = MALLOC (payload_sz);
	if (!dec)
		return ERR_CANT_CREATE;
	memcpy (dec, payload, payload_sz);

	if (mode == 0) // CBC
	{
		uint cbc_sz = payload_sz & ~15u;
		AES128_CBC_Decrypt (k, iv, dec, cbc_sz);
	}
	else if (mode == 2) // OFB
	{
		AES128_OFB_Crypt (k, iv, dec, payload_sz);
	}
	else // CTR (mode 1 or default)
	{
		AES128_CTR_Crypt (k, iv, dec, payload_sz);
	}

	uint out_sz = payload_sz;
	if (dec_size > 0 && dec_size <= payload_sz)
		out_sz = dec_size;

	*dest = dec;
	*dest_size = out_sz;
	return ERR_OK;
}

enumError EncodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16], const u8 iv[16], uint mode)
{
	if (!dest || !dest_size || !data)
		return EINVAL;

	uint pad_sz = size;
	if (mode == 0)
		pad_sz = (size + 15) & ~15u;

	u8 *out = CALLOC (1, 32 + (size_t)pad_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "SZE1", 4);
	wr_le32 (out + 4, size);
	wr_le32 (out + 8, mode);
	wr_le32 (out + 12, 0); // flags

	u8 actual_iv[16];
	if (iv)
	{
		memcpy (actual_iv, iv, 16);
	}
	else
	{
		for (int i = 0; i < 16; i++)
			actual_iv[i] = (u8)((size * 31 + i * 17 + 0x5a) & 0xff);
	}
	memcpy (out + 16, actual_iv, 16);

	u8 *payload = out + 32;
	memcpy (payload, data, size);

	const u8 *k = key ? key : default_sze_key;

	if (mode == 0)
	{
		AES128_CBC_Encrypt (k, actual_iv, payload, pad_sz);
	}
	else if (mode == 2)
	{
		AES128_OFB_Crypt (k, actual_iv, payload, pad_sz);
	}
	else
	{
		AES128_CTR_Crypt (k, actual_iv, payload, pad_sz);
	}

	*dest = out;
	*dest_size = 32 + pad_sz;
	return ERR_OK;
}
