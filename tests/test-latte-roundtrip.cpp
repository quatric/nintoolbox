#include "latte_bridge.h"

#include <cstdio>
#include <cstring>
#include <string>

int main ()
{
	static const char source[] = "00 EXP_DONE: PIX0, R0.xyzw\n"
								 "END_OF_PROGRAM\n";

	size_t binary_size = 0;
	unsigned char *binary = szs_latte_assemble (source, sizeof (source) - 1, &binary_size);
	if (!binary || binary_size != 8)
	{
		std::fprintf (stderr, "assembly failed or returned %zu bytes\n", binary_size);
		szs_latte_free (binary);
		return 1;
	}

	char *listing = szs_latte_disassemble (binary, binary_size);
	if (!listing || !std::strstr (listing, "EXP_DONE: PIX0, R0.xyzw"))
	{
		std::fprintf (stderr, "semantic disassembly did not preserve the export\n");
		szs_latte_free (listing);
		szs_latte_free (binary);
		return 1;
	}

	size_t rebuilt_size = 0;
	unsigned char *rebuilt = szs_latte_assemble (listing, std::strlen (listing), &rebuilt_size);
	const bool equal
		= rebuilt && rebuilt_size == binary_size && !std::memcmp (rebuilt, binary, binary_size);
	szs_latte_free (rebuilt);
	szs_latte_free (listing);
	szs_latte_free (binary);
	if (!equal)
	{
		std::fprintf (stderr, "semantic round trip changed the program\n");
		return 1;
	}
	return 0;
}
