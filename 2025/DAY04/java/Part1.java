/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final byte LF = 10;

private static final String COLOR_RED = "\033[1;91m";
private static final String COLOR_RESET = "\u001B[0m";

// Little-endian 64-bit view over the raw input for SWAR bit-packing.
private static final java.lang.invoke.VarHandle LONGS =
	java.lang.invoke.MethodHandles.byteArrayViewVarHandle(long[].class, java.nio.ByteOrder.LITTLE_ENDIAN);

// Bit-packed grid, 64 cells per word: the 8 neighbor masks are folded through a
// bit-sliced carry-save adder (3-bit vertical counter), so all 64 cells of a word
// get their ">= 4 neighbors" verdict simultaneously - one pass, no inner branches.
void main(String[] args)
{
	if (args.length == 0)
	{
		return;
	}

	final byte[] raw = args[0].getBytes();
	final int size = raw.length;

	int cols = 0;
	while (cols < size && raw[cols] != LF)
	{
		cols++;
	}
	final int rows = (size + 1) / (cols + 1);

	final int words = (cols + 63) >>> 6;
	final int stride = words + 2;
	final long[] g = new long[(rows + 2) * stride];

	// Pack 8 cells per load: '@' = 0x40 (bit 6 set), '.' = 0x2E (bit 6 clear);
	// gather the eight bit-6 flags into one byte with a magic-multiply movemask.
	for (int y = 0; y < rows; y++)
	{
		final int in = y * (cols + 1);
		final int out = (y + 1) * stride + 1;
		int x = 0;
		for (; x + 8 <= cols; x += 8)
		{
			final long v = (((long) LONGS.get(raw, in + x)) >>> 6) & 0x0101010101010101L;
			g[out + (x >>> 6)] |= ((v * 0x0102040810204080L) >>> 56) << (x & 63);
		}
		for (; x < cols; x++)
		{
			g[out + (x >>> 6)] |= ((long) ((raw[in + x] >>> 6) & 1)) << (x & 63);
		}
	}

	long deletions = 0L;

	for (int y = 1; y <= rows; y++)
	{
		final int base = y * stride;
		for (int x = 1; x <= words; x++)
		{
			final int idx = base + x;
			final long self = g[idx];
			final long nRow = g[idx - stride];
			final long sRow = g[idx + stride];

			// Neighbor occupancy aligned onto each cell (with cross-word spill).
			final long w = (self << 1) | (g[idx - 1] >>> 63);
			final long e = (self >>> 1) | (g[idx + 1] << 63);
			final long nw = (nRow << 1) | (g[idx - stride - 1] >>> 63);
			final long ne = (nRow >>> 1) | (g[idx - stride + 1] << 63);
			final long sw = (sRow << 1) | (g[idx + stride - 1] >>> 63);
			final long se = (sRow >>> 1) | (g[idx + stride + 1] << 63);

			// Bit-sliced saturating 3-bit adder: b2 = "at least 4 neighbors".
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

			deletions += Long.bitCount(self & ~b2);
		}
	}

	IO.println("Result: " + COLOR_RED + deletions + COLOR_RESET + " deletions");
}
