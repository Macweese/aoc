/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

void main(String[] args)
{
	long result = 0L;

	for (String range : args[0].split(","))
	{
		String[] parts = range.trim().split("-");
		long min = Long.parseLong(parts[0]);
		long max = Long.parseLong(parts[1]);

		result += aggregateInvalidIDs(min, max);
	}

	IO.println("Invalid IDs Result:\n" + result);
}

private long aggregateInvalidIDs(long min, long max)
{
	long intervalSum = 0L;
	Set<Long> invalids = new HashSet<>();

	int minDigits = (int) (Math.log10(min) + 1);
	int maxDigits = (int) (Math.log10(max) + 1);

	for (int digits = minDigits; digits <= maxDigits; digits++)
	{
		for (int segmentSize = 1; segmentSize <= digits / 2; segmentSize++)
		{
			if (digits % segmentSize != 0)
			{
				continue;
			}

			int repeats = digits / segmentSize;

			long multiplier = 0;
			for (int i = 0; i < repeats; i++)
			{
				multiplier += (long) Math.pow(10, i * segmentSize);
			}

			long lower = (long) Math.pow(10, segmentSize - 1);
			long upper = (long) Math.pow(10, segmentSize) - 1;

			for (long seg = lower; seg <= upper; seg++)
			{
				long i = seg * multiplier;

				if (i < min)
				{
					continue;
				}
				if (i > max)
				{
					break;
				}

				invalids.add(i);
			}
		}
	}

	for (long invalid : invalids)
	{
		intervalSum += invalid;
	}
	return intervalSum;
}