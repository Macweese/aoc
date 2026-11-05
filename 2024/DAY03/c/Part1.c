/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 3 Part 1
 * sum of mul(X,Y) over all well-formed instructions, X and Y being 1-3 digit numbers
 *
 * Build:	gcc -O3 -march=native -fno-plt -o part1 Part1.c
 *			AVX2 + BMI1/2 + POPCNT fast path, plain scalar fallback otherwise
 * Run:		./part1 path		argument that opens as a file is a PATH
 *			./part1 "<raw>"		any other argument is DATA
 *			./part1				input.txt, ../input.txt, ../../input.txt, 2024/DAY03/input.txt
 *
 * Intuition
 *
 * The floor for this problem is a single pass over the bytes, so the game is really about
 * what each byte costs. A regex or a hand written state machine pays a branch per byte, and
 * since roughly one in five 'm' characters turns out to be a dead end (mul(4*, mul[3,7] and
 * friends) those branches mispredict at a rate that dominates everything else. So the plan is
 * to split the work into two stages that are both essentially branch free
 *
 * The first stage only finds candidates. For every 64 byte block we compare against 'm' and
 * against '(' three bytes further along, AND the two, and squeeze the result into a 64 bit
 * mask. Testing just two of the four bytes of "mul(" is deliberate: the false positive rate
 * is negligible and the remaining two bytes get verified for free later with one dword
 * compare. The hits are then flattened into a plain u32 position list using four
 * unconditional stores and a popcount advance (the simdjson trick), so the roughly 2.4 hits
 * per block cost no branches at all. The lone branch, popcount > 4, is a cold path
 *
 * The second stage parses each candidate and it is where the mathematics lives. Everything
 * after "mul(" fits in eight bytes, because the longest legal tail is "123,456)". Load them as
 * one u64 and xor with 0x30 per byte so that digits become their values 0..9 and everything
 * else lands at 10 or more. A SWAR "byte >= 10" test then marks the non digit bytes, and
 * tzcnt of that mask is 8*len1+7 for the first non digit, i.e. the length of the first number,
 * while tzcnt after clearing the lowest set bit gives the position of the second. Validity is
 * just: both lengths in 1..3, a comma at the first stop, a ')' at the second, and the "mul("
 * prefix. Each check is a cmp plus cmov-to-zero, so a bad candidate simply contributes 0
 *
 * For the actual number crunching, shifts do the leading zero padding for us: the first number
 * is pushed to the top of a 32 bit lane and the second to the top of the upper lane, giving
 * four digit bytes each. Then y*10 + (y>>8) folds neighbouring digits into 2-digit values in
 * bytes 0,2,4,6 (nothing exceeds 99, so no carries can leak), masking with 0x00FF00FF.. keeps
 * them, and a single multiply by (1 + 100*2^16) places 100*hi + lo at bit 16 and bit 48, which
 * are exactly X and Y. The product X*Y is one imul. No division, no digit loop, no libc
 *
 * NOTE: input up to 1 MiB
 */
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#ifdef BENCH
#include <stdio.h>
#include <time.h>
#endif
#if defined(__AVX2__)
#include <immintrin.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif
#define BUFFER_SIZE (1 << 20)
#define MAX_CANDIDATES ((BUFFER_SIZE >> 2) + 64)
#define MUL_PREFIX 0x286C756Du

typedef uint32_t u32;
typedef uint64_t u64;

static char g_buffer[BUFFER_SIZE + 192] __attribute__((aligned(64)));
static u32 g_candidate_pos[MAX_CANDIDATES];

static long read_file(const char *path)
{
	const int fd = open(path, O_RDONLY | O_BINARY);
	if (fd < 0)
	{
		return -1;
	}
	long got = 0;
	long chunk;
	while ((chunk = read(fd, g_buffer + got, BUFFER_SIZE - got)) > 0)
	{
		got += chunk;
	}
	close(fd);
	return got;
}

static long get_input(const int argc, char **argv)
{
	if (argc > 1)
	{
		const long length = read_file(argv[1]);
		if (length >= 0)
		{
			return length;
		}
		size_t raw_length = strlen(argv[1]);
		if (raw_length > BUFFER_SIZE)
		{
			raw_length = BUFFER_SIZE;
		}
		memcpy(g_buffer, argv[1], raw_length);
		return (long) raw_length;
	}
	static const char *const fallback[4] = {"input.txt", "../input.txt", "../../input.txt", "2024/DAY03/input.txt"};
	for (int i = 0; i < 4; i++)
	{
		const long length = read_file(fallback[i]);
		if (length >= 0)
		{
			return length;
		}
	}
	return -1;
}

static inline u64 tz(const u64 value)
{
	return value ? (u64) __builtin_ctzll(value) : 64;
}

/* bit k set iff block[k] == 'm' && block[k+3] == '(' */
static inline u64 candidate_mask(const char *block)
{
#if defined(__AVX2__)
	const __m256i is_m = _mm256_set1_epi8('m');
	const __m256i is_paren = _mm256_set1_epi8('(');
	const __m256i low_half = _mm256_and_si256(
		_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) block), is_m),
		_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) (block + 3)), is_paren));
	const __m256i high_half = _mm256_and_si256(
		_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) (block + 32)), is_m),
		_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) (block + 35)), is_paren));
	return (u64) (u32) _mm256_movemask_epi8(low_half) | ((u64) (u32) _mm256_movemask_epi8(high_half) << 32);
#else
	u64 mask = 0;
	for (int k = 0; k < 64; k++)
	{
		mask |= (u64) (block[k] == 'm' && block[k + 3] == '(') << k;
	}
	return mask;
#endif
}

/* start points at the 'm'. Returns X*Y for a well formed mul(X,Y), 0 otherwise. Branch free */
static inline u64 mul_at(const char *start)
{
	u32 prefix;
	u64 tail;
	memcpy(&prefix, start, 4);
	memcpy(&tail, start + 4, 8);
	const u64 digit_values = tail ^ 0x3030303030303030ull;
	const u64 nondigit_flags = ((digit_values + 0x7676767676767676ull) | digit_values) & 0x8080808080808080ull;
	const u64 first_len_bits = tz(nondigit_flags) & ~7ull;
	const u64 second_stop_bits = tz(nondigit_flags & (nondigit_flags - 1)) & ~7ull;
	const u64 first_lane = (u64) ((u32) digit_values << ((32 - first_len_bits) & 31));
	const u64 second_lane = (digit_values >> ((first_len_bits + 8) & 63)) << ((first_len_bits + 8 - second_stop_bits) & 63);
	const u64 lanes = first_lane | second_lane;
	u64 pairs = ((lanes * 10) + (lanes >> 8)) & 0x00FF00FF00FF00FFull;
	pairs *= 0x640001ull;
	u64 product = ((pairs >> 16) & 0xFFFF) * (pairs >> 48);
	product = (prefix != MUL_PREFIX) ? 0 : product;
	product = ((first_len_bits - 8) > 16) ? 0 : product;
	product = ((second_stop_bits - first_len_bits - 16) > 16) ? 0 : product;
	product = (((tail >> (first_len_bits & 63)) & 0xFF) != ',') ? 0 : product;
	product = (((tail >> (second_stop_bits & 63)) & 0xFF) != ')') ? 0 : product;
	return product;
}

static u64 solve(const long length)
{
	u32 count = 0;
	for (u32 base = 0; base < (u32) length; base += 64)
	{
		u64 mask = candidate_mask(g_buffer + base);
		const u32 hits = (u32) __builtin_popcountll(mask);
		g_candidate_pos[count] = base + (u32) tz(mask);
		mask &= mask - 1;
		g_candidate_pos[count + 1] = base + (u32) tz(mask);
		mask &= mask - 1;
		g_candidate_pos[count + 2] = base + (u32) tz(mask);
		mask &= mask - 1;
		g_candidate_pos[count + 3] = base + (u32) tz(mask);
		mask &= mask - 1;
		if (__builtin_expect(hits > 4, 0))
		{
			for (u32 k = 4; k < hits; k++)
			{
				g_candidate_pos[count + k] = base + (u32) tz(mask);
				mask &= mask - 1;
			}
		}
		count += hits;
	}
	u64 sum = 0;
	for (u32 j = 0; j < count; j++)
	{
		sum += mul_at(g_buffer + g_candidate_pos[j]);
	}
	return sum;
}

static void print(u64 value)
{
	char out[48];
	char *cursor = out + sizeof(out);
	*--cursor = '\n';
	do
	{
		*--cursor = (char) ('0' + value % 10);
		value /= 10;
	} while (value);
	cursor -= 8;
	memcpy(cursor, "Part 1: ", 8);
	const ssize_t written = write(1, cursor, (size_t) (out + sizeof(out) - cursor));
	(void) written;
}

int main(const int argc, char **argv)
{
	const long length = get_input(argc, argv);
	if (length < 0)
	{
		const ssize_t written = write(2, "no input\n", 9);
		(void) written;
		return 1;
	}
#ifdef BENCH
	{
		u64 checksum = 0;
		struct timespec begin;
		struct timespec end;
		clock_gettime(CLOCK_MONOTONIC, &begin);
		for (int rep = 0; rep < 200000; rep++)
		{
			checksum += solve(length);
		}
		clock_gettime(CLOCK_MONOTONIC, &end);
		fprintf(stderr, "%.0f ns/solve (%llu)\n",
		        ((end.tv_sec - begin.tv_sec) * 1e9 + (end.tv_nsec - begin.tv_nsec)) / 200000.0,
		        (unsigned long long) checksum);
	}
#endif
	print(solve(length));
	return 0;
}