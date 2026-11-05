/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 5 Part 1
 * sum of middle pages of the updates that already respect the ordering rules
 *
 * Build:  g++ -O3 -march=native -std=c++20 -o part1 Part1.cpp
 * Run:    ./part1 "<raw input>"     arg whose 3rd byte is '|' is DATA
 *         ./part1 path              any other arg is a PATH
 *         ./part1                   ./input.txt, ../input.txt
 *
 * Intuition
 * The rule set is not just any relation, it is a complete tournament: 49 distinct pages
 * and exactly C(49,2) = 1176 rules, one for every unordered pair. Exactly one of a|b and
 * b|a exists for any two pages. Restricted to the pages of a single update the tournament
 * is also acyclic (otherwise no valid ordering could exist), and an acyclic tournament is a
 * transitive tournament, i.e. a strict total order. That kills the usual "check every pair
 * i<j" O(n^2) test: an update is sorted iff every ADJACENT pair (a,b) satisfies a|b, since
 * transitivity gives the rest for free. So validation is n-1 single-bit probes into a
 * 100 x 128 bit matrix (1.6 KB, lives in L1), AND-ed together with no branches.
 *
 * Because everything is fixed width (two-digit pages, "NN|NN\n" is exactly 6 bytes,
 * "NN," exactly 3) there is no tokenizer at all, just stride arithmetic. The only
 * data dependent branch is the end-of-line test, one per token. The page value is
 * used directly as row / column index (10..99 < 128), so there is no compression step.
 * Middle page of a valid update is tok[n >> 1], added with a mask instead of an if.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

typedef uint64_t u64;

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
		unsigned count = 0;
		u64 sorted = 1;
		unsigned previous = page_at(cursor);
		pages[0] = previous;
		count = 1;
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
			sorted &= Successor[previous * 2 + (current >> 6)] >> (current & 63);
			previous = current;
		}
		total += (u64) pages[count >> 1] & (0 - (sorted & 1));
	}
	printf("Part 1: %llu\n", (unsigned long long) total);
	return 0;
}
