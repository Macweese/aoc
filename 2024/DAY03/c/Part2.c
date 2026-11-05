/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 3 Part 2
 * same as part 1, but do() / don't() switch mul instructions on and off (initially on)
 *
 * Build:	gcc -O3 -march=native -fno-plt -o part2 Part2.c
 * Run:		./part2 path		argument that opens as a file is a PATH
 *			./part2 "<raw>"		any other argument is DATA
 *			./part2				input.txt, ../input.txt, ../../input.txt, 2024/DAY03/input.txt
 *
 * Intuition
 *
 * The lazy way to do this is a state machine walking the bytes and flipping a flag, but that
 * drags a data dependent branch back into the inner loop. There is a neater way to look at it.
 * Whether a mul counts is decided purely by the last event before it, and events are rare
 * (62 of them in 18 KB, against 672 muls). So we do not gate the muls at all. We evaluate
 * every mul as in part 1, keep a prefix sum P over the mul list in position order, and treat the
 * enable/disable machinery as a bookkeeping problem on the *complement*: the answer is
 * P[total] minus the sum of muls sitting inside disabled intervals, and a disabled interval is
 * simply the stretch between a don't() that finds the state enabled and the next do(). Each
 * such interval contributes P[r(do)] - P[r(dont)], where r(x) is the number of muls that start
 * before byte x.
 *
 * The nice part is that r(x) is O(1) with no searching. While scanning, every 64 byte block
 * already produces its 64 bit mul candidate mask, so we simply store that mask together with the
 * running candidate count before the block. Then r(x) is count_before[x>>6] plus the popcount of
 * mask[x>>6] truncated below bit (x&63), and the truncation is a single bzhi. The events
 * themselves are found by the same flatten trick as the muls: every 'd' byte is a potential event
 * start (in the real input every single one is), so a cmpeq against 'd' gives a mask, and the
 * positions are dumped into a list with unconditional stores. Each 'd' is classified by two plain
 * compares against "do()" and "don't()", and the little state machine (enabled flag, start of
 * the current disabled interval, accumulated subtraction) is written with cmov and multiplies
 * by 0/1 so it stays branch free too. A trailing disabled interval is closed against the end.
 *
 * Stage 2 for the muls is the exact same SWAR parse as part 1: eight bytes after "mul(", a
 * non digit mask via a byte >= 10 test, tzcnt for the two lengths, shifts for the zero padding,
 * one multiply by (1 + 100*2^16) to produce both numbers at once, and cmov-to-zero for validity.
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
#define DO_TEXT 0x29286F64u
#define DONT_TEXT_SHIFTED 0x292874276E6F6400ull

typedef uint32_t u32;
typedef uint64_t u64;

static char g_buffer[BUFFER_SIZE + 192] __attribute__((aligned(64)));
static u32 g_candidate_pos[MAX_CANDIDATES];
static u32 g_event_pos[BUFFER_SIZE + 64];
static u64 g_block_mask[(BUFFER_SIZE >> 6) + 2];
static u32 g_block_count_before[(BUFFER_SIZE >> 6) + 2];
static u64 g_prefix_sum[MAX_CANDIDATES + 1];

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

/* bit k set iff block[k] == 'd' */
static inline u64 d_mask(const char *block)
{
#if defined(__AVX2__)
	const __m256i is_d = _mm256_set1_epi8('d');
	const __m256i low_half = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) block), is_d);
	const __m256i high_half = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *) (block + 32)), is_d);
	return (u64) (u32) _mm256_movemask_epi8(low_half) | ((u64) (u32) _mm256_movemask_epi8(high_half) << 32);
#else
	u64 mask = 0;
	for (int k = 0; k < 64; k++)
	{
		mask |= (u64) (block[k] == 'd') << k;
	}
	return mask;
#endif
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
	u32 event_count = 0;
	for (u32 base = 0; base < (u32) length; base += 64)
	{
		u64 mask = candidate_mask(g_buffer + base);
		u64 events = d_mask(g_buffer + base);
		const u32 hits = (u32) __builtin_popcountll(mask);
		const u32 event_hits = (u32) __builtin_popcountll(events);
		g_block_mask[base >> 6] = mask;
		g_block_count_before[base >> 6] = count;
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
		g_event_pos[event_count] = base + (u32) tz(events);
		events &= events - 1;
		g_event_pos[event_count + 1] = base + (u32) tz(events);
		events &= events - 1;
		if (__builtin_expect(event_hits > 2, 0))
		{
			for (u32 k = 2; k < event_hits; k++)
			{
				g_event_pos[event_count + k] = base + (u32) tz(events);
				events &= events - 1;
			}
		}
		count += hits;
		event_count += event_hits;
	}

	u64 running = 0;
	g_prefix_sum[0] = 0;
	for (u32 j = 0; j < count; j++)
	{
		running += mul_at(g_buffer + g_candidate_pos[j]);
		g_prefix_sum[j + 1] = running;
	}

	u64 enabled = 1;
	u64 disabled_start = 0;
	u64 disabled_total = 0;
	for (u32 e = 0; e < event_count; e++)
	{
		const u32 at = g_event_pos[e];
		u32 head32;
		u64 head64;
		memcpy(&head32, g_buffer + at, 4);
		memcpy(&head64, g_buffer + at, 8);
		const u64 is_do = (head32 == DO_TEXT);
		const u64 is_dont = ((head64 << 8) == DONT_TEXT_SHIFTED);
		const u32 block = at >> 6;
		const u64 muls_before = g_block_count_before[block] + (u32) __builtin_popcountll(g_block_mask[block] & (((u64) 1 << (at & 63)) - 1));
		const u64 prefix_here = g_prefix_sum[muls_before];
		disabled_start = (is_dont & enabled) ? prefix_here : disabled_start;
		disabled_total += (prefix_here - disabled_start) * (is_do & (enabled ^ 1));
		enabled = is_do | (enabled & (is_dont ^ 1));
	}
	disabled_total += (running - disabled_start) * (enabled ^ 1);
	return running - disabled_total;
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
	memcpy(cursor, "Part 2: ", 8);
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