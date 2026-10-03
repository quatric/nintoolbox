#include "lib-jupiterpck.h"
#include <assert.h>
#include <stdio.h>

int main (void)
{
    // Header padding near UINT32_MAX must not wrap the payload probe.
    const u8 large_header[] = {
        0xff, 0xff, 0xff, 0xff, 1, 0, 0, 0, 4, 0, 0, 0
    };
    assert (IsJupiterPck (large_header, sizeof (large_header), 0x100000003ULL));
    assert (!IsJupiterPck (large_header, sizeof (large_header), 0xffffffffULL));

    const u8 valid[] = {
        12, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 'B', 'M', 'D', '0'
    };
    assert (IsJupiterPck (valid, sizeof (valid), sizeof (valid)));
    assert (!IsJupiterPck (valid, sizeof (valid), sizeof (valid) - 1));
    assert (!IsJupiterPck (valid, 11, sizeof (valid)));
    puts ("Jupiter PCK bounds passed");
    return 0;
}
