/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 11 Part 2
 * number of paths svr -> out that visit both dac and fft
 *
 * Build:	gcc -O3 -march=native -fno-plt -o part2 Part2.c
 * Run:		./part2 "<raw input>"
 *				arg whose 4th byte is ':' is DATA (every raw input starts "xxx:")
 *			./part2 path
 *				any other arg is a PATH
 *			./part2
 *				defaults to ../input.txt ../../input.txt ./input.txt 2025/DAY11/input.txt
 *
 * Intuition
 *  1.	A DAG path visits a vertex at most once, so "through fft and dac" splits uniquely at the
 *		two waypoints: #svr->fft * #fft->dac * #dac->out (fft first) plus the mirrored product
 *		(dac first). In a DAG at most one of the two is non-zero. NOTE fft/dac are NOT cut
 *		vertices (91% of all paths avoid fft), the product rule needs no dominance
 *  2.	Every factor is a "paths to target t" count, so one 3-lane vector per node carries all
 *		three targets at once: lane0 = #v->fft, lane1 = #v->dac, lane2 = #v->out, seeded
 *		e0 at fft, e1 at dac, e2 at out. One 32-byte vector add per edge (1 vpaddq)
 *		and one pass over the graph
 *		ans = L0[svr]*L1[fft]*L2[dac] + L1[svr]*L0[dac]*L2[fft]
 *		The same lanes sampled at other nodes give both visit orders => no topological assumption
 *		Every factor/product counts distinct paths, bounded by the total (~3.4e17) < 2^64
 *  3.	Lazy graph: only line HEADS are indexed (64-byte block newline masks, hashed
 *		15-bit name key -> id in an 8 KB open-addressed table); neighbour tokens are 4-byte
 *		strides decoded in place exactly when the edge is traversed. No edge array,
 *		no interning, no reverse graph, no topological sort
 *  4.	Iterative post-order DFS from svr fused with the DP, node vector doubles as accumulator.
 *		A visited byte (not "value != 0") keeps dead ends linear. Node 0 is a dead node,
 *		"out" is a synthetic node seeded e2 with no edges. Depth <= 39 on the real input
 *
 * NOTE: limits are MAXN = 1024 nodes and a 1 MiB input, my AOC input has 586 nodes ~~ 9.6 KB
 */
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

#define HB 11
#define HS (1u << HB)
#define HM (HS - 1u)
#define HK 0x9E3779B1u
#define MAXN 1024
#define BUFSZ (1 << 20)
#define KEY(a, b, c) (((u32) (a) & 31u) | (((u32) (b) & 31u) << 5) | (((u32) (c) & 31u) << 10))

typedef uint32_t u32;
typedef uint64_t u64;
typedef u64 v4 __attribute__((vector_size(32)));

static char BUF[BUFSZ + 128] __attribute__((aligned(64)));
static u32 TAB[HS];
static u32 P0[MAXN + 2];
static u32 P1[MAXN + 2];
static v4 VAL[MAXN + 2];
static unsigned char VIS[MAXN + 2];
static u32 SN[MAXN + 2];
static u32 SPP[MAXN + 2];
static u32 NN;

static inline u32 load32(const char *p)
{
	u32 w;
	memcpy(&w, p, 4);
	return w;
}

static inline u32 keyof(const u32 w)
{
	return (w & 0x1Fu) | ((w >> 3) & 0x3E0u) | ((w >> 6) & 0x7C00u);
}

static inline void insert(const u32 key, const u32 id)
{
	u32 h = (key * HK) >> (32 - HB);
	while (TAB[h])
	{
		h = (h + 1u) & HM;
	}
	TAB[h] = (key << 16) | id;
}

static inline u32 lookup(const u32 key)
{
	u32 h = (key * HK) >> (32 - HB);
	u32 e = TAB[h];
	while (e && (e >> 16) != key)
	{
		h = (h + 1u) & HM;
		e = TAB[h];
	}
	return e & 0xFFFFu;
}

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
		if (a[0] != 0 && a[1] != 0 && a[2] != 0 && a[3] == ':')
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
	static const char *const cand[4] = {"../input.txt", "../../input.txt", "input.txt", "2025/DAY11/input.txt"};
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

static inline void add_line(const size_t s, const size_t e)
{
	if (e - s >= 4)
	{
		const u32 id = ++NN;
		insert(keyof(load32(BUF + s)), id);
		P0[id] = (u32) s + 4u;
		P1[id] = (u32) s + 4u + (((u32) (e - s) - 4u) & ~3u);
	}
}

static void index_lines(const size_t len)
{
	BUF[len] = '\n';
	size_t s = 0;
#if defined(__AVX2__)
	const __m256i nl = _mm256_set1_epi8('\n');
	for (size_t i = 0; i <= len; i += 64)
	{
		const u64 lo = (u32) _mm256_movemask_epi8(
			_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) (BUF + i)), nl));
		const u64 hi = (u32) _mm256_movemask_epi8(
			_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) (BUF + i + 32)), nl));
		u64 m = lo | (hi << 32);
		while (m)
		{
			const size_t e = i + (size_t) __builtin_ctzll(m);
			m &= m - 1;
			add_line(s, e);
			s = e + 1;
		}
	}
#else
	for (size_t e = 0; e <= len; e++)
	{
		if (BUF[e] == '\n')
		{
			add_line(s, e);
			s = e + 1;
		}
	}
#endif
	/* synthetic sink: no edges, seeded with e2 */
	const u32 out = NN + 1;
	insert(KEY('o', 'u', 't'), out);
	const v4 e2 = {0, 0, 1, 0};
	VAL[out] = e2;
	VIS[out] = 1;
}

static void dfs(const u32 root)
{
	u32 node = root;
	u32 p = P0[node];
	u32 sp = 0;
	VIS[root] = 1;
	for (;;)
	{
		if (p < P1[node])
		{
			const u32 c = lookup(keyof(load32(BUF + p + 1)));
			p += 4;
			if (VIS[c])
			{
				VAL[node] += VAL[c];
			}
			else
			{
				VIS[c] = 1;
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
			const v4 r = VAL[node];
			sp--;
			node = SN[sp];
			p = SPP[sp];
			VAL[node] += r;
		}
	}
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
	memcpy(q, "Part 2: ", 8);
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
	index_lines(len);
	const u32 root = lookup(KEY('s', 'v', 'r'));
	const u32 f = lookup(KEY('f', 'f', 't'));
	const u32 d = lookup(KEY('d', 'a', 'c'));
	if (!root || !f || !d)
	{
		print(0);
		return 0;
	}
	const v4 e0 = {1, 0, 0, 0};
	const v4 e1 = {0, 1, 0, 0};
	VAL[f] = e0;
	VAL[d] = e1;
	dfs(root);
	const u64 a = VAL[root][0] * VAL[f][1] * VAL[d][2];
	const u64 b = VAL[root][1] * VAL[d][0] * VAL[f][2];
	print(a + b);
	return 0;
}
