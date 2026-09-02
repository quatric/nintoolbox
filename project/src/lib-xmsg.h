#ifndef SZS_LIB_XMSG_H
#define SZS_LIB_XMSG_H 1

#include "types.h"
#include "file-type.h"

// Wii Party Message and Text Archive format (.bin / XMSG / mess.bin)
// Used in Wii Party and related Nd Cube games.

#define XMSG_MAGIC "\x58\x4D\x53\x47\x20\x10\x05\x03"
#define XMSG_MAGIC_LEN 8

typedef struct xmsg_style_t
{
	u32 color;
	u32 outline;
	u8 width;
	u8 height;
	u8 horizontal_spacing;
	u8 vertical_spacing;
	u8 state_start;
	u8 state_middle;
	u8 state_end;
} xmsg_style_t;

typedef struct xmsg_message_t
{
	char *name;
	char *type;
	char *text; // UTF-8 string
	u32 style_index;
} xmsg_message_t;

typedef struct xmsg_t
{
	const u8 *data;
	size_t size;
	uint n_messages;
	xmsg_message_t *messages;
	uint n_styles;
	xmsg_style_t *styles;
} xmsg_t;

// Returns true if 'data' is a valid Wii Party XMSG message file.
bool IsXMSG (const u8 *data, size_t size);

// Scans and parses the XMSG archive container.
enumError ScanXMSG (xmsg_t *xmsg, const u8 *data, size_t size);

// Frees all memory associated with xmsg.
void ResetXMSG (xmsg_t *xmsg);

// Exports the XMSG contents into clean XML (matching Wii Party Text Editor / xmsg.py schema).
enumError ExtractXMSGXml (const xmsg_t *xmsg, char **out_text, size_t *out_size);

// Extracts the XMSG contents into human-readable plain text format.
enumError ExtractXMSGText (const xmsg_t *xmsg, char **out_text, size_t *out_size);

// Creates / compiles a binary XMSG file from in-memory structures.
enumError CreateXMSG (u8 **dest, size_t *dest_size, const xmsg_t *xmsg);

#endif // SZS_LIB_XMSG_H
