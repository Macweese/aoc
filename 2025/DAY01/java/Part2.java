/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Branchless linear solve, now with zero-allocation byte scanning.
// Left turns are handled in mirrored coordinates so that a zero-crossing is always
// a plain (start + distance) / 100 quotient -> one division, no floorMod, no branches.
void main(String[] ops)
{
	final byte[] b = ops[0].getBytes();
	final int n = b.length;
	int pos = 50;
	int c = 0;

	for (int i = 0; i < n; )
	{
		// 'L' (0x4C) has bit 3 set, 'R' (0x52) does not -> all-ones mask for L, 0 for R
		final int dirMask = -((b[i++] >> 3) & 1);
		int v = 0;
		for (int d; i < n && (d = b[i] - '0') >= 0; i++)
		{
			v = v * 10 + d;
		}
		i++; // skip LF

		final int mirror = ((-pos) >> 31) & (100 - pos);      // (100 - pos) % 100, branchless
		final int t = ((pos & ~dirMask) | (mirror & dirMask)) + v;
		c += t / 100;                                          // clicks landing on 0
		final int r = t % 100;
		final int back = ((-r) >> 31) & (100 - r);             // mirror back for L
		pos = (r & ~dirMask) | (back & dirMask);
	}

	IO.println("Password: " + c);
}
