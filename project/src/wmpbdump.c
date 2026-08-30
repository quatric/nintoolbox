#define _CRT_SECURE_NO_DEPRECATE // Allows Use of fopen and strncpy
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define COMPRESS_TYPE_NONE 0
#define COMPRESS_TYPE_LZSS 1
#define COMPRESS_TYPE_SLIDE 2
#define COMPRESS_TYPE_FSLIDE_ALT 3
#define COMPRESS_TYPE_FSLIDE 4
#define COMPRESS_TYPE_RLE 5
#define COMPRESS_TYPE_INFLATE 7

#define WINDOW_START 958
#define WINDOW_SIZE 1024
#define SHORTEST_MATCH_LENGTH 3

int main (int argc, char **argv);
void DecompressLZSS (
	FILE *fp, unsigned char *decompress_buffer, int compressed_buffer_offset, int length);
void DecompressSlide (
	FILE *fp, unsigned char *decompress_buffer, int compressed_buffer_offset, int length);
void DecompressRLE (
	FILE *fp, unsigned char *decompress_buffer, int compressed_buffer_offset, int length);
void DecompressInflate (FILE *fp, unsigned char *decompress_buffer, int buffer_offset);
void StripExtension (char *filename);
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

typedef struct type7_file_header
{
	int decompressed_size;
	int compressed_size;
} Type7FileHeader;

typedef struct file_header
{
	int decompressed_size;
	int compress_type;
} FileHeader;

int main (int argc, char **argv)
{
	FILE *bin_file;
	FILE *subfile_out;
	FILE *text_out;
	char name_buffer[256];
	char new_name_buffer[256];
	int compare_int;
	char new_extension[4];

	unsigned char *decompress_buffer;
	int i;
	int last_slash_pos;

	if (argc >= 2)
	{
		bin_file = fopen (argv[1], "rb");
		if (bin_file == NULL)
		{
			printf ("Failed to Open BIN File %s.\n", argv[1]);
			return 0;
		}
		strncpy (name_buffer, argv[1], 256);
		int text_path_len = strlen (name_buffer);
		name_buffer[text_path_len - 3] = 't';
		name_buffer[text_path_len - 2] = 'x';
		name_buffer[text_path_len - 1] = 't';
		text_out = fopen (name_buffer, "w");
		if (text_out == NULL)
		{
			printf ("Failed to Create Text File %s.\n", argv[1]);
			return 0;
		}
		int num_files = ReadFileU32BigEndian (bin_file, 0);
		for (i = 0; i < num_files; i++)
		{
			int file_offset = ReadFileU32BigEndian (bin_file, (i * sizeof (int)) + sizeof (int));
			int decompressed_size = ReadFileU32BigEndian (bin_file, file_offset);
			int compress_type = ReadFileU32BigEndian (
				bin_file, file_offset + offsetof (FileHeader, compress_type));
			decompress_buffer = malloc (decompressed_size);
			switch (compress_type)
			{
				case COMPRESS_TYPE_NONE:
					ReadFileArray (bin_file, decompress_buffer, file_offset + sizeof (FileHeader),
						decompressed_size);
					break;

				case COMPRESS_TYPE_LZSS:
					DecompressLZSS (bin_file, decompress_buffer, file_offset + sizeof (FileHeader),
						decompressed_size);
					break;

				case COMPRESS_TYPE_SLIDE:
					DecompressSlide (bin_file, decompress_buffer, file_offset + sizeof (FileHeader),
						decompressed_size);
					break;

				case COMPRESS_TYPE_FSLIDE_ALT:
					DecompressSlide (bin_file, decompress_buffer, file_offset + sizeof (FileHeader),
						decompressed_size);
					break;

				case COMPRESS_TYPE_FSLIDE:
					DecompressSlide (bin_file, decompress_buffer, file_offset + sizeof (FileHeader),
						decompressed_size);
					break;

				case COMPRESS_TYPE_RLE:
					DecompressRLE (bin_file, decompress_buffer, file_offset + sizeof (FileHeader),
						decompressed_size);
					break;

				case COMPRESS_TYPE_INFLATE:
					DecompressInflate (
						bin_file, decompress_buffer, file_offset + sizeof (FileHeader));
					break;

				default:
					printf ("Unknown Compression Type %d", compress_type);
					break;
			}
			strncpy (name_buffer, argv[1], 256);
			StripExtension (name_buffer);
			strncpy (new_extension, "dat", 4);
			if (0 == memcmp (decompress_buffer, "HSFV037", 7))
			{
				strncpy (new_extension, "hsf", 4);
			}
			compare_int = decompress_buffer[12] << 24 | decompress_buffer[13] << 16
				| decompress_buffer[14] << 8 | decompress_buffer[15];
			if (compare_int == 20)
			{
				strncpy (new_extension, "atb", 4);
			}
			sprintf (new_name_buffer, "%s_file%d.%s", name_buffer, i, new_extension);
			subfile_out = fopen (new_name_buffer, "wb");
			if (subfile_out == NULL)
			{
				printf ("Failed to Create File %s.", new_name_buffer);
				return 0;
			}
			WriteFileArray (subfile_out, decompress_buffer, 0, decompressed_size);
			last_slash_pos = GetLastSlashPos (new_name_buffer);
			if (last_slash_pos)
			{
				fprintf (text_out, "compress_type=%d: %s\n", compress_type,
					new_name_buffer + last_slash_pos + 1);
			}
			else
			{
				fprintf (text_out, "compress_type=%d: %s\n", compress_type, new_name_buffer);
			}

			fclose (subfile_out);
			free (decompress_buffer);
		}
		fclose (text_out);
		return 1;
	}
	else
	{
		printf ("Usage is bindump in.bin\n");
		printf ("in.bin is the path to a Mario Party BIN file.\n");
		return 0;
	}
}

void DecompressLZSS (
	FILE *fp, unsigned char *decompress_buffer, int compressed_buffer_offset, int length)
{
	int i = 0;
	char window_buffer[WINDOW_SIZE];
	int window_offset = WINDOW_START;
	int code_word = 0;
	int length_adjust = 0;
	int dest_offset = 0;
	int src_offset = compressed_buffer_offset;

	// Clear Sliding Window
	while (i < WINDOW_SIZE)
	{
		window_buffer[i] = 0;
		i++;
	}

	while (dest_offset < length)
	{
		// Reads New Code Word from Compressed Stream if Expired
		if ((code_word & 0x100) == 0)
		{
			code_word = ReadFileU8 (fp, src_offset);
			src_offset++;
			code_word |= 0xFF00;
		}

		// Copies a Byte from the Source to the Destination and Window Buffer
		if ((code_word & 0x1) != 0)
		{
			decompress_buffer[dest_offset] = ReadFileU8 (fp, src_offset);
			window_buffer[window_offset] = ReadFileU8 (fp, src_offset);
			src_offset++;
			dest_offset++;
			window_offset++;
			window_offset = window_offset % WINDOW_SIZE;
		}
		else
		{
			// Interpret Next 2 Bytes as an Offset and Length into the Window Buffer
			unsigned char byte1 = ReadFileU8 (fp, src_offset);
			src_offset++;
			unsigned char byte2 = ReadFileU8 (fp, src_offset);
			src_offset++;

			int offset = ((byte2 & 0xC0) << 2) | byte1;
			int copy_length = (byte2 & 0x3F) + SHORTEST_MATCH_LENGTH;

			i = 0;

			unsigned char value;
			// Copy Some Bytes from Window Buffer
			while (i < copy_length)
			{
				value = window_buffer[offset % WINDOW_SIZE];
				window_buffer[window_offset] = value;

				window_offset++;
				window_offset = window_offset % WINDOW_SIZE;
				decompress_buffer[dest_offset] = value;

				dest_offset++;
				offset++;
				i++;
			}
		}
		code_word = code_word >> 1;
	}
}

void DecompressSlide (
	FILE *fp, unsigned char *decompress_buffer, int compressed_buffer_offset, int length)
{
	int dest_offset = 0;
	int src_offset = compressed_buffer_offset + 4;
	unsigned int code_word = 0;
	int num_code_word_bits_left = 0;
	int i = 0;

	while (dest_offset < length)
	{
		if (num_code_word_bits_left == 0)
		{
			code_word = ReadFileU32BigEndian (fp, src_offset);
			src_offset += 4;
			num_code_word_bits_left = 32;
		}

		if ((code_word & 0x80000000) != 0)
		{
			decompress_buffer[dest_offset] = ReadFileU8 (fp, src_offset);
			dest_offset++;
			src_offset++;
		}
		else
		{
			// Interpret Next 2 Bytes as a Backwards Distance and Length
			unsigned char byte1 = ReadFileU8 (fp, src_offset);
			src_offset++;
			unsigned char byte2 = ReadFileU8 (fp, src_offset);
			src_offset++;

			int dist_back = (((byte1 & 0x0F) << 8) | byte2) + 1;
			int copy_length = ((byte1 & 0xF0) >> 4) + 2;

			// Special Case Where the Upper 4 Bits of byte1 are 0
			if (copy_length == 2)
			{
				copy_length = ReadFileU8 (fp, src_offset) + 18;
				src_offset++;
			}

			unsigned char value;
			i = 0;

			while (i < copy_length && dest_offset < length)
			{
				if (dist_back > dest_offset)
				{
					value = 0;
				}
				else
				{
					value = decompress_buffer[dest_offset - dist_back];
				}
				decompress_buffer[dest_offset] = value;
				dest_offset++;
				i++;
			}
		}
		code_word = code_word << 1;
		num_code_word_bits_left--;
	}
}

void DecompressRLE (
	FILE *fp, unsigned char *decompress_buffer, int compressed_buffer_offset, int length)
{
	int dest_offset = 0;
	int src_offset = compressed_buffer_offset;
	int code_byte = 0;
	unsigned char repeat_length;
	int i;

	while (dest_offset < length)
	{
		code_byte = ReadFileU8 (fp, src_offset);
		src_offset++;
		repeat_length = code_byte & 0x7F;

		if (code_byte & 0x80)
		{
			i = 0;
			while (i < repeat_length)
			{
				decompress_buffer[dest_offset] = ReadFileU8 (fp, src_offset);
				dest_offset++;
				src_offset++;
				i++;
			}
		}
		else
		{
			unsigned char repeated_byte = ReadFileU8 (fp, src_offset);
			src_offset++;

			i = 0;
			while (i < repeat_length)
			{
				decompress_buffer[dest_offset] = repeated_byte;
				dest_offset++;
				i++;
			}
		}
	}
}

void DecompressInflate (FILE *fp, unsigned char *decompress_buffer, int buffer_offset)
{
	z_stream stream = { 0 };
	void *compressed_buffer;
	int decompressed_size = ReadFileU32BigEndian (fp, buffer_offset);
	int compressed_size
		= ReadFileU32BigEndian (fp, buffer_offset + offsetof (Type7FileHeader, compressed_size));
	compressed_buffer = malloc (compressed_size);
	ReadFileArray (
		fp, compressed_buffer, buffer_offset + sizeof (Type7FileHeader), compressed_size);
	stream.total_in = stream.avail_in = compressed_size;
	stream.total_out = stream.avail_out = decompressed_size;
	stream.next_in = (Bytef *)compressed_buffer;
	stream.next_out = (Bytef *)decompress_buffer;
	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	int error = -1;
	error = inflateInit2 (&stream,
		(15 + 32)); // 15 window bits, and the +32 tells zlib to to detect if using gzip or zlib
	if (error == Z_OK)
	{
		error = inflate (&stream, Z_FINISH);
		inflateEnd (&stream);
	}
	else
	{
		inflateEnd (&stream);
	}
}

void StripExtension (char *filename)
{
	char *end = filename + strlen (filename);

	while (end > filename && *end != '.' && *end != '\\' && *end != '/')
	{
		--end;
	}
	if ((end > filename && *end == '.') && (*(end - 1) != '\\' && *(end - 1) != '/'))
	{
		*end = '\0';
	}
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
