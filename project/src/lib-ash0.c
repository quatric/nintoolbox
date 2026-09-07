#include "lib-std.h"
#include "lib-ash0.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef struct ash_bits_t
{
	const u8 *src;
	uint size, pos, word, used;
} ash_bits_t;

static bool ash_feed (ash_bits_t *br)
{
	if (br->pos > br->size - 4)
		return false;
	br->word = rd_be32 (br->src + br->pos);
	br->pos += 4;
	br->used = 0;
	return true;
}

static bool ash_init (ash_bits_t *br, const u8 *src, uint size, uint pos)
{
	if (!br || pos > size)
		return false;
	br->src = src;
	br->size = size;
	br->pos = pos;
	br->word = br->used = 0;
	return ash_feed (br);
}

static bool ash_read (ash_bits_t *br, uint n, uint *value)
{
	if (!n || n > 24 || !value)
		return false;
	uint val = 0;
	while (n--)
	{
		val = val << 1 | br->word >> 31;
		if (++br->used == 32)
		{
			if (!ash_feed (br))
				return false;
		}
		else
			br->word <<= 1;
	}
	*value = val;
	return true;
}

static bool ash_tree (ash_bits_t *br, uint width, uint *left, uint *right, uint *root)
{
	const uint max = 1u << width, cap = 2 * max - 1;
	uint work[2 * 2048], work_used = 0, nodes = 0, next = max;
	for (;;)
	{
		uint bit, value;
		if (!ash_read (br, 1, &bit))
			return false;
		if (bit)
		{
			if (work_used + 2 > sizeof (work) / sizeof (*work) || next >= cap)
				return false;
			work[work_used++] = next | 0x80000000u;
			work[work_used++] = next | 0x40000000u;
			nodes += 2;
			next++;
			continue;
		}
		if (!ash_read (br, width, &value) || value >= max)
			return false;
		*root = value;
		while (nodes)
		{
			const uint node = work[--work_used], index = node & 0x3fffffffu;
			if (index >= cap)
				return false;
			nodes--;
			if (node & 0x80000000u)
			{
				right[index] = *root;
				*root = index;
			}
			else
			{
				left[index] = *root;
				break;
			}
		}
		if (!nodes)
			return true;
	}
}

static bool ash_symbol (
	ash_bits_t *br, uint root, uint max, const uint *left, const uint *right, uint *value)
{
	uint sym = root;
	while (sym >= max)
	{
		uint bit;
		if (sym >= 2 * max - 1 || !ash_read (br, 1, &bit))
			return false;
		sym = bit ? right[sym] : left[sym];
	}
	*value = sym;
	return true;
}

// ASH0's distance-tree bit width is a build-time choice baked into the
// encoder, not a field in the file header -- confirmed against
// NinjaCheetah/ASH0-tools (Decompressor/main.c), a from-scratch clean-room
// ASH0 codec whose CLI exposes it as a manual `-d` flag defaulting to 11
// ("These work for ASH0 files found in the System Menu and Animal Crossing:
// City Folk. ASH0 files found in My Pokémon Ranch require setting the
// distance tree bits to 15 instead.") -- there is no header bit to switch
// on. Since a real file gives no way to know up front, try the common case
// first and fall back to the one confirmed exception on failure, rather
// than guess a detection rule with no evidence behind it.
static enumError DecodeASH0Try (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, uint dist_bits)
{
	const uint out_size = rd_be32 (src + 4) & 0x00ffffff;
	const uint dist_start = rd_be32 (src + 8);
	enumError err = AllocOutput (dest, dest_size, out_size);
	if (err)
		return err;
	ash_bits_t syms, dists;
	const uint sym_max = 1u << 9, dist_max = 1u << dist_bits;
	uint *sl = CALLOC (2 * sym_max - 1, sizeof (*sl)), *sr = CALLOC (2 * sym_max - 1, sizeof (*sr));
	uint *dl = CALLOC (2 * dist_max - 1, sizeof (*dl)),
		 *dr = CALLOC (2 * dist_max - 1, sizeof (*dr));
	uint sym_root = 0, dist_root = 0;
	if (!sl || !sr || !dl || !dr || !ash_init (&syms, src, src_size, 0x0c)
		|| !ash_init (&dists, src, src_size, dist_start) || !ash_tree (&syms, 9, sl, sr, &sym_root)
		|| !ash_tree (&dists, dist_bits, dl, dr, &dist_root))
		goto invalid;
	for (uint pos = 0; pos < out_size;)
	{
		uint sym;
		if (!ash_symbol (&syms, sym_root, sym_max, sl, sr, &sym))
			goto invalid;
		if (sym < 0x100)
			(*dest)[pos++] = sym;
		else
		{
			uint distance;
			const uint len = sym - 0x100 + 3;
			if (!ash_symbol (&dists, dist_root, dist_max, dl, dr, &distance) || distance >= pos
				|| len > out_size - pos)
				goto invalid;
			for (uint n = 0; n < len; n++)
				(*dest)[pos + n] = (*dest)[pos - distance - 1 + n];
			pos += len;
		}
	}
	FREE (sl);
	FREE (sr);
	FREE (dl);
	FREE (dr);
	return ERR_OK;
invalid:
	FREE (sl);
	FREE (sr);
	FREE (dl);
	FREE (dr);
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError DecodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 0x10 || memcmp (src, "ASH0", 4))
		return EINVAL;
	const uint out_size = rd_be32 (src + 4) & 0x00ffffff;
	const uint dist_start = rd_be32 (src + 8);
	if (!out_size || out_size > NFMT_MAX_OUTPUT || dist_start > src_size - 4)
		return EINVAL;

	enumError err = DecodeASH0Try (dest, dest_size, src, src_size, 11);
	if (err)
		err = DecodeASH0Try (dest, dest_size, src, src_size, 15);
	return err;
}


#define min(a,b)    (((a)<(b))?(a):(b))
#define max(a,b)    (((a)>(b))?(a):(b))


// ----- assertions

#ifndef NDEBUG
#define CX_ASSERT(x)                                              \
do {                                                              \
	if (!(x)) {                                                   \
		fprintf(stderr, "Assertion failed: line %d\n", __LINE__); \
		exit(1);                                                  \
	}                                                             \
} while (0)
#else // NDEBUG
#ifdef __GNUC__
#define CX_ASSERT(x)   if(!(x)) __builtin_unreachable()
#elif defined(_MSC_VER) // __GNUC__
#define CX_ASSERT(x)   __assume(x)
#else // _MSC_VER
#define CX_ASSERT(x)   (void)(x)
#endif // __GNUC__
#endif // NDEBUG



// ----- Bit writer implementation

typedef struct CxiBitWriter_ {
	uint32_t *bits;
	int nWords;
	int nBitsInLastWord;
	int nWordsAlloc;
	int length;
} CxiBitWriter;


static void CxiBitWriterCreate(CxiBitWriter *stream) {
	stream->nWords = 0;
	stream->length = 0;
	stream->nBitsInLastWord = 32;
	stream->nWordsAlloc = 16;
	stream->bits = (uint32_t *) CALLOC(stream->nWordsAlloc, sizeof(uint32_t));
}

static void CxiBitWriterFree(CxiBitWriter *stream) {
	FREE(stream->bits);
}

static void CxiBitWriterWrite(CxiBitWriter *stream, int bit) {
	if (stream->nBitsInLastWord == 32) {
		stream->nBitsInLastWord = 0;
		stream->nWords++;
		if (stream->nWords > stream->nWordsAlloc) {
			int newAllocSize = (stream->nWordsAlloc + 2) * 3 / 2;
			stream->bits = REALLOC(stream->bits, newAllocSize * sizeof(uint32_t));
			stream->nWordsAlloc = newAllocSize;
		}
		stream->bits[stream->nWords - 1] = 0;
	}

	stream->bits[stream->nWords - 1] |= (bit << (31 - stream->nBitsInLastWord));
	stream->nBitsInLastWord++;
	stream->length++;
}

static void *CxiBitWriterGetBytes(CxiBitWriter *stream, int wordAlign, int beBytes, int beBits, unsigned int *size) {
	//allocate buffer
	unsigned int outSize = stream->nWords * sizeof(uint32_t);
	if (!wordAlign) {
		//nBitsInLast word is 32 if last word is full, 0 if empty.
		if (stream->nBitsInLastWord <= 24) outSize--;
		if (stream->nBitsInLastWord <= 16) outSize--;
		if (stream->nBitsInLastWord <=  8) outSize--;
		if (stream->nBitsInLastWord <=  0) outSize--;
	}
	unsigned char *outbuf = (unsigned char *) CALLOC(outSize, 1);
	if (outbuf == NULL) return NULL;

	//this function handles converting byte and bit orders from the internal
	//representation. Internally, we store the bit sequence as an array of
	//words, where the first bits are inserted at the most significant bit.
	//

	for (unsigned int i = 0; i < outSize; i++) {
		int byteShift = 8 * (beBytes ? (3 - (i % 4)) : (i % 4));
		uint32_t word = stream->bits[i / 4];
		uint8_t byte = (word >> byteShift) & 0xFF;

		//if little endian bit order, swap here
		if (!beBits) {
			uint8_t temp = byte;
			byte = 0;
			for (int j = 0; j < 8; j++) byte |= ((temp >> j) & 1) << (7 - j);
		}
		outbuf[i] = byte;
	}

	*size = outSize;
	return outbuf;
}

static void CxiBitWriterWriteBitsBE(CxiBitWriter *stream, uint32_t bits, unsigned int nBits) {
	for (unsigned int i = 0; i < nBits; i++) CxiBitWriterWrite(stream, (bits >> (nBits - 1 - i)) & 1);
}



// ----- LZ search+tokenization implementation


//
// Structure for representing tokenized LZ data
//
typedef struct CxiLzToken_ {
	uint8_t isReference;
	union {
		uint8_t symbol;
		struct {
			uint16_t length;
			uint16_t distance;
		};
	};
} CxiLzToken;

//
// Stucture for accumulating LZ tokens
//
typedef struct CxiLzTokenBuffer_ {
	CxiLzToken *tokens;
	unsigned int nTokens;
	unsigned int capacity;
} CxiLzTokenBuffer;

//
// Structure used to construct the LZ graph
//
typedef struct CxiLzNode_ {
	CxiLzToken token;
	uint32_t weight;
} CxiLzNode;

//
// Structure for keeping track of LZ sliding window
//
typedef struct CxiLzState_ {
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


static unsigned int CxiLzHash3(const unsigned char *p) {
	unsigned char c0 = p[0];         // A
	unsigned char c1 = p[0] ^ p[1];  // A ^ B
	unsigned char c2 = p[0] ^ p[2];  // (A ^ B) ^ (B ^ C)
	return (c0 ^ (c1 << 1) ^ (c2 << 2) ^ (c2 >> 7)) & 0x1FF;
}

static void CxiLzStateInit(
	CxiLzState          *state,       // the LZ search state structure
	const unsigned char *buffer,      // the input buffer
	unsigned int         size,        // the size of the input buffer
	unsigned int         minLength,   // the minimum length to return from searches
	unsigned int         maxLength,   // the maximum length to return from searches
	unsigned int         minDistance, // the minimum distance value to return from searches
	unsigned int         maxDistance  // the maximum distance value to return from searches
) {
	state->buffer = buffer;
	state->size = size;
	state->pos = 0;
	state->minLength = minLength;
	state->maxLength = maxLength;
	state->minDistance = minDistance;
	state->maxDistance = maxDistance;

	for (unsigned int i = 0; i < 512; i++) {
		//init symbol lookup to empty
		state->symLookup[i] = UINT_MAX;
	}

	state->chain = (unsigned int *) CALLOC(state->maxDistance, sizeof(unsigned int));
	for (unsigned int i = 0; i < state->maxDistance; i++) {
		state->chain[i] = UINT_MAX;
	}
}

static void CxiLzStateFree(CxiLzState *state) {
	FREE(state->chain);
}

static unsigned int CxiLzStateGetChainIndex(CxiLzState *state, unsigned int index) {
	return (state->pos - index) % state->maxDistance;
}

static unsigned int CxiLzStateGetChain(CxiLzState *state, int index) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	return state->chain[chainIndex];
}

static void CxiLzStatePutChain(CxiLzState *state, unsigned int index, unsigned int data) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	state->chain[chainIndex] = data;
}

static void CxiLzStateSlideByte(CxiLzState *state) {
	if (state->pos >= state->size) return; // cannot slide

	//only update search structures when we have enough space left to necessitate searching.
	if ((state->size - state->pos) >= 3) {
		//fetch next 3 bytes' hash
		unsigned int next = CxiLzHash3(state->buffer + state->pos);

		//get the distance back to the next byte before sliding. If it exists in the window,
		//we'll have nextDelta less than UINT_MAX. We'll take this first occurrence and it 
		//becomes the offset from the current byte. Bear in mind the chain is 0-indexed starting
		//at a distance of 1. 
		unsigned int nextDelta = state->symLookup[next];
		if (nextDelta != UINT_MAX) {
			nextDelta++;
			if (nextDelta >= state->maxDistance) {
				nextDelta = UINT_MAX;
			}
		}
		CxiLzStatePutChain(state, 0, nextDelta);

		//increment symbol lookups
		for (int i = 0; i < 512; i++) {
			if (state->symLookup[i] != UINT_MAX) {
				state->symLookup[i]++;
				if (state->symLookup[i] > state->maxDistance) state->symLookup[i] = UINT_MAX;
			}
		}
		state->symLookup[next] = 0; // update entry for the current byte to the start of the chain
	}

	state->pos++;
}

static void CxiLzStateSlide(CxiLzState *state, unsigned int nSlide) {
	while (nSlide--) CxiLzStateSlideByte(state);
}

//
// Counts matched bytes from one location to another.
//
static unsigned int CxiCompareMemory(
	const unsigned char *b1,  // the first location
	const unsigned char *b2,  // the second location
	unsigned int         nMax // the maximum number of bytes to compare
) {
	//compare nAbsoluteMax bytes, do not perform any looping.
	unsigned int nSame = 0;
	while (nMax > 0) {
		if (*(b1++) != *(b2++)) break;
		nMax--;
		nSame++;
	}
	return nSame;
}

//
// Confirms a correct LZ match.
//
static int CxiLzConfirmMatch(
	const unsigned char *buffer,   // the buffer
	unsigned int         size,     // the buffer size
	unsigned int         pos,      // the position in the buffer to check at
	unsigned int         distance, // the distance back to check
	unsigned int         length    // the length to check
) {
	CX_ASSERT(pos <= size);
	CX_ASSERT((size - pos) >= length);
	CX_ASSERT(distance <= pos);
	
	return memcmp(buffer + pos, buffer + pos - distance, length) == 0;
}

//
// Searches the buffer managed by the LZ search state for a string match within the search bounds
// for a matching string at the current position.
//
static unsigned int CxiLzSearch(
	CxiLzState   *state,    // the LZ search state
	unsigned int *pDistance // the returned distance value
) {
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < 3 || nBytesLeft < state->minLength) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[CxiLzHash3(state->buffer + state->pos)];
	if (firstMatch == UINT_MAX) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//search backwards
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance) {
		//check only if distance is at least minDistance
		if (distance >= state->minDistance) {
			unsigned int matchLen = CxiCompareMemory(curp - distance, curp, nMaxCompare);

			if (matchLen > bestLength) {
				bestLength = matchLen;
				bestDistance = distance;
				if (bestLength == nMaxCompare) break;
			}
		}

		if (distance == state->maxDistance) break;
		unsigned int next = CxiLzStateGetChain(state, distance);
		if (next == UINT_MAX) break;
		distance += next;
	}

	if (bestLength < state->minLength) {
		//match length does not meet our minimum match length, return byte literal
		bestLength = 1;
		distance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}

static unsigned int CxiLzSearchRestricted(CxiLzState *state, const unsigned int *pDstDepths, unsigned int *pDistance) {
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < 3 || nBytesLeft < state->minLength) {
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[CxiLzHash3(state->buffer + state->pos)];
	if (firstMatch == UINT_MAX) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//search backwards
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance) {
		//check valid distance (a depth=0 indicates not in our tree)
		if (pDstDepths[distance - 1]) {
			//check only if distance is at least minDistance
			if (distance >= state->minDistance) {
				unsigned int matchLen = CxiCompareMemory(curp - distance, curp, nMaxCompare);

				if (matchLen > bestLength) {
					bestLength = matchLen;
					bestDistance = distance;
					if (bestLength == nMaxCompare) break;
				}
			}
		}

		if (distance == state->maxDistance) break;
		unsigned int next = CxiLzStateGetChain(state, distance);
		if (next == UINT_MAX) break;
		distance += next;
	}

	if (bestLength < state->minLength) {
		bestLength = 1;
		distance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}



static int CxiLzTokenBufferInit(CxiLzTokenBuffer *buf) {
	buf->nTokens = 0;
	buf->capacity = 16;
	buf->tokens = (CxiLzToken *) CALLOC(buf->capacity, sizeof(CxiLzToken));
	return buf->tokens != NULL;
}

static void CxiLzTokenBufferFree(CxiLzTokenBuffer *buf) {
	if (buf->tokens != NULL) FREE(buf->tokens);
	buf->tokens = NULL;
	buf->nTokens = 0;
	buf->capacity = 0;
}

static CxiLzToken *CxiLzTokenBufferAlloc(CxiLzTokenBuffer *buf) {
	buf->nTokens++;
	if (buf->nTokens > buf->capacity) {
		unsigned int cap1 = buf->capacity;
		unsigned int newCapacity = (buf->capacity + 1) * 3 / 2;
		buf->capacity = newCapacity;
		
		CxiLzToken *newbuf = (CxiLzToken *) REALLOC(buf->tokens, buf->capacity * sizeof(CxiLzToken));
		if (newbuf == NULL) {
			//alloc fail
			buf->capacity = cap1;
			return NULL;
		}
		buf->tokens = newbuf;
	}
	
	return &buf->tokens[buf->nTokens - 1];
}

static CxiLzToken *CxiLzTokenBufferFinalize(CxiLzTokenBuffer *buf, unsigned int *pnTokens) {
	CxiLzToken *toks = buf->tokens;
	*pnTokens = buf->nTokens;
	
	//un-own buffer
	buf->tokens = NULL;
	buf->nTokens = 0;
	buf->capacity = 0;
	return toks;
}



// ----- Huffman tree code

typedef struct CxiHuffNode_ {
	uint32_t sym;               // for a leaf node, this is the code value of this Huffman tree node
	uint32_t filter;            // a filter used to optimize tree lookups
	unsigned int freq;          // the total frequency of this Huffman node
	struct CxiHuffNode_ *left;  // this Huffman tree node's left child pointer
	struct CxiHuffNode_ *right; // this Huffman tree node's right child pointer
} CxiHuffNode;

#define ISLEAF(n) ((n)->left==NULL)

static int CxiHuffmanNodeComparator(const void *p1, const void *p2) {
	const CxiHuffNode *n1 = (const CxiHuffNode *) p1;
	const CxiHuffNode *n2 = (const CxiHuffNode *) p2;
	
	if (n2->freq > n1->freq) return +1;
	if (n2->freq < n1->freq) return -1;
	return 0;
}

//
// Initialize a huffman tree buffer. Pass a work buffer containing an array of Huffman nodes and a
// number of code values. The buffer should have a length of twice the number of code values. After
// this function returns, the work buffer is initialized such that it may be referenced as an array,
// where the node at index i corresponds to a code value of i.
//
static void CxiHuffmanInit(CxiHuffNode *nodes, unsigned int nNodes) {
	memset(nodes, 0, nNodes * 2 * sizeof(CxiHuffNode));
	for (unsigned int i = 0; i < nNodes; i++) {
		nodes[i].sym = i;
		nodes[i].filter = 1 << (i % 31);
	}
}

//
// Run this function after finalizing the frequency of each code value. This function takes in the
// Huffman node work buffer and constructs the final Huffman tree structure, using the first node
// in the buffer as the root.
//
static void CxiHuffmanConstructTree(CxiHuffNode *nodes, unsigned int nNodes) {
	//sort by frequency, then cut off the remainder (freq=0).
	qsort(nodes, nNodes, sizeof(CxiHuffNode), CxiHuffmanNodeComparator);
	for (unsigned int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			nNodes = i;
			break;
		}
	}

	//unflatten the histogram into a huffman tree. 
	unsigned int nRoots = nNodes;
	unsigned int nTotalNodes = nNodes;
	while (nRoots > 1) {
		//copy bottom two nodes to just outside the current range
		CxiHuffNode *srcA = nodes + nRoots - 2;
		CxiHuffNode *destA = nodes + nTotalNodes;
		memcpy(destA, srcA, sizeof(CxiHuffNode));

		CxiHuffNode *left = destA;
		CxiHuffNode *right = nodes + nRoots - 1;
		CxiHuffNode *branch = srcA;

		branch->freq = left->freq + right->freq;
		branch->sym = 0;
		branch->left = left;
		branch->right = right;
		branch->filter = left->filter | right->filter;

		nRoots--;
		nTotalNodes++;
		qsort(nodes, nRoots, sizeof(CxiHuffNode), CxiHuffmanNodeComparator);
	}
}

//
// Returns 1 if a Huffman tree contains a code value.
//
static int CxiHuffmanHasSymbol(const CxiHuffNode *node, uint16_t sym) {
	if (ISLEAF(node)) return node->sym == sym;
	
	//check filter
	if (!(node->filter & (1 << (sym % 31)))) return 0;
	
	CxiHuffNode *left = node->left;
	CxiHuffNode *right = node->right;
	return CxiHuffmanHasSymbol(left, sym) || CxiHuffmanHasSymbol(right, sym);
}

//
// Gets the node depth for a symbol in the Huffman tree. This should only be called for symbols
// within the Huffman tree.
//
static unsigned int CxiHuffmanGetNodeDepth(const CxiHuffNode *tree, unsigned int sym) {
	if (tree->left == NULL) {
		//this is the node
		CX_ASSERT(tree->sym == sym);
		return 0;
	}
	
	if (CxiHuffmanHasSymbol(tree->left, sym)) {
		return CxiHuffmanGetNodeDepth(tree->left, sym) + 1;
	} else {
		return CxiHuffmanGetNodeDepth(tree->right, sym) + 1;
	}
}

//
// Writes a symbol's Huffman code to a bit writer.
//
static void CxiHuffmanWriteSymbol(CxiBitWriter *bits, uint16_t sym, CxiHuffNode *tree) {
	if (ISLEAF(tree)) return;
	
	CxiHuffNode *left = tree->left;
	CxiHuffNode *right = tree->right;
	if (CxiHuffmanHasSymbol(left, sym)) {
		CxiBitWriterWrite(bits, 0);
		CxiHuffmanWriteSymbol(bits, sym, left);
	} else {
		CxiBitWriterWrite(bits, 1);
		CxiHuffmanWriteSymbol(bits, sym, right);
	}
}

static int CxiHuffmanCountSymbolsOver(CxiHuffNode *tree, unsigned int nMin) {
	if (tree->left == NULL) {
		return tree->sym >= nMin;
	}
	return CxiHuffmanCountSymbolsOver(tree->left, nMin) + CxiHuffmanCountSymbolsOver(tree->right, nMin);
}


typedef struct CxiHuffSymbolInfo_ {
	uint16_t sym;
	uint16_t depth;
} CxiHuffSymbolInfo;

static int CxiHuffSymbolInfoComparator(const void *e1, const void *e2) {
	const CxiHuffSymbolInfo *d1 = (const CxiHuffSymbolInfo *) e1;
	const CxiHuffSymbolInfo *d2 = (const CxiHuffSymbolInfo *) e2;
	
	if (d1->sym < d2->sym) return -1;
	if (d1->sym > d2->sym) return +1;
	return 0;
}

static CxiHuffSymbolInfo *CxiHuffmanEnumerateSymbolInfoInternal(CxiHuffNode *tree, CxiHuffSymbolInfo *buf, int depth, unsigned int nMin) {
	if (tree->left == NULL) {
		if (tree->sym >= nMin) {
			buf->sym = tree->sym;
			buf->depth = depth;
			buf++;
		}
	} else {
		//run recursion
		buf = CxiHuffmanEnumerateSymbolInfoInternal(tree->left, buf, depth + 1, nMin);
		buf = CxiHuffmanEnumerateSymbolInfoInternal(tree->right, buf, depth + 1, nMin);
	}
	return buf;
}

static CxiHuffSymbolInfo *CxiHuffmanEnumerateSymbolInfo(CxiHuffNode *tree, unsigned int *pCount, unsigned int nMin) {
	int nNode = CxiHuffmanCountSymbolsOver(tree, nMin);
	CxiHuffSymbolInfo *buf = (CxiHuffSymbolInfo *) CALLOC(nNode, sizeof(CxiHuffSymbolInfo));
	if (buf == NULL) return NULL;
	
	CxiHuffmanEnumerateSymbolInfoInternal(tree, buf, 0, nMin);
	
	//sort
	qsort(buf, nNode, sizeof(*buf), CxiHuffSymbolInfoComparator);
	
	*pCount = nNode;
	return buf;
}


// ----- ASH code

//
// Structure representing an ASH tokenization of a buffer.
//
typedef struct CxiAshTokenization_ {
	CxiLzToken *tokens;     // ASH tokenization
	unsigned int nTokens;   // number of tokens this tokenization
	unsigned int size;      // size of uncompressed data
	CxiHuffNode *symTree;   // ASH symbol code tree
	CxiHuffNode *dstTree;   // ASH distance code tree
} CxiAshTokenization;

//
// Ensures that we have a minimum number of code values in our frequency list by artifically setting
// some code frequencies to 1.
//
static void CxiAshEnsureTreeElements(
	CxiHuffNode *nodes,    // the Huffman code buffer
	unsigned int nNodes,   // the number of entries in the Huffman code buffer
	unsigned int nMinNodes // the desired minimum count of nonzero frequencies
) {
	CX_ASSERT(nMinNodes <= nNodes);
	
	//count nodes
	unsigned int nPresent = 0;
	for (unsigned int i = 0; i < nNodes; i++) {
		if (nodes[i].freq) nPresent++;
	}
	
	//have sufficient nodes?
	if (nPresent >= nMinNodes) return;
	
	//add dummy nodes
	for (unsigned int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			//dummy node: force frequency to 1, to ensure minimal representation
			nodes[i].freq = 1;
			if (++nPresent >= nMinNodes) return;
		}
	}
}

//
// Write an ASH code tree to a bit writer. It takes the following structure:
//   0: Leaf node value
//    n-bit leaf node value
//   1: Branch node
//    sub tree node left
//    sub tree node right
//
static void CxiAshWriteTree(
	CxiBitWriter      *stream,  // the output bit stream to write the tree to
	const CxiHuffNode *tree,    // the Huffman tree
	unsigned int       nBits    // the code width of the Huffman tree
) {
	if (!ISLEAF(tree)) {
		//branch node: write a 1 bit, followed by left and right subtrees
		CxiBitWriterWrite(stream, 1);
		CxiAshWriteTree(stream, tree->left, nBits);
		CxiAshWriteTree(stream, tree->right, nBits);
	} else {
		//leaf node: write a 0 bit, followed by the leaf node value
		CxiBitWriterWrite(stream, 0);
		CxiBitWriterWriteBitsBE(stream, tree->sym, nBits);
	}
}

//
// Greedily tokenize a byte array given the ASH encoding specifications. 
//
static CxiLzToken *CxiAshTokenizeGreedy(
	const unsigned char *buffer,    // the input buffer
	unsigned int         size,      // the input buffer size
	unsigned int         nSymBits,  // the symbol code width
	unsigned int         nDstBits,  // the distance code width
	unsigned int        *pnTokens   // the output number of tokens
) {
	CxiLzTokenBuffer tokenBuffer;
	if (!CxiLzTokenBufferInit(&tokenBuffer)) return NULL;
	
	//init LZ search state
	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, 3, (1 << nSymBits) - 1 - 0x100 + 3, 1, 1 << nDstBits);
	
	//iterate over the file and take the longest LZ match at each position.
	unsigned int curpos = 0;
	while (curpos < size) {
		//ensure buffer capacity
		CxiLzToken *token = CxiLzTokenBufferAlloc(&tokenBuffer);
		if (token == NULL) {
			CxiLzTokenBufferFree(&tokenBuffer);
			return NULL;
		}
		
		//search for string match
		unsigned int length, distance;
		length = CxiLzSearch(&state, &distance);
		
		if (length >= 3) {
			token->isReference = 1;
			token->length = length;
			token->distance = distance;
		} else  {
			token->isReference = 0;
			token->symbol = buffer[curpos];
		}
		
		CxiLzStateSlide(&state, length);
		curpos += length;
	}
	
	CxiLzStateFree(&state);
	return CxiLzTokenBufferFinalize(&tokenBuffer, pnTokens);
}

//
// Generate Huffman codes for symbol and distance values given a tokenization.
//
static void CxiAshGenHuffman(
	const CxiLzToken *tokens,    // the input LZ tokenization
	unsigned int      nTokens,   // the count of LZ tokens
	CxiHuffNode      *symNodes,  // the output symbol tree buffer
	unsigned int      nSymNodes, // the output symbol tree buffer size
	CxiHuffNode      *dstNodes,  // the output distance tree buffer
	unsigned int      nDstNodes  // the output distance tree buffer size
) {
	CX_ASSERT(nSymNodes >= 0x100);
	
	//initialize the huffman tree buffers
	CxiHuffmanInit(symNodes, nSymNodes);
	CxiHuffmanInit(dstNodes, nDstNodes);
	
	//construct frequency distribution
	for (unsigned int i = 0; i < nTokens; i++) {
		const CxiLzToken *token = &tokens[i];
		if (token->isReference) {
			//basic assumptions about token parameters
			CX_ASSERT(token->length >= 3);
			CX_ASSERT(token->distance > 0);
			CX_ASSERT((token->length - 3u + 0x100u) < nSymNodes);
			CX_ASSERT((token->distance - 1u) < nDstNodes);
			
			//increment length and distance code frequencies
			symNodes[token->length - 3 + 0x100].freq++;
			dstNodes[token->distance - 1].freq++;
		} else {
			symNodes[token->symbol].freq++;
		}
	}
	
	//pre-tree construction: ensure at least two nodes are used
	CxiAshEnsureTreeElements(symNodes, nSymNodes, 2);
	CxiAshEnsureTreeElements(dstNodes, nDstNodes, 2);
	
	//construct trees
	CxiHuffmanConstructTree(symNodes, nSymNodes);
	CxiHuffmanConstructTree(dstNodes, nDstNodes);
}

//
// Round down a code value to the next highest code value in a set of allowed values. This function
// expects a sorted array of allowed values. When the input value cannot be rounded down to any
// valid value, this function implicitly treats an index -1 as corresponding to a value of 1.
//
static unsigned int CxiAshRoundDown(
	unsigned int  sym,    // the input symbol value
	unsigned int *vals,   // the allowed symbol value array (sorted ascending)
	unsigned int  nVals,  // the length of the allowed symbol value array
	int          *pIndex  // the output index of the found array element
) {
	//sym=0 is an error. if sym = 0, no element can be equal or less
	CX_ASSERT(sym != 0);
	
	//sym=1: return the dummy value
	if (sym == 1) {
		*pIndex = -1;
		return 1;
	}
	
	//check lowest value <=
	unsigned int idxLo = 0, idxHi = nVals;
	unsigned int lo = 0;
	int loIndex = -1;
	while ((idxHi - idxLo) > 0) {
		unsigned int idxMed = (idxLo + idxHi) / 2;
		unsigned int valMed = vals[idxMed];
		
		//check low or high
		if (sym < valMed) {
			idxHi = idxMed;
		} else if (sym > valMed) {
			idxLo = idxMed + 1;
			loIndex = idxMed;
			lo = valMed;
		} else {
			//exact match
			*pIndex = idxMed;
			return sym;
		}
	}
	
	//if lo == 0, no match found, so return 1 with index of -1 (1 is implicitly in the list)
	if (lo == 0) lo = 1;
	*pIndex = loIndex;
	return lo;
}

//
// Create a new tokenization for the given byte array given existing Huffman codes.
//
static CxiLzToken *CxiAshRetokenize(
	const unsigned char *buffer,    // the input buffer
	unsigned int         size,      // the size of the input buffer
	unsigned int         nSymBits,  // the symbol code width
	unsigned int         nDstBits,  // the distance code width
	CxiHuffNode         *symNodes,  // the symbol code tree
	CxiHuffNode         *dstNodes,  // the distance code tree
	unsigned int        *pnTokens   // output number of tokens
) {
	//allocate graph
	CxiLzNode *nodes = (CxiLzNode *) CALLOC(size, sizeof(CxiLzNode));
	if (nodes == NULL) return NULL;
	
	//get a list of allowed distances
	unsigned int nLenNodesAvailable, nDstNodesAvailable;
	CxiHuffSymbolInfo *lenInfo = CxiHuffmanEnumerateSymbolInfo(symNodes, &nLenNodesAvailable, 0x100);
	CxiHuffSymbolInfo *dstInfo = CxiHuffmanEnumerateSymbolInfo(dstNodes, &nDstNodesAvailable,     0);

	//create array of symbol lengths
	unsigned int *symDepths = (unsigned int *) CALLOC(1 << nSymBits, sizeof(unsigned int));
	unsigned int *dstDepths = (unsigned int *) CALLOC(1 << nDstBits, sizeof(unsigned int));
	for (unsigned int i = 0; i < (1u << nSymBits); i++) {
		if (CxiHuffmanHasSymbol(symNodes, i)) symDepths[i] = CxiHuffmanGetNodeDepth(symNodes, i);
	}
	for (unsigned int i = 0; i < (1u << nDstBits); i++) {
		if (CxiHuffmanHasSymbol(dstNodes, i)) dstDepths[i] = CxiHuffmanGetNodeDepth(dstNodes, i);
	}
	
	//create array of allowed lengths
	unsigned int *lens = (unsigned int *) CALLOC(nLenNodesAvailable, sizeof(unsigned int));
	for (unsigned int i = 0; i < nLenNodesAvailable; i++) lens[i] = lenInfo[i].sym - 0x100 + 3;
	
	//create array of allowed distances
	unsigned int *dsts = (unsigned int *) CALLOC(nDstNodesAvailable, sizeof(unsigned int));
	for (unsigned int i = 0; i < nDstNodesAvailable; i++) dsts[i] = dstInfo[i].sym + 1;
	
	//get minimum distance node cost
	unsigned int minDstCost = UINT_MAX;
	for (unsigned int i = 0; i < (1u << nDstBits); i++) {
		if (dstDepths[i] && dstDepths[i] < minDstCost) minDstCost = dstDepths[i];
	}
	if (minDstCost == UINT_MAX) minDstCost = 0; // no distance nodes => min cost=0
	
	//create a buffer holding the hash of each byte triplet in the file. This hash will be a one to
	//one mapping of 3 bytes to a 24-bit hash, so comparing hashes is equivalent to comparing the
	//first 3 bytes.
	unsigned int *hashbuf = (unsigned int *) CALLOC(size, sizeof(unsigned int));
	if (size >= 3) {
		for (unsigned int i = 0; i < (size - 2); i++) {
			//hashbuf[i] = CxiLzHash3(buffer + i);
			hashbuf[i] = (buffer[i + 0] <<  0)
				| (buffer[i + 1] <<  8)
				| (buffer[i + 2] << 16);
		}
	}
	
	// ---------------------------------------------------------------------------------------------
	// PASS1: find longest string matches at each byte position.
	// ---------------------------------------------------------------------------------------------
	unsigned int pos = 0;
	unsigned int maxLen = (1 << nSymBits) - 1 - 0x100 + 3, maxDst = 1 << nDstBits;
	if (nLenNodesAvailable > 0) {
		//limit max length to what we have available
		maxLen = lens[nLenNodesAvailable - 1];
	}
	if (nDstNodesAvailable > 0) {
		//limit max distance to what we have available
		maxDst = dsts[nDstNodesAvailable - 1];
	}
	
	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, 3, maxLen, 1, maxDst);
	
	while (pos < size) {
		unsigned int length, distance;
		length = CxiLzSearchRestricted(&state, dstDepths, &distance);
		
		//put longest match
		nodes[pos].token.isReference = 1;
		nodes[pos].token.length = length;
		nodes[pos].token.distance = distance;
		
		//next byte
		pos++;
		CxiLzStateSlide(&state, 1);
	}
	CxiLzStateFree(&state);
	// ---------------------------------------------------------------------------------------------
	// END PASS1
	// ---------------------------------------------------------------------------------------------
	
	// ---------------------------------------------------------------------------------------------
	// PASS2: Search lengths down
	//
	// In the previous pass, we run over the file from beginning to end using our sliding window and
	// hash chaining to find long matches at each byte position quickly. Now we'll use those results
	// to find optimized length and distance pairs.
	//
	// The matches we've found before are guaranteed to be the longest matches we can make, so we
	// only need to check for length:distance pairs of equal or lesser length value.
	//
	// An exhaustive search of all possible length:distance pairs takes a long time and requires
	// more complex code. We use an approximation here that searches for lengths and distances
	// independently of each other. Because even the optimal length:distance pairs we would have
	// found are only a local heuristic used to approximate an optimal full encoding, it is also
	// not guaranteed to be better than simpler approximations in all cases.
	// ---------------------------------------------------------------------------------------------
	pos = size;
	while (pos-- > 0) {
		//retrieve longest match from buffer
		unsigned int length = nodes[pos].token.length;
		unsigned int distance = nodes[pos].token.distance;
		
		//round down length to next representable length given our encoding space and get the index
		//into our table
		int lengthIndex;
		length = CxiAshRoundDown(length, lens, nLenNodesAvailable, &lengthIndex);
		
		//sanity checks on search results
		CX_ASSERT(length != 2);             // length cannot be 2 (non-representable length)
		CX_ASSERT((pos + length) <= size);  // length must not run out of bounds
		CX_ASSERT(distance <= pos);         // distance must not run out of bounds
		
		//NOTE: all byte values that appear in the file will have a symbol associated since they must appear at least once.
		//thus we do not need to check that any byte value exists.
		CX_ASSERT(symDepths[buffer[pos]] > 0);
		
		//check length (should store reference?)
		unsigned int weight = 0;
		if (length < 3) {
			//rounded down to a sub-3 length copy, length should be 1.
			CX_ASSERT(length == 1);       // length must be 1
			CX_ASSERT(lengthIndex == -1); // pseudo-index for direct byte store
			CX_ASSERT(distance == 0);     // distance must be zero from search
			
			//compute cost of byte literal
			weight = symDepths[buffer[pos]];
			if ((pos + 1) < size) {
				//add next weight
				weight += nodes[pos + 1].weight;
			}
		} else {
			//sanity check the length index
			CX_ASSERT(lengthIndex >= 0 && ((unsigned int) lengthIndex) < nLenNodesAvailable); // legnth index must be valid
			CX_ASSERT(length == lens[lengthIndex]); // length must match at our found length index
			CX_ASSERT(distance > 0);                // found distance must be nonzero
			
			//get cost of selected distance
			unsigned int dstCost = dstDepths[distance - 1];
			CX_ASSERT(dstCost > 0);
			
			//scan size down
			unsigned int weightBest = UINT_MAX, lengthBest = length; 
			while (length) {
				//compute weight of this length value
				unsigned int thisWeight;
				if (length >= 3) {
					//length > 1: symbol (use cost of length symbol)
					thisWeight = lenInfo[lengthIndex].depth;
				} else {
					//length == 1: byte literal (use cost of byte literal)
					thisWeight = symDepths[buffer[pos]];
				}
				
				//accumulate cost of next node
				if ((pos + length) < size) {
					//cost is this node's weight plus the weight of the next node
					thisWeight += nodes[pos + length].weight;
				}
				
				//check if this node has better total weight. We allow the match to be equal since we're scanning
				//sizes down, we'll prefer shorter lengths. This pushes the distribution of lengths to the lower
				//end allowing for better Huffman coding. This also emphasizes byte literals, which have no distance
				//cost. We add the minimum distance cost here to offset this (lack of) cost.
				if (thisWeight <= weightBest || (length == 1 && thisWeight <= (weightBest + minDstCost))) {
					weightBest = thisWeight;
					lengthBest = length;
				}
				
				//decrement length (with respect to allowed length encodings)
				lengthIndex--;
				if      (lengthIndex >=  0) length = lens[lengthIndex]; // length in list
				else if (lengthIndex == -1) length = 1;                 // decrement to 1
				else                        break;                      // end
			}
			
			length = lengthBest;
			if (length < 3) {
				//byte literal (distance cost is thus now zero since we have no distance component)
				CX_ASSERT(length == 1);
				dstCost = 0;
			} else {
				//we ended up selecting an LZ copy-able length. but did we select the most optimal distance
				//encoding?
				//search possible distances where we can match the string at. We'll take the lowest-cost one.
				for (unsigned int i = 0; i < nDstNodesAvailable; i++) {
					unsigned int dst = dsts[i];
					if (dst > pos) break;
					
					//check distance cost
					if (dstInfo[i].depth >= dstCost) continue;
					
					//check the hash at this position as a preliminary check
					if (hashbuf[pos] != hashbuf[pos - dst]) continue;
					
					//check matching LZ string...
					if (CxiLzConfirmMatch(buffer, size, pos + 3, dst, length - 3)) {
						dstCost = dstInfo[i].depth;
						distance = dst;
					}
				}
			}
			weight = weightBest + dstCost;
		}
		
		//write node
		if (length >= 3) {
			nodes[pos].token.isReference = 1;
			nodes[pos].token.distance = distance;
			nodes[pos].token.length = length;
		} else {
			nodes[pos].token.isReference = 0;
			nodes[pos].token.symbol = buffer[pos];
		}
		nodes[pos].weight = weight;
	}
	// -----
	// END PASS2
	// -----
	
	FREE(hashbuf);
	FREE(lens);
	FREE(dsts);
	FREE(lenInfo);
	FREE(dstInfo);
	FREE(symDepths);
	FREE(dstDepths);
	
	//convert graph into node array
	unsigned int nTokens = 0;
	pos = 0;
	while (pos < size) {
		CxiLzNode *node = &nodes[pos];
		nTokens++;
		
		if (node->token.isReference) pos += node->token.length;
		else pos++;
	}
	
	CxiLzToken *tokens = (CxiLzToken *) CALLOC(nTokens, sizeof(CxiLzToken));
	if (tokens == NULL) {
		FREE(nodes);
		return NULL;
	}
	
	pos = 0;
	unsigned int i = 0;
	while (pos < size) {
		CxiLzNode *node = &nodes[pos];
		
		memcpy(&tokens[i++], &node->token, sizeof(CxiLzToken));
		
		if (node->token.isReference) pos += node->token.length;
		else pos++;
	}
	FREE(nodes);
	
	*pnTokens = nTokens;
	return tokens;
}

//
// Take a byte array and tokenize it.
//
static void CxiAshCreateTokens(
	CxiAshTokenization  *pTok,      // structure receiving the tokenization
	const unsigned char *buffer,    // input buffer
	unsigned int         size,      // input buffer size
	unsigned int         nSymBits,  // symbol code width
	unsigned int         nDstBits,  // distance code width
	unsigned int         nPasses    // number of compression passes to use
) {
	CxiHuffNode *symNodes = NULL, *dstNodes = NULL;
	memset(pTok, 0, sizeof(CxiAshTokenization));
	
	//allocate tree structures
	unsigned int nSymNodes = (1 << nSymBits);
	unsigned int nDstNodes = (1 << nDstBits);
	symNodes = (CxiHuffNode *) CALLOC(nSymNodes * 2, sizeof(CxiHuffNode));
	dstNodes = (CxiHuffNode *) CALLOC(nDstNodes * 2, sizeof(CxiHuffNode));
	if (symNodes == NULL || dstNodes == NULL) goto Error;
	
	//tokenize
	unsigned int nTokens = 0;
	CxiLzToken *tokens = CxiAshTokenizeGreedy(buffer, size, nSymBits, nDstBits, &nTokens);
	if (tokens == NULL) goto Error;
	
	CxiAshGenHuffman(tokens, nTokens, symNodes, nSymNodes, dstNodes, nDstNodes);
	
	// ----------------------------------------------------------------------------------------------
	//    Herein lies the really expensive operations (both memory and time).
	// ----------------------------------------------------------------------------------------------
	
	//iterate on adjusting the frequency distribution and traversing the encoding space
	for (unsigned int i = 0; i < nPasses; i++) {
		//discard tokenized sequence. 
		FREE(tokens);

		//re-tokenize
		tokens = CxiAshRetokenize(buffer, size, nSymBits, nDstBits, symNodes, dstNodes, &nTokens);
		if (tokens == NULL) goto Error;
		
		//regenerate huffman tree due to changes in frequency distribution
		CxiAshGenHuffman(tokens, nTokens, symNodes, nSymNodes, dstNodes, nDstNodes);
	}
	
	// ----------------------------------------------------------------------------------------------
	//    End of super intense operations
	// ----------------------------------------------------------------------------------------------
	
	//return ASH tokenization
	pTok->tokens = tokens;
	pTok->nTokens = nTokens;
	pTok->symTree = symNodes;
	pTok->dstTree = dstNodes;
	pTok->size = size;
	return;
	
Error:
	if (symNodes != NULL) FREE(symNodes);
	if (dstNodes != NULL) FREE(dstNodes);
}

static void CxiAshTokFree(CxiAshTokenization *pTok) {
	//FREE memories held by the tokenization structure
	if (pTok->tokens != NULL) FREE(pTok->tokens);
	if (pTok->symTree != NULL) FREE(pTok->symTree);
	if (pTok->dstTree != NULL) FREE(pTok->dstTree);
	
	memset(pTok, 0, sizeof(CxiAshTokenization));
}

//
// Take an ASH tokenization and convert it to a byte array.
//
static unsigned char *CxiAshTokenizationToBytes(
	const CxiAshTokenization *pTok,      // the input tokenization.
	unsigned int              nSymBits,  // the symbol code value width.
	unsigned int              nDstBits,  // the distance code value width.
	unsigned int             *pSize      // the output byte array size
) {
	//init streams
	CxiBitWriter symStream, dstStream;
	CxiBitWriterCreate(&symStream);
	CxiBitWriterCreate(&dstStream);
	
	//first, write huffman trees.
	CxiAshWriteTree(&symStream, pTok->symTree, nSymBits);
	CxiAshWriteTree(&dstStream, pTok->dstTree, nDstBits);
	
	//write data stream
	for (unsigned int i = 0; i < pTok->nTokens; i++) {
		CxiLzToken *token = &pTok->tokens[i];
		
		if (token->isReference) {
			CxiHuffmanWriteSymbol(&symStream, token->length - 3 + 0x100, pTok->symTree);
			CxiHuffmanWriteSymbol(&dstStream, token->distance - 1, pTok->dstTree);
		} else {
			CxiHuffmanWriteSymbol(&symStream, token->symbol, pTok->symTree);
		}
	}
	
	//encode data output
	unsigned int symStreamSize = 0;
	unsigned int dstStreamSize = 0;
	void *symBytes = CxiBitWriterGetBytes(&symStream, 1, 1, 1, &symStreamSize);
	void *dstBytes = CxiBitWriterGetBytes(&dstStream, 1, 1, 1, &dstStreamSize);
	
	//write data out
	unsigned char *out = (unsigned char *) CALLOC(0xC + symStreamSize + dstStreamSize, 1);
	{
		//write header
		unsigned int ofsSym = 0xC;
		unsigned int ofsDst = ofsSym + symStreamSize;
		unsigned int size = pTok->size;
		
		memcpy(out, "ASH0", 4);             // +0x00: Signature 'ASH0'
		out[4 + 0] = (size   >> 24) & 0xFF; // +0x04: File size
		out[4 + 1] = (size   >> 16) & 0xFF;
		out[4 + 2] = (size   >>  8) & 0xFF;
		out[4 + 3] = (size   >>  0) & 0xFF;
		out[8 + 0] = (ofsDst >> 24) & 0xFF; // +0x08: Offset to distance stream
		out[8 + 1] = (ofsDst >> 16) & 0xFF;
		out[8 + 2] = (ofsDst >>  8) & 0xFF;
		out[8 + 3] = (ofsDst >>  0) & 0xFF;
		
		//write streams
		memcpy(out + ofsSym, symBytes, symStreamSize);
		memcpy(out + ofsDst, dstBytes, dstStreamSize);
	}
	FREE(symBytes);
	FREE(dstBytes);
	
	//FREE stuff
	CxiBitWriterFree(&symStream);
	CxiBitWriterFree(&dstStream);
	
	*pSize = 0xC + symStreamSize + dstStreamSize;
	return out;
}

//
// Compress a byte array using the ASH compression format, with user-specified symbol and distance
// code sizes.
//
unsigned char *CxCompressAsh(
	const unsigned char *buffer,        // the input data
	unsigned int         size,          // size of the input data
	unsigned int         nSymBits,      // symbol code width
	unsigned int         nDstBits,      // distance code width
	unsigned int         nPasses,       // number of passes to use when compressing.
	unsigned int        *compressedSize // output size of compressed data.
) {
	CxiAshTokenization tok;
	CxiAshCreateTokens(&tok, buffer, size, nSymBits, nDstBits, nPasses);
	
	unsigned int outSize;
	unsigned char *out = CxiAshTokenizationToBytes(&tok, nSymBits, nDstBits, &outSize);
	
	//FREE node and tree structs
	CxiAshTokFree(&tok);
	*compressedSize = outSize;
	return out;
}


enumError EncodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	unsigned int out_size = 0;
	// nSymBits=9, nDstBits=11, nPasses=0 (fast single-pass optimal LZ+Huffman)
	unsigned char *out = CxCompressAsh(src, src_size, 9, 11, 0, &out_size);
	if (!out)
		return ERR_CANT_CREATE;

	*dest = out;
	*dest_size = out_size;
	return ERR_OK;
}
