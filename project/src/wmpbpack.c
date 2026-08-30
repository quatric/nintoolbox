#define _CRT_SECURE_NO_DEPRECATE // Allows Use of fopen and strncpy
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <zlib.h>

#define COMPRESS_TYPE_NONE 0
#define COMPRESS_TYPE_LZSS 1
#define COMPRESS_TYPE_SLIDE 2
#define COMPRESS_TYPE_FSLIDE_ALT 3
#define COMPRESS_TYPE_FSLIDE 4
#define COMPRESS_TYPE_RLE 5
#define COMPRESS_TYPE_INFLATE 7
#define DEFLATE_BUF_SIZE 16384

#define N 1024 /* size of ring buffer - must be power of 2 */
#define F 66 /* upper limit for match_length */
#define THRESHOLD                                                                                  \
	2 /* encode string into position and length                                                    \
		 if match_length is greater than this */
#define NIL N /* index for root of binary search trees */

struct encode_state
{
	/*
	 * left & right children & parent. These constitute binary search trees.
	 */
	int lchild[N + 1], rchild[N + 257], parent[N + 1];

	/* ring buffer of size N, with extra F-1 bytes to aid string comparison */
	unsigned char text_buf[N + F - 1];

	/*
	 * match_length of longest match.
	 * These are set by the insert_node() procedure.
	 */
	int match_position, match_length;
};

typedef struct ret
{
	int srcPos, dstPos;
} Ret;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

int main (int argc, char **argv);
void init_state (struct encode_state *sp);
void insert_node (struct encode_state *sp, int r);
static void delete_node (struct encode_state *sp, int p);
int CompressLZSS (char *input, int uncomp_len, FILE *fp, int offset);
u32 simpleEnc (u8 *src, int size, int pos, u32 *pMatchPos);
u32 nintendoEnc (u8 *src, int size, int pos, u32 *pMatchPos);
int CompressSlide (char *input, int uncomp_len, FILE *fp, int offset);
int CompressRLE (char *input, int uncomp_len, FILE *fp, int offset);
int CompressInflate (char *input, int uncomp_len, FILE *fp, int offset);
int GetLastSlashPos (char *string);
unsigned char ReadFileU8 (FILE *fp, int offset);
void ReadFileArray (FILE *fp, void *array, int offset, int len); // Assumes U8 Array
unsigned short ReadFileU16BigEndian (FILE *fp, int offset);
unsigned int ReadFileU32BigEndian (FILE *fp, int offset);
void WriteFileU8 (FILE *fp, int offset, unsigned char num);
void WriteFileArray (FILE *fp, void *array, int offset, int len); // Assumes U8 Array
void WriteFileU16BigEndian (FILE *fp, int offset, unsigned short num);
void WriteFileU32BigEndian (FILE *fp, int offset, unsigned int num);
void WriteFileFloatBigEndian (FILE *fp, int offset, float num);

int main (int argc, char **argv)
{
	FILE *text;
	FILE *bin;
	FILE *orig_file;
	FILE *c_header = NULL;
	int file_count = 0;
	int c;
	int i;
	int j;
	int compress_type;
	int file_offset;
	int header_slash_loc;
	int uncomp_size;
	char orig_filename[256];
	char new_filename[256];
	char filename_dir[256];
	char line_buf[256];
	char header_name[256];
	void *uncomp_buf;
	int comp_size;

	bool write_c_header = false;

	if (argc >= 2)
	{
		text = fopen (argv[1], "rb");
		if (text == NULL)
		{
			printf ("Failed to open text file %s.\n", argv[1]);
			return 0;
		}
		c = fgetc (text);
		while (c != EOF)
		{
			if (c == '\n')
			{
				file_count++;
			}
			c = fgetc (text);
		}
		// Fix File Counting for Files Ending Abruptly
		fseek (text, -1L, SEEK_END);
		c = fgetc (text);
		if (c != '\n')
		{
			file_count++;
		}
		strncpy (filename_dir, argv[1], 256);
		int slash_loc = GetLastSlashPos (filename_dir);
		if (slash_loc != 0)
		{
			filename_dir[slash_loc + 1] = '\0';
		}
		else
		{
			filename_dir[0] = '\0';
		}
		if (argc >= 3)
		{
			strncpy (new_filename, argv[2], 256);
		}
		else
		{
			strncpy (new_filename, argv[1], 256);
			int new_name_len = strlen (new_filename);
			new_filename[new_name_len - 3] = 'b';
			new_filename[new_name_len - 2] = 'i';
			new_filename[new_name_len - 1] = 'n';
		}
		bin = fopen (new_filename, "wb");
		if (bin == NULL)
		{
			printf ("Failed to create file %s.\n", new_filename);
			return 0;
		}
		if (argc >= 4)
		{
			c_header = fopen (argv[3], "wb");
			if (c_header == NULL)
			{
				printf ("Failed to create C header %s.\n", argv[3]);
				return 0;
			}
			write_c_header = true;
		}
		WriteFileU32BigEndian (bin, 0, file_count);
		fseek (text, 0, SEEK_SET);
		file_offset = (4 + (file_count * 4));
		if (write_c_header == true)
		{
			header_slash_loc = GetLastSlashPos (argv[3]);
			if (header_slash_loc != 0)
			{
				header_slash_loc++;
			}
			strncpy (header_name, argv[3] + header_slash_loc, 256);
			j = 0;
			while (header_name[j] != 0)
			{
				header_name[j] = toupper (header_name[j]);
				if (header_name[j] == '.')
				{
					header_name[j] = '_';
				}
				j++;
			}
			fprintf (c_header, "#ifndef %s\n", header_name);
			fprintf (c_header, "#define %s\n", header_name);
			fprintf (c_header, "\n");
		}
		for (i = 0; i < file_count; i++)
		{
			if (fgets (line_buf, 256, text) == NULL)
			{
				break;
			}
			sscanf (line_buf, "compress_type=%d: %s", &compress_type, orig_filename);
			snprintf (new_filename, 256, "%s%s", filename_dir, orig_filename);
			if (write_c_header)
			{
				header_slash_loc = GetLastSlashPos (orig_filename);
				if (header_slash_loc != 0)
				{
					header_slash_loc++;
				}
				strncpy (header_name, orig_filename + header_slash_loc, 256);
				j = 0;
				while (header_name[j] != 0)
				{
					header_name[j] = toupper (header_name[j]);
					if (header_name[j] == '.')
					{
						header_name[j] = '_';
					}
					j++;
				}
				fprintf (c_header, "#define %s %d\n", header_name, i);
			}
			orig_file = fopen (new_filename, "rb");
			if (orig_file == NULL)
			{
				printf ("Failed to open file %s", new_filename);
				return 0;
			}
			fseek (orig_file, 0L, SEEK_END);
			uncomp_size = ftell (orig_file);
			uncomp_buf = malloc (uncomp_size);
			ReadFileArray (orig_file, uncomp_buf, 0, uncomp_size);
			WriteFileU32BigEndian (bin, (4 + (i * 4)), file_offset);
			WriteFileU32BigEndian (bin, file_offset, uncomp_size);
			WriteFileU32BigEndian (bin, file_offset + 4, compress_type);
			switch (compress_type)
			{
				case COMPRESS_TYPE_NONE:
					WriteFileArray (bin, uncomp_buf, file_offset + 8, uncomp_size);
					comp_size = uncomp_size;
					file_offset += 8;
					break;

				case COMPRESS_TYPE_LZSS:
					comp_size = CompressLZSS (uncomp_buf, uncomp_size, bin, file_offset + 8);
					file_offset += 8;
					break;

				case COMPRESS_TYPE_SLIDE:
				case COMPRESS_TYPE_FSLIDE_ALT:
				case COMPRESS_TYPE_FSLIDE:
					WriteFileU32BigEndian (bin, file_offset + 8, uncomp_size);
					comp_size = CompressSlide (uncomp_buf, uncomp_size, bin, file_offset + 12);
					file_offset += 12;
					break;

				case COMPRESS_TYPE_RLE:
					comp_size = CompressRLE (uncomp_buf, uncomp_size, bin, file_offset + 8);
					file_offset += 8;
					break;

				case COMPRESS_TYPE_INFLATE:
					WriteFileU32BigEndian (bin, file_offset + 8, uncomp_size);
					comp_size = CompressInflate (uncomp_buf, uncomp_size, bin, file_offset + 16);
					WriteFileU32BigEndian (bin, file_offset + 12, comp_size);
					file_offset += 16;
					break;

				default:
					printf ("Unknown Compression Type %d", compress_type);
					break;
			}
			file_offset += comp_size;
		}
		if (write_c_header)
		{
			fprintf (c_header, "\n");
			fprintf (c_header, "#endif\n");
		}
		fclose (bin);
		fclose (text);
		if (write_c_header)
		{
			fclose (c_header);
		}
	}
	else
	{
		printf ("Usage is binpack list.txt out.bin c_header.h\n");
		printf ("list.txt is the text file created by bindump.\n");
		printf ("out.bin is the output BIN file for Mario Party.\n");
		printf ("If out.bin isn't provided then the output file will be the same name as the text "
				"file but with a .bin extension.\n");
		printf ("Last parameter is for developers only.\n");
		return 0;
	}
}

/*
 * initialize state, mostly the trees
 *
 * For i = 0 to N - 1, rchild[i] and lchild[i] will be the right and left
 * children of node i.  These nodes need not be initialized.  Also, parent[i]
 * is the parent of node i.  These are initialized to NIL (= N), which stands
 * for 'not used.'  For i = 0 to 255, rchild[N + i + 1] is the root of the
 * tree for strings that begin with character i.  These are initialized to NIL.
 * Note there are 256 trees. */
void init_state (struct encode_state *sp)
{
	int i;

	memset (sp, 0, sizeof (*sp));

	for (i = 0; i < N - F; i++)
		sp->text_buf[i] = '\0';
	for (i = N + 1; i <= N + 256; i++)
		sp->rchild[i] = NIL;
	for (i = 0; i < N; i++)
		sp->parent[i] = NIL;
}

/*
 * Inserts string of length F, text_buf[r..r+F-1], into one of the trees
 * (text_buf[r]'th tree) and returns the longest-match position and length
 * via the global variables match_position and match_length.
 * If match_length = F, then removes the old node in favor of the new one,
 * because the old one will be deleted sooner. Note r plays double role,
 * as tree node and position in buffer.
 */
void insert_node (struct encode_state *sp, int r)
{
	int i, p, cmp;
	unsigned char *key;

	cmp = 1;
	key = &sp->text_buf[r];
	p = N + 1 + key[0];
	sp->rchild[r] = sp->lchild[r] = NIL;
	sp->match_length = 0;
	for (;;)
	{
		if (cmp >= 0)
		{
			if (sp->rchild[p] != NIL)
				p = sp->rchild[p];
			else
			{
				sp->rchild[p] = r;
				sp->parent[r] = p;
				return;
			}
		}
		else
		{
			if (sp->lchild[p] != NIL)
				p = sp->lchild[p];
			else
			{
				sp->lchild[p] = r;
				sp->parent[r] = p;
				return;
			}
		}
		for (i = 1; i < F; i++)
		{
			if ((cmp = key[i] - sp->text_buf[p + i]) != 0)
				break;
		}
		if (i > sp->match_length)
		{
			sp->match_position = p;
			if ((sp->match_length = i) >= F)
				break;
		}
	}
	sp->parent[r] = sp->parent[p];
	sp->lchild[r] = sp->lchild[p];
	sp->rchild[r] = sp->rchild[p];
	sp->parent[sp->lchild[p]] = r;
	sp->parent[sp->rchild[p]] = r;
	if (sp->rchild[sp->parent[p]] == p)
		sp->rchild[sp->parent[p]] = r;
	else
		sp->lchild[sp->parent[p]] = r;
	sp->parent[p] = NIL; /* remove p */
}

/* deletes node p from tree */
static void delete_node (struct encode_state *sp, int p)
{
	int q;

	if (sp->parent[p] == NIL)
		return; /* not in tree */
	if (sp->rchild[p] == NIL)
		q = sp->lchild[p];
	else if (sp->lchild[p] == NIL)
		q = sp->rchild[p];
	else
	{
		q = sp->lchild[p];
		if (sp->rchild[q] != NIL)
		{
			do
			{
				q = sp->rchild[q];
			} while (sp->rchild[q] != NIL);
			sp->rchild[sp->parent[q]] = sp->lchild[q];
			sp->parent[sp->lchild[q]] = sp->parent[q];
			sp->lchild[q] = sp->lchild[p];
			sp->parent[sp->lchild[p]] = q;
		}
		sp->rchild[q] = sp->rchild[p];
		sp->parent[sp->rchild[p]] = q;
	}
	sp->parent[q] = sp->parent[p];
	if (sp->rchild[sp->parent[p]] == p)
		sp->rchild[sp->parent[p]] = q;
	else
		sp->lchild[sp->parent[p]] = q;
	sp->parent[p] = NIL;
}

int CompressLZSS (char *input, int uncomp_len, FILE *fp, int offset)
{
	/* Encoding state, mostly tree but some current match stuff */
	struct encode_state *sp;

	int i, c, len, r, s, last_match_length, code_buf_ptr, output_size = 0;
	char code_buf[65], mask;
	char *srcend = input + uncomp_len;

	/* initialize trees */
	sp = (struct encode_state *)malloc (sizeof (*sp));
	init_state (sp);

	/*
	 * code_buf[1..16] saves eight units of code, and code_buf[0] works
	 * as eight flags, "1" representing that the unit is an unencoded
	 * letter (1 byte), "" a position-and-length pair (2 bytes).
	 * Thus, eight units require at most 16 bytes of code.
	 */
	code_buf[0] = 0;
	code_buf_ptr = mask = 1;

	/* Clear the buffer with any character that will appear often. */
	s = 0;
	r = N - F;

	/* Read F bytes into the last F bytes of the buffer */
	for (len = 0; len < F && input < srcend; len++)
	{
		sp->text_buf[r + len] = *input++;
	}
	if (len == 0)
	{
		free (sp);
		return 0; /* text of size zero */
	}
	/*
	 * Insert the F strings, each of which begins with one or more
	 * 'space' characters.  Note the order in which these strings are
	 * inserted.  This way, degenerate trees will be less likely to occur.
	 */
	for (i = 1; i <= F; i++)
	{
		insert_node (sp, r - i);
	}

	/*
	 * Finally, insert the whole string just read.
	 * The global variables match_length and match_position are set.
	 */
	insert_node (sp, r);
	do
	{
		/* match_length may be spuriously long near the end of text. */
		if (sp->match_length > len)
		{
			sp->match_length = len;
		}
		if (sp->match_length <= THRESHOLD)
		{
			sp->match_length = 1; /* Not long enough match.  Send one byte. */
			code_buf[0] |= mask; /* 'send one byte' flag */
			code_buf[code_buf_ptr++] = sp->text_buf[r]; /* Send uncoded. */
		}
		else
		{
			int match_pos_lo = sp->match_position & 0xFF;
			int match_pos_hi = ((sp->match_position >> 2) & 0xC0);
			int match_len = ((sp->match_length - (THRESHOLD + 1)) & 0x3F);
			/* Send position and length pair. Note match_length > THRESHOLD. */
			code_buf[code_buf_ptr++] = (unsigned char)match_pos_lo;
			code_buf[code_buf_ptr++] = (unsigned char)(match_pos_hi | match_len);
		}
		if ((mask <<= 1) == 0) /* Shift mask left one bit. */
		{
			/* Send at most 8 units of code together */
			for (i = 0; i < code_buf_ptr; i++)
			{
				WriteFileU8 (fp, output_size + offset, code_buf[i]);
				output_size++;
			}
			code_buf[0] = 0;
			code_buf_ptr = mask = 1;
		}
		last_match_length = sp->match_length;
		for (i = 0; i < last_match_length && input < srcend; i++)
		{
			delete_node (sp, s); /* Delete old strings and */
			c = *input++;
			sp->text_buf[s] = c; /* read new bytes */

			/*
			 * If the position is near the end of buffer, extend the buffer
			 * to make string comparison easier.
			 */
			if (s < F - 1)
			{
				sp->text_buf[s + N] = c;
			}

			/* Since this is a ring buffer, increment the position modulo N. */
			s = (s + 1) & (N - 1);
			r = (r + 1) & (N - 1);

			/* Register the string in text_buf[r..r+F-1] */
			insert_node (sp, r);
		}
		while (i++ < last_match_length)
		{
			delete_node (sp, s);

			/* After the end of text, no need to read, */
			s = (s + 1) & (N - 1);
			r = (r + 1) & (N - 1);
			/* but buffer may not be empty. */
			if (len > 0)
			{
				insert_node (sp, r);
				len--;
			}
		}
	} while (len > 0); /* until length of string to be processed is zero */

	if (code_buf_ptr > 1) /* Send remaining code. */
	{
		for (i = 0; i < code_buf_ptr; i++)
		{
			WriteFileU8 (fp, output_size + offset, code_buf[i]);
			output_size++;
		}
	}

	free (sp);
	return output_size;
}

// simple and straight encoding scheme for Yaz0
u32 simpleEnc (u8 *src, int size, int pos, u32 *pMatchPos)
{
	int startPos = pos - 0x1000;
	u32 numBytes = 1;
	u32 matchPos = 0;
	int i;
	int j;

	if (startPos < 0)
		startPos = 0;
	for (i = startPos; i < pos; i++)
	{
		for (j = 0; j < size - pos; j++)
		{
			if (src[i + j] != src[j + pos])
				break;
		}
		if (j > numBytes)
		{
			numBytes = j;
			matchPos = i;
		}
	}
	*pMatchPos = matchPos;
	if (numBytes == 2)
		numBytes = 1;
	return numBytes;
}

// a lookahead encoding scheme for ngc Yaz0
u32 nintendoEnc (u8 *src, int size, int pos, u32 *pMatchPos)
{
	int startPos = pos - 0x1000;
	u32 numBytes = 1;
	static u32 numBytes1;
	static u32 matchPos;
	static int prevFlag = 0;

	// if prevFlag is set, it means that the previous position was determined by look-ahead try.
	// so just use it. this is not the best optimization, but nintendo's choice for speed.
	if (prevFlag == 1)
	{
		*pMatchPos = matchPos;
		prevFlag = 0;
		return numBytes1;
	}
	prevFlag = 0;
	numBytes = simpleEnc (src, size, pos, &matchPos);
	*pMatchPos = matchPos;

	// if this position is RLE encoded, then compare to copying 1 byte and next position(pos+1)
	// encoding
	if (numBytes >= 3)
	{
		numBytes1 = simpleEnc (src, size, pos + 1, &matchPos);
		// if the next position encoding is +2 longer than current position, choose it.
		// this does not guarantee the best optimization, but fairly good optimization with speed.
		if (numBytes1 >= numBytes + 2)
		{
			numBytes = 1;
			prevFlag = 1;
		}
	}
	return numBytes;
}

int CompressSlide (char *input, int uncomp_len, FILE *fp, int offset)
{
	Ret r = { 0, 0 };
	u8 dst[96]; // 8 codes * 3 bytes maximum
	int dstSize = 0;
	int percent = -1;

	u32 validBitCount = 0; // number of valid bits left in "code" byte
	u32 currCodeByte = 0;
	while (r.srcPos < uncomp_len)
	{
		u32 numBytes;
		u32 matchPos;
		u32 srcPosBak;

		numBytes = nintendoEnc (input, uncomp_len, r.srcPos, &matchPos);
		if (numBytes < 3)
		{
			// straight copy
			dst[r.dstPos] = input[r.srcPos];
			r.dstPos++;
			r.srcPos++;
			// set flag for straight copy
			currCodeByte |= (0x80000000 >> validBitCount);
		}
		else
		{
			// RLE part
			u32 dist = r.srcPos - matchPos - 1;
			u8 byte1, byte2, byte3;

			if (numBytes >= 0x12) // 3 byte encoding
			{
				byte1 = 0 | (dist >> 8);
				byte2 = dist & 0xff;
				dst[r.dstPos++] = byte1;
				dst[r.dstPos++] = byte2;
				// maximum runlength for 3 byte encoding
				if (numBytes > 0xff + 0x12)
					numBytes = 0xff + 0x12;
				byte3 = numBytes - 0x12;
				dst[r.dstPos++] = byte3;
			}
			else // 2 byte encoding
			{
				byte1 = ((numBytes - 2) << 4) | (dist >> 8);
				byte2 = dist & 0xff;
				dst[r.dstPos++] = byte1;
				dst[r.dstPos++] = byte2;
			}
			r.srcPos += numBytes;
		}
		validBitCount++;
		// write eight codes
		if (validBitCount == 32)
		{
			WriteFileU32BigEndian (fp, offset, currCodeByte);
			WriteFileArray (fp, &dst, offset + 4, r.dstPos);
			dstSize += r.dstPos + 4;
			offset += r.dstPos + 4;

			srcPosBak = r.srcPos;
			currCodeByte = 0;
			validBitCount = 0;
			r.dstPos = 0;
		}
	}
	if (validBitCount > 0)
	{
		WriteFileU32BigEndian (fp, offset, currCodeByte);
		WriteFileArray (fp, &dst, offset + 4, r.dstPos);
		dstSize += r.dstPos + 4;
		offset += r.dstPos + 4;

		currCodeByte = 0;
		validBitCount = 0;
		r.dstPos = 0;
	}

	return dstSize;
}

int CompressRLE (char *input, int uncomp_len, FILE *fp, int offset)
{
	int output_pos = 0;
	int input_pos = 0;
	int i;
	int copy_len = 0;
	int curr_byte;
	int next_byte;
	while (input_pos < uncomp_len)
	{
		curr_byte = input[input_pos];
		next_byte = input[input_pos + 1];
		if (curr_byte == next_byte)
		{
			copy_len = 0;
			for (i = 0; i < 127; i++)
			{
				curr_byte = input[input_pos + i];
				next_byte = input[input_pos + i + 1];
				if (curr_byte != next_byte || (input_pos + i) >= uncomp_len)
				{
					copy_len++;
					break;
				}
				copy_len++;
			}
			WriteFileU8 (fp, offset + output_pos, copy_len);
			WriteFileU8 (fp, offset + output_pos + 1, input[input_pos]);
			output_pos += 2;
			input_pos += copy_len;
		}
		else
		{
			copy_len = 0;
			for (i = 0; i < 127; i++)
			{
				curr_byte = input[input_pos + i];
				next_byte = input[input_pos + i + 1];
				if (curr_byte == next_byte || (input_pos + i) >= uncomp_len)
				{
					break;
				}
				copy_len++;
			}
			WriteFileU8 (fp, offset + output_pos, 0x80 | copy_len);
			WriteFileArray (fp, &input[input_pos], offset + output_pos + 1, copy_len);
			output_pos += copy_len;
			output_pos += 1;
			input_pos += copy_len;
		}
	}
	return output_pos;
}

int CompressInflate (char *input, int uncomp_len, FILE *fp, int offset)
{
	int ret;
	int flush;
	int have;
	int size_left = uncomp_len;
	int input_offset = 0;
	int output_size = 0;
	z_stream strm;
	unsigned char in[DEFLATE_BUF_SIZE];
	unsigned char out[DEFLATE_BUF_SIZE];
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = uncomp_len;
	strm.next_in = (Bytef *)input;
	ret = deflateInit (&strm, Z_BEST_COMPRESSION);
	if (ret != Z_OK)
	{
		return 0;
	}
	flush = Z_NO_FLUSH;
	while (flush != Z_FINISH)
	{
		strm.avail_in = DEFLATE_BUF_SIZE;
		if (size_left < DEFLATE_BUF_SIZE)
		{
			strm.avail_in = size_left;
			flush = Z_FINISH;
		}
		memcpy (in, &input[input_offset], strm.avail_in);
		strm.next_in = in;
		do
		{
			strm.avail_out = DEFLATE_BUF_SIZE;
			strm.next_out = out;
			ret = deflate (&strm, flush); /* no bad return value */
			have = DEFLATE_BUF_SIZE - strm.avail_out;
			WriteFileArray (fp, &out, offset + output_size, have);
			output_size += have;
		} while (strm.avail_out == 0);

		size_left -= DEFLATE_BUF_SIZE;
		input_offset += DEFLATE_BUF_SIZE;
	}
	(void)deflateEnd (&strm);
	return output_size;
}

int GetLastSlashPos (char *string)
{
	int slash_loc = 0;
	int curr_string_loc = 0;
	while (*string != '\0')
	{
		if (*string == '\\' || *string == '/')
		{
			slash_loc = curr_string_loc;
		}
		curr_string_loc++;
		string++;
	}
	return slash_loc;
}

unsigned char ReadFileU8 (FILE *fp, int offset)
{
	unsigned char temp;
	fseek (fp, offset, SEEK_SET);
	fread (&temp, sizeof (unsigned char), 1, fp);
	return temp;
}

void ReadFileArray (FILE *fp, void *array, int offset, int len)
{
	fseek (fp, offset, SEEK_SET);
	fread (array, sizeof (unsigned char), len, fp);
}

unsigned short ReadFileU16BigEndian (FILE *fp, int offset)
{
	unsigned short temp;
	unsigned char temp_buf[2];
	fseek (fp, offset, SEEK_SET);
	fread (&temp_buf, sizeof (unsigned char), 2, fp);
	temp = temp_buf[0] << 8 | temp_buf[1];
	return temp;
}

unsigned int ReadFileU32BigEndian (FILE *fp, int offset)
{
	unsigned int temp;
	unsigned char temp_buf[4];
	fseek (fp, offset, SEEK_SET);
	fread (&temp_buf, sizeof (unsigned char), 4, fp);
	temp = temp_buf[0] << 24 | temp_buf[1] << 16 | temp_buf[2] << 8 | temp_buf[3];
	return temp;
}

float ReadFileFloatBigEndian (FILE *fp, int offset)
{
	unsigned int temp;
	unsigned char temp_buf[4];
	fseek (fp, offset, SEEK_SET);
	fread (&temp_buf, sizeof (unsigned char), 4, fp);
	temp = temp_buf[0] << 24 | temp_buf[1] << 16 | temp_buf[2] << 8 | temp_buf[3];
	return *(float *)&temp;
}

void WriteFileU8 (FILE *fp, int offset, unsigned char num)
{
	unsigned char temp = num;
	fseek (fp, offset, SEEK_SET);
	fwrite (&temp, sizeof (unsigned char), 1, fp);
}

void WriteFileArray (FILE *fp, void *array, int offset, int len)
{
	fseek (fp, offset, SEEK_SET);
	fwrite (array, sizeof (unsigned char), len, fp);
}

void WriteFileU16BigEndian (FILE *fp, int offset, unsigned short num)
{
	unsigned short temp = 0;
	temp |= (num & 0xff) << 8;
	temp |= (num & 0xff00) >> 8;
	fseek (fp, offset, SEEK_SET);
	fwrite (&temp, sizeof (unsigned char), 2, fp);
}

void WriteFileU32BigEndian (FILE *fp, int offset, unsigned int num)
{
	unsigned int temp = 0;
	temp |= (num & 0x000000FF) << 24;
	temp |= (num & 0x0000FF00) << 8;
	temp |= (num & 0x00FF0000) >> 8;
	temp |= (num & 0xFF000000) >> 24;
	fseek (fp, offset, SEEK_SET);
	fwrite (&temp, sizeof (unsigned int), 1, fp);
}

void WriteFileFloatBigEndian (FILE *fp, int offset, float num)
{
	WriteFileU32BigEndian (fp, offset, *(unsigned int *)&num);
}