/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import com.macweese.utils.FastIO;

// AOC 2025 Day 11 Part 2
// number of paths svr -> out that visit both dac and fft
//
// Intuition
// A DAG path visits a vertex at most once, so "through fft and dac" splits
// uniquely at the two waypoints: #svr->fft * #fft->dac * #dac->out (fft first)
// + the mirrored product (dac first)
// At most one term is non-zero. fft/dac are NOT cut
// vertices (91% of all paths avoid fft), the product rule needs no dominance
// Every factor is a "paths to target t" count, so ONE pass carries all three
// targets per node: A0 = #v->fft, A1 = #v->dac, A2 = #v->out, seeded 1 at
// fft / dac / out respectively; a node is the lane-wise sum of its successors
//   ans = A0[svr]*A1[fft]*A2[dac] + A1[svr]*A0[dac]*A2[fft]
// the same lanes sampled at other nodes give both visit orders, no topological
// assumption. Every factor and product counts distinct paths, bounded by the
// total (~3.4e17) < 2^63
// Lazy graph: only line HEADS are indexed (hopping 4 bytes at a time over
// "abc:" + (" xyz")*d, a quarter of the byte-scan iterations, which matters
// because a one-shot JVM run is dominated by interpreter bytecode count);
// neighbour tokens are decoded in place when their edge is traversed. Avoids
// edge array, no reverse graph, no topological sort name ->
// 15-bit key (b & 31 = 1..26), key -> id in a 2048-slot open-addressed int
// table (key << 16 | id). Node 0 is a dead node and "out" a synthetic node
// with no edges
// Iterative post-order DFS from svr fused with the DP; a visited flag
// (not "value != 0") keeps dead ends linear
//
// Input: args[0] whose 4th char is ':' is treated as raw data
// (every input starts "xxx:"), any other args[0] is a path, no argument falls
// back to ../input.txt, ../../input.txt, ./input.txt, 2025/DAY11/input.txt
// Limits: 1024 nodes (my specific AOC input has 586)
//
// Run: java Part2.java <input>
// or
// javac + java, -XX:+UseSerialGC -XX:TieredStopAtLevel=1 -Xshare:auto

private static final int[] TAB = new int[2048];
private static final int[] P0 = new int[1026];
private static final int[] P1 = new int[1026];
private static final long[] A0 = new long[1026];
private static final long[] A1 = new long[1026];
private static final long[] A2 = new long[1026];
private static final boolean[] VIS = new boolean[1026];
private static final int[] SN = new int[1026];
private static final int[] SPP = new int[1026];

void main(String[] args) throws IOException
{
	final byte[] b = load(args);
	final int n = b.length;
	int cnt = 0;
	int s = 0;

	while (true)
	{
		while (s < n && b[s] < 'a')
		{
			s++;
		}
		if (s >= n)
		{
			break;
		}
		int p = s + 4;
		while (b[p] == ' ')
		{
			p += 4;
		}
		cnt++;
		final int key = (b[s] & 31) | (b[s + 1] & 31) << 5 | (b[s + 2] & 31) << 10;
		int h = key * 0x9E3779B1 >>> 21;
		while (TAB[h] != 0)
		{
			h = h + 1 & 2047;
		}
		TAB[h] = key << 16 | cnt;
		P0[cnt] = s + 4;
		P1[cnt] = p;
		s = p + 1;
	}

	// synthetic sink, seeded e2
	final int out = cnt + 1;
	final int okey = ('o' & 31) | ('u' & 31) << 5 | ('t' & 31) << 10;
	int oh = okey * 0x9E3779B1 >>> 21;
	while (TAB[oh] != 0)
	{
		oh = oh + 1 & 2047;
	}
	TAB[oh] = okey << 16 | out;
	A2[out] = 1L;
	VIS[out] = true;

	final int root = lookup(('s' & 31) | ('v' & 31) << 5 | ('r' & 31) << 10);
	final int f = lookup(('f' & 31) | ('f' & 31) << 5 | ('t' & 31) << 10);
	final int d = lookup(('d' & 31) | ('a' & 31) << 5 | ('c' & 31) << 10);
	long result = 0L;
	if (root != 0 && f != 0 && d != 0)
	{
		A0[f] = 1L;
		A1[d] = 1L;
		VIS[root] = true;
		int node = root;
		int p = P0[node];
		int sp = 0;
		while (true)
		{
			if (p < P1[node])
			{
				final int c = lookup((b[p + 1] & 31) | (b[p + 2] & 31) << 5 | (b[p + 3] & 31) << 10);
				p += 4;
				if (VIS[c])
				{
					A0[node] += A0[c];
					A1[node] += A1[c];
					A2[node] += A2[c];
				}
				else
				{
					VIS[c] = true;
					SN[sp] = node;
					SPP[sp] = p;
					sp++;
					node = c;
					p = P0[c];
				}
			}
			else
			{
				if (sp == 0)
				{
					break;
				}
				final long r0 = A0[node];
				final long r1 = A1[node];
				final long r2 = A2[node];
				sp--;
				node = SN[sp];
				p = SPP[sp];
				A0[node] += r0;
				A1[node] += r1;
				A2[node] += r2;
			}
		}
		result = A0[root] * A1[f] * A2[d] + A1[root] * A0[d] * A2[f];
	}

	io.println("Result: ".concat(Long.toString(result)));
	io.close();
}

private static int lookup(final int key)
{
	int h = key * 0x9E3779B1 >>> 21;
	int e = TAB[h];
	while (e != 0 && e >>> 16 != key)
	{
		h = h + 1 & 2047;
		e = TAB[h];
	}
	return e & 0xFFFF;
}

private static byte[] pad(final byte[] r)
{
	return Arrays.copyOf(r, r.length + 8);
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
