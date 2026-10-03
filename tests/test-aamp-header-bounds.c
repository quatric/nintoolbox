#include "lib-aamp.h"
#include "lib-std.h"
#include <assert.h>
#include <stdio.h>

int main (void)
{
    for (uint size = 12; size < 24; size++)
    {
        u8 *data = CALLOC (1, size);
        memcpy (data, "AAMP", 4);
        write_le32 (data + 4, 1);
        aamp_file_t aamp;
        assert (ScanAAMP (&aamp, data, size) == ERR_INVALID_DATA);
        ResetAAMP (&aamp);
        FREE (data);
    }
    u8 data[48] = { 0 };
    memcpy (data, "AAMP", 4);
    write_le32 (data + 4, 1);
    write_le32 (data + 20, 0xffffffff);
    aamp_file_t aamp;
    assert (ScanAAMP (&aamp, data, sizeof (data)) == ERR_INVALID_DATA);
    ResetAAMP (&aamp);
    write_le32 (data + 4, 2);
    write_le32 (data + 20, 0);
    assert (ScanAAMP (&aamp, data, sizeof (data)) == ERR_INVALID_DATA);
    ResetAAMP (&aamp);
    puts ("AAMP header bounds passed");
    return 0;
}
