/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 1 Part 2
 * similarity score: sum over left values a of a * (occurrences of a in the right column)
 *
 * Intuition
 * answer = sum_v v * cntL(v) * cntR(v): a multiset-intersection weighted by value, so no
 * ordering is needed => O(n) expected, no sort, no merge
 * A direct-address count table over [10^4, 10^5) is 90-200 KB: 1000 random touches hit
 * every page, ~25-50 first-touch page faults (~0.25 us each) cost more than all the
 * arithmetic. Instead: 2048-slot open-addressed u32 table = 8 KB = 2 pages, L1-resident
 * Slot = (key << 15) | count, key < 2^17, count < 2^15, 0 = empty (keys are never 0).
 * Insert is branch-free apart from the (rare) probe walk:  T[h] = e + 1 + (e ? 0 : key << 15)
 * Lookup contributes  v * (e & 0x7FFF): an empty slot has count 0, so the miss/hit branch
 * vanishes and the only branch is the collision walk (load factor <= 0.33 here: 671/2048)
 * Fixed 14-byte "ddddd   ddddd\n" stride + SWAR 5 digit parse (see Part1.c). Left values are
 * parked in an array in the same pass that builds the table from the right column
 * One read(), no stdio, write(2) output, -static
 *
 * NOTE: limits are MAXN = 4096 lines (count fits 15 bits), keys in [1, 2^17)
 */
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define MAXN 4096
#define BUFSZ (1 << 16)
#define HASH_CONSTANT 0x9E3779B1u

typedef uint32_t u32;
typedef uint64_t u64;

static char inputBuffer[BUFSZ + 64] __attribute__((aligned(64)));
static u32 LA[MAXN] __attribute__((aligned(64)));
static u32 HT[2048] __attribute__((aligned(64)));

static size_t read_file(const char *path)
{
	const int fileDescriptor = open(path, O_RDONLY);
	if (fileDescriptor < 0)
	{
		return (size_t) -1;
	}
	const ssize_t bytesRead = read(fileDescriptor, inputBuffer, BUFSZ);
	close(fileDescriptor);
	return bytesRead < 0 ? (size_t) -1 : (size_t) bytesRead;
}

static size_t getInput(const int argc, char **argv)
{
	if (argc > 1)
	{
		const char *argument = argv[1];
		if ((unsigned) ((unsigned char) argument[0] - '0') <= 9u)
		{
			size_t length = strlen(argument);
			if (length > BUFSZ)
			{
				length = BUFSZ;
			}
			memcpy(inputBuffer, argument, length);
			return length;
		}
		return read_file(argument);
	}
	static const char *const candidates[2] = {"../input.txt", "input.txt"};
	for (int i = 0; i < 2; i++)
	{
		const size_t length = read_file(candidates[i]);
		if (length != (size_t) -1)
		{
			return length;
		}
	}
	return (size_t) -1;
}

static inline u32 parse_five_digits(const char *pointer)
{
	u64 x;
	memcpy(&x, pointer, 8);
	x = (x & 0x0F0F0F0F0FULL) << 24;
	x = (x * 2561) >> 8;
	x = ((x & 0x00FF00FF00FF00FFULL) * 6553601) >> 16;
	x = ((x & 0x0000FFFF0000FFFFULL) * 42949672960001ULL) >> 32;
	return (u32) x;
}

static void emitResult(u64 resultValue)
{
	char outputBuffer[48];
	char *writePointer = outputBuffer + sizeof(outputBuffer);
	*--writePointer = '\n';
	do
	{
		*--writePointer = (char) ('0' + resultValue % 10);
		resultValue /= 10;
	} while (resultValue);
	writePointer -= 8;
	memcpy(writePointer, "Part 2: ", 8);
	const ssize_t bytesWritten = write(1, writePointer, (size_t) (outputBuffer + sizeof(outputBuffer) - writePointer));
	(void) bytesWritten;
}

int main(const int argc, char **argv)
{
	const size_t inputLength = getInput(argc, argv);
	if (inputLength == (size_t) -1)
	{
		const ssize_t bytesWritten = write(2, "no input\n", 9);
		(void) bytesWritten;
		return 1;
	}
	const u32 lineCount = (u32) ((inputLength + 1) / 14);

	const char *pointer = inputBuffer;
	for (u32 i = 0; i < lineCount; i++)
	{
		const u32 lVal = parse_five_digits(pointer);
		const u32 rVal = parse_five_digits(pointer + 8);
		LA[i] = lVal;
		u32 hashIndex = (rVal * HASH_CONSTANT) >> 21;
		u32 entry = HT[hashIndex];
		while (entry != 0 && (entry >> 15) != rVal)
		{
			hashIndex = (hashIndex + 1) & 2047;
			entry = HT[hashIndex];
		}
		HT[hashIndex] = entry + 1 + (entry ? 0 : (rVal << 15));
		pointer += 14;
	}

	u64 result = 0;
	for (u32 i = 0; i < lineCount; i++)
	{
		const u32 value = LA[i];
		u32 hashIndex = (value * HASH_CONSTANT) >> 21;
		u32 entry = HT[hashIndex];
		while (entry != 0 && (entry >> 15) != value)
		{
			hashIndex = (hashIndex + 1) & 2047;
			entry = HT[hashIndex];
		}
		result += (u64) value * (entry & 0x7FFFu);
	}
	emitResult(result);
	return 0;
}
