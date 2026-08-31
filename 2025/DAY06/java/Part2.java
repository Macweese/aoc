/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import com.macweese.utils.FastIO;

// my input has exactly 4 CRLF-terminated data rows before the operator row
private static final int RD = 4;
private static final byte LF = 10;
private static final byte CR = 13;
private static final long ONE = 0x0001000100010001L;
private static final long K15 = 0x7FFF7FFF7FFF7FFFL;
private static final long NIB = 0x000F000F000F000FL;

// AOC 2025 Day 6 Part 2, cephalopod math
// sum over problems of (sum | product) of the numbers read as vertical digit
// strings, one per column
//
// compute4C 4-byte lane loads and the operator-scan's trailing-sentinel
// read may probe a few bytes past n; FastIO.readFileBytes() returns an
// array sized to exactly n, so pad it the same way the old read() did
//
// geometry: data rows share one stride L (first \n), the operator row is
// the last line and may be padded or trimmed. Row k ends with \n at
// k*L-1 => rows are counted without scanning them
// This solve is hardcoded to my exact puzzle input layout: 4 data rows (RD)
// each CRLF-terminated, op row last and unterminated (no trailing \n)
void main(String[] args) throws IOException
{
	final FastIO io = load(args);
	final byte[] raw = io.readFileBytes();
	final int n = raw.length;
	final byte[] b = Arrays.copyOf(raw, n + 16);

	int i = 0;
	while (b[i] != LF)
	{
		i++;
	}
	final int L = i + 1;
	final int op = RD * L;
	final long total = process(b, n, L, op);

	io.print("Result: ");
	io.println(total);
	io.close();
}

private static long process(final byte[] b, final int n, final int L, final int op)
{
	final int W = b[L - 2] == CR ? L - 2 : L - 1;
	long total = 0L;
	int s = 0;
	while (true)
	{
		final int opc = b[op + s];

		// next operator = first non-blank of the operator row ; widths are 1..4 => byte loop beats wide load
		int p = op + s + 1;
		while (b[p] == ' ')
		{
			p++;
		}
		final boolean more = (b[p] & 0xFE) == 0x2A;
		final int nx = p - op;
		final int w = more ? nx - s - 1 : W - s;

		if ((opc & 1) != 0)
		{
			for (int j = 0; j < w; j += 4)
			{
				final int cw = w - j;
				total += (compute4C(b, s + j, L, cw > 4 ? 4 : cw) & K15) * ONE >>> 48;
			}
		}
		else
		{
			long pr = 1L;
			for (int j = 0; j < w; j += 4)
			{
				final int cw = w - j;
				final long a = compute4C(b, s + j, L, cw > 4 ? 4 : cw);
				final long v = a & K15 | ~a >>> 15 & ONE;
				pr *= (v & 0xFFFFL) * (v >>> 16 & 0xFFFFL) * (v >>> 32 & 0xFFFFL) * (v >>> 48);
			}
			total += pr;
		}

		if (!more)
		{
			break;
		}
		s = nx;
	}
	return total;
}

// 4 * columns -> 4 * 16-bit lanes, unrolled Horner over the 4 fixed data rows:
// acc = digit ? acc*10+d : acc
// ' ' = 0x20 and 0..9 = 0x30..0x39: bit 4 = is-digit, low nibble = digit / 0
// bit 15 of each lane = column holds at least one digit ;; a lone '0' is the
// number 0, not an empty column) ;; RD is hardcoded to 4 per my input
private static long compute4C(final byte[] b, final int c, final int L, final int cw)
{
	long x0 = (long) b[c] | (long) b[c + 1] << 16 | (long) b[c + 2] << 32 | (long) b[c + 3] << 48;
	long acc = x0 & NIB;
	long pr = x0 >>> 4 & ONE;

	int q = c + L;
	long x1 = (long) b[q] | (long) b[q + 1] << 16 | (long) b[q + 2] << 32 | (long) b[q + 3] << 48;
	long dm = x1 >>> 4 & ONE;
	acc += (acc * 9 & dm * 0xFFFFL) + (x1 & NIB);
	pr |= dm;

	q += L;
	long x2 = (long) b[q] | (long) b[q + 1] << 16 | (long) b[q + 2] << 32 | (long) b[q + 3] << 48;
	dm = x2 >>> 4 & ONE;
	acc += (acc * 9 & dm * 0xFFFFL) + (x2 & NIB);
	pr |= dm;

	q += L;
	long x3 = (long) b[q] | (long) b[q + 1] << 16 | (long) b[q + 2] << 32 | (long) b[q + 3] << 48;
	dm = x3 >>> 4 & ONE;
	acc += (acc * 9 & dm * 0xFFFFL) + (x3 & NIB);
	pr |= dm;

	return (acc | pr << 15) & -1L >>> (-16 * cw & 63);
}

private static FastIO load(String[] args) throws IOException
{
	if (args.length > 0)
	{
		if (args[0].indexOf(LF) >= 0)
		{
			return new FastIO(new ByteArrayInputStream(args[0].getBytes(StandardCharsets.ISO_8859_1)), System.out);
		}
		return new FastIO(args[0]);
	}
	for (String path : new String[]{"input.txt", "../input.txt", "./2025/DAY06/input.txt"})
	{
		try
		{
			return new FastIO(path);
		}
		catch (IOException ignored)
		{
			// igmore
		}
	}
	throw new FileNotFoundException("input.txt not found");
}
