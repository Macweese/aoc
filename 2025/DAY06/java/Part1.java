/*
 * 	Copyright (C) 2016 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Single pass over the raw bytes: no split(), no regex, no matrix materialization.
// The operator row is located by scanning backwards to the last line, each column's
// accumulator is seeded (1 for '*', 0 for '+') and every token is folded in as soon
// as it is parsed - a branchless masked select picks multiply vs add per column.
void main(String[] args)
{
	final byte[] b = args[0].getBytes();
	final int n = b.length;

	int end = n;
	while (end > 0 && b[end - 1] == '\n')
	{
		end--;
	}
	int opStart = end;
	while (opStart > 0 && b[opStart - 1] != '\n')
	{
		opStart--;
	}

	// '*' = 0x2A (even), '+' = 0x2B (odd): all-ones mask for multiply columns.
	final long[] mulMask = new long[end - opStart];
	int X = 0;
	for (int i = opStart; i < end; i++)
	{
		final byte c = b[i];
		if (c == '*' || c == '+')
		{
			mulMask[X++] = -(long) ((c & 1) ^ 1);
		}
	}

	final long[] acc = new long[X];
	for (int t = 0; t < X; t++)
	{
		acc[t] = mulMask[t] & 1L; // 1 for product columns, 0 for sum columns
	}

	int t = 0;
	long v = 0L;
	boolean in = false;
	for (int i = 0; i < opStart; i++)
	{
		final int d = b[i] - '0';
		if (d >= 0)
		{
			v = v * 10 + d;
			in = true;
		}
		else
		{
			if (in)
			{
				final long mask = mulMask[t];
				acc[t] = (mask & (acc[t] * v)) | (~mask & (acc[t] + v));
				t++;
				v = 0L;
				in = false;
			}
			if (b[i] == '\n')
			{
				t = 0;
			}
		}
	}
	if (in)
	{
		final long mask = mulMask[t];
		acc[t] = (mask & (acc[t] * v)) | (~mask & (acc[t] + v));
	}

	long result = 0L;
	for (int j = 0; j < X; j++)
	{
		result += acc[j];
	}

	IO.println("Result: " + result);
}
