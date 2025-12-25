/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// An invalid ID with 2k digits is h * (10^k + 1) for h in [10^(k-1), 10^k - 1]
// Clamping h into [min, max] turns the whole range check into a closed-form
// arithmetic series: O(1) per digit-width; zero iteration over candidates
private static final long[] P10 = {
	1L, 10L, 100L, 1_000L, 10_000L, 100_000L, 1_000_000L, 10_000_000L, 100_000_000L,
	1_000_000_000L, 10_000_000_000L, 100_000_000_000L, 1_000_000_000_000L, 10_000_000_000_000L,
	100_000_000_000_000L, 1_000_000_000_000_000L, 10_000_000_000_000_000L,
	100_000_000_000_000_000L, 1_000_000_000_000_000_000L
};

void main(String[] args)
{
	final byte[] b = args[0].getBytes();
	long result = 0L;
	long cur = 0L;
	long min = 0L;

	for (int i = 0, n = b.length; i < n; i++)
	{
		final int d = b[i] - '0';
		if (d >= 0)
		{
			cur = cur * 10 + d;
		}
		else if (d == -3) // '-'
		{
			min = cur;
			cur = 0L;
		}
		else // ','
		{
			result += aggregateInvalidIDs(min, cur);
			cur = 0L;
		}
	}
	result += aggregateInvalidIDs(min, cur);

	IO.println("Invalid IDs Result:\n" + result);
}

public static long aggregateInvalidIDs(long min, long max)
{
	long result = 0L;
	for (int k = 1; k <= 9; k++)
	{
		final long m = P10[k] + 1;
		final long lo = Math.max(P10[k - 1], (min + m - 1) / m);
		final long hi = Math.min(P10[k] - 1, max / m);
		if (lo <= hi)
		{
			result += m * (((lo + hi) * (hi - lo + 1)) >> 1);
		}
	}
	return result;
}