#include "lib-prc.h"
#include "lib-std.h"

bool IsPRC (const u8 *data, size_t size)
{
	if (!data || size < 8)
		return false;
	if (size >= 12 && !memcmp (data, "parambinary", 11))
		return true;
	if (!memcmp (data, "PRC\0", 4) || !memcmp (data, "BPAR", 4))
		return true;
	return false;
}

enumError DecodePRC_XML (char **dest_xml, const u8 *data, size_t size)
{
	if (!dest_xml || !data || size < 8 || !IsPRC (data, size))
		return ERR_INVALID_DATA;

	// Export structure manifest as XML
	char *buf = MALLOC (size * 8 + 1024);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	int written = snprintf (buf, size * 8 + 1024,
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<struct>\n"
		"  <!-- Smash Parameter Binary (PRC) export -->\n"
		"  <param type=\"binary\" size=\"%zu\" magic=\"%.4s\"/>\n"
		"</struct>\n",
		size, (const char *)data);

	if (written < 0)
	{
		FREE (buf);
		return ERR_INVALID_DATA;
	}

	*dest_xml = buf;
	return ERR_OK;
}
