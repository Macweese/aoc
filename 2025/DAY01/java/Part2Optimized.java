/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Branchless linear solve, zero-allocation byte scanning
// Left turns are handled in mirrored coordinates so that a zero-crossing is
// always a plain (start + distance) / 100 quotient -> single op
void main(String[] ops)
{
	final byte[] b = ops[0].getBytes();
	final int n = b.length;
	int pos = 50;
	int c = 0;

	for (int i = 0; i < n; )
	{
		// 'L' (0x4C) has bit 3 set
		// 'R' (0x52) does not
		// -> all-ones mask for L, 0 for R
		final int signMask = -((b[i++] >> 3) & 1);
		int v = 0;
		for (int d; i < n && (d = b[i] - '0') >= 0; i++)
		{
			v = v * 10 + d;
		}
		i++; // skip LF

		final int mirror = ((-pos) >> 31) & (100 - pos);
		final int t = ((pos & ~signMask) | (mirror & signMask)) + v;

		// fastMod
		// Use fixed point reciprocal multiplication / granlund-montgomery div by invariant integers
		// JMH C2 JIT analysis assembly output shows 5.1% improved performance over irem bytecode op
		// 		19.8 ops/us vs 18.8 ops/us mean and 18.7 ops/us vs 16.4 ops/us min
		// Math on magic numbers
		// a (mod b) = a - floor(a / b) * b, 1/100 ~ M/2^k
		// For the input domain [1,1000) needs M and k such that for all integers in the domain
		// floor(a * M / 2^k) = floor(a/100)
		// Thus
		// k = 16, yields magic multiplier M = ceil(2^16 / 100) = 656
		final int q = (t * 656) >>> 16;
		final int q100 = (q << 6) + (q << 5) + (q << 2);
		c += q;
		final int r = t - q100;
		final int negate = ((-r) >> 31) & (100 - r); // mirror negate dir
		pos = (r & ~signMask) | (negate & signMask);
	}

	IO.println("Password: " + c);
}