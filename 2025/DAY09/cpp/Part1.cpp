/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 9 Part 1
 * largest rectangle spanned by two red tiles
 *
 * Build:	g++ -std=c++20 -O3 -march=native -fno-plt -o part1 part1.cpp
 * Run:		./part1 "<raw input>"
 *				arg starting with a digit/'-' is DATA
 *			./part1 path
 *				any other arg is a PATH
 *			./part1
 *				defaults to ../input.txt ../../input.txt ./input.txt 2025/DAY09/input.txt
 *
 * Intuition
 *  1.	Pareto dominance instead of exclusive convex hull, which is NOT sufficient:
 *      a * b is bilinear per quadrant, so on a hull edge with dx*dy<0 it is concave
 *      and interior points can win; random search finds counterexamples
 *      If p is the top-left corner of an optimal pair (p,q) and some p' != p has
 *      p'.x<=p.x, p'.y<=p.y, then box(p',q) contains box(p,q) => area >= .
 *      So only the 4 dominance staircases (min-min, max-max, max-min, min-max)
 *      matter, and the optimum is TL x BR or TR x BL. abs() is used for every
 *      evaluated pair, so every evaluated value is a real pair area (<= OPT) and the
 *      set contains OPT => no quadrant-validity bookkeeping
 *  2.  Staircases need only per-distinct-x (ymin,ymax): one 8-bit LSD radix sort
 *      over the significant bits of x + one linear scan each way
 *  3.  Pair kernel: (|dx|+1)(|dy|+1) in double (exact < 2^53, |coord| < 2^26),
 *      |d| = max(a-b, b-a), 2 independent max accumulators, no branches
 *
 *  Total TC: O(N + h^2/VL) with h = staircase size (~~N/4 on ring-like input)
 */
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(__AVX512F__) || defined(__AVX2__)
#include <immintrin.h>
#endif

#define MAXP 4096
using u32 = std::uint32_t;
using u64 = std::uint64_t;

#if defined(__AVX512F__)
#define VL 8
typedef __m512d V;
#define VLOADU _mm512_loadu_pd
#define VSET_1  _mm512_set1_pd
#define VSUB   _mm512_sub_pd
#define VADD   _mm512_add_pd
#define VMUL   _mm512_mul_pd
#define VMAX   _mm512_max_pd
#define VSTOREU _mm512_storeu_pd
#elif defined(__AVX2__)
#define VL 4
typedef __m256d V;
#define VLOADU _mm256_loadu_pd
#define VSET_1  _mm256_set1_pd
#define VSUB   _mm256_sub_pd
#define VADD   _mm256_add_pd
#define VMUL   _mm256_mul_pd
#define VMAX   _mm256_max_pd
#define VSTOREU _mm256_storeu_pd
#else
#define VL 1
typedef double V;
#define VLOADU(p)  (*(const double *)(p))
#define VSET_1(x)   ((double)(x))
#define VSUB(a, b) ((a) - (b))
#define VADD(a, b) ((a) + (b))
#define VMUL(a, b) ((a) * (b))
#define VMAX(a, b) ((a) > (b) ? (a) : (b))
#define VSTOREU(p, v) (*(double *)(p) = (v))
#endif

namespace
{
	char BUF[1 << 21];
	u32 X[MAXP];
	u32 Y[MAXP];
	u64 K1[MAXP];
	u64 K2[MAXP];
	u32 GX[MAXP];
	u32 GLO[MAXP];
	u32 GHI[MAXP];
#define ST(n) static double n##x[MAXP + 32] __attribute__((aligned(64))); \
              static double n##y[MAXP + 32] __attribute__((aligned(64)));
	ST(TL)
	ST(BR)
	ST(TR)
	ST(BL)
	int TLn;
	int BRn;
	int TRn;
	int BLn;

	const char *read_file(const char *path, size_t *len)
	{
		const int fd = open(path, O_RDONLY);
		if (fd < 0)
		{
			return nullptr;
		}
		size_t got = 0;
		ssize_t r;
		while ((r = read(fd, BUF + got, sizeof(BUF) - 1 - got)) > 0)
		{
			got += (size_t) r;
		}
		close(fd);
		BUF[got] = 0;
		*len = got;
		return BUF;
	}

	const char *get_input(const int argc, char **argv, size_t *len)
	{
		if (argc > 1)
		{
			const unsigned char c = (unsigned char) argv[1][0];
			if ((unsigned) (c - '0') <= 9u || c == '-')
			{
				*len = strlen(argv[1]);
				return argv[1];
			}
			return read_file(argv[1], len);
		}

		static const char *cand[] = {"../input.txt", "../../input.txt", "input.txt", "2025/DAY09/input.txt"};
		for (int i = 0; i < 4; i++)
		{
			const char *b = read_file(cand[i], len);
			if (b)
			{
				return b;
			}
		}
		return nullptr;
	}

	int parse(const char *p, const char *end)
	{
		int n = 0;
		int c = 0;
		int have = 0;
		u32 v = 0;
		u32 t = 0;
		for (; p < end; p++)
		{
			const unsigned d = (unsigned) (unsigned char) *p - '0';
			if (d <= 9u)
			{
				v = v * 10 + d;
				have = 1;
			}
			else if (have)
			{
				have = 0;
				if (c)
				{
					X[n] = t;
					Y[n++] = v;
					c = 0;
				}
				else
				{
					t = v;
					c = 1;
				}
				v = 0;
			}
		}
		if (have && c)
		{
			X[n] = t;
			Y[n++] = v;
		}
		return n;
	}

	static inline void pad(double *x, double *y, const int m)
	{
		const int e = (m + 2 * VL - 1) & ~(2 * VL - 1);
		for (int i = m; i < e; i++)
		{
			x[i] = x[0];
			y[i] = y[0];
		}
	}

	static double kernel(const double *ax, const double *ay, const int na, const double *bx, const double *by,
	                     const int nb)
	{
		const int e = (nb + 2 * VL - 1) & ~(2 * VL - 1);
		const V one = VSET_1(1.0);
		V m0 = VSET_1(0.0);
		V m1 = m0;
		for (int i = 0; i < na; i++)
		{
			const V xi = VSET_1(ax[i]);
			const V yi = VSET_1(ay[i]);
			for (int j = 0; j < e; j += 2 * VL)
			{
				const V a0 = VLOADU(bx + j), c0 = VLOADU(by + j);
				const V a1 = VLOADU(bx + j + VL), c1 = VLOADU(by + j + VL);
				const V w0 = VADD(VMAX(VSUB(a0, xi), VSUB(xi, a0)), one);
				const V h0 = VADD(VMAX(VSUB(c0, yi), VSUB(yi, c0)), one);
				const V w1 = VADD(VMAX(VSUB(a1, xi), VSUB(xi, a1)), one);
				const V h1 = VADD(VMAX(VSUB(c1, yi), VSUB(yi, c1)), one);
				m0 = VMAX(VMUL(w0, h0), m0);
				m1 = VMAX(VMUL(w1, h1), m1);
			}
		}
		m0 = VMAX(m0, m1);
		double t[VL > 1 ? VL : 1];
		VSTOREU(t, m0);
		double r = t[0];
		for (int q = 1; q < VL; q++)
		{
			if (t[q] > r)
			{
				r = t[q];
			}
		}
		return r;
	}
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
	const int n = parse(buf, buf + len);
	if (n < 2)
	{
		puts("Part 1: 0");
		return 0;
	}
	TLn = BRn = TRn = BLn = 0;

	/* radix sort by x (8-bit digits, significant bits only) */
	u32 orx = 0;
	for (int i = 0; i < n; i++)
	{
		K1[i] = ((u64) X[i] << 32) | Y[i];
		orx |= X[i];
	}
	const int bits = 32 - std::countl_zero((u32) (orx | 1));
	u64 *s = K1;
	u64 *d = K2;
	for (int sh = 32; sh < 32 + bits; sh += 8)
	{
		unsigned cnt[256] = {0};
		for (int i = 0; i < n; i++)
		{
			cnt[(s[i] >> sh) & 255]++;
		}
		unsigned a = 0;
		for (int i = 0; i < 256; i++)
		{
			const unsigned t = cnt[i];
			cnt[i] = a;
			a += t;
		}
		for (int i = 0; i < n; i++)
		{
			d[cnt[(s[i] >> sh) & 255]++] = s[i];
		}
		u64 *t = s;
		s = d;
		d = t;
	}
	/* per distinct x: (ymin,ymax) */
	int g = 0;
	for (int i = 0; i < n;)
	{
		const u32 x = (u32) (s[i] >> 32);
		u32 lo = (u32) s[i];
		u32 hi = lo;
		int j = i + 1;
		while (j < n && (u32) (s[j] >> 32) == x)
		{
			const u32 y = (u32) s[j];
			lo = y < lo ? y : lo;
			hi = y > hi ? y : hi;
			j++;
		}
		GX[g] = x;
		GLO[g] = lo;
		GHI[g++] = hi;
		i = j;
	}
	/* ascending: TL (new strict min y), BL (new strict max y) */
	{
		long long mn = 1LL << 40;
		long long mx = -1;
		for (int i = 0; i < g; i++)
		{
			if ((long) GLO[i] < mn)
			{
				mn = GLO[i];
				TLx[TLn] = GX[i];
				TLy[TLn++] = GLO[i];
			}
			if ((long) GHI[i] > mx)
			{
				mx = GHI[i];
				BLx[BLn] = GX[i];
				BLy[BLn++] = GHI[i];
			}
		}
		mn = 1LL << 40;
		mx = -1;
		for (int i = g - 1; i >= 0; i--)
		{
			if ((long) GLO[i] < mn)
			{
				mn = GLO[i];
				TRx[TRn] = GX[i];
				TRy[TRn++] = GLO[i];
			}
			if ((long) GHI[i] > mx)
			{
				mx = GHI[i];
				BRx[BRn] = GX[i];
				BRy[BRn++] = GHI[i];
			}
		}
	}
	pad(TLx, TLy, TLn);
	pad(BRx, BRy, BRn);
	pad(TRx, TRy, TRn);
	pad(BLx, BLy, BLn);
	/* iterate the smaller side in the outer loop */
	double r1 = TLn <= BRn ? kernel(TLx, TLy, TLn, BRx, BRy, BRn) : kernel(BRx, BRy, BRn, TLx, TLy, TLn);
	double r2 = TRn <= BLn ? kernel(TRx, TRy, TRn, BLx, BLy, BLn) : kernel(BLx, BLy, BLn, TRx, TRy, TRn);
	const double r = r1 > r2 ? r1 : r2;

	printf("Part 1: %llu\n", (unsigned long long) r);
	return 0;
}
