/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

private static final String RESET = "\u001B[0m";
private static final String RED = "\033[0;31m";
private static final String GREEN = "\033[0;32m";
private static final String MAGENTA = "\033[0;35m";
private static final String CYAN = "\033[0;36m";
private static final String BLACK_BRIGHT = "\033[0;90m";
private static final String GREEN_BOLD_BRIGHT = "\033[1;92m";

private static final int OFFSET = 50;
private static final int DOMAIN = 100;

private static final StringBuilder SB = new StringBuilder();

void main(String[] ops)
{
	int i = OFFSET;
	int c = 0;

	for (String op : ops[0].split("\n"))
	{
		int delta = Integer.parseInt(op.substring(1)) * (op.getBytes()[0] / 3 - 26);
		i += delta;

		int signMask = delta >> 31;
		int zeroFlag = (((i - delta) | -(i - delta)) >>> 31) ^ 1;
		int quantize = ((DOMAIN - i) / DOMAIN) - zeroFlag;
		int r = ((i / DOMAIN) & ~signMask) | (quantize & signMask);

		debug(i, delta, r, c);

		c += ((i / DOMAIN) & ~signMask) | (quantize & signMask);

		i = Math.floorMod(i, DOMAIN);
	}

	IO.println(SB);
	IO.println("Password: " + c);
}

private static void debug(int i, int delta, int r, int clicks)
{
	SB.append(String.format("%-8s", "Start:"))
		.append(String.format("%-16s", (CYAN + (i - delta) + RESET)))
		.append(String.format("%-8s", "Dest:"))
		.append(String.format("%-19s", (MAGENTA + ((i + 100) % 100) + RESET)))
		.append(String.format("%-8s", "Steps:"))
		.append(String.format("%-17s%s", (delta > 0 ? (GREEN + delta) : delta < 0 ? (RED + delta) : (BLACK_BRIGHT + delta)), RESET))
		.append(String.format("%-9s", "Clicks:"))
		.append(String.format("%-15s", (r == 0 ? (BLACK_BRIGHT + r + RESET + "   ") : (GREEN_BOLD_BRIGHT + r + RESET))))
		.append(String.format("%-16s", "Total Clicks:"))
		.append(String.format("%s%n", (r == 0 ? (BLACK_BRIGHT + clicks + RESET) : (GREEN_BOLD_BRIGHT + clicks + RESET))));
}