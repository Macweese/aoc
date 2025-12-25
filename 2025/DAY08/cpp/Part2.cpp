/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 8 Part 2
 * product of x-coordinates of the edge that joins everything into one circuit
 *
 * Build:	g++ -O3 -std=c++17 -march=native -fno-plt -o part2 part2.cpp
 * Run:		./part2 "<raw input>"
 *			./part2 path
 *			./part2 (../input.txt, ../../input.txt, ./input.txt, 2025/DAY08/input.txt)
 *
 * Intuitoin
 *	The edge that completes Kruskal is exactly the MAXIMUM edge of the
 *	Euclidean MST (bottleneck edge)...
 *	So the 500k-edge materialize + sort + DSU pipeline collapses
 *	to dense O(N^2) Prim with ZERO memory of edges:
 *		-	8 (AVX-512) / 4 (AVX2) squared distances per instruction group,
 *			computed exactly in double via
 *			|a-c|^2 = |a|^2 - 2a.c + |c|^2
 *			(3 FMA + 1 add; integers < 2^53 are exact) instead of 3 vpmuldq
 *		-	update-key and arg-min are fused in one sweep: key is
 *			dist2*2^ib + slot (slot folded into the precomputed |a|^2 term),
 *			so a plain vector min carries the arg-min for free
 *		-	tree vertex is swap-removed from compacted SoA, so pass t touches
 *			only N-t live slots -> N^2/2 total, no visited[] test;
 *			freed slots refilled with far-away pad points that can never win
 *		-	only the running max of MST weights is tracked;
 *			the partner endpoint is recovered by one O(N) scan on each new
 *			record (~ln N times), so no parent[] array is maintained at all
 *
 * NOTE!
 *		Limits: 2^ib * 12 * maxabs^2 < 2^53; |coord| < ~8e5 at N=1000, N < 2^15
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <immintrin.h>
#include <unistd.h>
#include <sys/stat.h>

constexpr int MAXN = 32768;
using i64 = int64_t;
using u64 = uint64_t;

static i64 X[MAXN + 16], Y[MAXN + 16], Z[MAXN + 16];
static double A2X[MAXN + 32] __attribute__((aligned(64)));
static double A2Y[MAXN + 32] __attribute__((aligned(64)));
static double A2Z[MAXN + 32] __attribute__((aligned(64)));
static double NJ[MAXN + 32] __attribute__((aligned(64)));
static double KK[MAXN + 32] __attribute__((aligned(64)));
static i64 TX[MAXN], TY[MAXN], TZ[MAXN];

/*
 * Exact-in-double distance kernel.  For every live vertex a (slot s) we keep, pre-scaled by S1 = 2^ib:
 *     A2 = -2*S1*a          NJ = S1*|a|^2 + s
 * and for the current tree vertex c:  key(s) = NJ + A2.c + S1*|c|^2 = S1*|a-c|^2 + s
 * i.e. 3 FMAs + 1 ADD per lane-group, the slot index rides in the low bits (no index vectors), and
 * every intermediate is an integer < 2^53, so the double arithmetic is bit-exact.
 * Requires  S1 * 12 * maxabs^2 < 2^53   (AoC: 1024 * 12 * 1e10 ~ 1.2e14).
 */
#if defined(__AVX512F__)
#define VL 8
using V = __m512d;
#define vloadu       _mm512_loadu_pd
#define vstoreu      _mm512_storeu_pd
#define vset1        _mm512_set1_pd
#define vadd         _mm512_add_pd
#define vfma         _mm512_fmadd_pd
#define vmin         _mm512_min_pd
#elif defined(__AVX2__)
#define VL 4
using V = __m256d;
#define vloadu       _mm256_loadu_pd
#define vstoreu      _mm256_storeu_pd
#define vset1        _mm256_set1_pd
#define vadd         _mm256_add_pd
#ifdef __FMA__
#define vfma         _mm256_fmadd_pd
#else
#define vfma(a, b, c) _mm256_add_pd(_mm256_mul_pd(a, b), c)
#endif
#define vmin         _mm256_min_pd
#else
#define VL 1
using V = double;
#define vloadu(p)    (*(const double *)(p))
#define vstoreu(p, v) (*(double *)(p) = (v))
#define vset1(x)     ((double)(x))
#define vadd(a, b)   ((a) + (b))
#define vfma(a, b, c) ((a) * (b) + (c))
#define vmin(a, b)   ((a) < (b) ? (a) : (b))
#endif

static char *readFile(const char *path, size_t *len)
{
	const int fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		return nullptr;
	}
	struct stat st{};
	fstat(fd, &st);
	char *b = (char *) malloc((size_t) st.st_size + 16);
	size_t got = 0;
	ssize_t r;
	while (got < (size_t) st.st_size && (r = read(fd, b + got, (size_t) st.st_size - got)) > 0)
	{
		got += (size_t) r;
	}
	close(fd);
	b[got] = 0;
	*len = got;
	return b;
}

static const char *get_input(const int argc, char **argv, size_t *len)
{
	if (argc > 1)
	{
		const unsigned char c = (unsigned char) argv[1][0];
		if ((unsigned) (c - '0') <= 9u || c == '-')
		{
			*len = strlen(argv[1]);
			return argv[1];
		}
		return readFile(argv[1], len);
	}
	static const char *cand[] = {"../input.txt", "../../input.txt", "input.txt", "2025/DAY08/input.txt"};
	for (int i = 0; i < 4; i++)
	{
		const char *b = readFile(cand[i], len);
		if (b)
		{
			return b;
		}
	}
	return nullptr;
}

static int parse_points(const char *p, const char *end)
{
	int n = 0;
	int c = 0;
	i64 v[3] = {0, 0, 0};
	while (p < end)
	{
		const unsigned char ch = (unsigned char) *p;
		if ((unsigned) (ch - '0') <= 9u || ch == '-')
		{
			int neg = 0;
			if (ch == '-')
			{
				neg = 1;
				p++;
			}
			i64 val = 0;
			while (p < end && (unsigned) (*p - '0') <= 9u)
			{
				val = val * 10 + (*p++ - '0');
			}
			v[c++] = neg ? -val : val;
			if (c == 3)
			{
				X[n] = v[0];
				Y[n] = v[1];
				Z[n] = v[2];
				n++;
				c = 0;
			}
		}
		else
		{
			p++;
		}
	}
	return n;
}

int main(const int argc, char **argv)
{
	size_t len = 0;
	const char *buf = get_input(argc, argv, &len);
	if (!buf)
	{
		fputs("no input\n", stderr);
		return 1;
	}
	const int n = parse_points(buf, buf + len);
	if (n < 2)
	{
		puts("Part 2: 0");
		return 0;
	}

	i64 mxabs = 0;
	for (int i = 0; i < n; i++)
	{
		i64 a = X[i] < 0 ? -X[i] : X[i];
		if (a > mxabs)
		{
			mxabs = a;
		}
		a = Y[i] < 0 ? -Y[i] : Y[i];
		if (a > mxabs)
		{
			mxabs = a;
		}
		a = Z[i] < 0 ? -Z[i] : Z[i];
		if (a > mxabs)
		{
			mxabs = a;
		}
	}
	int ib = 1;
	while ((1 << ib) < n + VL + 8)
	{
		ib++;
	}
	const i64 mask = ((i64) 1 << ib) - 1;
	const double S1 = (double) ((i64) 1 << ib);
	const double S2 = S1 + S1;
	if (S1 * 12.0 * (double) mxabs * (double) mxabs >= 9.0e15)
	{
		fputs("warning: coordinates too large for exact double kernel\n", stderr);
	}

	int L = n - 1;
	for (int i = 0; i < L; i++)
	{
		const double x = (double) X[i + 1];
		const double y = (double) Y[i + 1];
		const double z = (double) Z[i + 1];
		A2X[i] = -S2 * x;
		A2Y[i] = -S2 * y;
		A2Z[i] = -S2 * z;
		NJ[i] = S1 * (x * x + y * y + z * z) + (double) i;
		KK[i] = __builtin_inf();
	}
	for (int i = L; i < L + 2 * VL + 8; i++)
	{
		A2X[i] = A2Y[i] = A2Z[i] = 0.0;
		NJ[i] = 1e17;
		KK[i] = __builtin_inf();
	}
	TX[0] = X[0];
	TY[0] = Y[0];
	TZ[0] = Z[0];

	i64 cx = X[0];
	i64 cy = Y[0];
	i64 cz = Z[0];
	i64 maxW = -1;
	i64 ans = 0;

	for (int t = 1; t < n; t++)
	{
		const V xv = vset1((double)cx);
		const V yv = vset1((double)cy);
		const V zv = vset1((double)cz);
		const V nc = vset1(S1 * (double)(cx * cx + cy * cy + cz * cz));
		V mv = vset1(__builtin_inf());
		V mv2 = mv;
		int j = 0;
		for (; j + VL < L; j += 2 * VL)
		{
			V ta = vfma(vloadu(&A2Z[j]), zv, vfma(vloadu(&A2Y[j]), yv, vfma(vloadu(&A2X[j]), xv, vloadu(&NJ[j]))));
			V tb = vfma(vloadu(&A2Z[j + VL]), zv,
			            vfma(vloadu(&A2Y[j + VL]), yv, vfma(vloadu(&A2X[j + VL]), xv, vloadu(&NJ[j + VL]))));
			V ka = vmin(vloadu(&KK[j]), vadd(ta, nc));
			V kb = vmin(vloadu(&KK[j + VL]), vadd(tb, nc));
			vstoreu(&KK[j], ka);
			vstoreu(&KK[j + VL], kb);
			mv = vmin(mv, ka);
			mv2 = vmin(mv2, kb);
		}
		for (; j < L; j += VL)
		{
			const V ta = vfma(vloadu(&A2Z[j]), zv, vfma(vloadu(&A2Y[j]), yv, vfma(vloadu(&A2X[j]), xv, vloadu(&NJ[j]))));
			V ka = vmin(vloadu(&KK[j]), vadd(ta, nc));
			vstoreu(&KK[j], ka);
			mv = vmin(mv, ka);
		}
		mv = vmin(mv, mv2);
		double tmp[VL] __attribute__((aligned(64)));
		vstoreu(tmp, mv);
		double bestd = tmp[0];
		for (int q = 1; q < VL; q++)
		{
			if (tmp[q] < bestd)
			{
				bestd = tmp[q];
			}
		}
		const i64 best = (i64) bestd;
		const int m = (int) (best & mask);
		const i64 W = best >> ib;
		const i64 vx = (i64) (-A2X[m]) >> (ib + 1);
		const i64 vy = (i64) (-A2Y[m]) >> (ib + 1);
		const i64 vz = (i64) (-A2Z[m]) >> (ib + 1);
		if (W > maxW)
		{
			maxW = W;
			for (int q = 0; q < t; q++)
			{
				const i64 a = TX[q] - vx;
				const i64 b = TY[q] - vy;
				const i64 c = TZ[q] - vz;
				if (a * a + b * b + c * c == W)
				{
					ans = vx * TX[q];
					break;
				}
			}
		}
		TX[t] = vx;
		TY[t] = vy;
		TZ[t] = vz;
		cx = vx;
		cy = vy;
		cz = vz;
		L--;
		const double d = (double) (L - m);
		A2X[m] = A2X[L];
		A2Y[m] = A2Y[L];
		A2Z[m] = A2Z[L];
		NJ[m] = NJ[L] - d;
		KK[m] = KK[L] - d;
		A2X[L] = A2Y[L] = A2Z[L] = 0.0;
		NJ[L] = 1e17;
		KK[L] = __builtin_inf();
	}
	printf("Part 2: %lld\n", (long long) ans);
	return 0;
}
