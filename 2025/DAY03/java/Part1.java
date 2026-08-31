/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final int DIGIT_LENGTH = 2;

void main(String[] args)
{
	int jolts = 0;
	for (String line : args[0].split("\n"))
	{
		jolts += maxLexSub(line.toCharArray(), DIGIT_LENGTH);
	}
	IO.print("Result: " + jolts);
}

static private int maxLexSub(char[] digits, int k)
{
	Stack<Integer> stack = new Stack<>();
	for (int i = 0; i < digits.length; i++)
	{
		while (!stack.empty() && digits[i] > digits[stack.peek()] && digits.length - i + stack.size() > k)
		{
			stack.pop();
		}
		if (stack.size() < k)
		{
			stack.push(i);
		}
	}

	char[] weightedDigits = new char[k];
	for (int i = k - 1; i >= 0; i--)
	{
		weightedDigits[i] = digits[stack.pop()];
	}

	return (weightedDigits[0] - '0') * 10 + (weightedDigits[1] - '0');
}