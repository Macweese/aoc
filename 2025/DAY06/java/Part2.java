/*
 * 	Copyright (C) 2016 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import com.macweese.utils.FastIO;

private static long result = 0L;
private static final List<List<String>> DATA = new ArrayList<>();

void main() throws IOException
{
	FastIO io = new FastIO(getClass().getResource("INPUT_DAY06_2.txt").getPath(), "OUTPUT_DAY06_2.txt");
	final String input = io.readFile();

	preprocess(input);
	process(DATA);

	io.print("Result: " + result);
	io.close();
	System.exit(0);
}

static void preprocess(String input)
{
	String[] lines = input.split("\n");

	int lastLine = lines.length - 1;
	String operandLine = lines[lastLine];
	int columnStart = 0;
	int columnEnd = 0;

	outer:
	for (int start = columnEnd; start < operandLine.length(); start++)
	{
		if (operandLine.charAt(start) == '+' || operandLine.charAt(start) == '*')
		{
			columnStart = start;
		}

		for (int end = start + 1; end < lines[0].length(); end++)
		{
			if (end >= operandLine.length())
			{
				columnEnd = lines[0].length();
				break;
			}
			if (operandLine.charAt(end) == '+' || operandLine.charAt(end) == '*')
			{
				columnEnd = end;
				break;
			}
		}

		List<String> columnSections = new ArrayList<>();
		for (int row = 0; row <= lastLine; row++)
		{
			if (columnStart >= columnEnd || columnStart >= lines[row].length())
			{
				break outer;
			}
			columnSections.add(lines[row].substring(columnStart, Math.min(lines[row].length(), columnEnd)));
		}
		columnStart = start = columnEnd - 1;
		DATA.add(columnSections);
	}
}

static void process(List<List<String>> data)
{
	for (int col = 0; col < data.size(); col++)
	{
		boolean multiply = false;
		long value = 0L;
		int maxIndex = 0;
		for (int row = 0; row < data.get(col).size(); row++)
		{
			maxIndex = Math.max(maxIndex, data.get(col).get(row).length());
		}

		List<Long> arguments = new ArrayList<>();
		for (int index = 0; index < maxIndex; index++)
		{
			boolean emptyColumn = true;
			int weight = data.get(col).size() - 1;
			for (int row = 0; row < data.get(col).size() - 1; row++)
			{
				if (index > data.get(col).get(row).length())
				{
					continue;
				}

				char c = data.get(col).get(row).charAt(index);
				weight -= 1;

				emptyColumn = false;

				if (c == ' ')
				{
					value /= 10;
				}
				else
				{
					value += (long) (Character.getNumericValue(c) * Math.pow(10, weight));
				}
			}

			String op = data.get(col).getLast().trim();
			multiply = op.equals("*");

			// redundant double conditions
			if (!emptyColumn && value != 0)
			{
				arguments.add(value);
			}
			value = 0L;
		}

		long resolution = multiply ? 1L : 0L;
		for (long arg : arguments)
		{
			if (multiply)
			{
				resolution *= arg;
			}
			else
			{
				resolution += arg;
			}
		}

		result += resolution;
		arguments.clear();
	}
}