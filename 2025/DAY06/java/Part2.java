/*
 * 	Copyright (C) 2016 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Cephalopod math without the List<List<String>> forest: one row-major pass feeds
// per-character-column Horner accumulators (digits are most significant at the top),
// then a single left-to-right fold over the columns splits problems on blank columns
// and applies each problem's operator with a branchless masked select.
// + and * are commutative, so right-to-left reading order is irrelevant.
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

	// Widest line determines the column count.
	int width = 0;
	int lineLen = 0;
	for (int i = 0; i < n; i++)
	{
		if (b[i] == '\n')
		{
			width = Math.max(width, lineLen);
			lineLen = 0;
		}
		else
		{
			lineLen++;
		}
	}
	width = Math.max(width, lineLen);

	final long[] colVal = new long[width];
	final byte[] colHasDigit = new byte[width];

	int x = 0;
	for (int i = 0; i < opStart; i++)
	{
		final byte c = b[i];
		if (c == '\n')
		{
			x = 0;
			continue;
		}
		final int d = c - '0';
		if (d >= 0)
		{
			colVal[x] = colVal[x] * 10 + d;
			colHasDigit[x] = 1;
		}
		x++;
	}

	final int opLen = end - opStart;
	long result = 0L;
	long sum = 0L;
	long prod = 1L;
	long mulMask = 0L;
	boolean inProblem = false;

	for (int c = 0; c < width; c++)
	{
		final int oc = c < opLen ? b[opStart + c] : ' ';
		final boolean isOp = oc == '*' || oc == '+';
		if (colHasDigit[c] == 1 || isOp)
		{
			inProblem = true;
			if (colHasDigit[c] == 1)
			{
				sum += colVal[c];
				prod *= colVal[c];
			}
			if (isOp)
			{
				// '*' = 0x2A (even), '+' = 0x2B (odd)
				mulMask = -(long) ((oc & 1) ^ 1);
			}
		}
		else if (inProblem)
		{
			result += (mulMask & prod) | (~mulMask & sum);
			sum = 0L;
			prod = 1L;
			inProblem = false;
		}
	}
	if (inProblem)
	{
		result += (mulMask & prod) | (~mulMask & sum);
	}

	IO.println("Result: " + result);
}
