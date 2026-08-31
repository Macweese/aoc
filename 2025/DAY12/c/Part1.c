/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 12 Part 1
 * number of regions that can hold all listed presents
 *
 * Build:	gcc -O3 -march=native -fno-plt -o part1 Part1.c
 * Run:		./part1 "<raw input>"
 *				arg whose 2nd byte is ':' is DATA (every raw input starts "0:")
 *			./part1 path
 *				any other arg is a PATH
 *			./part1
 *				defaults to ../../input.txt ../input.txt ./input.txt 2025/DAY12/input.txt
 *
 * Intuition
 *  1.	Promise-problem gap. Every shape lives in a 3x3 box (and stays in it under D4), so
 *		s = sum(q) <= floor(W/3)*floor(H/3) is a CONSTRUCTIVE fit: one present per 3x3 cell.
 *		A = sum(q*cells) > W*H is a pigeonhole misfit. On the real input the two tests cover
 *		all 1000 regions (587 / 413 / 0 undecided) so the answer is #(s <= slots) and no
 *		search ever runs. Passing the slot test already implies A <= W*H
 *  2.	Hot path is one fixed 25-byte stride per line "WWxHH: NN NN NN NN NN NN\n":
 *		pshufb gathers the 6 tens + 6 units digits, saturating-sub 48, punpcklbw + pmaddubsw
 *		(10,1) turns them into six 16-bit counts, then pmaddwd against (1..) and against the
 *		cell counts gives s and A, two phaddd fold both into one 64-bit register
 *		floor(x/3) = (x*171)>>9 exact for x < 512, no division
 *  3.	Header shapes are parsed once (~100 bytes) only to get cell counts (SIMD weights) and,
 *		lazily, the patterns for the cold path. Cold path (only regions with s > slots and
 *		A <= W*H; empty on the real input, hit by the puzzle example): first-empty-cell exact
 *		cover on u64 row bitboards with a waste budget (W*H - A) and a failed-state hash
 *		on (r, c, 3 profile rows, remaining counts). Any line breaking the fixed format
 *		(1-digit W/H/counts, CRLF, ...) falls back to a generic parser
 *
 * NOTE: limits are 6 shapes in 3x3 boxes, W <= 62, H <= 100, 1 MiB input
 */
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#if defined(__SSSE3__)
#include <immintrin.h>
#endif

#define BUFSZ (1 << 20)
#define MEMOB 20
#define MEMON_MAX ((1u << MEMOB) / 4u * 3u)

typedef uint32_t u32;
typedef uint64_t u64;

static char BUF[BUFSZ + 128] __attribute__((aligned(64)));
static int CELLS[6];
static u32 SHAPE[6][3];

static u64 MEMO[1u << MEMOB];
static u32 MEMON;
static int NOR[6];
static signed char ODR[6][8][9];
static signed char ODC[6][8][9];
static int ONC[6][8];
static int OREADY;
static u64 OCC[128];
static int REM[6];
static int LEFT;
static int GW;
static int GH;

static size_t read_file(const char *path)
{
	const int fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		return (size_t) -1;
	}
	size_t got = 0;
	ssize_t r;
	while ((r = read(fd, BUF + got, BUFSZ - got)) > 0)
	{
		got += (size_t) r;
	}
	close(fd);
	return got;
}

static size_t get_input(const int argc, char **argv)
{
	if (argc > 1)
	{
		const char *a = argv[1];
		if (a[0] != 0 && a[1] == ':')
		{
			size_t l = strlen(a);
			if (l > BUFSZ)
			{
				l = BUFSZ;
			}
			memcpy(BUF, a, l);
			return l;
		}
		return read_file(a);
	}
	static const char *const cand[4] = {"../../input.txt", "../input.txt", "input.txt", "2025/DAY12/input.txt"};
	for (int i = 0; i < 4; i++)
	{
		const size_t l = read_file(cand[i]);
		if (l != (size_t) -1)
		{
			return l;
		}
	}
	return (size_t) -1;
}

static void parse_header(const char *p, const char *end)
{
	int cur = 0;
	int row = 0;
	while (p < end)
	{
		const unsigned char ch = (unsigned char) *p;
		if (ch >= '0' && ch <= '9')
		{
			cur = ch - '0';
			row = 0;
		}
		else if (ch == '#' || ch == '.')
		{
			u32 bits = 0;
			for (int k = 0; k < 3; k++)
			{
				if (p[k] == '#')
				{
					bits |= 1u << k;
				}
			}
			if (cur < 6 && row < 3)
			{
				SHAPE[cur][row] = bits;
			}
			row++;
		}
		while (p < end && *p != '\n')
		{
			p++;
		}
		p++;
	}
	for (int i = 0; i < 6; i++)
	{
		CELLS[i] = __builtin_popcount(SHAPE[i][0]) + __builtin_popcount(SHAPE[i][1]) + __builtin_popcount(SHAPE[i][2]);
	}
}

/* ---- cold path: exact search ---- */

static void build_orients(void)
{
	for (int i = 0; i < 6; i++)
	{
		int cr[9];
		int cc[9];
		int n = 0;
		for (int r = 0; r < 3; r++)
		{
			for (int c = 0; c < 3; c++)
			{
				if ((SHAPE[i][r] >> c) & 1u)
				{
					cr[n] = r;
					cc[n] = c;
					n++;
				}
			}
		}
		u32 seen[8];
		int cnt = 0;
		for (int t = 0; t < 8; t++)
		{
			int tr[9];
			int tc[9];
			int mnr = 99;
			int mnc = 99;
			for (int k = 0; k < n; k++)
			{
				int r = cr[k];
				int c = cc[k];
				if (t & 4)
				{
					c = 2 - c;
				}
				for (int j = 0; j < (t & 3); j++)
				{
					const int nr = c;
					const int nc = 2 - r;
					r = nr;
					c = nc;
				}
				tr[k] = r;
				tc[k] = c;
				mnr = r < mnr ? r : mnr;
				mnc = c < mnc ? c : mnc;
			}
			u32 mask = 0;
			for (int k = 0; k < n; k++)
			{
				mask |= 1u << ((tr[k] - mnr) * 3 + (tc[k] - mnc));
			}
			int dup = 0;
			for (int j = 0; j < cnt; j++)
			{
				if (seen[j] == mask)
				{
					dup = 1;
				}
			}
			if (dup || mask == 0)
			{
				continue;
			}
			seen[cnt] = mask;
			const int first = __builtin_ctz(mask);
			const int ar = first / 3;
			const int ac = first % 3;
			int m = 0;
			for (int b = 0; b < 9; b++)
			{
				if ((mask >> b) & 1u)
				{
					ODR[i][cnt][m] = (signed char) (b / 3 - ar);
					ODC[i][cnt][m] = (signed char) (b % 3 - ac);
					m++;
				}
			}
			ONC[i][cnt] = m;
			cnt++;
		}
		NOR[i] = cnt;
	}
}

static inline u64 mix(u64 h)
{
	h ^= h >> 32;
	h *= 0x9E3779B97F4A7C15ULL;
	h ^= h >> 29;
	return h;
}

static int dfs(int r, int c, const int slack)
{
	if (LEFT == 0)
	{
		return 1;
	}
	while (r < GH && ((OCC[r] >> c) & 1u))
	{
		c++;
		if (c == GW)
		{
			c = 0;
			r++;
		}
	}
	if (r >= GH)
	{
		return 0;
	}
	u64 rp = 0;
	for (int i = 0; i < 6; i++)
	{
		rp = (rp << 8) | (u64) REM[i];
	}
	u64 key = mix(rp ^ ((u64) r << 48) ^ ((u64) c << 56));
	key = mix(key ^ OCC[r]);
	key = mix(key ^ OCC[r + 1]);
	key = mix(key ^ OCC[r + 2]);
	if (key == 0)
	{
		key = 1;
	}
	const u32 home = (u32) (key >> (64 - MEMOB));
	const u32 msk = (1u << MEMOB) - 1u;
	u32 sl = home;
	while (MEMO[sl])
	{
		if (MEMO[sl] == key)
		{
			return 0;
		}
		sl = (sl + 1u) & msk;
	}
	for (int i = 0; i < 6; i++)
	{
		if (REM[i] == 0)
		{
			continue;
		}
		for (int o = 0; o < NOR[i]; o++)
		{
			const int nc = ONC[i][o];
			int ok = 1;
			for (int k = 0; k < nc; k++)
			{
				const int rr = r + ODR[i][o][k];
				const int cc = c + ODC[i][o][k];
				if (cc < 0 || cc >= GW || rr >= GH || ((OCC[rr] >> cc) & 1u))
				{
					ok = 0;
					break;
				}
			}
			if (!ok)
			{
				continue;
			}
			for (int k = 0; k < nc; k++)
			{
				OCC[r + ODR[i][o][k]] |= 1ULL << (c + ODC[i][o][k]);
			}
			REM[i]--;
			LEFT--;
			if (dfs(r, c, slack))
			{
				return 1;
			}
			LEFT++;
			REM[i]++;
			for (int k = 0; k < nc; k++)
			{
				OCC[r + ODR[i][o][k]] ^= 1ULL << (c + ODC[i][o][k]);
			}
		}
	}
	if (slack > 0)
	{
		OCC[r] |= 1ULL << c;
		if (dfs(r, c, slack - 1))
		{
			return 1;
		}
		OCC[r] ^= 1ULL << c;
	}
	if (MEMON < MEMON_MAX)
	{
		sl = home;
		while (MEMO[sl])
		{
			sl = (sl + 1u) & msk;
		}
		MEMO[sl] = key;
		MEMON++;
	}
	return 0;
}

static u32 solve_region(const int w, const int h, const int *q, const int area)
{
	if (w > 62 || h > 100)
	{
		return 0;
	}
	if (!OREADY)
	{
		build_orients();
		OREADY = 1;
	}
	GW = w;
	GH = h;
	LEFT = 0;
	for (int i = 0; i < 6; i++)
	{
		REM[i] = q[i];
		LEFT += q[i];
	}
	memset(OCC, 0, sizeof(OCC));
	if (MEMON)
	{
		memset(MEMO, 0, sizeof(MEMO));
		MEMON = 0;
	}
	return (u32) dfs(0, 0, w * h - area);
}

/* ---- hot path ---- */

static const char *parse_generic(const char *p, const char *end, int *w, int *h, int *q)
{
	int v = 0;
	while (p < end && *p >= '0' && *p <= '9')
	{
		v = v * 10 + (*p - '0');
		p++;
	}
	*w = v;
	p++;
	v = 0;
	while (p < end && *p >= '0' && *p <= '9')
	{
		v = v * 10 + (*p - '0');
		p++;
	}
	*h = v;
	p++;
	for (int k = 0; k < 6; k++)
	{
		while (p < end && *p == ' ')
		{
			p++;
		}
		v = 0;
		while (p < end && *p >= '0' && *p <= '9')
		{
			v = v * 10 + (*p - '0');
			p++;
		}
		q[k] = v;
	}
	while (p < end && *p != '\n')
	{
		p++;
	}
	return p;
}

static void print(u64 v)
{
	char o[48];
	char *q = o + sizeof(o);
	*--q = '\n';
	do
	{
		*--q = (char) ('0' + v % 10);
		v /= 10;
	} while (v);
	q -= 8;
	memcpy(q, "Part 1: ", 8);
	const ssize_t w = write(1, q, (size_t) (o + sizeof(o) - q));
	(void) w;
}

int main(const int argc, char **argv)
{
	const size_t len = get_input(argc, argv);
	if (len == (size_t) -1)
	{
		const ssize_t w = write(2, "no input\n", 9);
		(void) w;
		return 1;
	}
	const char *end = BUF + len;
	const char *rs = memchr(BUF, 'x', len);
	if (!rs)
	{
		print(0);
		return 0;
	}
	while (rs > BUF && rs[-1] >= '0' && rs[-1] <= '9')
	{
		rs--;
	}
	parse_header(BUF, rs);

#if defined(__SSSE3__)
	const __m128i mt = _mm_setr_epi8(0, 3, 6, 9, 12, 15, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128);
	const __m128i c48 = _mm_set1_epi8(48);
	const __m128i wt = _mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1);
	const __m128i one = _mm_set1_epi16(1);
	const __m128i cv = _mm_setr_epi16((short) CELLS[0], (short) CELLS[1], (short) CELLS[2], (short) CELLS[3],
	                                  (short) CELLS[4], (short) CELLS[5], 0, 0);
#endif

	const char *p = rs;
	u32 cnt = 0;
	while (p < end)
	{
		if (p[2] == 'x' && p[5] == ':' && (unsigned) (p[24] | 10) == 10u)
		{
			const u32 w = (u32) ((p[0] - '0') * 10 + (p[1] - '0'));
			const u32 h = (u32) ((p[3] - '0') * 10 + (p[4] - '0'));
			const u32 slots = ((w * 171u) >> 9) * ((h * 171u) >> 9);
#if defined(__SSSE3__)
			__m128i a = _mm_loadu_si128((const __m128i *) (p + 7));
			__m128i b = _mm_loadu_si128((const __m128i *) (p + 8));
			a = _mm_subs_epu8(_mm_shuffle_epi8(a, mt), c48);
			b = _mm_subs_epu8(_mm_shuffle_epi8(b, mt), c48);
			const __m128i v = _mm_maddubs_epi16(_mm_unpacklo_epi8(a, b), wt);
			__m128i t = _mm_hadd_epi32(_mm_madd_epi16(v, one), _mm_madd_epi16(v, cv));
			t = _mm_hadd_epi32(t, t);
			const u64 r = (u64) _mm_cvtsi128_si64(t);
			const u32 s = (u32) r;
			const u32 area = (u32) (r >> 32);
#else
			u32 s = 0;
			u32 area = 0;
			for (int k = 0; k < 6; k++)
			{
				const u32 qk = (u32) ((p[7 + 3 * k] - '0') * 10 + (p[8 + 3 * k] - '0'));
				s += qk;
				area += qk * (u32) CELLS[k];
			}
#endif
			if (s <= slots)
			{
				cnt++;
			}
			else if (area <= w * h)
			{
				int q[6];
				for (int k = 0; k < 6; k++)
				{
					q[k] = (p[7 + 3 * k] - '0') * 10 + (p[8 + 3 * k] - '0');
				}
				cnt += solve_region((int) w, (int) h, q, (int) area);
			}
			p += 25;
		}
		else
		{
			while (p < end && (unsigned char) *p <= ' ')
			{
				p++;
			}
			if (p >= end)
			{
				break;
			}
			int w;
			int h;
			int q[6];
			p = parse_generic(p, end, &w, &h, q);
			int s = 0;
			int area = 0;
			for (int k = 0; k < 6; k++)
			{
				s += q[k];
				area += q[k] * CELLS[k];
			}
			if (s <= (w / 3) * (h / 3))
			{
				cnt++;
			}
			else if (area <= w * h)
			{
				cnt += solve_region(w, h, q, area);
			}
		}
	}
	print(cnt);
	return 0;
}
