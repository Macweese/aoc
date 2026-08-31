/*
 * 	Copyright (C) 2016 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import com.macweese.utils.FastIO;

void main() throws IOException
{
	FastIO io = new FastIO(getClass().getResource("INPUT_DAY06_1.txt").getPath(), "OUTPUT_DAY06_2.txt");
	String[] lines = io.readFile().split("\n");

	final int Y = lines.length - 1;

	char[] operands = lines[lines.length - 1].replaceAll("\\s+", "").toCharArray();
	final int X = operands.length;

	int[][] matrix = new int[Y][X];

	for (int y = 0; y < Y; y++)
	{
		String[] values = lines[y].trim().split("\\s+");
		for (int x = 0; x < X; x++)
		{
			matrix[y][x] = Integer.parseInt(values[x]);
		}
	}

	long result = 0L;
	for (int i = 0; i < X; i++)
	{
		char op = operands[i];

		long val = op == '*' ? 1L : 0L;
		for (int j = 0; j < Y; j++)
		{
			if (op == '*')
			{
				val *= matrix[j][i];
			}
			else
			{
				val += matrix[j][i];
			}
		}

		result += val;
	}

	io.print("Result: " + result);
	io.close();
}