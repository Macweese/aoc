/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final int LF = 10;
private static final int MAX_ADJACENT = 4;
private static final byte OBJECT_BIT = 6; // @ >> 6 = 1, . >> 6 = 0
private static final byte EMPTY_BYTE = 46; // '.'
private static final byte OBJECT_BYTE = 64; // '@'

private static final String COLOR_RED = "\033[1;91m";
private static final String COLOR_RESET = "\u001B[0m";

void main(String[] args)
{
	if (args.length == 0)
	{
		return;
	}

	byte[] raw = args[0].getBytes();
	int size = raw.length;

	int cols = 0;
	while (cols < size && raw[cols] != LF)
	{
		cols++;
	}
	int rows = (size + 1) / (cols + 1);

	int W = cols + 2;
	int H = rows + 2;

	// Double buffering
	byte[] src = new byte[H * W];
	byte[] dst = new byte[H * W];

	for (int i = 0; i < src.length; i++)
	{
		src[i] = EMPTY_BYTE;
		dst[i] = EMPTY_BYTE;
	}

	for (int r = 0; r < rows; r++)
	{
		System.arraycopy(raw, r * (cols + 1), src, (r + 1) * W + 1, cols);
	}

	long totalDeletions = 0;
	int cycleDeletions;

	do
	{
		cycleDeletions = 0;

		for (int y = 1; y <= rows; y++)
		{
			int rowOffset = y * W;
			for (int x = 1; x <= cols; x++)
			{
				int idx = rowOffset + x;

				int isObject = src[idx] >> OBJECT_BIT;
				// Unrolled 8-neighbor dxdy iterator
				int adjacent = (src[idx - W - 1] >> OBJECT_BIT)
					+ (src[idx - W] >> OBJECT_BIT)
					+ (src[idx - W + 1] >> OBJECT_BIT)
					+ (src[idx - 1] >> OBJECT_BIT)
					+ (src[idx + 1] >> OBJECT_BIT)
					+ (src[idx + W - 1] >> OBJECT_BIT)
					+ (src[idx + W] >> OBJECT_BIT)
					+ (src[idx + W + 1] >> OBJECT_BIT);

				int isDeletable = isObject & ((((MAX_ADJACENT - 1) - adjacent) >>> 31) ^ 1);
				int mask = -isDeletable;
				dst[idx] = (byte) ((EMPTY_BYTE & mask) | (src[idx] & ~mask));
				cycleDeletions += isDeletable;
			}
		}

		// Swap the buffers
		byte[] temp = src;
		src = dst;
		dst = temp;

		totalDeletions += cycleDeletions;
	} while (cycleDeletions > 0);

	IO.println(String.format("Result: %s%s%s deletions", COLOR_RED, totalDeletions, COLOR_RESET));
}