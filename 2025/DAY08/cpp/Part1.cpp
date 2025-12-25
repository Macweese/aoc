/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 8 Part 1
 * 3D single-linkage ;; "product of 3 largest circuits after K shortest links"
 *
 * Build:	g++ -O3 -std=c++20 -march=native -fno-plt -o part1 part1.cpp
 *				AVX-512 / AVX2 / scalar chosen at compile time
 * Run:		./part1 "<raw input>"
 *				argument starting with a digit or '-' is treated as data
 *			./part1 path/to/file
 *				any other argument is a path
 *			./part1
 *				falls back to ../input.txt, ../../input.txt, ./input.txt, 2025/DAY08/input.txt
 *
* Intuition
 *  1.	Union of an edge SET is order independent -> sorting is redundant
 *		we only need the K smallest edges as a set ;; quickselect, not sorting
 *  2.	Points are radix-sorted by x, meaning sny pair with (dx)^2 > T can
 *		never be among edges <= T, so each point scans only a sliding x-window
 *		instead of all N-i partners (~10x fewer pair evaluations)
 *  3.	T (the K-th smallest squared distance, roughly) is estimated from 4096
 *		random pair samples (+2x margin), 1 SIMD pass collects every edge <= T
 *		packed as (w<<2b | u<<b | v) in a single u64, a bisecting fallback
 *		keeps it exact if the estimate is off
 *  4.	Packed u64 keys -> quickselect gives the K smallest, DSU over them,
 *		top-3 running insert
 *
 * NOTE!
 *		Limits: |coord| < ~8e6, N < 2^15, T<<2b must fit 63 bits
 *		should be ok for AOC ;; T~2^25, b=10
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

static i64 X[MAXN + 16] __attribute__((aligned(64)));
static i64 Y[MAXN + 16] __attribute__((aligned(64)));
static i64 Z[MAXN + 16] __attribute__((aligned(64)));
static i64 SX[MAXN + 16] __attribute__((aligned(64)));
static i64 SY[MAXN + 16] __attribute__((aligned(64)));
static i64 SZ[MAXN + 16] __attribute__((aligned(64)));
static u64 KA[MAXN], KB[MAXN];

/*  SIMD shim  */
#if defined(__AVX512F__)
#define VL 8
using V = __m512i;
#define vloadu(p)   _mm512_loadu_si512((const void *)(p))
#define vset1(x)    _mm512_set1_epi64(x)
#define vsub        _mm512_sub_epi64
#define vadd        _mm512_add_epi64
#define vmul        _mm512_mul_epi32
#define vle_mask(w, t) ((unsigned)_mm512_cmple_epi64_mask(w, t))
#elif defined(__AVX2__)
#define VL 4
using V = __m256i;
#define vloadu(p)   _mm256_loadu_si256((const __m256i *)(p))
#define vset1(x)    _mm256_set1_epi64x(x)
#define vsub        _mm256_sub_epi64
#define vadd        _mm256_add_epi64
#define vmul        _mm256_mul_epi32
/* t is passed as T+1: w < T+1 */
#define vle_mask(w, t) ((unsigned)_mm256_movemask_pd(_mm256_castsi256_pd(_mm256_cmpgt_epi64(t, w))))
#else
#define VL 1
using V = i64;
#define vloadu(p)   (*(const i64 *)(p))
#define vset1(x)    ((i64)(x))
#define vsub(a, b)  ((a) - (b))
#define vadd(a, b)  ((a) + (b))
#define vmul(a, b)  ((a) * (b))
#define vle_mask(w, t) ((unsigned)((w) < (t)))
#endif

/*  input  */
static char *readFile(const char *path, size_t *len)
{
	const int fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		return nullptr;
	}
	struct stat st;
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
			return b;
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

/*  helpers  */
static u64 qselect(u64 *a, const int n, const int r)
{
	int lo = 0;
	int hi = n - 1;
	while (lo < hi)
	{
		const u64 p = a[(lo + hi) >> 1];
		int i = lo, j = hi;
		while (i <= j)
		{
			while (a[i] < p)
			{
				i++;
			}
			while (a[j] > p)
			{
				j--;
			}
			if (i <= j)
			{
				u64 t = a[i];
				a[i] = a[j];
				a[j] = t;
				i++;
				j--;
			}
		}
		if (r <= j)
		{
			hi = j;
		}
		else if (r >= i)
		{
			lo = i;
		}
		else
		{
			break;
		}
	}
	return a[r];
}

/* LSD radix sort of point indices by x, 8-bit digits over only the significant bits */
static void sort_by_x(int n)
{
	i64 mn = X[0];
	i64 mx = X[0];
	for (int i = 1; i < n; i++)
	{
		if (X[i] < mn)
		{
			mn = X[i];
		}
		if (X[i] > mx)
		{
			mx = X[i];
		}
	}
	for (int i = 0; i < n; i++)
	{
		KA[i] = ((u64) (X[i] - mn) << 32) | (u64) i;
	}
	const u64 range = (u64) (mx - mn);
	const int bits = 64 - __builtin_clzll(range | 1);
	u64 *src = KA;
	u64 *dst = KB;
	for (int sh = 32; sh < 32 + bits; sh += 8)
	{
		unsigned cnt[256] = {0};
		for (int i = 0; i < n; i++)
		{
			cnt[(src[i] >> sh) & 255]++;
		}
		unsigned s = 0;
		for (int i = 0; i < 256; i++)
		{
			unsigned t = cnt[i];
			cnt[i] = s;
			s += t;
		}
		for (int i = 0; i < n; i++)
		{
			dst[cnt[(src[i] >> sh) & 255]++] = src[i];
		}
		u64 *t = src;
		src = dst;
		dst = t;
	}
	for (int i = 0; i < n; i++)
	{
		const u64 id = src[i] & 0xFFFFFFFFu;
		SX[i] = X[id];
		SY[i] = Y[id];
		SZ[i] = Z[id];
	}
}

static inline u64 dist2(int i, int j)
{
	const i64 dx = SX[i] - SX[j];
	const i64 dy = SY[i] - SY[j];
	const i64 dz = SZ[i] - SZ[j];
	return (u64) (dx * dx + dy * dy + dz * dz);
}

static i64 estimate_T(const int n, const int k, const u64 M)
{
	enum { S = 4096 };
	u64 s[S];
	u64 st = 0x9E3779B97F4A7C15ull ^ (u64) n;
	int m = 0;
	for (int t = 0; t < S; t++)
	{
		st ^= st << 13;
		st ^= st >> 7;
		st ^= st << 17;
		const uint32_t i = (uint32_t) (((st & 0xFFFFFFFFull) * (u64) n) >> 32);
		const uint32_t j = (uint32_t) (((st >> 32) * (u64) n) >> 32);
		if (i == j)
		{
			continue;
		}
		s[m++] = dist2((int) i, (int) j);
	}
	int rank = (int) (2.0 * (double) k / (double) M * (double) m) + 4;
	if (rank >= m)
	{
		rank = m - 1;
	}
	return (i64) qselect(s, m, rank);
}

/* Collect all pairs with w <= T. Returns count, or -1 on overflow of cap. */
static int collect(const int n, const i64 T, u64 *out, const int cap, const int ib)
{
	int cnt = 0;
	const i64 r = (i64) _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd((double) T))) + 2;
	int e = 0;
	const int sh2 = 2 * ib;
#if defined(__AVX2__) && !defined(__AVX512F__)
	V tv = vset1(T + 1);
#else
	const V tv = vset1(T);
#endif
	for (int i = 0; i < n - 1; i++)
	{
		const i64 xi = SX[i];
		if (e < i + 1)
		{
			e = i + 1;
		}
		while (e < n && SX[e] - xi <= r)
		{
			e++;
		}
		const V xv = vset1(xi);
		const V yv = vset1(SY[i]);
		const V zv = vset1(SZ[i]);
		for (int j = i + 1; j < e; j += VL)
		{
			const V dx = vsub(xv, vloadu(&SX[j]));
			const V dy = vsub(yv, vloadu(&SY[j]));
			const V dz = vsub(zv, vloadu(&SZ[j]));
			V w = vadd(vadd(vmul(dx, dx), vmul(dy, dy)), vmul(dz, dz));
			unsigned m = vle_mask(w, tv);
			if (__builtin_expect(m != 0, 0))
			{
				i64 tmp[VL] __attribute__((aligned(64)));
#if VL == 8
				_mm512_store_si512((void *) tmp, w);
#elif VL == 4
				_mm256_store_si256((__m256i *) tmp, w);
#else
				tmp[0] = w;
#endif
				while (m)
				{
					const int b = __builtin_ctz(m);
					m &= m - 1;
					const int jj = j + b;
					if (jj >= e)
					{
						continue;
					}
					if (cnt >= cap)
					{
						return -1;
					}
					out[cnt++] = ((u64) tmp[b] << sh2) | ((u64) i << ib) | (u64) jj;
				}
			}
		}
	}
	return cnt;
}

static uint32_t par[MAXN], sz[MAXN];

static inline uint32_t find(uint32_t a)
{
	while (par[a] != a)
	{
		par[a] = par[par[a]];
		a = par[a];
	}
	return a;
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
		puts("Part 1: 0");
		return 0;
	}
	int k = (n <= 20) ? 10 : 1000; /* puzzles example is 10 links */
	const u64 M = (u64) n * (u64) (n - 1) / 2;
	if ((u64) k > M)
	{
		k = (int) M;
	}

	/* pad with far-away points so full-width vector loads stay in bounds and never qualify */
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
	sort_by_x(n);
	const i64 P = 5 * mxabs + 8;
	for (int i = n; i < n + 16; i++)
	{
		SX[i] = P;
		SY[i] = P;
		SZ[i] = P;
	}

	int ib = 1;
	while ((1 << ib) < n)
	{
		ib++;
	}
	const i64 Tcap = 12 * mxabs * mxabs + 1;
	i64 T;
	if (M <= 8ull * (u64) k + 64)
	{
		T = Tcap;
	}
	else
	{
		T = estimate_T(n, k, M);
		if (T > Tcap)
		{
			T = Tcap;
		}
	}

	int cap = 8 * k + 4096;
	if ((u64) cap > M)
	{
		cap = (int) M;
	}
	if (cap > MAXN)
	{
		cap = MAXN;
	}
	i64 lo = -1;
	i64 hi = -1;
	int cnt;
	for (;;)
	{
		cnt = collect(n, T, KA, cap, ib);
		if (cnt >= k)
		{
			break;
		}
		if (cnt < 0)
		{
			hi = T;
			T = (lo < 0) ? T / 2 : lo / 2 + hi / 2;
		}
		else if (T >= Tcap)
		{
			break;
		}
		else
		{
			lo = T;
			T = (hi < 0) ? (T + T / 2 + 1) : (lo / 2 + hi / 2 + 1);
			if (T > Tcap)
			{
				T = Tcap;
			}
		}
	}
	if (cnt > k)
	{
		qselect(KA, cnt, k - 1);
	}

	for (int i = 0; i < n; i++)
	{
		par[i] = (uint32_t) i;
		sz[i] = 1;
	}
	const u64 mask = ((u64) 1 << ib) - 1;
	for (int t = 0; t < k; t++)
	{
		uint32_t a = find((uint32_t) ((KA[t] >> ib) & mask)), b = find((uint32_t) (KA[t] & mask));
		if (a != b)
		{
			if (sz[a] < sz[b])
			{
				uint32_t q = a;
				a = b;
				b = q;
			}
			par[b] = a;
			sz[a] += sz[b];
		}
	}
	u64 t1 = 0;
	u64 t2 = 0;
	u64 t3 = 0;
	for (int i = 0; i < n; i++)
	{
		if (par[i] != (uint32_t) i)
		{
			continue;
		}
		const u64 s = sz[i];
		if (s > t1)
		{
			t3 = t2;
			t2 = t1;
			t1 = s;
		}
		else if (s > t2)
		{
			t3 = t2;
			t2 = s;
		}
		else if (s > t3)
		{
			t3 = s;
		}
	}
	printf("Part 1: %llu\n", (unsigned long long) (t1 * t2 * t3));
	return 0;
}
