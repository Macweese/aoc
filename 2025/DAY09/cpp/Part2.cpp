/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 9 Part 2
 * largest red-corner rectangle lying entirely on red/green tiles
 *
 * Build:	g++ -std=c++20 -O3 -march=native -fno-plt -o part2 part2.cpp
 * Run:		./part2 "<raw input>"
 *			./part2 path
 *			./part2  defaults to ../input.txt, ../../input.txt, ./input.txt, 2025/DAY09/input.txt
 *
 * Intuition
 *  1.	Coordinate compression to <= 2 slots per distinct coordinate (a "line" slot
 *		+ one "gap" slot iff the gap is >= 1 tile wide). Within a slot every cell has
 *		the same status,, so the tile grid collapses to ~N x N and stays EXACT
 *		(incl. width-1 slits: exterior with zero cells gets no slot)
 *  2.	The exterior ("bad") grid is never rasterised per cell -> Per row it is a bitset:
 *		good = parity(A) | edgecols(E) | horizontals(H); A = XOR of suffix masks of the
 *		active vertical edges. A and E are prefix-XORs over rows of per-row toggle masks
 *		written once per edge (active on a<=k<b <=> toggled at a and at b).   O(N*W).
 *  3.	"Rectangle contains no bad cell" = range-OR over rows masked to a column range,
 *		OR is idempotent => sparse table over rows (row = W words): built by one flat
 *		OR of two shifted arrays per level (auto-vectorised); query = 2 rows x <= W words
 *  4.	Cross-hair reach. A valid rectangle with a corner at p contains the whole row
 *		and column segments from p to the other corner, so the other corner must lie in
 *		p's maximal good horizontal run x vertical run, and vice versa. Runs come from
 *		ctz/clz scans over the bitset (rows) and over a second bitset built with axes
 *		swapped (= exact transpose, columns). Filter is 6 compares/lane, fused in SIMD
 *  5.	Points sorted by x (radix): for point i only sorted j in (i, last x <= XR_i]
 *		is a contiguous SIMD range; area > best, reach tests are AND-ed in the lane mask;
 *		survivors pass two 1-bit corner probes, then the sparse-table test
 *
 * NOTE: |coord| < 2^26, N <= 2048 points
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

#define MAXP 2048
#define MAXR 2048
#define MAXW 32
#define MAXL 12
using u32 = std::uint32_t;
using u64 = std::uint64_t;

#if defined(__AVX512F__)
#define VL 8
typedef __m512d V;
typedef unsigned M;
#define vloadu _mm512_loadu_pd
#define vset1  _mm512_set1_pd
#define vsub   _mm512_sub_pd
#define vadd   _mm512_add_pd
#define vmul   _mm512_mul_pd
#define vmax   _mm512_max_pd
#define mgt(a, b) ((M) _mm512_cmp_pd_mask(a, b, _CMP_GT_OQ))
#define mge(a, b) ((M) _mm512_cmp_pd_mask(a, b, _CMP_GE_OQ))
#define mle(a, b) ((M) _mm512_cmp_pd_mask(a, b, _CMP_LE_OQ))
#define mand(a, b) ((a) & (b))
#define mbits(m) (m)
#elif defined(__AVX2__)
#define VL 4
typedef __m256d V;
typedef __m256d M;
#define vloadu _mm256_loadu_pd
#define vset1  _mm256_set1_pd
#define vsub   _mm256_sub_pd
#define vadd   _mm256_add_pd
#define vmul   _mm256_mul_pd
#define vmax   _mm256_max_pd
#define mgt(a, b) _mm256_cmp_pd(a, b, _CMP_GT_OQ)
#define mge(a, b) _mm256_cmp_pd(a, b, _CMP_GE_OQ)
#define mle(a, b) _mm256_cmp_pd(a, b, _CMP_LE_OQ)
#define mand(a, b) _mm256_and_pd(a, b)
#define mbits(m) ((unsigned) _mm256_movemask_pd(m))
#else
#define VL 1
typedef double V;
typedef unsigned M;
#define vloadu(p)  (*(const double *)(p))
#define vset1(x)   ((double)(x))
#define vsub(a, b) ((a) - (b))
#define vadd(a, b) ((a) + (b))
#define vmul(a, b) ((a) * (b))
#define vmax(a, b) ((a) > (b) ? (a) : (b))
#define mgt(a, b) ((unsigned) ((a) > (b)))
#define mge(a, b) ((unsigned) ((a) >= (b)))
#define mle(a, b) ((unsigned) ((a) <= (b)))
#define mand(a, b) ((a) & (b))
#define mbits(m) (m)
#endif

struct Grid
{
	int R;
	int C;
	int W;
	int colpos[MAXP];
	int rowpos[MAXP];
	u32 cs[2 * MAXP + 2];
	u32 ce[2 * MAXP + 2];
};

namespace
{
	static char BUF[1 << 21];
	static u32 PX[MAXP];
	static u32 PY[MAXP];
	static u32 UX[MAXP];
	static u32 UY[MAXP];
	static int CI[MAXP];
	static int RI[MAXP];
	static int ORD[MAXP];
	static u64 K1[MAXP];
	static u64 K2[MAXP];
	static u64 DA[MAXP * MAXW];
	static u64 DE[MAXP * MAXW];
	static u64 HR[MAXP * MAXW];
	static u64 TB[(size_t) MAXL * MAXR * MAXW] __attribute__((aligned(64)));
	static u64 TB2[(size_t) MAXR * MAXW] __attribute__((aligned(64)));
	static struct Grid G1;
	static struct Grid G2;
	static int W;
	static int RW;
#define AL(n) static double n[MAXP + 16] __attribute__((aligned(64)))
	AL(SX);
	AL(SY);
	AL(SXL);
	AL(SXR);
	AL(SYU);
	AL(SYD);
	static u32 SXI[MAXP];
	static int SRP[MAXP], SCP[MAXP];

	static const char *readFile(const char *path, size_t *len)
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
		static const char *cand[] = {"../input.txt", "../../input.txt", "input.txt", "2025/DAY09/input.txt"};
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

	static int parse(const char *p, const char *end)
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
					PX[n] = t;
					PY[n++] = v;
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
			PX[n] = t;
			PY[n++] = v;
		}
		return n;
	}

	/* sort+unique via 8-bit LSD radix on (value<<16|idx); idx[i] = rank of v[i], ord[k] = index of k-th smallest */
	static int compress(const u32 *v, const int n, u32 *uniq, int *idx, int *ord)
	{
		u32 o = 0;
		for (int i = 0; i < n; i++)
		{
			K1[i] = ((u64) v[i] << 16) | (u64) i;
			o |= v[i];
		}
		const int bits = 32 - std::countl_zero((u32) (o | 1));
		u64 *s = K1;
		u64 *d = K2;
		for (int sh = 16; sh < 16 + bits; sh += 8)
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
		int m = 0;
		u32 last = 0;
		for (int i = 0; i < n; i++)
		{
			const u32 val = (u32) (s[i] >> 16);
			if (m == 0 || val != last)
			{
				uniq[m++] = val;
				last = val;
			}
			idx[s[i] & 0xFFFF] = m - 1;
			if (ord)
			{
				ord[i] = (int) (s[i] & 0xFFFF);
			}
		}
		return m;
	}

	static inline void xor_suffix(u64 *row, const int c, const int Wd)
	{
		const int w = c >> 6;
		row[w] ^= ~0ULL << (c & 63);
		for (int k = w + 1; k < Wd; k++)
		{
			row[k] ^= ~0ULL;
		}
	}

	static inline void set_range(u64 *row, const int lo, const int hi)
	{
		const int wl = lo >> 6;
		const int wh = hi >> 6;
		const u64 fm = ~0ULL << (lo & 63);
		const u64 lm = ~0ULL >> (63 - (hi & 63));
		if (wl == wh)
		{
			row[wl] |= fm & lm;
			return;
		}
		row[wl] |= fm;
		for (int k = wl + 1; k < wh; k++)
		{
			row[k] = ~0ULL;
		}
		row[wh] |= lm;
	}

	/* bad-cell bitset of the compressed grid;; ax/ay are the (possibly axis-swapped) coordinates */
	static void build(struct Grid *g, const u32 *ax, const u32 *ay, const int n, const u32 *ux, const int nx,
	                  const int *ci, const u32 *uy, const int ny, const int *ri, u64 *bits)
	{
		int C = 0;
		int R = 0;
		for (int j = 0; j < nx; j++)
		{
			g->colpos[j] = C;
			g->cs[C] = g->ce[C] = ux[j];
			C++;
			if (j + 1 < nx && ux[j + 1] - ux[j] > 1)
			{
				g->cs[C] = ux[j] + 1;
				g->ce[C] = ux[j + 1] - 1;
				C++;
			}
		}
		for (int j = 0; j < ny; j++)
		{
			g->rowpos[j] = R++;
			if (j + 1 < ny && uy[j + 1] - uy[j] > 1)
			{
				R++;
			}
		}
		const int Wd = (C + 63) >> 6;
		g->R = R;
		g->C = C;
		g->W = Wd;
		memset(DA, 0, (size_t) ny * Wd * 8);
		memset(DE, 0, (size_t) ny * Wd * 8);
		memset(HR, 0, (size_t) ny * Wd * 8);
		for (int i = 0; i < n; i++)
		{
			const int j = (i + 1 == n) ? 0 : i + 1;
			const int ci_i = g->colpos[ci[i]], ci_j = g->colpos[ci[j]];
			if (ax[i] == ax[j] && ay[i] != ay[j])
			{
				const int a = ri[i] < ri[j] ? ri[i] : ri[j];
				const int b = ri[i] < ri[j] ? ri[j] : ri[i];
				xor_suffix(DA + (size_t) a * Wd, ci_i, Wd);
				xor_suffix(DA + (size_t) b * Wd, ci_i, Wd);
				DE[(size_t) a * Wd + (ci_i >> 6)] ^= 1ULL << (ci_i & 63);
				DE[(size_t) b * Wd + (ci_i >> 6)] ^= 1ULL << (ci_i & 63);
			}
			else
			{
				const int lo = ci_i < ci_j ? ci_i : ci_j;
				const int hi = ci_i < ci_j ? ci_j : ci_i;
				set_range(HR + (size_t) ri[i] * Wd, lo, hi);
			}
		}
		u64 VM[MAXW], A[MAXW] = {0}, E[MAXW] = {0};
		for (int w = 0; w < Wd; w++)
		{
			VM[w] = ~0ULL;
		}
		if (C & 63)
		{
			VM[Wd - 1] = (1ULL << (C & 63)) - 1;
		}
		for (int k = 0; k < ny; k++)
		{
			const int gap = (k + 1 < ny && uy[k + 1] - uy[k] > 1);
			u64 *b0 = bits + (size_t) g->rowpos[k] * Wd;
			for (int w = 0; w < Wd; w++)
			{
				A[w] ^= DA[(size_t) k * Wd + w];
				E[w] ^= DE[(size_t) k * Wd + w];
				const u64 gd = A[w] | E[w];
				b0[w] = ~(gd | HR[(size_t) k * Wd + w]) & VM[w];
				if (gap)
				{
					b0[Wd + w] = ~gd & VM[w];
				}
			}
		}
	}

	/* maximal good run through column slot c of "row": [*lo, *hi] in real coordinates */
	static inline void runs(const struct Grid *g, const u64 *bits, const int row, const int c,
	                        const u32 gmin, const u32 gmax, double *lo, double *hi)
	{
		const int Wd = g->W;
		const u64 *r = bits + (size_t) row * Wd;
		u32 hv = gmax;
		u32 lv = gmin;
		int pos = c + 1;
		if (pos < g->C)
		{
			int w = pos >> 6;
			u64 m = r[w] & (~0ULL << (pos & 63));
			while (!m && ++w < Wd)
			{
				m = r[w];
			}
			if (m)
			{
				hv = g->cs[(w << 6) + std::countr_zero((u64) (m))] - 1;
			}
		}
		if (c > 0)
		{
			pos = c - 1;
			int w = pos >> 6;
			u64 m = r[w] & (~0ULL >> (63 - (pos & 63)));
			while (!m && w > 0)
			{
				m = r[--w];
			}
			if (m)
			{
				lv = g->ce[(w << 6) + 63 - std::countl_zero((u64) (m))] + 1;
			}
		}
		*lo = (double) lv;
		*hi = (double) hv;
	}

	/* true ifff no bad cell in rows [r1,r2] x cols [c1,c2] */
	static inline int rect_ok(const int r1, const int r2, const int c1, const int c2)
	{
		const int lvl = 31 - std::countl_zero((u32) (r2 - r1 + 1));
		const u64 *pa = TB + (size_t) lvl * RW + (size_t) r1 * W;
		const u64 *pb = TB + (size_t) lvl * RW + (size_t) (r2 - (1 << lvl) + 1) * W;
		const int w1 = c1 >> 6;
		const int w2 = c2 >> 6;
		const u64 fm = ~0ULL << (c1 & 63);
		const u64 lm = ~0ULL >> (63 - (c2 & 63));
		if (w1 == w2)
		{
			return !((pa[w1] | pb[w1]) & fm & lm);
		}
		if ((pa[w1] | pb[w1]) & fm)
		{
			return 0;
		}
		for (int w = w1 + 1; w < w2; w++)
		{
			if (pa[w] | pb[w])
			{
				return 0;
			}
		}
		return !((pa[w2] | pb[w2]) & lm);
	}

	static inline int bad_at(const int r, const int c)
	{
		return (int) ((TB[(size_t) r * W + (c >> 6)] >> (c & 63)) & 1);
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
		puts("Part 2: 0");
		return 0;
	}

	const int NX = compress(PX, n, UX, CI, ORD);
	const int NY = compress(PY, n, UY, RI, NULL);
	build(&G1, PX, PY, n, UX, NX, CI, UY, NY, RI, TB);
	build(&G2, PY, PX, n, UY, NY, RI, UX, NX, CI, TB2);
	W = G1.W;
	const int R = G1.R;
	RW = R * W;
	for (int l = 1; (1 << l) <= R; l++)
	{
		const u64 *p = TB + (size_t) (l - 1) * RW;
		const u64 *q = p + (size_t) (1 << (l - 1)) * W;
		u64 *o = TB + (size_t) l * RW;
		const size_t cnt = (size_t) (R - (1 << l) + 1) * W;
		for (size_t t = 0; t < cnt; t++)
		{
			o[t] = p[t] | q[t];
		}
	}

	/* points in x-sorted order with cross-hair reach */
	for (int k = 0; k < n; k++)
	{
		const int i = ORD[k];
		const int rp = G1.rowpos[RI[i]];
		const int cp = G1.colpos[CI[i]];
		SX[k] = (double) PX[i];
		SY[k] = (double) PY[i];
		SXI[k] = PX[i];
		SRP[k] = rp;
		SCP[k] = cp;
		runs(&G1, TB, rp, cp, UX[0], UX[NX - 1], &SXL[k], &SXR[k]);
		runs(&G2, TB2, G2.rowpos[CI[i]], G2.colpos[RI[i]], UY[0], UY[NY - 1], &SYU[k], &SYD[k]);
	}
	{
		const double nan = __builtin_nan("");
		for (int k = n; k < n + VL; k++)
		{
			SX[k] = SY[k] = SXL[k] = SXR[k] = SYU[k] = SYD[k] = nan;
		}
	}

	double best = 0.0;
	V bv = vset1(0.0);
	const V one = vset1(1.0);
	for (int i = 0; i < n - 1; i++)
	{
		const double xr = SXR[i];
		int lo = i + 1, hi = n; /* first k with SXI[k] > xr */
		while (lo < hi)
		{
			const int mid = (lo + hi) >> 1;
			if ((double) SXI[mid] <= xr)
			{
				lo = mid + 1;
			}
			else
			{
				hi = mid;
			}
		}
		const int end = lo;
		const V xi = vset1(SX[i]), yi = vset1(SY[i]), xrv = vset1(xr);
		const V yuv = vset1(SYU[i]), ydv = vset1(SYD[i]);
		for (int j = i + 1; j < end; j += VL)
		{
			const V x = vloadu(SX + j);
			const V y = vloadu(SY + j);
			const V area = vmul(vadd(vsub(x, xi), one), vadd(vmax(vsub(y, yi), vsub(yi, y)), one));
			M m = mgt(area, bv);
			m = mand(m, mle(x, xrv));
			m = mand(m, mge(y, yuv));
			m = mand(m, mle(y, ydv));
			m = mand(m, mge(xi, vloadu(SXL + j)));
			m = mand(m, mge(yi, vloadu(SYU + j)));
			m = mand(m, mle(yi, vloadu(SYD + j)));
			unsigned mb = mbits(m);
			while (__builtin_expect(mb != 0, 0))
			{
				const int jj = j + std::countr_zero((unsigned) mb);
				mb &= mb - 1;
				const double dy = SY[jj] - SY[i];
				const double ar = (SX[jj] - SX[i] + 1.0) * ((dy < 0 ? -dy : dy) + 1.0);
				if (ar <= best)
				{
					continue;
				}
				if (bad_at(SRP[jj], SCP[i]) | bad_at(SRP[i], SCP[jj]))
				{
					continue;
				}
				const int r1 = SRP[i] < SRP[jj] ? SRP[i] : SRP[jj], r2 = SRP[i] < SRP[jj] ? SRP[jj] : SRP[i];
				const int c1 = SCP[i] < SCP[jj] ? SCP[i] : SCP[jj], c2 = SCP[i] < SCP[jj] ? SCP[jj] : SCP[i];
				if (rect_ok(r1, r2, c1, c2))
				{
					best = ar;
					bv = vset1(best);
				}
			}
		}
	}
	printf("Part 2: %llu\n", (unsigned long long) best);
	return 0;
}
