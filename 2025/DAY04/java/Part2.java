/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final byte LF = 10;

private static final String COLOR_RED = "\033[1;91m";
private static final String COLOR_RESET = "\u001B[0m";

// Monotone deletoin: removing a cell never makes another cell less deletable,
// -> the synchronous-rounds fixpoint equals the asynchronous cascade. Seed a
// worklist with every initially deletable cell, then propagate neighbor-count
// decrements, TC = O(cells + 8 * deletions)
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

	final int W = cols + 2;
	final byte[] alive = new byte[(rows + 2) * W];
	final byte[] cnt = new byte[(rows + 2) * W];
	final int[] stack = new int[rows * cols];
	int sp = 0;

	// 0x40 (bit 6 set), 0x2E (bit 6 clear) -> occupancy without compares
	for (int y = 0; y < rows; y++)
	{
		final int in = y * (cols + 1);
		final int out = (y + 1) * W + 1;
		for (int x = 0; x < cols; x++)
		{
			alive[out + x] = (byte) ((raw[in + x] >>> 6) & 1);
		}
	}

	// Initial live-neighbor counts, branchless sentinel ring
	for (int y = 1; y <= rows; y++)
	{
		final int base = y * W;
		for (int x = 1; x <= cols; x++)
		{
			final int i = base + x;
			cnt[i] = (byte) (alive[i - W - 1] + alive[i - W] + alive[i - W + 1]
				+ alive[i - 1] + alive[i + 1]
				+ alive[i + W - 1] + alive[i + W] + alive[i + W + 1]);
		}
	}

	long totalDeletions = 0L;

	// Seed: everything deletable in round one
	for (int y = 1; y <= rows; y++)
	{
		final int base = y * W;
		for (int x = 1; x <= cols; x++)
		{
			final int i = base + x;
			if (alive[i] == 1 && cnt[i] < 4)
			{
				alive[i] = 0;
				stack[sp++] = i;
				totalDeletions++;
			}
		}
	}

	// Cascade: each deletion decrements its neighbors; a live cell crossing 4 -> 3
	// becomes doomed exactly once
	while (sp > 0)
	{
		final int i = stack[--sp];
		int j;
		j = i - W - 1; if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i - W;     if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i - W + 1; if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i - 1;     if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i + 1;     if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i + W - 1; if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i + W;     if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
		j = i + W + 1; if (--cnt[j] == 3 && alive[j] == 1) { alive[j] = 0; stack[sp++] = j; totalDeletions++; }
	}

	IO.println("Result: " + COLOR_RED + totalDeletions + COLOR_RESET + " deletions");
}