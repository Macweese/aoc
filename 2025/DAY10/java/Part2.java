/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// AOC 2025 Day 10 Part 2
// fewest presses to reach the joltage vector b (min 1.x, A x = b, x >= 0)
//
// Intuition
// Parity halving (exact): every solution splits as x = 2y + s, s = x mod 2 in {0,1}^m, so
//     f(b) = min over s with A s = b (mod 2), A s <= b of  |s| + 2 f((b - A s) / 2),  f(0) = 0
// Depth <= ceil(log2(max b)) + 1 ~ 9. The admissible s form ONE coset of a GF(2) null space
// (dimension k <= 4 on real inputs), found with the same provenance-tagged XOR basis as Part 1,
// so no 2^m subset table is ever built. The search runs forward, level by level, with the best
// accumulated cost kept per distinct residual vector (cost-to-go is identical for equal states)
//
// Counters live in two longs (5 lanes x 12 bits: 11 payload + 1 guard). (b|G) - A s subtracts all
// lanes at once with no cross-lane borrow; surviving guard bits prove b - A s >= 0 lane-wise
// Halving is (r ^ G) >>> 1 & mask. The parity pattern of a state is a single multiply-gather
// A s for a button mask s is two lookups in per-machine 128/64-entry half tables
//
// Input: args[0] starting with '[' is treated as raw data, any other args[0] is a path,
// no argument falls back to ../input.txt, ../../input.txt, ./input.txt, 2025/DAY10/input.txt
private static final long LSB = 1L | 1L << 12 | 1L << 24 | 1L << 36 | 1L << 48;
private static final long GUARD = LSB << 11;
private static final long PM = LSB * 0x7FF;
private static final long GATHER = 1L | 1L << 11 | 1L << 22 | 1L << 33 | 1L << 44;
private static final long K1 = 0x9E3779B97F4A7C15L;
private static final long K2 = 0xC2B2AE3D27D4EB4FL;

private static final int HMASK = (1 << 14) - 1;
private static final int[] ST = new int[HMASK + 1];
private static final int[] TI = new int[HMASK + 1];
private static int epoch = 0;

private static final int[] BV = new int[32];
private static final int[] BT = new int[32];
private static final int[] NUL = new int[32];
private static final int[] CM = new int[32];
private static final long[] CLO = new long[32];
private static final long[] CHI = new long[32];
private static final long[] LLO = new long[128];
private static final long[] LHI = new long[128];
private static final long[] HLO = new long[128];
private static final long[] HHI = new long[128];

private static long[] aLo = new long[1 << 13];
private static long[] aHi = new long[1 << 13];
private static int[] aG = new int[1 << 13];
private static long[] bLo = new long[1 << 13];
private static long[] bHi = new long[1 << 13];
private static int[] bG = new int[1 << 13];

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
			continue;
		}
		while (b[i++] != ']')
		{
		}

		int m = 0;
		int c;
		while ((c = b[i++]) != '{')
		{
			if (c == '(')
			{
				int mask = 0;
				long lo = 0L;
				long hi = 0L;
				int v = 0;
				do
				{
					c = b[i++];
					if (c == ',' || c == ')')
					{
						mask |= 1 << v;
						if (v < 5)
						{
							lo += 1L << 12 * v;
						}
						else
						{
							hi += 1L << 12 * (v - 5);
						}
						v = 0;
					}
					else
					{
						v = v * 10 + c - '0';
					}
				} while (c != ')');
				CM[m] = mask;
				CLO[m] = lo;
				CHI[m++] = hi;
			}
		}

		long tlo = 0L;
		long thi = 0L;
		int k = 0;
		int v = 0;
		do
		{
			c = b[i++];
			if (c >= '0' && c <= '9')
			{
				v = v * 10 + c - '0';
			}
			else
			{
				if (k < 5)
				{
					tlo |= (long) v << 12 * k;
				}
				else
				{
					thi |= (long) v << 12 * (k - 5);
				}
				k++;
				v = 0;
			}
		} while (c != '}');

		total += solve(m, tlo, thi);
	}

	io.println("Result: ");
	io.println(total);
	io.close();
}

private static int solve(final int m, final long tlo, final long thi)
{
	if ((tlo | thi) == 0L)
	{
		return 0;
	}

	// GF(2) basis with provenance over the parity structure of A
	int occ = 0;
	int nk = 0;
	for (int j = 0; j < m; j++)
	{
		int x = CM[j];
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
			NUL[nk++] = tag;
		}
	}

	// A s as (lo, hi) for the low 7 and high (m-7) button bits
	final int lm = Math.min(m, 7);
	for (int s = 1, e = 1 << lm; s < e; s++)
	{
		final int q = s & (s - 1);
		final int j = Integer.numberOfTrailingZeros(s);
		LLO[s] = LLO[q] + CLO[j];
		LHI[s] = LHI[q] + CHI[j];
	}
	for (int s = 1, e = 1 << (m - lm); s < e; s++)
	{
		final int q = s & (s - 1);
		final int j = Integer.numberOfTrailingZeros(s) + 7;
		HLO[s] = HLO[q] + CLO[j];
		HHI[s] = HHI[q] + CHI[j];
	}

	long[] fLo = aLo;
	long[] fHi = aHi;
	int[] fG = aG;
	long[] nLo = bLo;
	long[] nHi = bHi;
	int[] nG = bG;
	fLo[0] = tlo;
	fHi[0] = thi;
	fG[0] = 0;
	int fn = 1;
	int best = Integer.MAX_VALUE;
	int w = 1;
	final int ge = 1 << nk;

	while (fn > 0)
	{
		int nn = 0;
		final int ep = ++epoch;
		for (int q = 0; q < fn; q++)
		{
			final int g = fG[q];
			if (g >= best)
			{
				continue;
			}
			final long lo = fLo[q];
			final long hi = fHi[q];

			// parity pattern of the state -> particular s (if p lies in the column space)
			int x = (int) (((lo & LSB) * GATHER) >>> 44) & 31 | (int) (((hi & LSB) * GATHER) >>> 44) << 5 & 0x3E0;
			int s = 0;
			while (x != 0)
			{
				final int h = 31 - Integer.numberOfLeadingZeros(x);
				if ((occ >>> h & 1) == 0)
				{
					break;
				}
				x ^= BV[h];
				s ^= BT[h];
			}
			if (x != 0)
			{
				continue;
			}

			for (int gi = 0; gi < ge; gi++)
			{
				if (gi != 0)
				{
					s ^= NUL[Integer.numberOfTrailingZeros(gi)];
				}
				final long rlo = (lo | GUARD) - (LLO[s & 127] + HLO[s >>> 7]);
				final long rhi = (hi | GUARD) - (LHI[s & 127] + HHI[s >>> 7]);
				if ((rlo & rhi & GUARD) != GUARD)
				{
					continue;
				}
				final int ng = g + w * Integer.bitCount(s);
				if (ng >= best)
				{
					continue;
				}
				final long xlo = ((rlo ^ GUARD) >>> 1) & PM;
				final long xhi = ((rhi ^ GUARD) >>> 1) & PM;
				if ((xlo | xhi) == 0L)
				{
					best = ng;
					continue;
				}
				int h = (int) ((xlo * K1 ^ xhi * K2) >>> 50);
				while (true)
				{
					if (ST[h] != ep)
					{
						ST[h] = ep;
						TI[h] = nn;
						nLo[nn] = xlo;
						nHi[nn] = xhi;
						nG[nn++] = ng;
						break;
					}
					final int t = TI[h];
					if (nLo[t] == xlo && nHi[t] == xhi)
					{
						if (ng < nG[t])
						{
							nG[t] = ng;
						}
						break;
					}
					h = h + 1 & HMASK;
				}
			}
		}
		long[] tl = fLo;
		fLo = nLo;
		nLo = tl;
		tl = fHi;
		fHi = nHi;
		nHi = tl;
		final int[] tg = fG;
		fG = nG;
		nG = tg;
		fn = nn;
		w <<= 1;
	}
	return best;
}

private static byte[] load(String[] args) throws IOException
{
	if (args.length > 0)
	{
		if (args[0].length() > 0 && args[0].charAt(0) == '[')
		{
			return args[0].getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
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

