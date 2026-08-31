/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final int DIGIT_LENGTH = 12;

// Monotonic stack on a reused primitive byte[12]: each byte is pushed/popped
// at most once (amortized O(n) per line), using Horner fold
void main(String[] args)
{
	final byte[] b = args[0].getBytes();
	final int n = b.length;
	final byte[] stack = new byte[DIGIT_LENGTH];
	long jolts = 0L;

	int i = 0;
	while (i < n)
	{
		int e = i;
		while (e < n && b[e] != '\n')
		{
			e++;
		}
		if (e > i)
		{
			int top = 0;
			for (int j = i; j < e; j++)
			{
				final byte c = b[j];
				while (top > 0 && stack[top - 1] < c && e - j + top > DIGIT_LENGTH)
				{
					top--;
				}
				if (top < DIGIT_LENGTH)
				{
					stack[top++] = c;
				}
			}
			long v = 0L;
			for (int j = 0; j < DIGIT_LENGTH; j++)
			{
				v = v * 10 + (stack[j] - '0');
			}
			jolts += v;
		}
		i = e + 1;
	}

	IO.print("Result: " + jolts);
}