/*
 * 	Copyright (C) 2016 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Zero-allocation byte-scan parse; intervals packed as (min << 11 | index) into one
// long[] so the JDK's dual-pivot primitive sort does the heavy lifting (no boxed
// Comparator, no ArrayList of long[2]); merged in one sweep; each ID resolved with a
// branch-light bitwise binary search instead of a linear scan over all intervals.
void main(String[] args)
{
	final byte[] b = args[0].getBytes();
	final int n = b.length;

	long[] maxs = new long[2048];
	long[] packed = new long[2048];
	int cnt = 0;

	int i = 0;
	while (b[i] != '\n')
	{
		long lo = 0L;
		while (b[i] != '-')
		{
			lo = lo * 10 + b[i++] - '0';
		}
		i++;
		long hi = 0L;
		while (b[i] != '\n')
		{
			hi = hi * 10 + b[i++] - '0';
		}
		i++;
		packed[cnt] = (lo << 11) | cnt;
		maxs[cnt++] = hi;
	}
	i++; // blank line

	java.util.Arrays.sort(packed, 0, cnt);

	// Merge in one sweep over sort order.
	final long[] mMin = new long[cnt];
	final long[] mMax = new long[cnt];
	int m = 0;
	long curMin = packed[0] >>> 11;
	long curMax = maxs[(int) (packed[0] & 2047)];
	for (int j = 1; j < cnt; j++)
	{
		final long mn = packed[j] >>> 11;
		final long mx = maxs[(int) (packed[j] & 2047)];
		if (mn <= curMax + 1)
		{
			curMax = Math.max(curMax, mx);
		}
		else
		{
			mMin[m] = curMin;
			mMax[m++] = curMax;
			curMin = mn;
			curMax = mx;
		}
	}
	mMin[m] = curMin;
	mMax[m++] = curMax;

	final int steps = Integer.highestOneBit(m);
	int count = 0;
	while (i < n)
	{
		long v = 0L;
		while (i < n && b[i] != '\n')
		{
			v = v * 10 + b[i++] - '0';
		}
		i++;

		// Bitwise binary search: rightmost merged interval with min <= v.
		int pos = -1;
		for (int step = steps; step > 0; step >>= 1)
		{
			final int np = pos + step;
			if (np < m && mMin[np] <= v)
			{
				pos = np;
			}
		}
		if (pos >= 0 && v <= mMax[pos])
		{
			count++;
		}
	}

	IO.println("Result: " + count);
}
