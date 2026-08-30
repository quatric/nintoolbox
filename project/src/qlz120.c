// QuickLZ 1.20 compiled as a namespaced translation unit.
//
// The vendor drop (wqlz_comp_120.c) is a complete program: it exports the
// codec AND a main(). Both QuickLZ versions export the same symbol names, so
// linking the two together needs one of them renamed. Renaming here rather
// than editing the vendor file keeps that file byte-identical to upstream and
// makes future re-drops a straight copy.
//
// Every non-static symbol the vendor file exports is listed below; the set was
// taken from nm(1) on the compiled object, not guessed.

#define main qlz120_vendor_main_unused
#define fast_read qlz120_fast_read
#define fast_read_safe qlz120_fast_read_safe
#define fast_write qlz120_fast_write
#define memcpy_up qlz120_memcpy_up
#define qlz_compress qlz120_compress
#define qlz_compress_core qlz120_compress_core
#define qlz_compress_packet qlz120_compress_packet
#define qlz_decompress qlz120_decompress
#define qlz_decompress_core qlz120_decompress_core
#define qlz_decompress_packet qlz120_decompress_packet
#define qlz_size_compressed qlz120_size_compressed
#define qlz_size_decompressed qlz120_size_decompressed

#include "wqlz_comp_120.c"
