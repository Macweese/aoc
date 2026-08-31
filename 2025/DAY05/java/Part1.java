/*
 * 	Copyright (C) 2016 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final int MIN = 0;
private static final int MAX = 1;

void main(String[] args)
{
	final String[] input = args[0].split("\n\n");

	List<long[]> intervals = new ArrayList<>();
	for (String interval : input[0].split("\n"))
	{
		int separator = interval.indexOf('-');
		long min = Long.parseLong(interval.substring(0, separator));
		long max = Long.parseLong(interval.substring(separator + 1));

		intervals.add(new long[]{min, max});
	}

	intervals.sort(Comparator.comparingLong(i -> i[MIN]));
	List<long[]> merged = new ArrayList<>();
	for (long[] interval : intervals)
	{
		if (merged.isEmpty())
		{
			merged.add(interval);
			continue;
		}

		long[] last = merged.getLast();
		if ((last[MAX] + 1) >= interval[MIN])
		{
			last[MAX] = Math.max(last[MAX], interval[MAX]);
		}
		else
		{
			merged.add(interval);
		}
	}

	int count = 0;
	for (String id : input[1].split("\n"))
	{
		long i = Long.parseLong(id);
		for (long[] range : merged)
		{
			long min = range[MIN];
			long max = range[MAX];
			if (min <= i && i <= max)
			{
				count++;
				break;
			}
		}
	}

	IO.println("Result: " + count);
}