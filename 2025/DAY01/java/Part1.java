/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

void main(String[] ops)
{
	int i = 50;
	int c = 0;

	for (var op : ops[0].split("\n"))
	{
		i = (i + (Integer.parseInt(op.substring(1)) * (op.getBytes()[0] / 3 - 26))) % 100;
		c += ((i | -i) >> 31) + 1;
	}
	IO.println("Password:\n" + c);
}
