#include "lib-std.h"
#include "lib-miirender.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

bool IsMiiData (const u8 *data, uint size, ccp filename)
{
	if (filename)
	{
		ccp ext = strrchr (filename, '.');
		if (ext)
		{
			if (!strcasecmp (ext, ".ffsd") || !strcasecmp (ext, ".ffcd") ||
			    !strcasecmp (ext, ".rsd") || !strcasecmp (ext, ".rcd") ||
			    !strcasecmp (ext, ".cflsd") || !strcasecmp (ext, ".cfcd") ||
			    !strcasecmp (ext, ".aflsd") || !strcasecmp (ext, ".afcd") ||
			    !strcasecmp (ext, ".nflsd") || !strcasecmp (ext, ".nfcd") ||
			    !strcasecmp (ext, ".miigsd") || !strcasecmp (ext, ".mii") ||
			    !strcasecmp (ext, ".mnms"))
				return true;
		}
	}
	if (data && size)
	{
		// Common Mii Data sizes:
		// 74 bytes (RFLStoreData without checksum)
		// 76 bytes (RFLStoreData with CRC16)
		// 88 bytes (Gen2 / Gen3 CoreData)
		// 92 bytes (FFLCoreData)
		// 96 bytes (FFLStoreData / CFLStoreData / AFLStoreData)
		// 756 bytes (RFLCustomData)
		if (size == 74 || size == 76 || size == 88 || size == 92 || size == 96 || size == 756)
			return true;
	}
	return false;
}

enumError RenderMiiPNG (u8 **dest_png, uint *dest_size, const u8 *data, uint size, uint width)
{
	if (!dest_png || !dest_size || !data || !size)
		return EINVAL;

	*dest_png = 0;
	*dest_size = 0;

	// Convert raw binary to hex string
	char *hex = MALLOC (size * 2 + 1);
	if (!hex)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < size; i++)
		snprintf (hex + i * 2, 3, "%02x", data[i]);
	hex[size * 2] = '\0';

	char cmd[4096];
	uint w = width ? width : 512;
	snprintf (cmd, sizeof (cmd), "curl -s -f \"https://mii-unsecure.ariankordi.net/miis/image.png?data=%s&width=%u\"", hex, w);
	FREE (hex);

	FILE *p = popen (cmd, "r");
	if (!p)
		return ERR_CANT_OPEN;

	uint cap = 64 * 1024;
	uint used = 0;
	u8 *buf = MALLOC (cap);
	if (!buf)
	{
		pclose (p);
		return ERR_CANT_CREATE;
	}

	size_t n;
	while ((n = fread (buf + used, 1, cap - used, p)) > 0)
	{
		used += (uint)n;
		if (used == cap)
		{
			cap *= 2;
			u8 *new_buf = REALLOC (buf, cap);
			if (!new_buf)
			{
				FREE (buf);
				pclose (p);
				return ERR_CANT_CREATE;
			}
			buf = new_buf;
		}
	}
	pclose (p);

	if (used < 8 || memcmp (buf, "\x89PNG\r\n\x1a\n", 8))
	{
		FREE (buf);
		return ERR_INVALID_DATA;
	}

	*dest_png = buf;
	*dest_size = used;
	return ERR_OK;
}

enumError RenderMiiGLB (u8 **dest_glb, uint *dest_size, const u8 *data, uint size)
{
	if (!dest_glb || !dest_size || !data || !size)
		return EINVAL;

	*dest_glb = 0;
	*dest_size = 0;

	// Convert raw binary to hex string
	char *hex = MALLOC (size * 2 + 1);
	if (!hex)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < size; i++)
		snprintf (hex + i * 2, 3, "%02x", data[i]);
	hex[size * 2] = '\0';

	char cmd[4096];
	snprintf (cmd, sizeof (cmd), "curl -s -f \"https://mii-unsecure.ariankordi.net/miis/image.glb?data=%s\"", hex);
	FREE (hex);

	FILE *p = popen (cmd, "r");
	if (!p)
		return ERR_CANT_OPEN;

	uint cap = 128 * 1024;
	uint used = 0;
	u8 *buf = MALLOC (cap);
	if (!buf)
	{
		pclose (p);
		return ERR_CANT_CREATE;
	}

	size_t n;
	while ((n = fread (buf + used, 1, cap - used, p)) > 0)
	{
		used += (uint)n;
		if (used == cap)
		{
			cap *= 2;
			u8 *new_buf = REALLOC (buf, cap);
			if (!new_buf)
			{
				FREE (buf);
				pclose (p);
				return ERR_CANT_CREATE;
			}
			buf = new_buf;
		}
	}
	pclose (p);

	if (used < 12 || memcmp (buf, "glTF", 4))
	{
		FREE (buf);
		return ERR_INVALID_DATA;
	}

	*dest_glb = buf;
	*dest_size = used;
	return ERR_OK;
}
