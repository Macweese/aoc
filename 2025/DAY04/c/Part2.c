/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* TODO:                                                                                         */
/* 1. Store the grid as columns of bits in uint64_t arrays                                       */
/* (one uint64_t holds 64 rows of one x-position).                                               */
/*    Use three uint64_t arrays per "lane" to hold the 3-bit neighbor count vertically:          */
/*    c0: bit 0 (1s)                                                                             */
/*    c1: bit 1 (2s)                                                                             */
/*    c2: bit 2 (4s)                                                                             */
/*                                                                                               */
/*    void add_vertical(uint64_t neigh, uint64_t *c0, uint64_t *c1, uint64_t *c2) {              */
/*        uint64_t t0 = *c0 ^ neigh;                                                             */
/*        uint64_t carry = *c0 & neigh;                                                          */
/*        *c0 = t0;                                                                              */
/*                                                                                               */
/*        t0 = *c1 ^ carry;                                                                      */
/*        carry = *c1 & carry;                                                                   */
/*        *c1 = t0;                                                                              */
/*                                                                                               */
/*        *c2 ^= carry;  // Overflow into bit2 (we only need >=4)                                */
/*    }                                                                                          */
/*                                                                                               */
/*    self & (c2 | other conditions if needed)                                                   */
/*    64 rows at once per column                                                                 */
/*                                                                                               */
/* 2. HC SIMD + Multi-Threading                                                                  */
/*    Use AVX-512 (512-bit vectors = 8× uint64_t lanes)                                          */
/*        Process 512 rows × width columns per instruction                                       */
/*        Use _mm512_loadu_epi64, shifts, AND/OR/XOR for neighbor adds                           */
/*    Transpose storage: bit-packed columns → easy vertical ops                                  */
/*    Multi-threading: OpenMP or std::thread across horizontal chunks                            */
/*    Double-buffer with aligned memory (_mm_malloc)                                             */
/*                                                                                               */
/* 3. Cache                                                                                      */
/*    Custom memory pools                                                                        */
/*    Lock-free queues for active cells only (if sparsity helps)                                 */
/*    Precomputed shift tables                                                                   */
/*    Assembly intrinsics mixed with C++                                                         */
/*    Cache-oblivious tiling                                                                     */
/*                                                                                               */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

/* We need 3 bits to store a max neighbor count of 8. */
/* bit0 is the 1s place, bit1 is the 2s place, bit2 is the 4s place. */
static uint64_t bit0;
static uint64_t bit1;
static uint64_t bit2;
#define LF  10  /* Line feed */
#define OBJ 64  /* "@" */
#define UINT64_BITS (CHAR_BIT * sizeof(uint64_t))

/* Compute kernel: */
/* 64-set neighbor SIMD using POPCNT */
static void
add64(uint64_t neighbors)
{
	uint64_t bi0 = bit0;
	uint64_t bi1 = bit1;
	bit0 = bi0 ^ neighbors;
	uint64_t carry0 = bi0 & neighbors;
	bit1 = bi1 ^ carry0;
	/* Once bit2 is set, count is >= MAX_ADJ */
	uint64_t carry1 = bi1 & carry0;
	bit2 |= carry1;
}

/* Branchless SWAR optimization */
int
main(int argc, char *argv[])
{
	uint64_t *src;
	uint64_t *dst;
	uint64_t *temp;

	const unsigned char *data;
	size_t dataSize;

	int X;
	int Y;
	int size;
	int segmentSize;
	int y;
	int x;

	long totalDeletions;
	int running;
	long cycleBits;

	int base;
	int idx;
	uint64_t self;

	uint64_t w;
	uint64_t e;
	uint64_t n;
	uint64_t s;
	uint64_t nw;
	uint64_t ne;
	uint64_t sw;
	uint64_t se;

	uint64_t deleted;

 	if (argc < 2)
 	{
 		printf("No input data provided - aborting\n");
 		return 0;
 	}

	data = (const unsigned char *) argv[0];
	dataSize = strlen(argv[0]);

	X = 0;
	while (X < (int)dataSize && data[X] != LF)
	{
		X++;
	}
	Y = (int)((dataSize + 1) / (X + 1));

	/* Kernel */
	size = (X + (UINT64_BITS - 1)) / UINT64_BITS;
	/* Sentinel */
	segmentSize = size + 2;
	/* Double-buffer */
	src = calloc((Y + 2) * segmentSize, sizeof(uint64_t));
	dst = calloc((Y + 2) * segmentSize, sizeof(uint64_t));

	if (src == NULL || dst == NULL)
	{
		free(src);
		free(dst);
		return 1;
	}

	/* Bitpacking */
	for (y = 0; y < Y; y++)
	{
		int yOffset = (y + 1) * segmentSize + 1;
		for (x = 0; x < X; x++)
		{
			int longIdx = x / UINT64_BITS;
			int bitIdx = x % UINT64_BITS;
			if (data[y * (X + 1) + x] == OBJ)
			{
				src[yOffset + longIdx] |= ((uint64_t)1 << bitIdx);
			}
		}
	}

	totalDeletions = 0;
	do
	{
		cycleBits = 0;

		for (y = 1; y <= Y; y++)
		{
			base = y * segmentSize + 1;

			for (x = 0; x < size; x++)
			{
				idx = base + x;
				self = src[idx];
				/* Reset */
				bit0 = 0;
				bit1 = 0;
				bit2 = 0;

				/* Horizontal spill handling */
				/* Unrolled dxdy flagging */
				w = (self >> 1) | (src[idx + 1] << (UINT64_BITS - 1));
				e = (self << 1) | (src[idx - 1] >> (UINT64_BITS - 1));

				n = src[idx - segmentSize];
				s = src[idx + segmentSize];

				nw = (n >> 1) | (src[idx - segmentSize + 1] << (UINT64_BITS - 1));
				ne = (n << 1) | (src[idx - segmentSize - 1] >> (UINT64_BITS - 1));

				sw = (s >> 1) | (src[idx + segmentSize + 1] << (UINT64_BITS - 1));
				se = (s << 1) | (src[idx + segmentSize - 1] >> (UINT64_BITS - 1));

				add64(n);
				add64(s);
				add64(e);
				add64(w);
				add64(nw);
				add64(ne);
				add64(sw);
				add64(se);

				deleted = self & ~bit2;
				dst[base + x] = self & bit2;

				totalDeletions += __builtin_popcountll(deleted);
				cycleBits |= deleted;
			}
		}

		running = (cycleBits != 0);

		/* Swap & clear buffers */
		temp = src;
		src = dst;
		dst = temp;
		memset(dst, 0, (Y + 2) * segmentSize * sizeof(uint64_t));
	} while (running);

	printf("Result: %ld\n", totalDeletions);

	free(src);
	free(dst);
	return 0;
}