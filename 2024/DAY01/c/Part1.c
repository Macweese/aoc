/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 1 Part 1
 * sum of |a_(i) - b_(i)| over the two independently sorted columns
 *
 * Intuition
 * Rearrangement inequality: sorted pairing minimises sum |a-b| (exchange argument, any
 * convex cost). Equivalently the 1-D Wasserstein-1 distance. The sort is unavoidable,
 * but the keys are 17-bit integers => O(n) LSD radix, no comparison sort
 * Every line is exactly "ddddd   ddddd\n": fixed 14-byte stride, no tokenising, no
 * branches in the parser. n = (len + 1) / 14 works with or without the final '\n'
 * 5 ASCII digits parsed SWAR: one unaligned u64 load, keep the 5 digit nibbles, shift them
 * to the top 5 bytes (low 3 bytes = zero = leading zeros of an 8 digit number), then the
 * classic 3-multiply 8-digit reduction
 * Key < 2^17 => passes of 9 + 8 bits. Both columns and BOTH digit histograms are
 * built during the parse (no separate counting pass), the two columns are scattered in the
 * same loop for ILP, ping-pong buffers A -> T -> A
 * |d| reduction over two sorted arrays is a plain loop, vectorised by the compiler (vpabsd)
 * Process cost dominates: one read() (regular file => no EOF read), no libc stdio, write(2) out,
 * -static removes the dynamic loader
 *
 * NOTE: limits are MAXN = 4096 lines, keys < 2^17
 */
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define MAXN 4096
#define BUFSZ (1 << 16)

typedef uint32_t u32;
typedef uint64_t u64;

static char INPUT_BUFFER[BUFSZ + 64] __attribute__((aligned(64)));
static u32 LA[MAXN] __attribute__((aligned(64)));
static u32 RA[MAXN] __attribute__((aligned(64)));
static u32 LT[MAXN] __attribute__((aligned(64)));
static u32 RT[MAXN] __attribute__((aligned(64)));
static u32 L_HIST_LO[512];
static u32 L_HIST_HI[256];
static u32 R_HIST_LO[512];
static u32 R_HIST_HI[256];

static size_t read_file(const char *path)
{
	const int fileDescriptor = open(path, O_RDONLY);
	if (fileDescriptor < 0)
	{
		return (size_t) -1;
	}
	const ssize_t bytesRead = read(fileDescriptor, INPUT_BUFFER, BUFSZ);
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
			memcpy(INPUT_BUFFER, argument, length);
			return length;
		}
		return read_file(argument);
	}
	static const char *const candidates[2] = {"../input.txt", "input.txt"};
	for (int index = 0; index < 2; index++)
	{
		const size_t length = read_file(candidates[index]);
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

static void print(u64 value)
{
	char outputBuffer[48];
	char *writePointer = outputBuffer + sizeof(outputBuffer);
	*--writePointer = '\n';
	do
	{
		*--writePointer = (char) ('0' + value % 10);
		value /= 10;
	} while (value);
	writePointer -= 8;
	memcpy(writePointer, "Part 1: ", 8);
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
	const u32 lines = (u32) ((inputLength + 1) / 14);

	const char *pointer = INPUT_BUFFER;
	for (u32 index = 0; index < lines; index++)
	{
		const u32 lVal = parse_five_digits(pointer);
		const u32 rVal = parse_five_digits(pointer + 8);
		LA[index] = lVal;
		RA[index] = rVal;
		L_HIST_LO[lVal & 511]++;
		L_HIST_HI[lVal >> 9]++;
		R_HIST_LO[rVal & 511]++;
		R_HIST_HI[rVal >> 9]++;
		pointer += 14;
	}

	u32 lPrefixSum = 0;
	u32 rPrefixSum = 0;
	for (int bucket = 0; bucket < 512; bucket++)
	{
		const u32 lCount = L_HIST_LO[bucket];
		const u32 rCount = R_HIST_LO[bucket];
		L_HIST_LO[bucket] = lPrefixSum;
		R_HIST_LO[bucket] = rPrefixSum;
		lPrefixSum += lCount;
		rPrefixSum += rCount;
	}
	lPrefixSum = 0;
	rPrefixSum = 0;
	for (int bucket = 0; bucket < 256; bucket++)
	{
		const u32 lCount = L_HIST_HI[bucket];
		const u32 rCount = R_HIST_HI[bucket];
		L_HIST_HI[bucket] = lPrefixSum;
		R_HIST_HI[bucket] = rPrefixSum;
		lPrefixSum += lCount;
		rPrefixSum += rCount;
	}

	for (u32 index = 0; index < lines; index++)
	{
		const u32 lVal = LA[index];
		const u32 rVal = RA[index];
		LT[L_HIST_LO[lVal & 511]++] = lVal;
		RT[R_HIST_LO[rVal & 511]++] = rVal;
	}
	for (u32 index = 0; index < lines; index++)
	{
		const u32 lVal = LT[index];
		const u32 rVal = RT[index];
		LA[L_HIST_HI[lVal >> 9]++] = lVal;
		RA[R_HIST_HI[rVal >> 9]++] = rVal;
	}

	u32 result = 0;
	for (u32 index = 0; index < lines; index++)
	{
		const int delta = (int) LA[index] - (int) RA[index];
		result += (u32) (delta < 0 ? -delta : delta);
	}
	print(result);
	return 0;
}
