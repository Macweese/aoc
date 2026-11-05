/*
 * 	Copyright (C) 2024 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package com.macweese.adventOfCode.y2024.DAY01.java;

import com.macweese.utils.FastIO;

// AOC 2024 Day 1 Part 2
public class Part2
{
	private static final int[] TAB = new int[2048];

	public static void main(String[] args)
	{
		FastIO io = new FastIO(Part2.class.getResourceAsStream("../input.txt"), new Sink(System.out));

		final byte[] b = io.readFileBytes();
		final int n = (b.length + 1) / 14;
		final int[] la = new int[n];

		int p = 0;
		for (int i = 0; i < n; i++)
		{
			la[i] = b[p] * 10000 + b[p + 1] * 1000 + b[p + 2] * 100 + b[p + 3] * 10 + b[p + 4] - 533328;
			final int y = b[p + 8] * 10000 + b[p + 9] * 1000 + b[p + 10] * 100 + b[p + 11] * 10 + b[p + 12] - 533328;
			int h = y * 0x9E3779B1 >>> 21;
			int e = TAB[h];
			while (e != 0 && e >>> 15 != y)
			{
				h = h + 1 & 2047;
				e = TAB[h];
			}
			TAB[h] = e + 1 + (e == 0 ? y << 15 : 0);
			p += 14;
		}

		long sum = 0L;
		for (int i = 0; i < n; i++)
		{
			final int v = la[i];
			int h = v * 0x9E3779B1 >>> 21;
			int e = TAB[h];
			while (e != 0 && e >>> 15 != v)
			{
				h = h + 1 & 2047;
				e = TAB[h];
			}
			sum += (long) v * (e & 0x7FFF);
		}

		io.println("Result: " + sum);
		io.close();
	}
}
