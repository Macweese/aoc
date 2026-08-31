/*
 * Copyright (C) 2025 Macweese <https://www.github.com/Macweese> <hugo.git@pm.me> <hugo.n@linux.com>
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__AVX512F__) || defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(_WIN32)
#include <malloc.h> // _aligned_malloc / _aligned_free
#else
#include <cstdlib> // aligned_alloc / free
#endif

namespace tachyon
{
	// The allocator and deallocator MUST be a matched pair: _aligned_malloc's
	// block carries extra bookkeeping (the original, pre-alignment pointer)
	// immediately before the address it returns, and only _aligned_free knows
	// how to walk back to it. Freeing _aligned_malloc block with plain
	// free() -- or vice versa -- corrupts the heap on win systems
	// (Windows will eventually report this as STATUS_HEAP_CORRUPTION, 0xC0000374)
	struct aligned_deleter
	{
		void operator()(uint64_t *ptr) const noexcept
		{
#if defined(_WIN32)
			_aligned_free(ptr);
#else
			free(ptr);
#endif
		}
	};

	using aligned_buffer = std::unique_ptr<uint64_t[], aligned_deleter>;

	aligned_buffer make_aligned_buffer(const size_t size)
	{
		constexpr size_t alignment = 64;
		const size_t bytes = size * sizeof(uint64_t);
		// aligned_alloc's contract requires size to be a multiple of the
		// alignment; round up to be safe on both allocators
		size_t aligned_bytes = (bytes + alignment - 1) & ~(alignment - 1);

#if defined(_WIN32)
		void *ptr = _aligned_malloc(aligned_bytes, alignment);
#else
		void *ptr = aligned_alloc(alignment, aligned_bytes);
#endif
		if (!ptr)
		{
			throw std::bad_alloc();
		}
		// One-time zero
		// This is the ONLY zeroing this buffer ever needs:
		//		see the invariant argument at the memset removal site below
		std::memset(ptr, 0, aligned_bytes);
		return aligned_buffer(static_cast<uint64_t *>(ptr));
	}

	struct solution_result
	{
		uint64_t part1_splits{0};
		uint64_t part2_timelines{0};
	};

	class solver
	{
	public:
		static solution_result process_manifold(std::string_view input)
		{
			solution_result result{};
			if (input.empty())
			{
				return result;
			}

			const size_t width = input.find_first_of("\r\n");
			if (width == std::string_view::npos)
			{
				return result;
			}

			size_t stride = width;
			if (stride < input.size() && input[stride] == '\r')
			{
				stride++;
			}
			if (stride < input.size() && input[stride] == '\n')
			{
				stride++;
			}

			size_t height = input.size() / stride;
			if (input.size() % stride != 0 && input.back() != '\n')
			{
				height++;
			}

			const size_t start_col = input.find('S');
			if (start_col == std::string_view::npos)
			{
				return result;
			}

			auto N_curr_buf = make_aligned_buffer(width);
			auto N_next_buf = make_aligned_buffer(width);

			// Raw, restrict-qualified pointers: tell compiler N_curr/N_next/row_ptr
			// never alias each other, helps scalar remainder loops
			// auto-vectorize and intrinsics code schedule more freely
			uint64_t *__restrict N_curr = N_curr_buf.get();
			uint64_t *__restrict N_next = N_next_buf.get();

			size_t min_x = start_col;
			size_t max_x = start_col;
			N_curr[min_x] = 1;

			uint64_t exit_total = 0;

			for (size_t y = 1; y < height; ++y)
			{
				const size_t row_offset = y * stride;
				if (row_offset >= input.size())
				{
					break;
				}
				const char *__restrict row_ptr = input.data() + row_offset;

				// Skip rows with no splitter inside the active range (blank rows)
				if (std::memchr(row_ptr + min_x, '^', max_x - min_x + 1) == nullptr)
				{
					continue;
				}

				// Prefetch next row we'll actually need (2 rows, since every
				// other row is a blank separator) to hide memory latency on large inputs
				const size_t next_row_offset = row_offset + 2 * stride;
				if (next_row_offset < input.size())
				{
					__builtin_prefetch(input.data() + next_row_offset + min_x, 0, 1);
				}

				const size_t start_c = (min_x > 0) ? min_x - 1 : 0;
				const size_t end_c = (max_x + 1 < width) ? max_x + 1 : width - 1;

				// NOTE: No memset of N_next here
				//
				// Every cell in [start_c, end_c] below is written via direct
				// assignment (scalar `N_next[c] = next;` or vector `store`),
				// never accumulated, so nothing in that range needs pre-zeroing
				//
				// Cells OUTSIDE [start_c, end_c] must read as zero.
				// They do, for free: active window [min_x, max_x] is
				// monotonically non-shrinking across active rows
				//		a splitter at column c can only ever deposit mass at
				//		c-1/c+1, and pass-through never changes the window
				// so N_next's underlying buffer == last used as N_next two
				// active rows ago -- had a window that is a strict subset of the
				// current one. Anything outside that old window was already zero
				// (ultimately from the one-time memset in make_aligned_buffer)
				// and never touched since.
				// Removing per-row O(width) memset turns O(height * width)
				// memory-traffic term into O(height * active_range),
				// which matters whenever the beam cone is narrower than the
				// manifold's full width

				size_t new_min = width;
				size_t new_max = 0;

				// Handle exits at left and right boundaries
				if (row_ptr[0] == '^')
				{
					exit_total += N_curr[0];
				}
				if (row_ptr[width - 1] == '^')
				{
					exit_total += N_curr[width - 1];
				}

				// Left edge ; c = 0
				if (start_c == 0)
				{
					const size_t c = 0;
					uint64_t next = 0;
					if (row_ptr[c] != '^')
					{
						next += N_curr[c];
					}
					if (c + 1 < width && row_ptr[c + 1] == '^')
					{
						next += N_curr[c + 1];
					}
					N_next[c] = next;
					if (next > 0)
					{
						if (c < new_min)
						{
							new_min = c;
						}
						if (c > new_max)
						{
							new_max = c;
						}
					}
					if (N_curr[c] > 0 && row_ptr[c] == '^')
					{
						result.part1_splits++;
					}
				}

				// Right edge ; c = width - 1
				if (end_c == width - 1)
				{
					const size_t c = width - 1;
					uint64_t next = 0;
					if (row_ptr[c] != '^')
					{
						next += N_curr[c];
					}
					if (c > 0 && row_ptr[c - 1] == '^')
					{
						next += N_curr[c - 1];
					}
					N_next[c] = next;
					if (next > 0)
					{
						if (c < new_min)
						{
							new_min = c;
						}
						if (c > new_max)
						{
							new_max = c;
						}
					}
					if (N_curr[c] > 0 && row_ptr[c] == '^')
					{
						result.part1_splits++;
					}
				}

				// interior col
				const size_t interior_start = (start_c > 1) ? start_c : 1;
				const size_t interior_end = (end_c < width - 1) ? end_c : width - 2;

				if (interior_start <= interior_end)
				{
					size_t i = interior_start;

#if defined(__AVX512F__)
					const __m512i v_caret = _mm512_set1_epi64('^');
					const __m512i v_zero = _mm512_setzero_si512();

					size_t vec_end = (interior_end >= 7) ? interior_end - 7 : 0;
					if (vec_end > width - 9)
					{
						vec_end = width - 9;
					}

					for (; i <= vec_end; i += 8)
					{
						__m512i v_mid = _mm512_loadu_si512((const void *) &N_curr[i]);
						__m512i v_left = _mm512_loadu_si512((const void *) &N_curr[i - 1]);
						__m512i v_right = _mm512_loadu_si512((const void *) &N_curr[i + 1]);

						__m128i c_mid = _mm_loadl_epi64((const __m128i *) (row_ptr + i));
						__m128i c_left = _mm_loadl_epi64((const __m128i *) (row_ptr + i - 1));
						__m128i c_right = _mm_loadl_epi64((const __m128i *) (row_ptr + i + 1));

						__m512i v_char_mid = _mm512_cvtepu8_epi64(c_mid);
						__m512i v_char_left = _mm512_cvtepu8_epi64(c_left);
						__m512i v_char_right = _mm512_cvtepu8_epi64(c_right);

						__mmask8 eq_mid = _mm512_cmpeq_epi64_mask(v_char_mid, v_caret);
						__mmask8 eq_left = _mm512_cmpeq_epi64_mask(v_char_left, v_caret);
						__mmask8 eq_right = _mm512_cmpeq_epi64_mask(v_char_right, v_caret);

						__m512i v_stay = _mm512_maskz_mov_epi64(~eq_mid, v_mid);
						__m512i v_from_left = _mm512_maskz_mov_epi64(eq_left, v_left);
						__m512i v_from_right = _mm512_maskz_mov_epi64(eq_right, v_right);

						__m512i v_next = _mm512_add_epi64(_mm512_add_epi64(v_stay, v_from_left), v_from_right);

						_mm512_storeu_si512((void *) &N_next[i], v_next);

						__mmask8 nz_mid = _mm512_cmpneq_epi64_mask(v_mid, v_zero);
						__mmask8 split_mask = eq_mid & nz_mid;
						result.part1_splits += __builtin_popcount(split_mask);

						__mmask8 nz_next = _mm512_cmpneq_epi64_mask(v_next, v_zero);
						if (nz_next)
						{
							int first = __builtin_ctz(nz_next);
							int last = 31 - __builtin_clz(nz_next);
							if (i + first < new_min)
							{
								new_min = i + first;
							}
							if (i + last > new_max)
							{
								new_max = i + last;
							}
						}
					}
#elif defined(__AVX2__)
					const __m256i v_caret = _mm256_set1_epi64x('^');
					const __m256i v_zero = _mm256_setzero_si256();
					const __m256i v_all_ones = _mm256_set1_epi64x(-1);

					size_t vec_end = (interior_end >= 3) ? interior_end - 3 : 0;
					if (vec_end > width - 5)
					{
						vec_end = width - 5;
					}

					for (; i <= vec_end; i += 4)
					{
						__m256i v_mid = _mm256_loadu_si256((const __m256i *) &N_curr[i]);
						__m256i v_left = _mm256_loadu_si256((const __m256i *) &N_curr[i - 1]);
						__m256i v_right = _mm256_loadu_si256((const __m256i *) &N_curr[i + 1]);

						uint32_t c_mid = *reinterpret_cast<const uint32_t *>(row_ptr + i);
						uint32_t c_left = *reinterpret_cast<const uint32_t *>(row_ptr + i - 1);
						uint32_t c_right = *reinterpret_cast<const uint32_t *>(row_ptr + i + 1);

						__m256i v_char_mid = _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(static_cast<int>(c_mid)));
						__m256i v_char_left = _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(static_cast<int>(c_left)));
						__m256i v_char_right = _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(static_cast<int>(c_right)));

						__m256i eq_mid = _mm256_cmpeq_epi64(v_char_mid, v_caret);
						__m256i eq_left = _mm256_cmpeq_epi64(v_char_left, v_caret);
						__m256i eq_right = _mm256_cmpeq_epi64(v_char_right, v_caret);

						__m256i v_stay = _mm256_andnot_si256(eq_mid, v_mid);
						__m256i v_from_left = _mm256_and_si256(eq_left, v_left);
						__m256i v_from_right = _mm256_and_si256(eq_right, v_right);

						__m256i v_next = _mm256_add_epi64(_mm256_add_epi64(v_stay, v_from_left), v_from_right);

						_mm256_storeu_si256((__m256i *) &N_next[i], v_next);

						__m256i v_nonzero = _mm256_cmpeq_epi64(v_mid, v_zero);
						v_nonzero = _mm256_andnot_si256(v_nonzero, v_all_ones);
						__m256i v_split = _mm256_and_si256(eq_mid, v_nonzero);
						int split_mask = _mm256_movemask_pd(_mm256_castsi256_pd(v_split)) & 0xF;
						result.part1_splits += __builtin_popcount(split_mask);

						__m256i v_next_zero = _mm256_cmpeq_epi64(v_next, v_zero);
						int nz_mask = (~_mm256_movemask_pd(_mm256_castsi256_pd(v_next_zero))) & 0xF;
						if (nz_mask)
						{
							int first = __builtin_ctz(nz_mask);
							int last = 31 - __builtin_clz(nz_mask);
							if (i + first < new_min)
							{
								new_min = i + first;
							}
							if (i + last > new_max)
							{
								new_max = i + last;
							}
						}
					}
#endif

					// interior scalar remainder
					for (; i <= interior_end; ++i)
					{
						uint64_t next = 0;
						if (row_ptr[i] != '^')
						{
							next += N_curr[i];
						}
						if (row_ptr[i - 1] == '^')
						{
							next += N_curr[i - 1];
						}
						if (row_ptr[i + 1] == '^')
						{
							next += N_curr[i + 1];
						}
						N_next[i] = next;
						if (next > 0)
						{
							if (i < new_min)
							{
								new_min = i;
							}
							if (i > new_max)
							{
								new_max = i;
							}
						}
						if (N_curr[i] > 0 && row_ptr[i] == '^')
						{
							result.part1_splits++;
						}
					}
				}

				N_curr_buf.swap(N_next_buf);
				N_curr = N_curr_buf.get();
				N_next = N_next_buf.get();
				min_x = new_min;
				max_x = new_max;
				if (min_x > max_x)
				{
					break;
				}
			}

			uint64_t part2 = exit_total;
			for (size_t c = min_x; c <= max_x; ++c)
			{
				part2 += N_curr[c];
			}
			result.part2_timelines = part2;
			return result;
		}
	};
} // NAMESPACE TACHYON

int main(const int argc, char **argv)
{
	try
	{
		// default to "input.txt" in pwd
		// explicit path can still be passed as argv[1] to override
		const char *path = (argc >= 2) ? argv[1] : "input.txt";

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			std::cerr << "Failed to open file: " << path << "\n";
			return 1;
		}

		// read whole file in one shot
		// avoid per-char overhead of istreambuf_iterator for what can be a large manifold
		file.seekg(0, std::ios::end);
		const std::streamoff size = file.tellg();
		if (size < 0)
		{
			std::cerr << "Failed to determine size of file: " << path << "\n";
			return 1;
		}
		file.seekg(0, std::ios::beg);

		std::string input(static_cast<size_t>(size), '\0');
		if (size > 0 && !file.read(input.data(), size))
		{
			std::cerr << "Failed to read file: " << path << "\n";
			return 1;
		}

		auto [p1, p2] = tachyon::solver::process_manifold(
			std::string_view(input.data(), input.size()));

		std::cout << "Part 1 - Total Splits: " << p1 << "\n";
		std::cout << "Part 2 - Total Timelines: " << p2 << "\n";
	}
	catch (const std::exception &ex)
	{
		std::cerr << "Error: " << ex.what() << "\n";
		return 1;
	}
	return 0;
}
