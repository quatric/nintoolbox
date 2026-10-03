#include "lib-ue4pak.h"
#include "lib-std.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main (void)
{
    const char *names[] = { "first.bin", "empty.bin", "second.bin" };
    const u8 *payloads[] = { (const u8 *)"hello", NULL, (const u8 *)"world!" };
    size_t sizes[] = { 5, 0, 6 };
    u8 *data = NULL;
    size_t size = 0;
    assert (CreateUE4Pak (&data, &size, "../../../", 3, names, payloads, sizes) == ERR_OK);
    ue4_pak_t pak;
    assert (ScanUE4Pak (&pak, data, size) == ERR_OK);
    assert (pak.n_entries == 3);
    for (uint i = 0; i < 3; i++)
    {
        u8 *out = NULL;
        size_t out_size = 0;
        assert (!strcmp (pak.entries[i].filename, names[i]));
        assert (ExtractUE4PakEntry (&pak, i, &out, &out_size) == ERR_OK);
        assert (out_size == sizes[i]);
        assert (!out_size || !memcmp (out, payloads[i], out_size));
        FREE (out);
    }
    ResetUE4Pak (&pak);
    FREE (data);

    assert (CreateUE4Pak (&data, &size, NULL, 1, NULL, payloads, sizes) == ERR_INVALID_DATA);
    assert (CreateUE4Pak (&data, &size, NULL, 1, names, NULL, sizes) == ERR_INVALID_DATA);
    const u8 *missing[] = { NULL };
    assert (CreateUE4Pak (&data, &size, NULL, 1, names, missing, sizes) == ERR_INVALID_DATA);
    const char *missing_name[] = { NULL };
    assert (CreateUE4Pak (&data, &size, NULL, 1, missing_name, payloads, sizes) == ERR_INVALID_DATA);
    char long_name[PATH_MAX + 1];
    memset (long_name, 'x', sizeof (long_name));
    long_name[PATH_MAX] = 0;
    const char *long_names[] = { long_name };
    assert (CreateUE4Pak (&data, &size, NULL, 1, long_names, payloads, sizes) == ERR_INVALID_DATA);
    assert (CreateUE4Pak (&data, &size, long_name, 1, names, payloads, sizes) == ERR_INVALID_DATA);
    sizes[0] = (size_t)-1;
    assert (CreateUE4Pak (&data, &size, NULL, 1, names, payloads, sizes) == ERR_FILE_TOO_BIG);
    puts ("UE4 PAK creation regressions passed");
    return 0;
}
