/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 5 Part 2
 * sum of middle pages AFTER sorting the updates that were out of order
 *
 * Build:  g++ -O3 -march=native -std=c++20 -o part2 Part2.cpp
 * Run:    ./part2 "<raw input>"     arg whose 3rd byte is '|' is DATA
 *         ./part2 path              any other arg is a PATH
 *         ./part2                   ./input.txt, ../input.txt
 *
 * Intuition
 * Same structural fact as part 1: the rules form a complete tournament, and inside one
 * update it is a transitive tournament, i.e. a strict total order. Sorting is therefore
 * pure overkill, we never need the permutation, only the median of it. In a total order the
 * page at sorted position k has exactly n-1-k pages after it, and "pages after p" is
 * exactly succ(p) intersected with the update's page set M. So the median is the unique page
 * p with |succ(p) & M| = (n-1)/2 = n >> 1 (odd n). One popcount per page, no comparisons
 * between pages, no sort, no topological order, no graph. That is O(n) per update
 * instead of O(n log n) comparison sorting, and it is branch-free apart from the loop
 * control: the hit is folded in with a compare mask.
 *
 * Pages are 10..99 so a 128-bit set is enough with the page value as its own bit index
 * (no compression); succ(p) is two u64 words. M is built while parsing with a single
 * 128-bit shift-or per token. Validity is the part 1 adjacent-pair test (transitivity),
 * done branch-free in the same pass; the popcount pass only runs on the invalid updates.
 * Input is fixed width ("NN|NN\n" = 6 bytes, "NN," = 3), so parsing is stride arithmetic.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

typedef uint64_t u64;
typedef unsigned __int128 u128;

static char Buffer[1 << 16] __attribute__((aligned(64)));
static u64 Successor[200] __attribute__((aligned(64)));

static long slurp(const char *path)
{
	const int descriptor = open(path, O_RDONLY);
	if (descriptor < 0)
	{
		return -1;
	}
	long total = 0;
	long got = 0;
	while ((got = read(descriptor, Buffer + total, sizeof(Buffer) - 16 - total)) > 0)
	{
		total += got;
	}
	close(descriptor);
	return total;
}

static long get_input(const int argc, char **argv)
{
	if (argc > 1)
	{
		if (argv[1][0] != 0 && argv[1][1] != 0 && argv[1][2] == '|')
		{
			long length = (long) strlen(argv[1]);
			if (length > (long) sizeof(Buffer) - 16)
			{
				length = (long) sizeof(Buffer) - 16;
			}
			memcpy(Buffer, argv[1], (size_t) length);
			return length;
		}
		return slurp(argv[1]);
	}
	static const char *const candidates[2] = {"input.txt", "../input.txt"};
	for (int i = 0; i < 2; i++)
	{
		const long length = slurp(candidates[i]);
		if (length >= 0)
		{
			return length;
		}
	}
	return -1;
}

static inline unsigned page_at(const char *text)
{
	return (unsigned) (unsigned char) text[0] * 10u + (unsigned) (unsigned char) text[1] - 528u;
}

int main(const int argc, char **argv)
{
	const long length = get_input(argc, argv);
	if (length < 0)
	{
		return 1;
	}
	Buffer[length] = '\n';
	const char *cursor = Buffer;
	const char *const end = Buffer + length;

	while (*cursor != '\n')
	{
		const unsigned before = page_at(cursor);
		const unsigned after = page_at(cursor + 3);
		Successor[before * 2 + (after >> 6)] |= 1ULL << (after & 63);
		cursor += 6;
	}
	cursor++;

	unsigned pages[32];
	u64 total = 0;
	while (cursor < end)
	{
		unsigned previous = page_at(cursor);
		unsigned count = 1;
		u64 sorted = 1;
		u128 members = (u128) 1 << previous;
		pages[0] = previous;
		for (;;)
		{
			const char terminator = cursor[2];
			cursor += 3;
			if (terminator == '\n')
			{
				break;
			}
			const unsigned current = page_at(cursor);
			pages[count++] = current;
			members |= (u128) 1 << current;
			sorted &= Successor[previous * 2 + (current >> 6)] >> (current & 63);
			previous = current;
		}
		if (sorted & 1)
		{
			continue;
		}
		const u64 memberLow = (u64) members;
		const u64 memberHigh = (u64) (members >> 64);
		const unsigned half = count >> 1;
		for (unsigned i = 0; i < count; i++)
		{
			const unsigned page = pages[i];
			const unsigned after = (unsigned) __builtin_popcountll(Successor[page * 2] & memberLow)
				+ (unsigned) __builtin_popcountll(Successor[page * 2 + 1] & memberHigh);
			total += (u64) page & (0 - (u64) (after == half));
		}
	}
	printf("Part 2: %llu\n", (unsigned long long) total);
	return 0;
}
