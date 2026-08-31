/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// AOC 2025 Day 12 Part 1
// number of regions that can hold all listed presents
//
// Intuition
// Promise-problem gap; every shape lives in a 3x3 box (and stays in it under
// D4), so s = sum(q) <= floor(W/3)*floor(H/3) is a constructive fit: one
// present per 3x3 cell;; A = sum(q*cells) > W*H is a pigeonhole misfit. On the
// real input the two tests cover all 1000 regions (587 / 413 / 0 undecided) so
// the answer is #(s <= slots) and no search runs -> Passing the slot test
// already implies A <= W*H
//
// Hot path: fixed 25-byte stride "WWxHH: NN NN NN NN NN NN\n". The count sum
// needs no per-number parse: s = 10 * (sum of the six tens bytes) + (sum of
// the six units bytes) - 6 * 528 floor(x/3) = (x * 171) >>> 9 is exact for
// x < 512. The cell-weighted area is only decoded for lines that fail the slot
// test (and only then, if ever, the exact search)
//
// Cold path (regions with s > slots and A <= W*H; empty on the real input, hit
// by the puzzle example): first-empty-cell exact cover on long row bitboards,
// waste budget W*H - A, and a failed state hash on (r, c, 3 profile rows,
// remaining counts). Any line that breaks the fixed format (1-digit W/H/counts,
// CRLF, ...) falls back to a generic parser
//
// Input: args[0] whose 2nd char is ':' is treated as raw data
// (every input starts "0:"), any other args[0] is a path, no argument falls
// back to ../../input.txt, ../input.txt, ./input.txt, 2025/DAY12/input.txt
//
// Limits: 6 shapes in 3x3 boxes, W <= 62, H <= 100

private static final int MEMOB = 20;
private static final int MEMO_MAX = (1 << MEMOB) / 4 * 3;

private static final int[] CELLS = new int[6];
private static final int[] SHAPE = new int[18];
private static final int[] NOR = new int[6];
private static final int[] ODR = new int[6 * 8 * 9];
private static final int[] ODC = new int[6 * 8 * 9];
private static final int[] ONC = new int[6 * 8];
private static final long[] OCC = new long[128];
private static final int[] REM = new int[6];
private static final int[] Q = new int[6];
private static long[] memo;
private static int memoN;
private static boolean ready;
private static int left;
private static int gw;
private static int gh;
private static int pw;
private static int ph;

void main(String[] args) throws java.io.IOException
{
	final byte[] b = load(args);
	final int n = b.length;

	int x = 0;
	while (x < n && b[x] != 'x')
	{
		x++;
	}
	if (x >= n)
	{
		IO.println("Result: 0");
		return;
	}
	int p = x;
	while (p > 0 && b[p - 1] >= '0' && b[p - 1] <= '9')
	{
		p--;
	}
	parseHeader(b, p);

	int cnt = 0;
	while (p < n)
	{
		if (p + 24 < n && b[p + 2] == 'x' && b[p + 5] == ':' && (b[p + 24] | 10) == 10)
		{
			final int w = b[p] * 10 + b[p + 1] - 528;
			final int h = b[p + 3] * 10 + b[p + 4] - 528;
			final int t = b[p + 7] + b[p + 10] + b[p + 13] + b[p + 16] + b[p + 19] + b[p + 22];
			final int u = b[p + 8] + b[p + 11] + b[p + 14] + b[p + 17] + b[p + 20] + b[p + 23];
			final int s = t * 10 + u - 3168;
			if (s <= ((w * 171) >>> 9) * ((h * 171) >>> 9))
			{
				cnt++;
			}
			else
			{
				int area = 0;
				for (int k = 0; k < 6; k++)
				{
					final int qk = b[p + 7 + 3 * k] * 10 + b[p + 8 + 3 * k] - 528;
					Q[k] = qk;
					area += qk * CELLS[k];
				}
				if (area <= w * h)
				{
					cnt += solve(w, h, area);
				}
			}
			p += 25;
		}
		else
		{
			while (p < n && b[p] <= ' ')
			{
				p++;
			}
			if (p >= n)
			{
				break;
			}
			p = parseLine(b, p, n);
			int s = 0;
			int area = 0;
			for (int k = 0; k < 6; k++)
			{
				s += Q[k];
				area += Q[k] * CELLS[k];
			}
			if (s <= (pw / 3) * (ph / 3))
			{
				cnt++;
			}
			else if (area <= pw * ph)
			{
				cnt += solve(pw, ph, area);
			}
		}
	}

	io.println("Result: ");
	io.println(cnt);
	io.close();
}

private static int parseLine(final byte[] b, int p, final int n)
{
	int v = 0;
	while (p < n && b[p] >= '0' && b[p] <= '9')
	{
		v = v * 10 + b[p] - '0';
		p++;
	}
	pw = v;
	p++;
	v = 0;
	while (p < n && b[p] >= '0' && b[p] <= '9')
	{
		v = v * 10 + b[p] - '0';
		p++;
	}
	ph = v;
	p++;
	for (int k = 0; k < 6; k++)
	{
		while (p < n && b[p] == ' ')
		{
			p++;
		}
		v = 0;
		while (p < n && b[p] >= '0' && b[p] <= '9')
		{
			v = v * 10 + b[p] - '0';
			p++;
		}
		Q[k] = v;
	}
	while (p < n && b[p] != '\n')
	{
		p++;
	}
	return p;
}

private static void parseHeader(final byte[] b, final int end)
{
	int cur = 0;
	int row = 0;
	int p = 0;
	while (p < end)
	{
		final int ch = b[p];
		if (ch >= '0' && ch <= '9')
		{
			cur = ch - '0';
			row = 0;
		}
		else if (ch == '#' || ch == '.')
		{
			int bits = 0;
			for (int k = 0; k < 3; k++)
			{
				if (b[p + k] == '#')
				{
					bits |= 1 << k;
				}
			}
			if (cur < 6 && row < 3)
			{
				SHAPE[cur * 3 + row] = bits;
			}
			row++;
		}
		while (p < end && b[p] != '\n')
		{
			p++;
		}
		p++;
	}
	for (int i = 0; i < 6; i++)
	{
		CELLS[i] = Integer.bitCount(SHAPE[i * 3]) + Integer.bitCount(SHAPE[i * 3 + 1]) + Integer.bitCount(SHAPE[i * 3 + 2]);
	}
}

// cold path exact search

private static void buildOrients()
{
	final int[] cr = new int[9];
	final int[] cc = new int[9];
	final int[] tr = new int[9];
	final int[] tc = new int[9];
	final int[] seen = new int[8];
	for (int i = 0; i < 6; i++)
	{
		int n = 0;
		for (int r = 0; r < 3; r++)
		{
			for (int c = 0; c < 3; c++)
			{
				if ((SHAPE[i * 3 + r] >> c & 1) != 0)
				{
					cr[n] = r;
					cc[n] = c;
					n++;
				}
			}
		}
		int cnt = 0;
		for (int t = 0; t < 8; t++)
		{
			int mnr = 99;
			int mnc = 99;
			for (int k = 0; k < n; k++)
			{
				int r = cr[k];
				int c = cc[k];
				if ((t & 4) != 0)
				{
					c = 2 - c;
				}
				for (int j = 0; j < (t & 3); j++)
				{
					final int nr = c;
					final int nc = 2 - r;
					r = nr;
					c = nc;
				}
				tr[k] = r;
				tc[k] = c;
				mnr = Math.min(mnr, r);
				mnc = Math.min(mnc, c);
			}
			int mask = 0;
			for (int k = 0; k < n; k++)
			{
				mask |= 1 << (tr[k] - mnr) * 3 + tc[k] - mnc;
			}
			boolean dup = false;
			for (int j = 0; j < cnt; j++)
			{
				if (seen[j] == mask)
				{
					dup = true;
				}
			}
			if (dup || mask == 0)
			{
				continue;
			}
			seen[cnt] = mask;
			final int first = Integer.numberOfTrailingZeros(mask);
			final int ar = first / 3;
			final int ac = first % 3;
			int m = 0;
			for (int bit = 0; bit < 9; bit++)
			{
				if ((mask >> bit & 1) != 0)
				{
					ODR[(i * 8 + cnt) * 9 + m] = bit / 3 - ar;
					ODC[(i * 8 + cnt) * 9 + m] = bit % 3 - ac;
					m++;
				}
			}
			ONC[i * 8 + cnt] = m;
			cnt++;
		}
		NOR[i] = cnt;
	}
}

private static long mix(long h)
{
	h ^= h >>> 32;
	h *= 0x9E3779B97F4A7C15L;
	h ^= h >>> 29;
	return h;
}

private static boolean dfs(int r, int c, final int slack)
{
	if (left == 0)
	{
		return true;
	}
	while (r < gh && (OCC[r] >>> c & 1L) != 0L)
	{
		c++;
		if (c == gw)
		{
			c = 0;
			r++;
		}
	}
	if (r >= gh)
	{
		return false;
	}
	long rp = 0L;
	for (int i = 0; i < 6; i++)
	{
		rp = rp << 8 | REM[i];
	}
	long key = mix(rp ^ (long) r << 48 ^ (long) c << 56);
	key = mix(key ^ OCC[r]);
	key = mix(key ^ OCC[r + 1]);
	key = mix(key ^ OCC[r + 2]);
	if (key == 0L)
	{
		key = 1L;
	}
	final int home = (int) (key >>> 64 - MEMOB);
	final int msk = (1 << MEMOB) - 1;
	int sl = home;
	while (memo[sl] != 0L)
	{
		if (memo[sl] == key)
		{
			return false;
		}
		sl = sl + 1 & msk;
	}
	for (int i = 0; i < 6; i++)
	{
		if (REM[i] == 0)
		{
			continue;
		}
		for (int o = 0; o < NOR[i]; o++)
		{
			final int nc = ONC[i * 8 + o];
			final int base = (i * 8 + o) * 9;
			boolean ok = true;
			for (int k = 0; k < nc; k++)
			{
				final int rr = r + ODR[base + k];
				final int cc = c + ODC[base + k];
				if (cc < 0 || cc >= gw || rr >= gh || (OCC[rr] >>> cc & 1L) != 0L)
				{
					ok = false;
					break;
				}
			}
			if (!ok)
			{
				continue;
			}
			for (int k = 0; k < nc; k++)
			{
				OCC[r + ODR[base + k]] |= 1L << c + ODC[base + k];
			}
			REM[i]--;
			left--;
			if (dfs(r, c, slack))
			{
				return true;
			}
			left++;
			REM[i]++;
			for (int k = 0; k < nc; k++)
			{
				OCC[r + ODR[base + k]] ^= 1L << c + ODC[base + k];
			}
		}
	}
	if (slack > 0)
	{
		OCC[r] |= 1L << c;
		if (dfs(r, c, slack - 1))
		{
			return true;
		}
		OCC[r] ^= 1L << c;
	}
	if (memoN < MEMO_MAX)
	{
		sl = home;
		while (memo[sl] != 0L)
		{
			sl = sl + 1 & msk;
		}
		memo[sl] = key;
		memoN++;
	}
	return false;
}

private static int solve(final int w, final int h, final int area)
{
	if (w > 62 || h > 100)
	{
		return 0;
	}
	if (!ready)
	{
		buildOrients();
		memo = new long[1 << MEMOB];
		ready = true;
	}
	gw = w;
	gh = h;
	left = 0;
	for (int i = 0; i < 6; i++)
	{
		REM[i] = Q[i];
		left += Q[i];
	}
	Arrays.fill(OCC, 0L);
	if (memoN != 0)
	{
		Arrays.fill(memo, 0L);
		memoN = 0;
	}
	return dfs(0, 0, w * h - area) ? 1 : 0;
}

private static byte[] load(String[] args) throws IOException
{
	if (args.length > 0)
	{
		final String a = args[0];
		if (a.length() > 3 && a.charAt(3) == ':')
		{
			return pad(a.getBytes(StandardCharsets.ISO_8859_1));
		}
		try (final FastIO io = new FastIO(a))
		{
			return pad(io.readFile().getBytes());
		}
	}

	// check bunch of places (disorganized...)
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
