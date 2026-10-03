#include "lib-nurpdb.h"
#include "lib-std.h"
#include <assert.h>
#include <stdio.h>

int main (void)
{
    u8 data[0xb0] = { 0 };
    memcpy (data, "HBSS", 4);
    memcpy (data + 16, "DPRN", 4);
    write_le16 (data + 20, 1);
    // A table pointing near UINT64_MAX used to wrap its element bounds check.
    write_le64 (data + 24, 0xffffffffffffffe0ULL);
    write_le64 (data + 32, 1);
    FILE *out = tmpfile();
    assert (out);
    assert (DecodeNURPDB_Text (out, data, sizeof (data)) == ERR_OK);
    assert (!ferror (out));
    fclose (out);
    puts ("NURPDB bounds passed");
    return 0;
}
