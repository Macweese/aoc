/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

void main(String[] args)
{
	final byte[] b = args[0].getBytes();

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

	java.util.Arrays.sort(packed, 0, cnt);

	long span = 0L;
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
			span += curMax - curMin + 1;
			curMin = mn;
			curMax = mx;
		}
	}
	span += curMax - curMin + 1;

	IO.println("Result: " + span);
}