/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 4 Part 2
 * count of X-MAS crosses: two MAS diagonals (either direction) sharing the middle A
 *
 * Build:	g++ -O3 -march=native -o part2 Part2.cpp
 * Run:		./part2 "<raw input>"     argument starting with X/M/A/S is DATA
 *			./part2 path              any other argument is a PATH
 *			./part2                   input.txt, ../input.txt, ../../input.txt, 2024/DAY04/input.txt
 *
 * Intuition
 *
 * Same trick as part 1: never leave the raw byte buffer. Row r, column c is byte
 * r*S + c with S the stride, so the four diagonal neighbours of an A at p are
 * p-S-1, p+S+1 (main diagonal) and p-S+1, p+S-1 (anti diagonal). The newline column
 * doubles as a sentinel: an A on an edge column steps onto a terminator (or into the
 * zero padding around the buffer) on at least one of its diagonals, and a terminator
 * is never M or S, so no edge masking exists anywhere.
 *
 * The cross condition per diagonal is "the two ends are {M,S} in some order". That
 * looks like a two-way disjunction, but over the bytes that can appear here (X M A S
 * LF CR NUL) the XOR of two of them is 0x1E if and only if they are exactly M and S,
 * since 'M' = 0x4D and 'S' = 0x53, and a quick enumeration of all 21 unordered pairs
 * shows no other pair hits that value (equal bytes give 0, everything else lands
 * elsewhere). So the whole cross test collapses to
 *
 *     byte[p] == 'A'  &&  (byte[p-S-1] ^ byte[p+S+1]) == 0x1E  &&  (byte[p-S+1] ^ byte[p+S-1]) == 0x1E
 *
 * which is three byte compares in a row with no branches and no lookups. With AVX-512BW
 * the compare results live in mask registers and the second and third compares are
 * masked by the previous one, so a 64-byte block costs five loads, two xors, three
 * compares and one popcount, and the ~20 KB board is about 310 such blocks. The buffer
 * is surrounded by zero padding (wider than any diagonal step), which also makes the
 * unused tail of the last block harmless: NUL is not 'A', and NUL never xors to 0x1E
 * against a letter. Input is one read() into static storage and the answer one write().
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(__AVX512BW__) || defined(__AVX2__)
#include <immintrin.h>
#endif

typedef std::uint64_t u64;

namespace
{
	constexpr std::size_t kMaxInput = 1u << 20;
	constexpr std::size_t kPad = 4096;

	alignas(64) char inputBuffer[kPad + kMaxInput + kPad];

	std::size_t readAll(const int fd, char *destination)
	{
		std::size_t total = 0;
		for (;;)
		{
			const ssize_t got = read(fd, destination + total, kMaxInput - total);
			if (got <= 0)
			{
				break;
			}
			total += static_cast<std::size_t>(got);
		}
		return total;
	}

	std::size_t loadInput(const int argc, char **argv, char *destination)
	{
		if (argc > 1)
		{
			const char first = argv[1][0];
			if (first == 'X' || first == 'M' || first == 'A' || first == 'S')
			{
				std::size_t length = std::strlen(argv[1]);
				if (length > kMaxInput)
				{
					length = kMaxInput;
				}
				std::memcpy(destination, argv[1], length);
				return length;
			}
			const int fd = open(argv[1], O_RDONLY);
			if (fd < 0)
			{
				return static_cast<std::size_t>(-1);
			}
			const std::size_t length = readAll(fd, destination);
			close(fd);
			return length;
		}
		static const char *const candidates[4] = {"input.txt", "../input.txt", "../../input.txt", "2024/DAY04/input.txt"};
		for (int i = 0; i < 4; i++)
		{
			const int fd = open(candidates[i], O_RDONLY);
			if (fd >= 0)
			{
				const std::size_t length = readAll(fd, destination);
				close(fd);
				return length;
			}
		}
		return static_cast<std::size_t>(-1);
	}

	void emit(u64 value)
	{
		char out[48];
		char *cursor = out + sizeof(out);
		*--cursor = '\n';
		do
		{
			*--cursor = static_cast<char>('0' + value % 10);
			value /= 10;
		} while (value != 0);
		cursor -= 8;
		std::memcpy(cursor, "Part 2: ", 8);
		const ssize_t written = write(1, cursor, static_cast<std::size_t>(out + sizeof(out) - cursor));
		(void) written;
	}

	u64 solve(const char *base, const std::size_t length, const std::ptrdiff_t stride)
	{
		u64 total = 0;
#if defined(__AVX512BW__)
		const __m512i letterA = _mm512_set1_epi8('A');
		const __m512i pairXor = _mm512_set1_epi8(0x1E);
		for (std::size_t p = 0; p < length; p += 64)
		{
			const __m512i center = _mm512_load_si512(base + p);
			const __m512i upLeft = _mm512_loadu_si512(base + p - stride - 1);
			const __m512i downRight = _mm512_loadu_si512(base + p + stride + 1);
			const __m512i upRight = _mm512_loadu_si512(base + p - stride + 1);
			const __m512i downLeft = _mm512_loadu_si512(base + p + stride - 1);
			__mmask64 hits = _mm512_cmpeq_epi8_mask(center, letterA);
			hits = _mm512_mask_cmpeq_epi8_mask(hits, _mm512_xor_si512(upLeft, downRight), pairXor);
			hits = _mm512_mask_cmpeq_epi8_mask(hits, _mm512_xor_si512(upRight, downLeft), pairXor);
			total += static_cast<u64>(__builtin_popcountll(hits));
		}
#elif defined(__AVX2__)
		const __m256i letterA = _mm256_set1_epi8('A');
		const __m256i pairXor = _mm256_set1_epi8(0x1E);
		for (std::size_t p = 0; p < length; p += 32)
		{
			const __m256i center = _mm256_load_si256(reinterpret_cast<const __m256i *>(base + p));
			const __m256i upLeft = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(base + p - stride - 1));
			const __m256i downRight = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(base + p + stride + 1));
			const __m256i upRight = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(base + p - stride + 1));
			const __m256i downLeft = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(base + p + stride - 1));
			const __m256i isCenter = _mm256_cmpeq_epi8(center, letterA);
			const __m256i mainOk = _mm256_cmpeq_epi8(_mm256_xor_si256(upLeft, downRight), pairXor);
			const __m256i antiOk = _mm256_cmpeq_epi8(_mm256_xor_si256(upRight, downLeft), pairXor);
			const __m256i hits = _mm256_and_si256(isCenter, _mm256_and_si256(mainOk, antiOk));
			total += static_cast<u64>(__builtin_popcount(static_cast<unsigned>(_mm256_movemask_epi8(hits))));
		}
#else
		for (std::size_t p = 0; p < length; p++)
		{
			const unsigned char mainEnds = static_cast<unsigned char>(base[p - stride - 1] ^ base[p + stride + 1]);
			const unsigned char antiEnds = static_cast<unsigned char>(base[p - stride + 1] ^ base[p + stride - 1]);
			total += static_cast<u64>((base[p] == 'A') & (mainEnds == 0x1E) & (antiEnds == 0x1E));
		}
#endif
		return total;
	}
}

int main(const int argc, char **argv)
{
	char *const base = inputBuffer + kPad;
	const std::size_t length = loadInput(argc, argv, base);
	if (length == static_cast<std::size_t>(-1))
	{
		const ssize_t written = write(2, "no input\n", 9);
		(void) written;
		return 1;
	}
	if (length == 0)
	{
		emit(0);
		return 0;
	}
	const void *newline = std::memchr(base, '\n', length);
	std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(length) + 1;
	if (newline != nullptr)
	{
		stride = static_cast<const char *>(newline) - base + 1;
	}
	emit(solve(base, length, stride));
	return 0;
}