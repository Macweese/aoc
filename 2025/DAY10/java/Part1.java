/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// AOC 2025 Day 10 Part 1
// fewest presses to match the light pattern
//
// Intuition
// Pressing a button twice cancels, so x_j in {0,1} and the task is the minimum-weight
// element of the coset {S : XOR_{j in S} c_j = T} over GF(2)
// Instead of a 2^m / 2^n search:
// insert the button columns into an XOR linear basis (indexed by top bit)
// that carries an m-bit provenance tag per basis vector (the set of buttons it is the XOR of)
// A column that reduces to zero yields a null-space vector (its tag);
// k = m - rank <= 4 on real inputs
// Reducing T through the basis and XOR-ing the tags gives a particular solution p;
// the answer is min popcount(p ^ span(null tags)), a Gray-coded walk of 2^k words:
// one XOR + popcnt each ->> Avoids allocation,
// no per-machine clearing (basis occupancy is a 32-bit mask), single byte scan
//
// Input: args[0] starting with '[' is treated as raw data, any other args[0] is a path,
// if no argument defaults to parse input.txt in pwd or parent dirs
// ../input.txt, ../../input.txt, ./input.txt, 2025/DAY10/input.txt
private static final int[] BV = new int[32];
private static final int[] BT = new int[32];
private static final int[] NUL = new int[32];
private static final int[] COL = new int[32];

void main(String[] args) throws IOException
{
	final byte[] b = load(args);
	final int n = b.length;
	int total = 0;
	int i = 0;

	while (i < n)
	{
		if (b[i++] != '[')
		{
			continue; // digits/commas of the ignored; the {joltage} block, newlines
		}

		// '#' = 0x23 has bit 3 clear
		// '.' = 0x2E has bit 3 set
		int t = 0;
		int bit = 0;
		int c;
		while ((c = b[i++]) != ']')
		{
			t |= ((~c >> 3) & 1) << bit++;
		}

		int m = 0;
		while ((c = b[i++]) != '{')
		{
			if (c == '(')
			{
				int mask = 0;
				int v = 0;
				while ((c = b[i++]) != ')')
				{
					if (c == ',')
					{
						mask |= 1 << v;
						v = 0;
					}
					else
					{
						v = v * 10 + c - '0';
					}
				}
				COL[m++] = mask | 1 << v;
			}
		}

		// Linear basis with provenance
		int occ = 0;
		int k = 0;
		for (int j = 0; j < m; j++)
		{
			int x = COL[j];
			int tag = 1 << j;
			while (x != 0)
			{
				final int h = 31 - Integer.numberOfLeadingZeros(x);
				if ((occ >>> h & 1) == 0)
				{
					occ |= 1 << h;
					BV[h] = x;
					BT[h] = tag;
					break;
				}
				x ^= BV[h];
				tag ^= BT[h];
			}
			if (x == 0)
			{
				NUL[k++] = tag;
			}
		}

		// Particular solution
		int x = t;
		int p = 0;
		while (x != 0)
		{
			final int h = 31 - Integer.numberOfLeadingZeros(x);
			if ((occ >>> h & 1) == 0)
			{
				p = -1; // inconcistent machine (cannot happen on valid input)
				break;
			}
			x ^= BV[h];
			p ^= BT[h];
		}
		if (p == -1)
		{
			continue;
		}

		// gray-coded min-weight element of the coset
		int best = Integer.bitCount(p);
		int cur = p;
		for (int g = 1, e = 1 << k; g < e; g++)
		{
			cur ^= NUL[Integer.numberOfTrailingZeros(g)];
			best = Math.min(best, Integer.bitCount(cur));
		}
		total += best;
	}

	io.print("Result: ");
	io.println(total);
	io.close();
}

private static byte[] load(String[] args) throws IOException
{
	if (args.length > 0)
	{
		if (args[0].length() > 0 && args[0].charAt(0) == '[')
		{
			return args[0].getBytes(StandardCharsets.ISO_8859_1);
		}
		try (final FastIO io = new FastIO(args[0]))
		{
			return pad(io.readFile().getBytes());
		}
	}

	for (String path : new String[]{"input.txt", "../input.txt", "../../input.txt", "2025/DAY11/input.txt"})
	{
		try (final FastIO io = new FastIO(path))
		{
			return pad(io.readFile().getBytes());
		}
		catch (IOException ignored)
		{
		}
	}
	throw new FileNotFoundException("input.txt not found");
}
