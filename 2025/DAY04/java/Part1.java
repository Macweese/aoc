/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final char OBJECT = '@';
private static final int MAX_ADJACENT = 4;
private static final int OBJ = 6;
private static final int LF = 10;
private static final int[] dx = {-1, -1, -1, 0, 0, 1, 1, 1};
private static final int[] dy = {-1, 0, 1, -1, 1, -1, 0, 1};

private static final String COLOR_RED = "\033[1;91m";
private static final String COLOR_RESET = "\u001B[0m";

void pre(String[] args)
{
	long deletions = 0L;
	String[] lines = args[0].split("\n");
	int rows = lines.length;
	int cols = lines[0].length();

	char[][] padded = new char[rows + 2][cols + 2];

	for (int r = 0; r < rows; r++)
	{
		for (int c = 0; c < cols; c++)
		{
			padded[r + 1][c + 1] = lines[r].charAt(c);
		}
	}

	for (int y = 1; y <= rows; y++)
	{
		for (int x = 1; x <= cols; x++)
		{

			int isObject = padded[y][x] / OBJECT;

			int adjacentObjects = 0;
			for (int k = 0; k < 8 && adjacentObjects < MAX_ADJACENT; k++)
			{
				adjacentObjects += padded[y + dy[k]][x + dx[k]] / OBJECT;
			}

			int isDeletable = (((MAX_ADJACENT - 1) - adjacentObjects) >>> 31) ^ 1;
			deletions += (isObject & isDeletable);
		}
	}

	System.out.println("Result: " + deletions);
}

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

	int sentinelCols = cols + 2;
	int sentinelRows = rows + 2;
	byte[] matrix = new byte[sentinelRows * sentinelCols];

	for (int r = 0; r < rows; r++)
	{
		System.arraycopy(raw, r * (cols + 1), matrix, (r + 1) * sentinelCols + 1, cols);
	}

	long deletions = 0L;

	for (int y = 1; y <= rows; y++)
	{
		int rowOffset = y * sentinelCols;
		for (int x = 1; x <= cols; x++)
		{
			int idx = rowOffset + x;

			int isObject = matrix[idx] >> OBJ;
			// Unrolled loop ; would be dxdy check
			int adjacentObjects = (matrix[idx - sentinelCols - 1] >> 6)
				+ (matrix[idx - sentinelCols]     >> 6)
				+ (matrix[idx - sentinelCols + 1] >> 6)
				+ (matrix[idx - 1]     >> 6)
				+ (matrix[idx + 1]     >> 6)
				+ (matrix[idx + sentinelCols - 1] >> 6)
				+ (matrix[idx + sentinelCols]     >> 6)
				+ (matrix[idx + sentinelCols + 1] >> 6);
			int isDeletable = ((MAX_ADJACENT - 1) - adjacentObjects) >>> 31;
			deletions += (isObject & (isDeletable ^ 1));
		}
	}

	IO.println(String.format("Result: %s%s%s deletions", COLOR_RED, deletions, COLOR_RESET));
}