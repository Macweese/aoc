/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import java.nio.ByteOrder;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.Arrays;
import jdk.incubator.vector.LongVector;
import jdk.incubator.vector.VectorSpecies;
import jdk.incubator.vector.VectorOperators;

// Vectorized (SIMD-across-words) version of the bit-sliced neighbor-erosion solver
//
// jdk.incubator.vector is an incubator module (stable API, not yet a permanent
// module) ; both javac and java need --add-modules jdk.incubator.vector until
// it graduates. On an AVX-512 host SPECIES_PREFERRED resolves to 8 lanes and the
// JIT emits vpxorq/vpandq/vpopcntq etc. directly; on AVX2/NEON/scalar hosts the
// same source recompiles itself down to 4/2/1 lanes with no code changes needed
//
// Optimized (Vector API, AVX-512-wide bit-sliced adder) +
// a bit-set replacing the boolean[] dirty-row trackers,
//
// The System.arraycopy row-snapshot was deliberately LEFT AS-IS. A fused
// version that filled the snapshot progressively inside the SIMD loop instead
// of via one bulk arraycopy was implemented and measured 15-23% SLOWER on a
// 2000x2000-4000x4000 sweep: System.arraycopy lowers to an C-intrinsic memmove
// that beats interleaving many small vector stores into the compute loop
// ;; trusting measurement over instruction-counting arguments
//
// Compile:  javac --add-modules jdk.incubator.vector Part2Optimized.java
// Run:      java  --add-modules jdk.incubator.vector -XX:+UseParallelGC \
// 					-XX:+AlwaysPreTouch -XX:+UseTransparentHugePages \
// 					-Xmx<size> Part2Optimized "<input>"
public class Part2Optimized
{
	private static final byte LF = 10;
	private static final VarHandle LONGS =
		MethodHandles.byteArrayViewVarHandle(long[].class, ByteOrder.LITTLE_ENDIAN);

	// Preferred species adapts to the host at class-init time: 8 lanes on AVX-512,
	// 4 on AVX2, 2 on NEON/SSE, 1 (pure scalar fallback) anywhere else
	private static final VectorSpecies<Long> SPECIES = LongVector.SPECIES_PREFERRED;
	private static final int VLEN = SPECIES.length();

	static long run(byte[] data)
	{
		final int dataSize = data.length;
		int X = 0;
		while (X < dataSize && data[X] != LF)
		{
			X++;
		}
		final int Y = (dataSize + 1) / (X + 1);

		final int words = (X + 63) >>> 6;
		// Round the row width up to a whole number of SIMD chunks so the hot loop
		// never needs a scalar remainder / masked tail
		final int paddedWords = ((words + VLEN - 1) / VLEN) * VLEN;
		// OFF = VLEN sentinel words before the row, VLEN sentinel words after it
		// This guarantees every unaligned "neighbor" vector load (x-1 .. x+paddedWords)
		// stays fully inside the allocated buffer, so no bounds checks / branches
		// are ever needed inside the SIMD loop
		final int OFF = VLEN;
		final int stride = paddedWords + 2 * VLEN;
		final long[] g = new long[(Y + 2) * stride];

		for (int y = 0; y < Y; y++)
		{
			final int in = y * (X + 1);
			final int out = (y + 1) * stride + OFF;
			int x = 0;
			for (; x + 8 <= X; x += 8)
			{
				final long v = (((long) LONGS.get(data, in + x)) >>> 6) & 0x0101010101010101L;
				g[out + (x >>> 6)] |= ((v * 0x0102040810204080L) >>> 56) << (x & 63);
			}
			for (; x < X; x++)
			{
				g[out + (x >>> 6)] |= ((long) ((data[in + x] >>> 6) & 1)) << (x & 63);
			}
		}

		long[] prevOrig = new long[stride];
		long[] currOrig = new long[stride];

		final int dirtyWords = ((Y + 2) + 63) >>> 6;
		long[] dirty = new long[dirtyWords];
		long[] nextDirty = new long[dirtyWords];
		Arrays.fill(dirty, -1L);

		long totalDeletions = 0L;
		boolean running = true;

		final LongVector ZERO = LongVector.zero(SPECIES);

		while (running)
		{
			running = false;
			boolean prevProcessed = false;

			for (int y = 1; y <= Y; y++)
			{
				if (!(bit(dirty, y - 1) | bit(dirty, y) | bit(dirty, y + 1)))
				{
					prevProcessed = false;
					continue;
				}

				final int base = y * stride;
				System.arraycopy(g, base, currOrig, 0, stride);

				final long[] north = prevProcessed ? prevOrig : g;
				final int nOff = prevProcessed ? 0 : base - stride;
				final int sOff = base + stride;
				boolean rowChanged = false;

				for (int x = OFF; x < OFF + paddedWords; x += VLEN)
				{

					final LongVector selfV = LongVector.fromArray(SPECIES, currOrig, x);
					final LongVector nRowV = LongVector.fromArray(SPECIES, north, nOff + x);
					final LongVector sRowV = LongVector.fromArray(SPECIES, g, sOff + x);

					// Cross-word carries: an offset (unaligned) vector load at x-1/x+1
					// lines each lane up with exactly the neighbor word it needs
					// cirvcumvent shuffle/permute instruction requirement
					final LongVector w = selfV.lanewise(VectorOperators.LSHL, 1).or(LongVector.fromArray(SPECIES, currOrig, x - 1).lanewise(VectorOperators.LSHR, 63));
					final LongVector e = selfV.lanewise(VectorOperators.LSHR, 1).or(LongVector.fromArray(SPECIES, currOrig, x + 1).lanewise(VectorOperators.LSHL, 63));
					final LongVector nw = nRowV.lanewise(VectorOperators.LSHL, 1).or(LongVector.fromArray(SPECIES, north, nOff + x - 1).lanewise(VectorOperators.LSHR, 63));
					final LongVector ne = nRowV.lanewise(VectorOperators.LSHR, 1).or(LongVector.fromArray(SPECIES, north, nOff + x + 1).lanewise(VectorOperators.LSHL, 63));
					final LongVector sw = sRowV.lanewise(VectorOperators.LSHL, 1).or(LongVector.fromArray(SPECIES, g, sOff + x - 1).lanewise(VectorOperators.LSHR, 63));
					final LongVector se = sRowV.lanewise(VectorOperators.LSHR, 1).or(LongVector.fromArray(SPECIES, g, sOff + x + 1).lanewise(VectorOperators.LSHL, 63));

					// Bit-sliced saturating 3-bit adder, 8 (or more) words wide at once
					LongVector b0 = nRowV.lanewise(VectorOperators.XOR, sRowV);
					LongVector b1 = nRowV.and(sRowV);
					LongVector b2 = ZERO;
					LongVector c0, c1;
					c0 = b0.and(e);  b0 = b0.lanewise(VectorOperators.XOR, e);  c1 = b1.and(c0); b1 = b1.lanewise(VectorOperators.XOR, c0); b2 = b2.or(c1);
					c0 = b0.and(w);  b0 = b0.lanewise(VectorOperators.XOR, w);  c1 = b1.and(c0); b1 = b1.lanewise(VectorOperators.XOR, c0); b2 = b2.or(c1);
					c0 = b0.and(ne); b0 = b0.lanewise(VectorOperators.XOR, ne); c1 = b1.and(c0); b1 = b1.lanewise(VectorOperators.XOR, c0); b2 = b2.or(c1);
					c0 = b0.and(nw); b0 = b0.lanewise(VectorOperators.XOR, nw); c1 = b1.and(c0); b1 = b1.lanewise(VectorOperators.XOR, c0); b2 = b2.or(c1);
					c0 = b0.and(se); b0 = b0.lanewise(VectorOperators.XOR, se); c1 = b1.and(c0); b1 = b1.lanewise(VectorOperators.XOR, c0); b2 = b2.or(c1);
					c0 = b0.and(sw); b0 = b0.lanewise(VectorOperators.XOR, sw); c1 = b1.and(c0); b1 = b1.lanewise(VectorOperators.XOR, c0); b2 = b2.or(c1);

					final LongVector deletedV = selfV.and(b2.not());

					// Onne shot reduction ; indicator whether *any* of the VLEN
					// words changed ; avoid per-lane branch
					if (deletedV.reduceLanes(VectorOperators.OR) != 0L)
					{
						selfV.and(b2).intoArray(g, base + x);
						totalDeletions += deletedV.lanewise(VectorOperators.BIT_COUNT)
							.reduceLanes(VectorOperators.ADD);
						rowChanged = true;
					}
				}

				if (rowChanged)
				{
					setBit(nextDirty, y);
				}
				running |= rowChanged;

				final long[] temp = prevOrig;
				prevOrig = currOrig;
				currOrig = temp;
				prevProcessed = true;
			}

			final long[] temp = dirty;
			dirty = nextDirty;
			nextDirty = temp;
			Arrays.fill(nextDirty, 0L);
		}

		return totalDeletions;
	}

	private static boolean bit(long[] bits, int i)
	{
		return ((bits[i >>> 6] >>> (i & 63)) & 1L) != 0L;
	}

	private static void setBit(long[] bits, int i)
	{
		bits[i >>> 6] |= 1L << (i & 63);
	}

	public static void main(String[] args)
	{
		if (args.length == 0)
		{
			System.out.println("\033[1;91mNo input data provided - aborting\u001B[0m");
			return;
		}
		System.out.println("Result: " + run(args[0].getBytes()));
	}
}