#include "bcn_wrapper.h"
#include "bcn.h"

#include <cstddef>

static void bgra_to_rgba ( uint8_t *pixels, size_t count )
{
    for (size_t i=0; i<count; i++, pixels+=4)
    {
        const uint8_t blue=pixels[0];
        pixels[0]=pixels[2];
        pixels[2]=blue;
    }
}

extern "C" int szs_decode_bc6
    ( const uint8_t *src, uint32_t width, uint32_t height,
      int is_signed, uint8_t *rgba )
{
    if (!src || !width || !height || !rgba) return 0;
    const int ok = is_signed
        ? decode_bc6_signed(src,width,height,reinterpret_cast<uint32_t*>(rgba))
        : decode_bc6(src,width,height,reinterpret_cast<uint32_t*>(rgba));
    if (ok) bgra_to_rgba(rgba,static_cast<size_t>(width)*height);
    return ok;
}

extern "C" int szs_decode_bc6_half
    ( const uint8_t *src, uint32_t width, uint32_t height,
      int is_signed, uint16_t *out )
{
    if (!src || !width || !height || !out) return 0;
    return decode_bc6_half(src,width,height,is_signed,out);
}

extern "C" int szs_decode_bc7
    ( const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba )
{
    if (!src || !width || !height || !rgba) return 0;
    const int ok=decode_bc7(src,width,height,reinterpret_cast<uint32_t*>(rgba));
    if (ok) bgra_to_rgba(rgba,static_cast<size_t>(width)*height);
    return ok;
}
