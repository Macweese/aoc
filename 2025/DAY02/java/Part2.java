/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// A D-digit ID built from a p-digit block repeated D/p times is block * R(p,D)
// where R(p,D) = (10^D - 1) / (10^p - 1), strictly increasing in block.
// So each "period p" family clamped to [min, max] is one arithmetic series, and the
// distinct union over all proper divisors p | D falls out of Mobius-style
// inclusion-exclusion coefficients on the divisor lattice: O(d(D)^2) per range,
// no HashSet, no enumeration, no Math.pow.
private static final long[] P10 = {
	1L, 10L, 100L, 1_000L, 10_000L, 100_000L, 1_000_000L, 10_000_000L, 100_000_000L,
	1_000_000_000L, 10_000_000_000L, 100_000_000_000L, 1_000_000_000_000L, 10_000_000_000_000L,
	100_000_000_000_000L, 1_000_000_000_000_000L, 10_000_000_000_000_000L,
	100_000_000_000_000_000L, 1_000_000_000_000_000_000L
};

private static final int MAX_D = 19;
private static final int[][] DIVS = new int[MAX_D + 1][];
private static final long[][] COEF = buildTables();

private static long[][] buildTables()
{
	final long[][] table = new long[MAX_D + 1][];
	for (int D = 1; D <= MAX_D; D++)
	{
		int cnt = 0;
		final int[] divs = new int[6];
		for (int p = 1; p < D; p++)
		{
			if (D % p == 0)
			{
				divs[cnt++] = p;
			}
		}
		final long[] coef = new long[cnt];
		for (int j = cnt - 1; j >= 0; j--)
		{
			long c = 1L;
			for (int l = j + 1; l < cnt; l++)
			{
				if (divs[l] % divs[j] == 0)
				{
					c -= coef[l];
				}
			}
			coef[j] = c;
		}
		DIVS[D] = java.util.Arrays.copyOf(divs, cnt);
		table[D] = coef;
	}
	return table;
}

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

private static long aggregateInvalidIDs(long min, long max)
{
	long sum = 0L;
	final int dHi = digits(max);
	for (int D = digits(min); D <= dHi; D++)
	{
		final long lo = Math.max(min, P10[D - 1]);
		final long hi = D >= 19 ? max : Math.min(max, P10[D] - 1);
		if (lo > hi)
		{
			continue;
		}
		final int[] divs = DIVS[D];
		final long[] coef = COEF[D];
		for (int j = 0; j < divs.length; j++)
		{
			final int p = divs[j];
			final long m = D >= 19 ? 1_111_111_111_111_111_111L : (P10[D] - 1) / (P10[p] - 1);
			final long blo = Math.max(P10[p - 1], (lo + m - 1) / m);
			final long bhi = Math.min(P10[p] - 1, hi / m);
			if (blo <= bhi)
			{
				sum += coef[j] * m * (((blo + bhi) * (bhi - blo + 1)) >> 1);
			}
		}
	}
	return sum;
}

// Branch-free digit count: CLZ + magic multiply, one table probe to correct.
private static int digits(long v)
{
	final int t = ((64 - Long.numberOfLeadingZeros(v | 1)) * 1233) >>> 12;
	return t + (v >= P10[t] ? 1 : 0);
}
