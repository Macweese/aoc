/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// We need 3 bits to store a max neighbor count of 8.
// bit0 is the 1s place, bit1 is the 2s place, bit2 is the 4s place.
private static long bit0;
private static long bit1;
private static long bit2;
private static final byte LF = 10; // Line feed
private static final byte OBJ = 64; // "@"

// Branchless SWAR optimization
void main(String[] args)
{
	if (args.length == 0)
	{
		IO.println("\033[1;91mNo input data provided - aborting\u001B[0m");
		return;
	}

	byte[] data = args[0].getBytes();
	int dataSize = data.length;
	int X = 0;
	while (X < dataSize && data[X] != LF)
	{
		X++;
	}
	int Y = (dataSize + 1) / (X + 1);

	// Kernel
	final int size = (X + (Long.SIZE - 1)) / Long.SIZE;
	// Sentinel
	final int segmentSize = size + 2;
	// Double-buffer
	long[] src = new long[(Y + 2) * segmentSize];
	long[] dst = new long[(Y + 2) * segmentSize];

	// Bitpacking
	for (int y = 0; y < Y; y++)
	{
		int yOffset = (y + 1) * segmentSize + 1;
		for (int x = 0; x < X; x++)
		{
			int longIdx = x / Long.SIZE;
			int bitIdx = x % Long.SIZE;
			if (data[y * (X + 1) + x] == OBJ)
			{
				src[yOffset + longIdx] |= (1L << bitIdx);
			}
		}
	}

	long totalDeletions = 0;
	boolean running;
	do
	{
		long cycleBits = 0L;

		for (int y = 1; y <= Y; y++)
		{
			int base = y * segmentSize + 1;

			for (int x = 0; x < size; x++)
			{
				int idx = base + x;
				long self = src[idx];
				// Reset
				bit0 = 0;
				bit1 = 0;
				bit2 = 0;

				// Horizontal spill handling
				// Unrolled neighbor check ; would be dxdy loop
				long w = (self >>> 1) | (src[idx + 1] << 63);
				long e = (self << 1) | (src[idx - 1] >>> 63);

				long n = src[idx - segmentSize];
				long s = src[idx + segmentSize];

				long nw = (n >>> 1) | (src[idx - segmentSize + 1] << 63);
				long ne = (n << 1) | (src[idx - segmentSize - 1] >>> 63);

				long sw = (s >>> 1) | (src[idx + segmentSize + 1] << 63);
				long se = (s << 1) | (src[idx + segmentSize - 1] >>> 63);

				add64(n);
				add64(s);
				add64(e);
				add64(w);
				add64(nw);
				add64(ne);
				add64(sw);
				add64(se);

				long deleted = self & ~bit2;
				dst[base + x] = self & bit2;

				totalDeletions += Long.bitCount(deleted);
				cycleBits |= deleted;
			}
		}

		running = (cycleBits != 0);

		// Swap & clear buffers
		long[] temp = src;
		src = dst;
		dst = temp;
		Arrays.fill(dst, 0L);
	} while (running);

	IO.println("Result: " + totalDeletions);
}

// Compute kernel:
// 64-set SIMD POPCNT
private static void add64(long neighbors)
{
	long b0 = bit0;
	long b1 = bit1;
	bit0 = b0 ^ neighbors;
	long carry0 = b0 & neighbors;
	bit1 = b1 ^ carry0;
	// Once bit2 is set, count is >= MAX_ADJ
	long carry1 = b1 & carry0;
	bit2 |= carry1;
}