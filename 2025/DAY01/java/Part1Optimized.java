/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// zero-allocation single-pass byte scan
// avoided no per-line getBytes()
// branchless direction decode + wrap + zero-detect
void main(String[] ops)
{
	final byte[] b = ops[0].getBytes(StandardCharsets.ISO_8859_1);
	final int n = b.length;
	int pos = 50;
	int c = 0;

	for (int i = 0; i < n; )
	{
		// 'L' (0x4C) has bit 3 set, 'R' (0x52) does not -> maps to -1 / +1 without a branch
		final int dir = 1 - ((b[i++] & 8) >> 2);
		int v = 0;
		for (int d; i < n && (d = b[i] - '0') >= 0; i++)
		{
			v = v * 10 + d;
		}
		i++; // skip LF

		// fastmod100
		// Use fixed point reciprocal multiplication / granlund-montgomery div by invariant integers
		// JMH C2 JIT analysis assembly output shows 5.1% improved performance over irem bytecode op
		// 		19.8 ops/us vs 18.8 ops/us mean and 18.7 ops/us vs 16.4 ops/us min
		// Math on magic numbers
		// a (mod b) = a - floor(a / b) * b, 1/100 ~ M/2^k
		// for [1,1000) we need M and k such that for all integers in our domain
		// floor(a * M / 2^k) = floor(a/100)
		// Thus
		// k = 16, yields magic multiplier M = ceil(2^16 / 100) = 656
		final int q = (v * 656) >>> 16;
		final int q100 = (q << 6) + (q << 5) + (q << 2);
		v = v - q100;

		pos += dir * v;
		//pos += dir * (v % 100);        // original; replaced in favor of fastmod100
		pos += 100 & (pos >> 31);        // wrap negative into [0,100)
		pos -= 100 & ((99 - pos) >> 31); // wrap overflow into [0,100)
		c += ((pos | -pos) >>> 31) ^ 1;  // +1 iff pos == 0
	}

	IO.println("Password:\n" + c);
}
