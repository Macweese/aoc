/*
 * Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * AOC 2024 Day 4 Part 1
 * occurrences of XMAS in a letter grid, any of 8 directions
 *
 * Build:	g++ -O3 -march=native -o part1 Part1.cpp
 * Run:		./part1 "<raw input>"     argument starting with X/M/A/S is DATA
 *			./part1 path              any other argument is a PATH
 *			./part1                   input.txt, ../input.txt, ../../input.txt, 2024/DAY04/input.txt
 *
 * Intuition
 *
 * The grid is not really two dimensional. If we keep the raw file bytes exactly as they
 * arrive, row r column c lives at r*S + c where S is the stride (width plus the line
 * terminator). Every direction is then a plain 1D step: 1 for east, S for south, S+1 for
 * south-east and S-1 for south-west, and the west/north/etc. directions are just the same
 * steps read backwards, i.e. the word SAMX going forward. The newline column is a free
 * sentinel: stepping one column at a time can never skip over it, and it is not a letter,
 * so a word that would wrap around a row edge always trips over a terminator and dies. That
 * deletes all the bounds checking, and we never touch a 2D index.
 *
 * Next we go to bitboards. One 64-byte compare per letter turns the buffer into four
 * bitsets X, M, A, S (bit p set iff byte p is that letter). Then "XMAS starts at p going
 * by d" is X[p] & M[p+d] & A[p+2d] & S[p+3d], which for the whole board at once is
 * X & (M >> d) & (A >> 2d) & (S >> 3d) with >> meaning a shift of the entire bitset by
 * d bits, and the reversed word is S & (A >> d) & (M >> 2d) & (X >> 3d). The count is the
 * popcount of the results, 4 axes times 2 orientations. The whole board is only ~20k bits,
 * so it is a few dozen 512-bit vectors; an arbitrary bit shift of a long bitset is one
 * unaligned load pair plus a funnel shift (vpshrdvq on VBMI2), and the popcount is
 * vpopcntq. Total work is a couple thousand instructions, so the run time is dominated by
 * the read syscall, which is why input is one read() into static storage (no mmap, no
 * stdio, no allocation) and the answer leaves through a single write().
 *
 * Without AVX-512 the same math runs on 64-bit words with 128-bit funnel shifts, and
 * the planes are built with AVX2 movemasks if available.
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
typedef std::uint32_t u32;
typedef u32 __attribute__((may_alias)) u32Alias;

namespace
{
	constexpr std::size_t kMaxInput = 1u << 20;
	constexpr std::size_t kPad = 4096;
	constexpr std::size_t kPlaneWords = (kMaxInput >> 6) + 512;

	alignas(64) char inputBuffer[kPad + kMaxInput + kPad];
	alignas(64) u64 planeX[kPlaneWords];
	alignas(64) u64 planeM[kPlaneWords];
	alignas(64) u64 planeA[kPlaneWords];
	alignas(64) u64 planeS[kPlaneWords];

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
		std::memcpy(cursor, "Part 1: ", 8);
		const ssize_t written = write(1, cursor, static_cast<std::size_t>(out + sizeof(out) - cursor));
		(void) written;
	}

#if defined(__AVX512BW__) && defined(__AVX512F__)
	inline __m512i fetch(const u64 *plane, const std::size_t word, const std::size_t quotient, const __m512i shift)
	{
		const __m512i low = _mm512_loadu_si512(plane + word + quotient);
		const __m512i high = _mm512_loadu_si512(plane + word + quotient + 1);
#if defined(__AVX512VBMI2__)
		return _mm512_shrdv_epi64(low, high, shift);
#else
		const __m512i complement = _mm512_sub_epi64(_mm512_set1_epi64(64), shift);
		return _mm512_or_si512(_mm512_srlv_epi64(low, shift), _mm512_sllv_epi64(high, complement));
#endif
	}

	inline __m512i countBits(const __m512i value)
	{
#if defined(__AVX512VPOPCNTDQ__)
		return _mm512_popcnt_epi64(value);
#else
		alignas(64) u64 lanes[8];
		_mm512_store_si512(lanes, value);
		u64 sum = 0;
		for (int i = 0; i < 8; i++)
		{
			sum += static_cast<u64>(__builtin_popcountll(lanes[i]));
		}
		return _mm512_set_epi64(0, 0, 0, 0, 0, 0, 0, static_cast<long long>(sum));
#endif
	}

	u64 solve(const char *base, const std::size_t length, const std::size_t stride)
	{
		const std::size_t blocks = (length + 63) >> 6;
		const __m512i letterX = _mm512_set1_epi8('X');
		const __m512i letterM = _mm512_set1_epi8('M');
		const __m512i letterA = _mm512_set1_epi8('A');
		const __m512i letterS = _mm512_set1_epi8('S');
		for (std::size_t i = 0; i < blocks; i++)
		{
			const __m512i chunk = _mm512_load_si512(base + 64 * i);
			planeX[i] = _mm512_cmpeq_epi8_mask(chunk, letterX);
			planeM[i] = _mm512_cmpeq_epi8_mask(chunk, letterM);
			planeA[i] = _mm512_cmpeq_epi8_mask(chunk, letterA);
			planeS[i] = _mm512_cmpeq_epi8_mask(chunk, letterS);
		}

		const u64 steps[4] = {1, stride - 1, stride, stride + 1};
		std::size_t quotient[4][4];
		__m512i shift[4][4];
		for (int axis = 0; axis < 4; axis++)
		{
			for (int mult = 1; mult <= 3; mult++)
			{
				const u64 distance = steps[axis] * static_cast<u64>(mult);
				quotient[axis][mult] = static_cast<std::size_t>(distance >> 6);
				shift[axis][mult] = _mm512_set1_epi64(static_cast<long long>(distance & 63));
			}
		}

		__m512i total = _mm512_setzero_si512();
		const std::size_t vectors = (blocks + 7) >> 3;
		for (std::size_t v = 0; v < vectors; v++)
		{
			const std::size_t word = v << 3;
			const __m512i headX = _mm512_loadu_si512(planeX + word);
			const __m512i headS = _mm512_loadu_si512(planeS + word);
			for (int axis = 0; axis < 4; axis++)
			{
				const __m512i m1 = fetch(planeM, word, quotient[axis][1], shift[axis][1]);
				const __m512i a2 = fetch(planeA, word, quotient[axis][2], shift[axis][2]);
				const __m512i s3 = fetch(planeS, word, quotient[axis][3], shift[axis][3]);
				const __m512i forward = _mm512_and_si512(_mm512_and_si512(headX, m1), _mm512_and_si512(a2, s3));
				const __m512i a1 = fetch(planeA, word, quotient[axis][1], shift[axis][1]);
				const __m512i m2 = fetch(planeM, word, quotient[axis][2], shift[axis][2]);
				const __m512i x3 = fetch(planeX, word, quotient[axis][3], shift[axis][3]);
				const __m512i backward = _mm512_and_si512(_mm512_and_si512(headS, a1), _mm512_and_si512(m2, x3));
				total = _mm512_add_epi64(total, countBits(forward));
				total = _mm512_add_epi64(total, countBits(backward));
			}
		}
		return static_cast<u64>(_mm512_reduce_add_epi64(total));
	}
#else
	inline u64 fetchWord(const u64 *plane, const std::size_t word, const std::size_t quotient, const unsigned remainder)
	{
		const unsigned __int128 pair = (static_cast<unsigned __int128>(plane[word + quotient + 1]) << 64) | plane[word + quotient];
		return static_cast<u64>(pair >> remainder);
	}

	u64 solve(const char *base, const std::size_t length, const std::size_t stride)
	{
#if defined(__AVX2__)
		const std::size_t halves = (length + 31) >> 5;
		const __m256i letterX = _mm256_set1_epi8('X');
		const __m256i letterM = _mm256_set1_epi8('M');
		const __m256i letterA = _mm256_set1_epi8('A');
		const __m256i letterS = _mm256_set1_epi8('S');
		u32Alias *outX = reinterpret_cast<u32Alias *>(planeX);
		u32Alias *outM = reinterpret_cast<u32Alias *>(planeM);
		u32Alias *outA = reinterpret_cast<u32Alias *>(planeA);
		u32Alias *outS = reinterpret_cast<u32Alias *>(planeS);
		for (std::size_t i = 0; i < halves; i++)
		{
			const __m256i chunk = _mm256_load_si256(reinterpret_cast<const __m256i *>(base + 32 * i));
			outX[i] = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, letterX)));
			outM[i] = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, letterM)));
			outA[i] = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, letterA)));
			outS[i] = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, letterS)));
		}
#else
		for (std::size_t i = 0; i < length; i++)
		{
			const char letter = base[i];
			const std::size_t word = i >> 6;
			const unsigned bit = static_cast<unsigned>(i & 63);
			planeX[word] |= static_cast<u64>(letter == 'X') << bit;
			planeM[word] |= static_cast<u64>(letter == 'M') << bit;
			planeA[word] |= static_cast<u64>(letter == 'A') << bit;
			planeS[word] |= static_cast<u64>(letter == 'S') << bit;
		}
#endif
		const std::size_t blocks = (length + 63) >> 6;
		const u64 steps[4] = {1, stride - 1, stride, stride + 1};
		u64 total = 0;
		for (int axis = 0; axis < 4; axis++)
		{
			const std::size_t quotient1 = static_cast<std::size_t>(steps[axis] >> 6);
			const std::size_t quotient2 = static_cast<std::size_t>((steps[axis] * 2) >> 6);
			const std::size_t quotient3 = static_cast<std::size_t>((steps[axis] * 3) >> 6);
			const unsigned remainder1 = static_cast<unsigned>(steps[axis] & 63);
			const unsigned remainder2 = static_cast<unsigned>((steps[axis] * 2) & 63);
			const unsigned remainder3 = static_cast<unsigned>((steps[axis] * 3) & 63);
			for (std::size_t word = 0; word < blocks; word++)
			{
				const u64 forward = planeX[word] & fetchWord(planeM, word, quotient1, remainder1) & fetchWord(planeA, word, quotient2, remainder2) & fetchWord(planeS, word, quotient3, remainder3);
				const u64 backward = planeS[word] & fetchWord(planeA, word, quotient1, remainder1) & fetchWord(planeM, word, quotient2, remainder2) & fetchWord(planeX, word, quotient3, remainder3);
				total += static_cast<u64>(__builtin_popcountll(forward)) + static_cast<u64>(__builtin_popcountll(backward));
			}
		}
		return total;
	}
#endif
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
	std::size_t stride = length + 1;
	if (newline != nullptr)
	{
		stride = static_cast<std::size_t>(static_cast<const char *>(newline) - base) + 1;
	}
	emit(solve(base, length, stride));
	return 0;
}