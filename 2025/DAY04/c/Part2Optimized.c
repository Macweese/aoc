/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Branchless SWAR optimization, generation 2.                                                   */
/*                                                                                               */
/* - The grid is bit-packed 64 cells per uint64_t; the 8 neighbor masks are folded               */
/*   through a bit-sliced carry-save adder held entirely in registers (no globals),             */
/*   so all 64 cells of a word get their ">= 4 neighbors" verdict simultaneously.               */
/* - The grid is updated in place through two rolling row snapshots, which kills                 */
/*   the full-size double buffer, the O(grid) memset and the buffer swap per round.             */
/* - Settled rows are skipped: a row is re-examined only if it or a vertical                     */
/*   neighbor changed in the previous round, so late rounds touch a handful of rows.            */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define LF  10  /* Line feed */
#define OBJ 64  /* "@" */

/* Compute kernel: one carry-save insertion into the bit-sliced 3-bit counter. */
#define CSA(x)                 \
	do                         \
	{                          \
		carry0 = b0 & (x);     \
		b0 ^= (x);             \
		carry1 = b1 & carry0;  \
		b1 ^= carry0;          \
		b2 |= carry1;          \
	} while (0)

int
main(int argc, char *argv[])
{
	const unsigned char *data;
	size_t dataSize;
	int X, Y, words, stride, x, y;
	long totalDeletions;
	int running;

	if (argc < 2)
	{
		printf("No input data provided - aborting\n");
		return 0;
	}

	data = (const unsigned char *) argv[1];
	dataSize = strlen(argv[1]);

	X = 0;
	while (X < (int) dataSize && data[X] != LF)
	{
		X++;
	}
	Y = (int) ((dataSize + 1) / (X + 1));

	/* Kernel */
	words = (X + 63) >> 6;
	/* Sentinel */
	stride = words + 2;

	{
		uint64_t *g = calloc((size_t) (Y + 2) * stride, sizeof(uint64_t));
		/* Rolling start-of-round snapshots of the previous and current row. */
		uint64_t *prevOrig = calloc(stride, sizeof(uint64_t));
		uint64_t *curOrig = calloc(stride, sizeof(uint64_t));
		unsigned char *dirty = malloc(Y + 2);
		unsigned char *nextDirty = calloc(Y + 2, 1);

		if (g == NULL || prevOrig == NULL || curOrig == NULL || dirty == NULL || nextDirty == NULL)
		{
			free(g);
			free(prevOrig);
			free(curOrig);
			free(dirty);
			free(nextDirty);
			return 1;
		}

		/* Bitpacking */
		for (y = 0; y < Y; y++)
		{
			const unsigned char *row = data + (size_t) y * (X + 1);
			uint64_t *out = g + (size_t) (y + 1) * stride + 1;
			for (x = 0; x < X; x++)
			{
				out[x >> 6] |= (uint64_t) ((row[x] >> 6) & 1) << (x & 63);
			}
		}

		memset(dirty, 1, Y + 2);
		totalDeletions = 0;
		running = 1;

		while (running)
		{
			int prevProcessed = 0;
			running = 0;

			for (y = 1; y <= Y; y++)
			{
				uint64_t *base;
				const uint64_t *north;
				const uint64_t *south;
				int rowChanged;
				uint64_t *swp;

				if (!(dirty[y - 1] | dirty[y] | dirty[y + 1]))
				{
					prevProcessed = 0;
					continue;
				}

				base = g + (size_t) y * stride;
				memcpy(curOrig, base, stride * sizeof(uint64_t));

				/* Row y-1 originals: its snapshot if it was rewritten this round,   */
				/* otherwise it is untouched and can be read straight from the grid. */
				north = prevProcessed ? prevOrig : base - stride;
				south = base + stride; /* row y+1 is untouched this round */
				rowChanged = 0;

				for (x = 1; x <= words; x++)
				{
					uint64_t self = curOrig[x];
					uint64_t nRow = north[x];
					uint64_t sRow = south[x];

					/* Horizontal spill handling */
					/* Unrolled neighbor alignment ; would be dxdy loop */
					uint64_t w = (self << 1) | (curOrig[x - 1] >> 63);
					uint64_t e = (self >> 1) | (curOrig[x + 1] << 63);
					uint64_t nw = (nRow << 1) | (north[x - 1] >> 63);
					uint64_t ne = (nRow >> 1) | (north[x + 1] << 63);
					uint64_t sw = (sRow << 1) | (south[x - 1] >> 63);
					uint64_t se = (sRow >> 1) | (south[x + 1] << 63);

					/* Bit-sliced saturating 3-bit adder over 64 lanes;    */
					/* once bit2 is set, the count is >= MAX_ADJ.          */
					uint64_t b0 = nRow ^ sRow;
					uint64_t b1 = nRow & sRow;
					uint64_t b2 = 0;
					uint64_t carry0, carry1;
					uint64_t deleted;

					CSA(e);
					CSA(w);
					CSA(ne);
					CSA(nw);
					CSA(se);
					CSA(sw);

					deleted = self & ~b2;
					if (deleted)
					{
						base[x] = self & b2;
						totalDeletions += __builtin_popcountll(deleted);
						rowChanged = 1;
					}
				}

				nextDirty[y] = (unsigned char) rowChanged;
				running |= rowChanged;

				/* Swap the row snapshots */
				swp = prevOrig;
				prevOrig = curOrig;
				curOrig = swp;
				prevProcessed = 1;
			}

			{
				unsigned char *tmp = dirty;
				dirty = nextDirty;
				nextDirty = tmp;
				memset(nextDirty, 0, Y + 2);
			}
		}

		printf("Result: %ld\n", totalDeletions);

		free(g);
		free(prevOrig);
		free(curOrig);
		free(dirty);
		free(nextDirty);
	}
	return 0;
}
