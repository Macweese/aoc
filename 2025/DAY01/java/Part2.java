/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final int DOMAIN = 100;

void main(String[] ops)
{
	int i = 50;
	int c = 0;

	for (String op : ops[0].split("\n"))
	{
		int delta = Integer.parseInt(op.substring(1)) * (op.getBytes()[0] / 3 - 26);
		i += delta;

		int signMask = delta >> 31;
		int zeroFlag = (((i - delta) | -(i - delta)) >>> 31) ^ 1;
		int quantize = ((DOMAIN - i) / DOMAIN) - zeroFlag;

		c += ((i / DOMAIN) & ~signMask) | (quantize & signMask);

		i = Math.floorMod(i, DOMAIN);
	}

	IO.println("Password: " + c);
}
