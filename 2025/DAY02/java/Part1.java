/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

void main(String[] args)
{
	String[] intervals = args[0].split(",");

	long result = 0;
	for (String interval : intervals)
	{
		String[] r = interval.split("-");
		result += aggregateInvalidIDs(Long.parseLong(r[0]), Long.parseLong(r[1]));
	}

	IO.println("Invalid IDs Result:\n" + result);
}

public static long aggregateInvalidIDs(long min, long max)
{
	long result = 0;
	int minDigits = (int) (Math.log10(min) + 1);
	int maxDigits = (int) (Math.log10(max) + 1);

	for (int digits = (minDigits % 2 == 0 ? minDigits : minDigits + 1); digits <= maxDigits; digits += 2)
	{
		int d = digits / 2;
		long l = (long) Math.pow(10, d - 1);
		long r = (long) Math.pow(10, d) - 1;
		for (long i = l; i <= r; i++)
		{
			long seq = i * (long) Math.pow(10, d) + i;
			if (seq < min)
			{
				continue;
			}
			if (seq > max)
			{
				break;
			}
			result += seq;
		}
	}
	return result;
}