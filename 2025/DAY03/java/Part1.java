/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// k = 2 degenerates to a running max: best pair = max(bestSoFar, maxDigitSoFar * 10 + d).
// Single pass over the raw bytes, all state in registers, no Stack, no boxing,
// no split(), no toCharArray().
void main(String[] args)
{
	final byte[] b = args[0].getBytes();
	final int n = b.length;
	int jolts = 0;
	int best = 0;
	int maxd = 0;

	for (int i = 0; i < n; i++)
	{
		final int d = b[i] - '0';
		if (d >= 0)
		{
			best = Math.max(best, maxd * 10 + d);
			maxd = Math.max(maxd, d);
		}
		else
		{
			jolts += best;
			best = 0;
			maxd = 0;
		}
	}
	jolts += best;

	IO.print("Result: " + jolts);
}
