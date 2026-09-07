// Monster Games MvDK compression -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

// ============================================================
// Mario vs. Donkey Kong custom deflate (MVDK) decompressor
// Ported from Garhoogin/NitroPaint compression.c (MIT licence)
// Only the decompressor and validator are ported; encoder stubs
// out to NULL for now (compress support can be added later).
// ============================================================
// Temporarily restore standard allocators overridden by dclib.
#undef free
#undef calloc
#undef malloc
#undef realloc
#include <stdint.h>
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static void *CxiShrink (void *block, unsigned int to)
{
	void *newblock = realloc (block, to);
	if (newblock == NULL)
	{
		// alloc fail, return old block
		return block;
	}
	return newblock;
}

static uint32_t CxiBitReverse32 (uint32_t x)
{
	x = ((x & 0xFFFF0000) >> 16) | ((x & ~0xFFFF0000) << 16);
	x = ((x & 0xFF00FF00) >> 8) | ((x & ~0xFF00FF00) << 8);
	x = ((x & 0xF0F0F0F0) >> 4) | ((x & ~0xF0F0F0F0) << 4);
	x = ((x & 0xCCCCCCCC) >> 2) | ((x & ~0xCCCCCCCC) << 2);
	x = ((x & 0xAAAAAAAA) >> 1) | ((x & ~0xAAAAAAAA) << 1);
	return x;
}

static unsigned char CxiBitReverse8 (unsigned char x)
{
	return CxiBitReverse32 (x << 24);
}

static uint32_t CxiByteSwap (uint32_t x)
{
	return ((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8)
		| ((x & 0x000000FF) << 24);
}

typedef struct CxiLzNode_
{
	uint32_t distance : 15; // distance of node if reference
	uint32_t length : 17; // length of node
	uint32_t weight; // weight of node
} CxiLzNode;

// struct for representing tokenized LZ data
typedef struct CxiLzToken_
{
	uint8_t isReference;
	union
	{
		uint8_t symbol;
		struct
		{
			int16_t length;
			int16_t distance;
		};
	};
} CxiLzToken;

// struct for keeping track of LZ sliding window
typedef struct CxiLzState_
{
	const unsigned char *buffer;
	unsigned int size;
	unsigned int pos;
	unsigned int minLength;
	unsigned int maxLength;
	unsigned int minDistance;
	unsigned int maxDistance;
	unsigned int symLookup[512];
	unsigned int *chain;
} CxiLzState;

static unsigned int CxiLzHash3 (const unsigned char *p)
{
	unsigned char c0 = p[0]; // A
	unsigned char c1 = p[0] ^ p[1]; // A ^ B
	unsigned char c2 = p[0] ^ p[2]; // (A ^ B) ^ (B ^ C)
	return (c0 ^ (c1 << 1) ^ (c2 << 2) ^ (c2 >> 7)) & 0x1FF;
}

static void CxiLzStateInit (CxiLzState *state, const unsigned char *buffer, unsigned int size,
	unsigned int minLength, unsigned int maxLength, unsigned int minDistance,
	unsigned int maxDistance)
{
	state->buffer = buffer;
	state->size = size;
	state->pos = 0;
	state->minLength = minLength;
	state->maxLength = maxLength;
	state->minDistance = minDistance;
	state->maxDistance = maxDistance;

	for (unsigned int i = 0; i < 512; i++)
	{
		// init symbol lookup to empty
		state->symLookup[i] = UINT_MAX;
	}

	state->chain = (unsigned int *)calloc (state->maxDistance, sizeof (unsigned int));
	for (unsigned int i = 0; i < state->maxDistance; i++)
	{
		state->chain[i] = UINT_MAX;
	}
}

static void CxiLzStateFree (CxiLzState *state)
{
	free (state->chain);
}

static unsigned int CxiLzStateGetChainIndex (CxiLzState *state, unsigned int index)
{
	return (state->pos - index) % state->maxDistance;
}

static unsigned int CxiLzStateGetChain (CxiLzState *state, int index)
{
	unsigned int chainIndex = CxiLzStateGetChainIndex (state, index);

	return state->chain[chainIndex];
}

static void CxiLzStatePutChain (CxiLzState *state, unsigned int index, unsigned int data)
{
	unsigned int chainIndex = CxiLzStateGetChainIndex (state, index);

	state->chain[chainIndex] = data;
}

static void CxiLzStateSlideByte (CxiLzState *state)
{
	if (state->pos >= state->size)
		return; // cannot slide

	// only update search structures when we have enough space left to necessitate searching.
	if ((state->size - state->pos) >= 3)
	{
		// fetch next 3 bytes' hash
		unsigned int next = CxiLzHash3 (state->buffer + state->pos);

		// get the distance back to the next byte before sliding. If it exists in the window,
		// we'll have nextDelta less than UINT_MAX. We'll take this first occurrence and it
		// becomes the offset from the current byte. Bear in mind the chain is 0-indexed starting
		// at a distance of 1.
		unsigned int nextDelta = state->symLookup[next];
		if (nextDelta != UINT_MAX)
		{
			nextDelta++;
			if (nextDelta >= state->maxDistance)
			{
				nextDelta = UINT_MAX;
			}
		}
		CxiLzStatePutChain (state, 0, nextDelta);

		// increment symbol lookups
		for (int i = 0; i < 512; i++)
		{
			if (state->symLookup[i] != UINT_MAX)
			{
				state->symLookup[i]++;
				if (state->symLookup[i] > state->maxDistance)
					state->symLookup[i] = UINT_MAX;
			}
		}
		state->symLookup[next] = 0; // update entry for the current byte to the start of the chain
	}

	state->pos++;
}

static void CxiLzStateSlide (CxiLzState *state, unsigned int nSlide)
{
	while (nSlide--)
		CxiLzStateSlideByte (state);
}

static unsigned int CxiCompareMemory (
	const unsigned char *b1, const unsigned char *b2, unsigned int nMax)
{
	// compare nAbsoluteMax bytes, do not perform any looping.
	unsigned int nSame = 0;
	while (nMax > 0)
	{
		if (*(b1++) != *(b2++))
			break;
		nMax--;
		nSame++;
	}
	return nSame;
}

static int CxiLzConfirmMatch (const unsigned char *buffer, unsigned int size, unsigned int pos,
	unsigned int distance, unsigned int length)
{
	(void)size;

	// compare string match
	return memcmp (buffer + pos, buffer + pos - distance, length) == 0;
}

static unsigned int CxiLzSearch (CxiLzState *state, unsigned int *pDistance)
{
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < 3 || nBytesLeft < state->minLength)
	{
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[CxiLzHash3 (state->buffer + state->pos)];
	if (firstMatch == UINT_MAX)
	{
		// return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft)
		nMaxCompare = nBytesLeft;

	// search backwards
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance)
	{
		// check only if distance is at least minDistance
		if (distance >= state->minDistance)
		{
			unsigned int matchLen = CxiCompareMemory (curp - distance, curp, nMaxCompare);

			if (matchLen > bestLength)
			{
				bestLength = matchLen;
				bestDistance = distance;
				if (bestLength == nMaxCompare)
					break;
			}
		}

		if (distance == state->maxDistance)
			break;
		unsigned int next = CxiLzStateGetChain (state, distance);
		if (next == UINT_MAX)
			break;
		distance += next;
	}

	if (bestLength < state->minLength)
	{
		bestLength = 1;
		distance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}

static unsigned int CxiSearchLZ (const unsigned char *buffer, unsigned int size,
	unsigned int curpos, unsigned int minDistance, unsigned int maxDistance, unsigned int maxLength,
	unsigned int *pDistance)
{
	// nProcessedBytes = curpos
	unsigned int nBytesLeft = size - curpos;

	// the maximum distance we can search backwards is limited by how far into the buffer we are. It
	// won't make sense to a decoder to copy bytes from before we've started.
	if (maxDistance > curpos)
		maxDistance = curpos;

	// the longest string we can match, including repetition by overwriting the source.
	unsigned int nMaxCompare = maxLength;
	if (nMaxCompare > nBytesLeft)
		nMaxCompare = nBytesLeft;

	// begin searching backwards.
	unsigned int bestLength = 0, bestDistance = 0;
	for (unsigned int i = minDistance; i <= maxDistance; i++)
	{
		unsigned int nMatched
			= CxiCompareMemory (buffer + curpos - i, buffer + curpos, nMaxCompare);
		if (nMatched > bestLength)
		{
			bestLength = nMatched;
			bestDistance = i;
			if (bestLength == nMaxCompare)
				break;
		}
	}

	*pDistance = bestDistance;
	return bestLength;
}

// ----- Bit reader routines

typedef struct CxiBitReader_
{
	const unsigned char *start;
	const unsigned char *end;
	const unsigned char *pos;
	uint32_t current;
	uint8_t nBitsBuffered;
	uint8_t error;
	uint8_t beBits : 1; // big-endian bit order
	uint8_t beBytes : 1; // big-endian byte order (requires full word buffer)
	uint32_t nBitsRead;
} CxiBitReader;

static void CxiBitReaderFetch (CxiBitReader *reader)
{
	// when bit and byte endianness do not match, we must fetch full words. When they match,
	// we can get by with fetching one byte at a time.
	int fullWords = reader->beBits != reader->beBytes;
	unsigned int unitSize = fullWords ? 4 : 1;

	if ((reader->pos + unitSize) <= reader->end)
	{
		if (!fullWords)
		{
			// fetch byte
			reader->current = *reader->pos;
		}
		else
		{
			// fetch word
			reader->current = reader->pos[0] | (reader->pos[1] << 8) | (reader->pos[2] << 16)
				| (reader->pos[3] << 24);
			if (reader->beBytes)
			{
				reader->current = CxiByteSwap (reader->current);
			}
		}
		reader->nBitsBuffered = 8 * unitSize;
		reader->pos += unitSize;

		// in big endian bit order we internally reverse the bit buffer
		if (reader->beBits)
		{
			if (!fullWords)
			{
				reader->current = CxiBitReverse8 (reader->current);
			}
			else
			{
				reader->current = CxiBitReverse32 (reader->current);
			}
		}
	}
	else
	{
		// out of bounds access
		reader->error = 1;
	}
}

static void CxiBitReaderInit (CxiBitReader *reader, const unsigned char *pos,
	const unsigned char *end, int beBits, int beBytes)
{
	reader->pos = pos;
	reader->end = end;
	reader->start = pos;
	reader->beBits = beBits;
	reader->beBytes = beBytes;
	reader->nBitsBuffered = 0;
	reader->nBitsRead = 0;
	reader->current = 0;
	reader->error = 0;
}

static uint32_t CxiBitReaderReadBit (CxiBitReader *reader)
{
	if (reader->nBitsBuffered == 0)
	{
		// fetch next bits
		CxiBitReaderFetch (reader);
	}

	uint32_t current = reader->current;
	reader->current >>= 1;
	reader->nBitsBuffered--;
	reader->nBitsRead++;
	return current & 1;
}

static uint32_t CxiBitReaderReadBits (CxiBitReader *reader, unsigned int nBits)
{
	uint32_t string = 0, i = 0;
	for (i = 0; i < nBits; i++)
	{
		uint32_t bit = CxiBitReaderReadBit (reader);
		if (reader->error)
			return string;

		if (reader->beBits)
		{
			string <<= 1;
			string |= bit;
		}
		else
		{
			string |= bit << i;
		}
	}

	return string;
}

// ----- Bit writer routines

typedef struct CxiBitWriter_
{
	uint32_t *bits;
	unsigned int nWords;
	unsigned int nBitsInLastWord;
	unsigned int nWordsAlloc;
	unsigned int length;
} CxiBitWriter;

static void CxiBitWriterInit (CxiBitWriter *writer)
{
	writer->nWords = 0;
	writer->length = 0;
	writer->nBitsInLastWord = 32;
	writer->nWordsAlloc = 16;
	writer->bits = (uint32_t *)calloc (writer->nWordsAlloc, 4);
}

static void CxiBitWriterFree (CxiBitWriter *writer)
{
	free (writer->bits);
}

static void CxiBitWriterWriteBit (CxiBitWriter *writer, int bit)
{
	if (writer->nBitsInLastWord == 32)
	{
		writer->nBitsInLastWord = 0;
		writer->nWords++;
		if (writer->nWords > writer->nWordsAlloc)
		{
			unsigned int newAllocSize = (writer->nWordsAlloc + 2) * 3 / 2;
			writer->bits = realloc (writer->bits, newAllocSize * 4);
			writer->nWordsAlloc = newAllocSize;
		}
		writer->bits[writer->nWords - 1] = 0;
	}

	writer->bits[writer->nWords - 1] |= bit << (31 - writer->nBitsInLastWord);
	writer->nBitsInLastWord++;
	writer->length++;
}

static void *CxiBitWriterGetBytes (
	CxiBitWriter *writer, int wordAlign, int beBytes, int beBits, unsigned int *size)
{
	// allocate buffer
	unsigned int outSize = writer->nWords * 4;
	if (!wordAlign && beBytes != beBits)
	{
		// nBitsInLast word is 32 if last word is full, 0 if empty.
		if (writer->nBitsInLastWord <= 24)
			outSize--;
		if (writer->nBitsInLastWord <= 16)
			outSize--;
		if (writer->nBitsInLastWord <= 8)
			outSize--;
		if (writer->nBitsInLastWord <= 0)
			outSize--;
	}
	unsigned char *outbuf = (unsigned char *)calloc (outSize, 1);

	// this function handles converting byte and bit orders from the internal
	// representation. Internally, we store the bit sequence as an array of
	// words, where the first bits are inserted at the most significant bit.
	for (unsigned int i = 0; i < outSize; i++)
	{
		uint32_t word = writer->bits[i / 4];
		if (beBytes)
			word = CxiByteSwap (word);

		// if little endian bit order, swap here
		uint8_t byte = (word >> (8 * (i % 4))) & 0xFF;
		if (!beBits)
			byte = CxiBitReverse8 (byte);
		outbuf[i] = byte;
	}

	*size = outSize;
	return outbuf;
}

static void CxiBitWriterWriteBits (CxiBitWriter *writer, uint32_t bits, unsigned int nBits)
{
	for (unsigned int i = 0; i < nBits; i++)
		CxiBitWriterWriteBit (writer, (bits >> i) & 1);
}

static void CxiBitWriterWriteBitsBE (CxiBitWriter *writer, uint32_t bits, unsigned int nBits)
{
	for (unsigned int i = 0; i < nBits; i++)
		CxiBitWriterWriteBit (writer, (bits >> (nBits - 1 - i)) & 1);
}

// ----- Huffman coding routines

typedef struct CxiHuffNode_
{
	uint16_t sym;
	uint16_t symMin;
	uint16_t symMax;
	int freq;
	struct CxiHuffNode_ *left;
	struct CxiHuffNode_ *right;
} CxiHuffNode;

typedef struct CxiHuffCode_
{
	uint16_t value;
	uint16_t length;
	uint32_t encoding;
} CxiHuffCode;

#define ISLEAF(n) ((n)->left == NULL && (n)->right == NULL)

static int CxiHuffFrequencyComparator (const void *p1, const void *p2)
{
	const CxiHuffNode *n1 = (const CxiHuffNode *)p1;
	const CxiHuffNode *n2 = (const CxiHuffNode *)p2;

	// sort first according to descending frequency
	if (n2->freq != n1->freq)
		return n2->freq - n1->freq;

	// sort secondarily by symbol value (low symbols first)
	if (n1->sym < n2->sym)
		return -1;
	if (n1->sym > n2->sym)
		return 1;
	return 0;
}

static int CxiHuffCanonicalComparator (const void *p1, const void *p2)
{
	const CxiHuffCode *c1 = (const CxiHuffCode *)p1;
	const CxiHuffCode *c2 = (const CxiHuffCode *)p2;

	// force 0-length (excluded) symbols to the end
	if (c1->length == 0)
		return 1;
	if (c2->length == 0)
		return -1;

	if (c1->length < c2->length)
		return -1;
	if (c1->length > c2->length)
		return 1;
	if (c1->value < c2->value)
		return -1;
	if (c1->value > c2->value)
		return 1;
	return 0;
}

static int CxiHuffSymbolComparator (const void *p1, const void *p2)
{
	const CxiHuffCode *c1 = (const CxiHuffCode *)p1;
	const CxiHuffCode *c2 = (const CxiHuffCode *)p2;

	if (c1->value < c2->value)
		return -1;
	if (c1->value > c2->value)
		return 1;
	return 0;
}

static int CxiHuffmanHasSymbol (CxiHuffNode *node, uint16_t sym)
{
	if (ISLEAF (node))
		return node->sym == sym;
	if (sym < node->symMin || sym > node->symMax)
		return 0;

	return CxiHuffmanHasSymbol (node->left, sym) || CxiHuffmanHasSymbol (node->right, sym);
}

static void CxiHuffmanWriteSymbol (CxiBitWriter *bits, uint16_t sym, const CxiHuffNode *tree)
{
	if (ISLEAF (tree))
		return;

	if (CxiHuffmanHasSymbol (tree->left, sym))
	{
		CxiBitWriterWriteBit (bits, 0);
		CxiHuffmanWriteSymbol (bits, sym, tree->left);
	}
	else
	{
		CxiBitWriterWriteBit (bits, 1);
		CxiHuffmanWriteSymbol (bits, sym, tree->right);
	}
}

static unsigned int CxiHuffmanConstructTree (
	CxiHuffNode *nodes, unsigned int nNodes, unsigned int nNodeMin)
{
	// initialize symMin, symMax
	for (unsigned int i = 0; i < nNodes; i++)
	{
		nodes[i].symMin = nodes[i].symMax = nodes[i].sym;
	}

	// sort by frequency, then cut off the remainder (freq=0).
	qsort (nodes, nNodes, sizeof (CxiHuffNode), CxiHuffFrequencyComparator);
	for (unsigned int i = 0; i < nNodes; i++)
	{
		if (nodes[i].freq == 0)
		{
			nNodes = i;
			break;
		}
	}
	if (nNodes < nNodeMin)
		nNodes = nNodeMin;

	// unflatten the histogram into a huffman tree.
	int nRoots = nNodes;
	int nTotalNodes = nNodes;
	while (nRoots > 1)
	{
		// copy bottom two nodes to just outside the current range
		CxiHuffNode *srcA = nodes + nRoots - 2;
		CxiHuffNode *destA = nodes + nTotalNodes;
		memcpy (destA, srcA, sizeof (CxiHuffNode));

		CxiHuffNode *left = destA;
		CxiHuffNode *right = nodes + nRoots - 1;
		CxiHuffNode *branch = srcA;

		branch->freq = left->freq + right->freq;
		branch->sym = 0;
		branch->left = left;
		branch->right = right;
		branch->symMin = min (left->symMin, right->symMin);
		branch->symMax = max (right->symMax, left->symMax);

		nRoots--;
		nTotalNodes++;
		qsort (nodes, nRoots, sizeof (CxiHuffNode), CxiHuffFrequencyComparator);
	}

	return nNodes;
}

static int CxiHuffAppendCanonicalCode (
	CxiHuffNode *tree, CxiHuffCode *codes, uint32_t encoding, int depth)
{
	if (ISLEAF (tree))
	{
		codes[tree->sym].length = depth;
		return 1;
	}

	// recurse
	int nl = CxiHuffAppendCanonicalCode (tree->left, codes, (encoding << 1) | 0, depth + 1);
	int nr = CxiHuffAppendCanonicalCode (tree->right, codes, (encoding << 1) | 1, depth + 1);
	return nl + nr;
}

static void CxiHuffMakeCanonicalCodes (CxiHuffNode *tree, CxiHuffCode *codes, int nMaxNodes)
{
	// first, recursively append to the list.
	int nNodes = CxiHuffAppendCanonicalCode (tree, codes, 0, 1);
	for (int i = 0; i < nMaxNodes; i++)
	{
		codes[i].value = i;
	}

	// next, apply sort. Unassigned codes are pushed to the end of the list.
	qsort (codes, nMaxNodes, sizeof (CxiHuffCode), CxiHuffCanonicalComparator);

	// next, we can start assigning codes.
	uint32_t curcode = 0, curbits = 0, curmask = 0;
	for (int i = 0; i < nNodes; i++)
	{
		// shift code
		while (curbits < codes[i].length)
		{
			curcode <<= 1;
			curmask = (curmask << 1) | 1;
			curbits++;
		}
		codes[i].encoding = curcode;

		// increment current code
		curcode++;
		if ((curcode & curmask) == 0)
		{
			curmask = (curmask << 1) | 1;
			curbits++;
		}
	}

	// sort codes by symbol value again (for constant code lookup time)
	qsort (codes, nMaxNodes, sizeof (CxiHuffCode), CxiHuffSymbolComparator);
}

int CxIsCompressedLZ (const unsigned char *buffer, unsigned int size)
{
	if (size < 4)
		return 0;
	if (*buffer != 0x10)
		return 0;
	uint32_t length = (*(uint32_t *)buffer) >> 8;
	if ((length / 144) * 17 + 4 > size)
		return 0;

	// start a dummy decompression
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1)
	{
		uint8_t head = buffer[offset];
		offset++;

		// loop 8 times
		for (int i = 0; i < 8; i++)
		{
			int flag = head >> 7;
			head <<= 1;

			if (!flag)
			{
				if (dstOffset >= length || offset >= size)
					return 0;
				dstOffset++, offset++;
				if (dstOffset == length)
					goto checkSize;
			}
			else
			{
				if (offset + 1 >= size)
					return 0;
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				// length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;

				if (dstOffset < offs)
					return 0;
				for (uint32_t j = 0; j < len; j++)
				{
					if (dstOffset >= length)
						return 0;
					dstOffset++;
					if (dstOffset == length)
						goto checkSize;
				}
			}
		}
	}

	// check the size of the remaining data
	unsigned int remaining;
checkSize:
	remaining = size - offset;
	if (remaining > 7)
		return 0;

	return 1;
}
int CxIsCompressedRL (const unsigned char *buffer, unsigned int size)
{
	if (size < 4)
		return 0;
	if (*buffer != 0x30)
		return 0;
	uint32_t header = *(uint32_t *)buffer;
	unsigned int uncompSize = header >> 8;

	unsigned int dstOfs = 0;
	unsigned int srcOfs = 4;
	while (dstOfs < uncompSize)
	{
		if (srcOfs >= size)
			return 0;
		unsigned char head = buffer[srcOfs++];

		int compressed = head >> 7;
		if (compressed)
		{
			int chunkLen = (head & 0x7F) + 3;
			if (srcOfs >= size)
				return 0;
			srcOfs++;

			for (int i = 0; i < chunkLen; i++)
			{
				dstOfs++;
			}
		}
		else
		{
			int chunkLen = (head & 0x7F) + 1;
			for (int i = 0; i < chunkLen; i++)
			{
				if (srcOfs >= size)
					return 0;
				dstOfs++;
				srcOfs++;
			}
		}

		if (dstOfs > uncompSize)
			return 0;
	}

	// allow up to 3 bytes padding
	if (size - srcOfs > 3)
		return 0;

	return 1;
}
// ----- MvDK Routines

#define MVDK_DUMMY 0
#define MVDK_LZ 1
#define MVDK_DEFLATE 2
#define MVDK_RLE 3
#define MVDK_INVALID -1

typedef struct DEFLATE_TABLE_ENTRY_
{
	uint16_t nMinorBits;
	uint16_t majorPart;
} DEFLATE_TABLE_ENTRY;

typedef struct DEFLATE_TREE_NODE
{
	struct DEFLATE_TREE_NODE *left;
	struct DEFLATE_TREE_NODE *right;
	uint8_t depth;
	uint8_t isLeaf;
	uint16_t value;
	uint32_t path;
} DEFLATE_TREE_NODE;

typedef struct DEFLATE_WORK_BUFFER_
{
	DEFLATE_TREE_NODE symbolNodeBuffer[855];
	DEFLATE_TREE_NODE lengthNodeBuffer[855];
	DEFLATE_TREE_NODE *nextAvailable;
} DEFLATE_WORK_BUFFER;

static const DEFLATE_TABLE_ENTRY sDeflateLengthTable[] = { { 0, 0x00 }, { 0, 0x01 }, { 0, 0x02 },
	{ 0, 0x03 }, { 0, 0x04 }, { 0, 0x05 }, { 0, 0x06 }, { 0, 0x07 }, { 1, 0x08 }, { 1, 0x0A },
	{ 1, 0x0C }, { 1, 0x0E }, { 2, 0x10 }, { 2, 0x14 }, { 2, 0x18 }, { 2, 0x1C }, { 3, 0x20 },
	{ 3, 0x28 }, { 3, 0x30 }, { 3, 0x38 }, { 4, 0x40 }, { 4, 0x50 }, { 4, 0x60 }, { 4, 0x70 },
	{ 5, 0x80 }, { 5, 0xA0 }, { 5, 0xC0 }, { 5, 0xE0 }, { 0, 0xFF } };

static const DEFLATE_TABLE_ENTRY sDeflateOffsetTable[] = { { 0, 0x0000 }, { 0, 0x0001 },
	{ 0, 0x0002 }, { 0, 0x0003 }, { 1, 0x0004 }, { 1, 0x0006 }, { 2, 0x0008 }, { 2, 0x000C },
	{ 3, 0x0010 }, { 3, 0x0018 }, { 4, 0x0020 }, { 4, 0x0030 }, { 5, 0x0040 }, { 5, 0x0060 },
	{ 6, 0x0080 }, { 6, 0x00C0 }, { 7, 0x0100 }, { 7, 0x0180 }, { 8, 0x0200 }, { 8, 0x0300 },
	{ 9, 0x0400 }, { 9, 0x0600 }, { 10, 0x0800 }, { 10, 0x0C00 }, { 11, 0x1000 }, { 11, 0x1800 },
	{ 12, 0x2000 }, { 12, 0x3000 }, { 13, 0x4000 }, { 13, 0x6000 } };

// deflate decompress (inflate?)

// ----- Huffman tree construction

void CxiHuffmanInsertNode (DEFLATE_WORK_BUFFER *auxBuffer, DEFLATE_TREE_NODE *root,
	DEFLATE_TREE_NODE *node2, unsigned int depth)
{
	// 0 for left, 1 for right
	int pathbit = (node2->path >> depth) & 1;

	// depth=0 means insert here
	if (depth == 0)
	{
		if (pathbit)
		{
			root->right = node2;
		}
		else
		{
			root->left = node2;
		}
		return;
	}

	if (pathbit)
	{
		// create a right node if it doesn't exist
		if (root->right == NULL)
		{
			DEFLATE_TREE_NODE *available = auxBuffer->nextAvailable;
			auxBuffer->nextAvailable++;
			root->right = available;
		}
		CxiHuffmanInsertNode (auxBuffer, root->right, node2, depth - 1);
	}
	else
	{
		// create a left node if it doesn't exist
		if (root->left == NULL)
		{
			DEFLATE_TREE_NODE *available = auxBuffer->nextAvailable;
			auxBuffer->nextAvailable++;
			root->left = available;
		}
		CxiHuffmanInsertNode (auxBuffer, root->left, node2, depth - 1);
	}
}

DEFLATE_TREE_NODE *CxiHuffmanReadTree (DEFLATE_WORK_BUFFER *auxBuffer, CxiBitReader *reader,
	DEFLATE_TREE_NODE *nodeBuffer, unsigned int nNodes)
{
	unsigned int i, j;
	int paths[32];
	int depthCounts[32];

	// clear buffers
	memset (nodeBuffer, 0, nNodes * 2 * sizeof (DEFLATE_TREE_NODE));
	memset (depthCounts, 0, sizeof (depthCounts));
	memset (paths, 0, sizeof (paths));

	i = 0;
	while (i < nNodes)
	{
		// Read 1 bit - determines format of node structure?
		if (CxiBitReaderReadBit (reader))
		{
			// read 7-bit number from 2 to 129 (number of loop iterations)
			unsigned int nNodesBlock = CxiBitReaderReadBits (reader, 7) + 2;
			if (reader->error)
				return NULL;
			if (i + nNodesBlock > nNodes)
				return NULL;

			// this 5-bit value gets put into the depth of all nodes written here
			unsigned int depth = CxiBitReaderReadBits (reader, 5);
			if (reader->error)
				return NULL;

			for (j = 0; j < nNodesBlock; j++)
			{
				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		}
		else
		{
			// read 7-bit number from 1 to 128. Number of loop iterations.
			unsigned int nNodesBlock = CxiBitReaderReadBits (reader, 7) + 1;
			if (reader->error)
				return NULL;
			if (i + nNodesBlock > nNodes)
				return NULL;

			for (j = 0; j < nNodesBlock; j++)
			{
				uint8_t depth = CxiBitReaderReadBits (reader, 5);
				if (reader->error)
					return NULL;

				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		}
	}

	// written too many nodes
	if (i > nNodes)
		return NULL;

	int depth = 0;
	depthCounts[0] = 0;
	for (i = 1; i < 32; i++)
	{
		depth = (depth + depthCounts[i - 1]) << 1;
		paths[i] = depth;
	}

	DEFLATE_TREE_NODE *root = nodeBuffer + nNodes;
	auxBuffer->nextAvailable = root + 1;

	for (i = 0; i < nNodes; i++)
	{
		DEFLATE_TREE_NODE *node = nodeBuffer + i;
		node->isLeaf = 1;

		if (node->depth > 0)
		{
			node->path = paths[node->depth];
			node->value = i;
			paths[node->depth]++;
			CxiHuffmanInsertNode (auxBuffer, root, node, node->depth - 1);
		}
	}
	return root;
}

uint32_t CxiLookupTreeNode (DEFLATE_TREE_NODE *node, CxiBitReader *reader)
{
	if (node == NULL)
		return (uint32_t)-1;

	while (!node->isLeaf)
	{
		if (CxiBitReaderReadBit (reader))
		{
			node = node->right;
		}
		else
		{
			node = node->left;
		}
		if (reader->error || node == NULL)
			return (uint32_t)-1;
	}
	return node->value;
}

unsigned char *CxiDecompressDeflateChunk (DEFLATE_WORK_BUFFER *auxBuffer, unsigned char *destBase,
	const unsigned char **pPos, unsigned char *dest, unsigned char *end,
	const unsigned char *srcEnd, int write)
{
	// init reader
	CxiBitReader reader;
	const unsigned char *pos = *pPos;
	uint32_t nBytesConsumed = 0;
	CxiBitReaderInit (&reader, pos, srcEnd, 0, 0);

	int isCompressed = CxiBitReaderReadBit (&reader);
	if (reader.error)
		return NULL;
	uint32_t chunkLen = CxiBitReaderReadBits (&reader, 31);
	if (reader.error)
		return NULL;

	if (!isCompressed)
	{
		// uncompressed chunk, just memcpy out
		if ((dest + chunkLen) > end || (dest + chunkLen) < destBase
			|| (pos + 4 + chunkLen) > srcEnd)
			return NULL;
		if (write)
			memcpy (dest, pos + 4, chunkLen);

		nBytesConsumed = chunkLen + 4;
		dest += chunkLen;
	}
	else
	{
		const unsigned char *tableBase = reader.pos;

		// Consume a Huffman tree. The length of the tree data (in bits) is given by the next 16
		// bits in the stream.
		uint32_t lzLen2 = CxiBitReaderReadBits (&reader, 16);
		uint32_t table1SizeBytes = (lzLen2 + 7) >> 3;
		const unsigned char *postTree = reader.pos + table1SizeBytes;
		DEFLATE_TREE_NODE *huffRoot1
			= CxiHuffmanReadTree (auxBuffer, &reader, auxBuffer->symbolNodeBuffer, 0x11D);
		if (huffRoot1 == NULL)
			return NULL; // Huffman tree error
		if (postTree > srcEnd)
			return NULL; // Validate tree size

		// Reposition stream after the Huffman tree. Read out the LZ distance tree next.
		// Its size in bits is given by the following 16 bits from the stream.
		CxiBitReaderInit (&reader, postTree, srcEnd, 0, 0);
		reader.nBitsRead = (postTree - pos) * 8;
		lzLen2 = CxiBitReaderReadBits (&reader, 16);
		uint32_t table2SizeBytes = (lzLen2 + 7) >> 3;

		postTree = reader.pos + table2SizeBytes;
		DEFLATE_TREE_NODE *huffDistancesRoot
			= CxiHuffmanReadTree (auxBuffer, &reader, auxBuffer->lengthNodeBuffer, 0x1E);
		if (huffDistancesRoot == NULL)
			return NULL; // Huffman tree error
		if (postTree > srcEnd)
			return NULL; // Validate tree size

		// Reposition stream after this tree to prepare for reading the compressed sequence.
		CxiBitReaderInit (&reader, postTree, srcEnd, 0, 0);
		reader.nBitsRead = (reader.pos - pos) * 8;

		while (reader.nBitsRead < chunkLen && dest < end)
		{
			uint32_t huffVal = CxiLookupTreeNode (huffRoot1, &reader);
			if (huffVal == (uint32_t)-1)
				return NULL;

			if (huffVal < 0x100)
			{
				// simple byte value Huffman
				if (write)
					*dest = (unsigned char)huffVal;
				dest++;
			}
			else
			{
				// LZ part Huffman

				// read out length
				uint32_t nLengthMinorBits = sDeflateLengthTable[huffVal - 0x100].nMinorBits;
				uint32_t lzLen1 = sDeflateLengthTable[huffVal - 0x100].majorPart;
				uint32_t lzLen2 = CxiBitReaderReadBits (&reader, nLengthMinorBits);
				uint32_t lzLen = lzLen1 + lzLen2 + 3;

				// read out offset
				uint32_t nodeVal2 = CxiLookupTreeNode (huffDistancesRoot, &reader);
				if (nodeVal2 == (uint32_t)-1)
					return NULL;

				uint32_t nOffsetMinorBits = sDeflateOffsetTable[nodeVal2].nMinorBits;
				uint32_t lzOffset1 = sDeflateOffsetTable[nodeVal2].majorPart;
				uint32_t lzOffset2 = CxiBitReaderReadBits (&reader, nOffsetMinorBits);
				uint32_t lzOffset = lzOffset1 + lzOffset2 + 1;

				size_t curoffs = dest - destBase;
				size_t remaining = end - dest;
				if (lzOffset > curoffs)
					return NULL;
				if (lzLen > remaining)
					return NULL;

				unsigned char *lzSrc = dest - lzOffset;
				unsigned int i;
				for (i = 0; i < lzLen && dest < end; i++)
				{
					if (write)
						*dest = *lzSrc;
					dest++, lzSrc++;
				}
			}
		}
		nBytesConsumed = (chunkLen + 7) >> 3;
	}

	*pPos = pos + nBytesConsumed;
	return dest;
}

void CxDecompressDeflate (
	const unsigned char *filebuf, unsigned char *dest, void *auxBuffer, unsigned int size)
{
	const unsigned char *pos = filebuf + 4;
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *)filebuf) >> 2);

	while (dest < end)
	{
		dest = CxiDecompressDeflateChunk (
			(DEFLATE_WORK_BUFFER *)auxBuffer, destBase, &pos, dest, end, filebuf + size, 1);
	}
}
static int CxiMvdkIsValidLZ (const unsigned char *buffer, unsigned int size)
{
	// same format as standard LZ, with different header
	uint32_t uncompSize = (*(uint32_t *)buffer) >> 2;
	char *copy = (char *)malloc (size);
	memcpy (copy, buffer, size);
	*(uint32_t *)copy = 0x10 | (uncompSize << 8);
	int valid = CxIsCompressedLZ (copy, size);
	free (copy);
	return valid;
}

static int CxiMvdkIsValidRL (const unsigned char *buffer, unsigned int size)
{
	// same format as standard LZ, with different header
	uint32_t uncompSize = (*(uint32_t *)buffer) >> 2;
	char *copy = (char *)malloc (size);
	memcpy (copy, buffer, size);
	*(uint32_t *)copy = 0x30 | (uncompSize << 8);
	int valid = CxIsCompressedRL (copy, size);
	free (copy);
	return valid;
}

static int CxiMvdkIsValidDeflate (const unsigned char *buffer, unsigned int size)
{
	const unsigned char *pos = buffer + 4;
	unsigned char *dest = NULL; // won't be written to
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *)buffer) >> 2); // for address comparison
	DEFLATE_WORK_BUFFER *work = (DEFLATE_WORK_BUFFER *)calloc (1, sizeof (DEFLATE_WORK_BUFFER));

	while (dest < end)
	{
		dest = CxiDecompressDeflateChunk (work, destBase, &pos, dest, end, buffer + size, 0);
		if (dest == NULL)
		{
			free (work);
			return 0;
		}
	}
	free (work);

	// test buffer remaining (allow up to 3 bytes trailing for 4-byte aligned file size)
	unsigned int nConsumed = pos - buffer;
	nConsumed = (nConsumed + 3) & ~3;

	// check bytes unconsumed (Nintendo's encoder sometimes adds 4 bytes? uncompressed block
	// indicator?)
	if ((nConsumed + 4) < ((size + 3) & ~3))
		return 0;

	return 1;
}

// --- additional NitroPaint helpers required by CxDecompressMvDK ---
unsigned char *CxDecompressLZ (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	if (size < 4)
		return NULL;

	// find the length of the decompressed buffer.
	uint32_t length = (*(uint32_t *)buffer) >> 8;

	// create a buffer for the decompressed buffer
	unsigned char *result = (unsigned char *)malloc (length);
	if (result == NULL)
		return NULL;
	*uncompressedSize = length;

	// initialize variables
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1)
	{
		uint8_t head = buffer[offset];
		offset++;
		// loop 8 times
		for (int i = 0; i < 8; i++)
		{
			int flag = head >> 7;
			head <<= 1;

			if (!flag)
			{
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if (dstOffset == length)
					return result;
			}
			else
			{
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				// length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;
				for (uint32_t j = 0; j < len; j++)
				{
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if (dstOffset == length)
						return result;
				}
			}
		}
	}
	return result;
}

unsigned char *CxDecompressRL (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	unsigned int uncompSize = (*(uint32_t *)buffer) >> 8;
	unsigned char *out = (unsigned char *)calloc (uncompSize, 1);
	*uncompressedSize = uncompSize;

	unsigned int dstOfs = 0;
	unsigned int srcOfs = 4;
	while (dstOfs < uncompSize)
	{
		unsigned char head = buffer[srcOfs++];

		int compressed = head >> 7;
		if (compressed)
		{
			int chunkLen = (head & 0x7F) + 3;
			unsigned char b = buffer[srcOfs++];
			for (int i = 0; i < chunkLen; i++)
			{
				out[dstOfs++] = b;
			}
		}
		else
		{
			int chunkLen = (head & 0x7F) + 1;
			for (int i = 0; i < chunkLen; i++)
			{
				out[dstOfs++] = buffer[srcOfs++];
			}
		}
	}

	return out;
}

static int CxiMvdkGetCompressionType (const unsigned char *buffer, unsigned int size)
{
	return (*(uint32_t *)buffer) & 3;
}

int CxIsCompressedMvDK (const unsigned char *buffer, unsigned int size)
{
	if (size < 4)
		return 0;

	uint32_t uncompSize = (*(uint32_t *)buffer) >> 2;
	int type = CxiMvdkGetCompressionType (buffer, size);
	switch (type)
	{
		case MVDK_DUMMY:
			// check size
			return (((size - 4 + 3) & ~3) == ((uncompSize + 3) & ~3)) && ((size - 4) >= uncompSize);
		case MVDK_LZ:
			return CxiMvdkIsValidLZ (buffer, size);
		case MVDK_RLE:
			return CxiMvdkIsValidRL (buffer, size);
		case MVDK_DEFLATE:
			return CxiMvdkIsValidDeflate (buffer, size);
	}
	return 0;
}

static unsigned char *CxiMvdkDecompressDummy (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	uint32_t outlen = (*(uint32_t *)buffer) >> 2;
	unsigned char *out = (unsigned char *)malloc (outlen);
	*uncompressedSize = outlen;

	memcpy (out, buffer + 4, outlen);
	return out;
}

static unsigned char *CxiMvdkDecompressLZ (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	uint32_t outlen = (*(uint32_t *)buffer) >> 2;

	unsigned char *copy = (unsigned char *)malloc (size);
	memcpy (copy, buffer, size);
	*(uint32_t *)copy = 0x10 | (outlen << 8);
	unsigned char *out = CxDecompressLZ (copy, size, uncompressedSize);
	free (copy);

	return out;
}

static unsigned char *CxiMvdkDecompressRL (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	uint32_t outlen = (*(uint32_t *)buffer) >> 2;

	char *copy = (char *)malloc (size);
	memcpy (copy, buffer, size);
	*(uint32_t *)copy = 0x30 | (outlen << 8);
	char *out = CxDecompressRL (copy, size, uncompressedSize);
	free (copy);

	return out;
}

static unsigned char *CxiMvdkDecompressDeflate (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	uint32_t outlen = (*(uint32_t *)buffer) >> 2;
	*uncompressedSize = outlen;
	char *dest = malloc (outlen);

	void *aux = calloc (1, sizeof (DEFLATE_WORK_BUFFER));
	CxDecompressDeflate (buffer, dest, aux, size);
	free (aux);
	return dest;
}

unsigned char *CxDecompressMvDK (
	const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize)
{
	int type = (*(uint32_t *)buffer) & 3;
	switch (type)
	{
		case MVDK_DUMMY:
			return CxiMvdkDecompressDummy (buffer, size, uncompressedSize);
		case MVDK_LZ:
			return CxiMvdkDecompressLZ (buffer, size, uncompressedSize);
		case MVDK_RLE:
			return CxiMvdkDecompressRL (buffer, size, uncompressedSize);
		case MVDK_DEFLATE:
			return CxiMvdkDecompressDeflate (buffer, size, uncompressedSize);
	}
	*uncompressedSize = 0;
	return NULL;
}

// ------- Wrappers using this codebase's error conventions -------
enumError DecodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (src_size < 4)
		return EINVAL;
	unsigned int uncomp_size;
	unsigned char *res = CxDecompressMvDK (src, src_size, &uncomp_size);
	if (!res)
		return EINVAL;
	enumError err = AllocOutput (dest, dest_size, uncomp_size);
	if (err)
	{
		free (res);
		return err;
	}
	memcpy (*dest, res, uncomp_size);
	free (res);
	// Restore sentinels after using free()
#define calloc do_not_use_calloc
#define malloc do_not_use_malloc
#define realloc do_not_use_realloc
	return ERR_OK;
}
#define free do_not_use_free

enumError EncodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x3fffffff)
		return EINVAL;

	u8 *lz_data = 0;
	uint lz_size = 0;
	enumError err = EncodeLZ10LZ11 (&lz_data, &lz_size, src, src_size, false);
	if (err || !lz_data || lz_size < 4)
	{
		u8 *out = MALLOC (4 + src_size);
		if (!out)
			return ERR_CANT_CREATE;
		u32 hdr = (src_size << 2) | 0;
		out[0] = (u8)hdr;
		out[1] = (u8)(hdr >> 8);
		out[2] = (u8)(hdr >> 16);
		out[3] = (u8)(hdr >> 24);
		memcpy (out + 4, src, src_size);
		*dest = out;
		*dest_size = 4 + src_size;
		return ERR_OK;
	}

	u32 hdr = (src_size << 2) | 1;
	lz_data[0] = (u8)hdr;
	lz_data[1] = (u8)(hdr >> 8);
	lz_data[2] = (u8)(hdr >> 16);
	lz_data[3] = (u8)(hdr >> 24);

	*dest = lz_data;
	*dest_size = lz_size;
	return ERR_OK;
}
