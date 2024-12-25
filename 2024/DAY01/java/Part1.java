/*
 * 	Copyright (C) 2024 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package com.macweese.adventOfCode.y2024.DAY01.java;

import com.macweese.utils.FastIO;

// AOC 2024 Day 1 Part 1
public class Part1
{
	private static final int[] LEFT_HIST_LO = new int[512];
	private static final int[] LEFT_HIST_HI = new int[256];
	private static final int[] RIGHT_HIST_LO = new int[512];
	private static final int[] RIGHT_HIST_HI = new int[256];

	public static void main(String[] args)
	{
		FastIO io = new FastIO(Part1.class.getResourceAsStream("../input.txt"), new Sink(System.out));

		final byte[] bytes = io.readFileBytes();
		final int lines = (bytes.length + 1) / 14;
		final int[] leftArray = new int[lines];
		final int[] rightArray = new int[lines];
		final int[] leftTemp = new int[lines];
		final int[] rightTemp = new int[lines];

		int pos = 0;
		for (int i = 0; i < lines; i++)
		{
			final int leftValue = bytes[pos] * 10000 + bytes[pos + 1] * 1000 + bytes[pos + 2] * 100 + bytes[pos + 3] * 10 + bytes[pos + 4] - 533328;
			final int rightValue = bytes[pos + 8] * 10000 + bytes[pos + 9] * 1000 + bytes[pos + 10] * 100 + bytes[pos + 11] * 10 + bytes[pos + 12] - 533328;
			leftArray[i] = leftValue;
			rightArray[i] = rightValue;
			LEFT_HIST_LO[leftValue & 511]++;
			LEFT_HIST_HI[leftValue >>> 9]++;
			RIGHT_HIST_LO[rightValue & 511]++;
			RIGHT_HIST_HI[rightValue >>> 9]++;
			pos += 14;
		}

		int leftPrefixSum = 0;
		int rightPrefixSum = 0;
		for (int bucket = 0; bucket < 512; bucket++)
		{
			final int leftCount = LEFT_HIST_LO[bucket];
			final int rightCount = RIGHT_HIST_LO[bucket];
			LEFT_HIST_LO[bucket] = leftPrefixSum;
			RIGHT_HIST_LO[bucket] = rightPrefixSum;
			leftPrefixSum += leftCount;
			rightPrefixSum += rightCount;
		}
		leftPrefixSum = 0;
		rightPrefixSum = 0;
		for (int bucket = 0; bucket < 256; bucket++)
		{
			final int leftCount = LEFT_HIST_HI[bucket];
			final int rightCount = RIGHT_HIST_HI[bucket];
			LEFT_HIST_HI[bucket] = leftPrefixSum;
			RIGHT_HIST_HI[bucket] = rightPrefixSum;
			leftPrefixSum += leftCount;
			rightPrefixSum += rightCount;
		}

		for (int i = 0; i < lines; i++)
		{
			final int leftValue = leftArray[i];
			final int rightValue = rightArray[i];
			leftTemp[LEFT_HIST_LO[leftValue & 511]++] = leftValue;
			rightTemp[RIGHT_HIST_LO[rightValue & 511]++] = rightValue;
		}
		for (int i = 0; i < lines; i++)
		{
			final int leftValue = leftTemp[i];
			final int rightValue = rightTemp[i];
			leftArray[LEFT_HIST_HI[leftValue >>> 9]++] = leftValue;
			rightArray[RIGHT_HIST_HI[rightValue >>> 9]++] = rightValue;
		}

		int result = 0;
		for (int i = 0; i < lines; i++)
		{
			result += Math.abs(leftArray[i] - rightArray[i]);
		}

		io.println("Result: " + result);
		io.close();
	}
}
