/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2025 Day 11 Part 1
 * number of paths you -> out in a DAG
 *
 * Build:	gcc -O3 -march=native -fno-plt -o part1 Part1.c
 * Run:		./part1 "<raw input>"
 *				arg whose 4th byte is ':' is DATA (every raw input starts "xxx:")
 *			./part1 path
 *				any other arg is a PATH
 *			./part1
 *				defaults to ../input.txt ../../input.txt ./input.txt 2025/DAY11/input.txt
 *
 * Intuition
 *  1.	P(v) = sum P(w) over successors, P(out) = 1. Exact in a DAG, Theta(V+E), which
 *		matches the Omega(input) lower bound. Answers < 2^63 (total paths ~3.4e17), no bignum
 *  2.	Lazy graph: only the line HEADS are indexed. Every line is "abc:" + (" xyz")*d, so
 *		a neighbour is one 4-byte stride and is decoded in place when its edge is traversed.
 *		Part 1 reaches 111 of 586 nodes / 239 of 1665 edges, so ~75% of the tokens are never decoded
 *		and there is no edge array, no interning pass, no CSR
 *  3.	Line boundaries: one 64-byte block per iteration (2x vpcmpeqb + movemask), bit-iterate
 *		the newline mask. Line degree = (len - 4) >> 2 (also tolerates a trailing '\r')
 *  4.	Name -> 15-bit key with 3 mask/shift ops ('a'&31 = 1..26). key -> node id through a
 *		2048-slot open-addressed u32 table (key << 16 | id): 8 KB, so first-touch
 *		page faults stay minimal versus a 35-64 KB direct table
 *  5.	Node 0 is a dead node, "out" is a synthetic node with value 1 and no edges, so the
 *		DFS has no leaf special case. memo == value != 0 (every node reaches out); a node
 *		on the DFS stack can never be queried (acyclic), so its partial sum lives in the memo slot
 *  6.	Iterative post-order DFS fused with the DP: each edge is touched once, no sort.
 *		Depth <= 8 for the real input; stack arrays are sized for the worst case
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

static char BUF[BUFSZ + 128] __attribute__((aligned(64)));
static u32 TAB[HS];
static u32 P0[MAXN + 2];
static u32 P1[MAXN + 2];
static u64 VAL[MAXN + 2];
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
	/* synthetic sink: no edges, value 1 */
	const u32 out = NN + 1;
	insert(KEY('o', 'u', 't'), out);
	VAL[out] = 1;
}

static u64 dfs(const u32 root)
{
	u32 node = root;
	u32 p = P0[node];
	u32 sp = 0;
	for (;;)
	{
		if (p < P1[node])
		{
			const u32 c = lookup(keyof(load32(BUF + p + 1)));
			const u64 v = VAL[c];
			p += 4;
			if (v)
			{
				VAL[node] += v;
			}
			else
			{
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
			const u64 r = VAL[node];
			sp--;
			node = SN[sp];
			p = SPP[sp];
			VAL[node] += r;
		}
	}
	return VAL[root];
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
	index_lines(len);
	const u32 root = lookup(KEY('y', 'o', 'u'));
	print(root ? dfs(root) : 0);
	return 0;
}
