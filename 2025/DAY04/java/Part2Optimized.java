/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// We need 3 bits to store a max neighbor count of 8.
// bit0 is the 1s place, bit1 is the 2s place, bit2 is the 4s place.
private static final byte LF = 10; // Line feed

// Little-endian 64-bit view over the raw input for SWAR bit-packing.
private static final java.lang.invoke.VarHandle LONGS =
	java.lang.invoke.MethodHandles.byteArrayViewVarHandle(long[].class, java.nio.ByteOrder.LITTLE_ENDIAN);

// Branchless SWAR optimization, generation 2:
// - the bit-sliced adder runs on locals (registers) instead of static fields,
// - the grid is updated in place through two rolling row snapshots, killing the
//   full-size double buffer, the O(grid) Arrays.fill and the buffer swap per round,
// - settled rows are skipped: a row is re-examined only if it or a vertical
//   neighbor changed in the previous round, so late rounds touch a handful of rows,
// - input is bit-packed 8 cells per load via a magic-multiply movemask.
void main(String[] args)
{
	if (args.length == 0)
	{
		IO.println("\033[1;91mNo input data provided - aborting\u001B[0m");
		return;
	}

	final byte[] data = args[0].getBytes();
	final int dataSize = data.length;
	int X = 0;
	while (X < dataSize && data[X] != LF)
	{
		X++;
	}
	final int Y = (dataSize + 1) / (X + 1);

	// Kernel
	final int words = (X + 63) >>> 6;
	// Sentinel
	final int stride = words + 2;
	final long[] g = new long[(Y + 2) * stride];

	// Bitpacking: '@' = 0x40 (bit 6 set), '.' = 0x2E (bit 6 clear).
	for (int y = 0; y < Y; y++)
	{
		final int in = y * (X + 1);
		final int out = (y + 1) * stride + 1;
		int x = 0;
		for (; x + 8 <= X; x += 8)
		{
			final long v = (((long) LONGS.get(data, in + x)) >>> 6) & 0x0101010101010101L;
			g[out + (x >>> 6)] |= ((v * 0x0102040810204080L) >>> 56) << (x & 63);
		}
		for (; x < X; x++)
		{
			g[out + (x >>> 6)] |= ((long) ((data[in + x] >>> 6) & 1)) << (x & 63);
		}
	}

	// Rolling start-of-round snapshots of the previous and current row.
	long[] prevOrig = new long[stride];
	long[] curOrig = new long[stride];

	boolean[] dirty = new boolean[Y + 2];
	boolean[] nextDirty = new boolean[Y + 2];
	java.util.Arrays.fill(dirty, true);

	long totalDeletions = 0L;
	boolean running = true;

	while (running)
	{
		running = false;
		boolean prevProcessed = false;

		for (int y = 1; y <= Y; y++)
		{
			if (!(dirty[y - 1] | dirty[y] | dirty[y + 1]))
			{
				prevProcessed = false;
				continue;
			}

			final int base = y * stride;
			System.arraycopy(g, base, curOrig, 0, stride);

			// Row y-1 originals: its snapshot if it was rewritten this round,
			// otherwise it is untouched and can be read straight from the grid.
			final long[] north = prevProcessed ? prevOrig : g;
			final int nOff = prevProcessed ? 0 : base - stride;
			final int sOff = base + stride; // row y+1 is untouched this round
			boolean rowChanged = false;

			for (int x = 1; x <= words; x++)
			{
				final long self = curOrig[x];
				final long nRow = north[nOff + x];
				final long sRow = g[sOff + x];

				// Horizontal spill handling
				// Unrolled neighbor alignment ; would be dxdy loop
				final long w = (self << 1) | (curOrig[x - 1] >>> 63);
				final long e = (self >>> 1) | (curOrig[x + 1] << 63);
				final long nw = (nRow << 1) | (north[nOff + x - 1] >>> 63);
				final long ne = (nRow >>> 1) | (north[nOff + x + 1] << 63);
				final long sw = (sRow << 1) | (g[sOff + x - 1] >>> 63);
				final long se = (sRow >>> 1) | (g[sOff + x + 1] << 63);

				// Compute kernel: bit-sliced saturating 3-bit adder over 64 lanes;
				// once bit2 is set, the count is >= MAX_ADJ.
				long b0 = nRow ^ sRow;
				long b1 = nRow & sRow;
				long b2 = 0L;
				long c0, c1;
				c0 = b0 & e; b0 ^= e; c1 = b1 & c0; b1 ^= c0; b2 |= c1;
				c0 = b0 & w; b0 ^= w; c1 = b1 & c0; b1 ^= c0; b2 |= c1;
				c0 = b0 & ne; b0 ^= ne; c1 = b1 & c0; b1 ^= c0; b2 |= c1;
				c0 = b0 & nw; b0 ^= nw; c1 = b1 & c0; b1 ^= c0; b2 |= c1;
				c0 = b0 & se; b0 ^= se; c1 = b1 & c0; b1 ^= c0; b2 |= c1;
				c0 = b0 & sw; b0 ^= sw; c1 = b1 & c0; b1 ^= c0; b2 |= c1;

				final long deleted = self & ~b2;
				if (deleted != 0L)
				{
					g[base + x] = self & b2;
					totalDeletions += Long.bitCount(deleted);
					rowChanged = true;
				}
			}

			nextDirty[y] = rowChanged;
			running |= rowChanged;

			// Swap the row snapshots
			final long[] temp = prevOrig;
			prevOrig = curOrig;
			curOrig = temp;
			prevProcessed = true;
		}

		final boolean[] temp = dirty;
		dirty = nextDirty;
		nextDirty = temp;
		java.util.Arrays.fill(nextDirty, false);
	}

	IO.println("Result: " + totalDeletions);
}
