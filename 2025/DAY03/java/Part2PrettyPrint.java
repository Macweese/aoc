/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final String RESET = "\u001B[0m";
private static final String PURPLE = "\u001B[35m";
private static final String PURPLE_BOLD_BRIGHT = "\033[1;95m";

private static final int DIGIT_LENGTH = 12;
private static final Stack<Integer> idx = new Stack<>();

void main(String[] args)
{
	long jolts = 0L;
	for (String line : args[0].split("\n"))
	{
		StringBuilder sb = new StringBuilder(line);
		jolts += lexMaxSub(line.toCharArray(), DIGIT_LENGTH);

		int i;
		while (!idx.empty())
		{
			i = idx.pop();
			sb.insert(i, PURPLE_BOLD_BRIGHT);
			i += PURPLE_BOLD_BRIGHT.length() + 1;
			sb.insert(i, RESET);
		}
		IO.print(String.format("%-120s%20s%n", sb, jolts));
	}
}

static private long lexMaxSub(char[] digits, int k)
{
	Stack<Integer> stack = new Stack<>();
	char[] result = new char[k];
	for (int i = 0; i < digits.length; i++)
	{
		while (!stack.empty() && digits[i] > digits[stack.peek()] && digits.length - i + stack.size() > k)
		{
			idx.pop();
			stack.pop();
		}
		if (stack.size() < k)
		{
			idx.push(i);
			stack.push(i);
		}
	}

	long n = 0L;
	for (int i = k - 1; i >= 0; i--)
	{
		result[i] = digits[stack.pop()];
		n += ((long) (result[i] - ('0')) * ((long) Math.pow(10, k - (i + 1))));
	}

	IO.print(String.format("%s%s%-6s", PURPLE, n, RESET));
	return n;
}