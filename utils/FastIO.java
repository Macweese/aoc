/*
 * 	Copyright (C) 2025 Macweese <https://www.github.com/Macweese>
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.Writer;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.InputMismatchException;

/**
 * TODO:
 * 		headroom improvements
 * 		- nextDouble; manual fixed/scientific-notation parser
 * 			(parse integer + fractional digit runs, combine via power-of-10 table)
 * 			~~ cost risk correctness on edge cases
 * 		- wider SIMD via vector api jdk.incubator.vector / or stabilized successor
 * 		- memory mapped input; (filechannel.map / memorysegment)
 * 			avoids OS-buffer->buf copy on refill for file-input constructors
 * 			doesn't apply to sys.in;; can't mmap pipe/stdin
 * 		- batched SWAR digit parsing for numbers (eg. simult. 8 ascii digits into
 * 			binary value via multiply-and-mask) --- worth? idk, triage+bench
 *
 * 	explore unsafe//ffa + simd intrinsics
 */

// BeginCodeSnip{FastIO}
public class FastIO extends PrintWriter
{
	private static final VarHandle LONGS =
		MethodHandles.byteArrayViewVarHandle(long[].class, ByteOrder.LITTLE_ENDIAN);
	private static final long ONES = 0x0101010101010101L;
	private static final long HIGH = 0x8080808080808080L;
	// 2-digit LUT: DIGITS[2k] = '0' + k / 10, DIGITS[2k + 1] = '0' + k % 10
	private static final byte[] DIGITS = digitPairs();
	private static final byte[] LF = System.lineSeparator().getBytes(StandardCharsets.ISO_8859_1);
	private static final byte[] TRUE = {'t', 'r', 'u', 'e'};
	private static final byte[] FALSE = {'f', 'a', 'l', 's', 'e'};
	private static final byte[] NULL = {'n', 'u', 'l', 'l'};
	private final Sink sink;
	private InputStream stream;
	private byte[] buf = new byte[1 << 16];
	private int curChar;
	private int numChars;

	private static byte[] digitPairs()
	{
		byte[] d = new byte[200];
		for (int k = 0; k < 100; k++)
		{
			d[k << 1] = (byte) ('0' + k / 10);
			d[(k << 1) + 1] = (byte) ('0' + k % 10);
		}
		return d;
	}

	// Standard input
	public FastIO()
	{
		this(System.in, System.out);
	}

	public FastIO(InputStream i, OutputStream o)
	{
		this(i, new Sink(o));
	}

	/**
	 * File input
	 *
	 * @param i Name of file to read
	 * @param o Name of file to write
	 * @throws IOException
	 */
	public FastIO(String i, String o) throws IOException
	{
		this(new FileInputStream(i), new Sink(new FileOutputStream(o)));
	}

	/**
	 * File input, standard output
	 *
	 * @param i Name of file to read
	 * @throws IOException
	 */
	public FastIO(String i) throws IOException
	{
		this(new FileInputStream(i), new Sink(System.out));
	}

	private FastIO(InputStream i, Sink s)
	{
		super(s);
		sink = s;
		stream = i;
	}

	// Throws InputMismatchException() if previously detected end of file
	private int nextByte()
	{
		int c = curChar;
		if (c < numChars)
		{
			curChar = c + 1;
			return buf[c];
		}
		return refill();
	}

	private int refill()
	{
		if (numChars == -1)
		{
			throw new InputMismatchException();
		}
		int n;
		try
		{
			do
			{
				n = stream.read(buf);
			} while (n == 0);
		}
		catch (IOException e)
		{
			throw new InputMismatchException();
		}
		numChars = n;
		if (n == -1)
		{
			return -1;  // end of file
		}
		curChar = 1;
		return buf[0];
	}

	/**
	 * SWAR scan: index of first byte in [i, n) whose signed value is <= flag, else n.
	 * A byte b stops the scan iff (b & 0x80) != 0 (negative) or unsigned(b) < flag + 1;
	 * the latter is the classic "hasless" trick, exact for flag + 1 <= 128.
	 */
	private static int scan(byte[] b, int i, int n, int flag, long kn)
	{
		for (int lim = n - 8; i <= lim; i += 8)
		{
			long x = (long) LONGS.get(b, i);
			long d = (((x - kn) & ~x) | x) & HIGH;
			if (d != 0)
			{
				return i + (Long.numberOfTrailingZeros(d) >> 3);
			}
		}
		while (i < n && b[i] > flag)
		{
			i++;
		}
		return i;
	}

	// read until a byte <= flag (the terminator is consumed, not returned);
	// bytes <= flag are also skipped before the token

	/**
	 * Core implementation:
	 * read until a byte <= flag (the terminator is consumed, not returned);
	 * leading bytes <= flag are also skipped before the token
	 * @param flag; the token to terminate at
	 * @return allocated byte[]
	 */
	private byte[] readBytes(int flag)
	{
		int c;
		do
		{
			c = nextByte();
		} while (c <= flag && c != -1);

		if (c == -1)
		{
			return new byte[0];
		}

		long kn = ONES * (flag + 1L);
		int start = curChar - 1;

		// Fast path, token already fully inside the current buffer
		int i = scan(buf, curChar, numChars, flag, kn);
		if (i < numChars)
		{
			int len = i - start;
			byte[] result = new byte[len];
			System.arraycopy(buf, start, result, 0, len);
			curChar = i + 1;
			return result;
		}

		// Slow path, spans one or more refills
		int len = numChars - start;
		byte[] acc = new byte[Math.max(len << 1, 64)];
		System.arraycopy(buf, start, acc, 0, len);
		curChar = numChars;

		while (true)
		{
			c = nextByte();
			if (c <= flag)
			{
				break;
			}

			int s = curChar - 1;
			int j = scan(buf, curChar, numChars, flag, kn);
			int chunk = j - s;

			if (len + chunk > acc.length)
			{
				acc = Arrays.copyOf(acc, Math.max(len + chunk, acc.length << 1));
			}
			System.arraycopy(buf, s, acc, len, chunk);
			len += chunk;

			if (j < numChars)
			{
				curChar = j + 1;
				break;
			}
			curChar = numChars;
		}

		return (len == acc.length) ? acc : Arrays.copyOf(acc, len);
	}

	/**
	 * Raw bytes until whitespace (or any byte <= ' ')
	 *
	 * @return byte[]
	 */
	public byte[] nextBytes()
	{
		return readBytes(' ');
	}

	/**
	 * Raw bytes of the next line (terminator '\n' consumed)
	 *
	 * @return byte[]
	 */
	public byte[] readLineBytes()
	{
		return readBytes('\n');
	}

	/**
	 * Raw bytes of the entire remaining input
	 *
	 * @return byte[]
	 */
	public byte[] readFileBytes()
	{
		return readBytes(0); // stop at '\0' or EOF
	}

	/**
	 * Reads until next whitespace (literal space char)
	 *
	 * @implNote one allocation + a pure memcpy inside the JDK
	 * @return {@code String} The contents until the next space
	 */
	public String next()
	{
		byte[] b = readBytes(' ');
		return new String(b, 0, b.length, StandardCharsets.ISO_8859_1);
	}

	/**
	 * Reads until custom flag
	 *
	 * @return {@code String} decoded from the raw bytes up to (not including) {@code flag}
	 */
	private String read(char flag)
	{
		byte[] b = readBytes(flag);
		return new String(b, 0, b.length, StandardCharsets.ISO_8859_1);
	}

	/**
	 * Read entire line
	 *
	 * @return {@code String} The contents of a line
	 */
	public String readLine()
	{
		return read('\n');
	}

	/**
	 * Read entire file, until '\0' (NULL char)
	 *
	 * @return {@code String} The contents of the file
	 */
	public String readFile()
	{
		return read('\0');
	}

	/**
	 * Next found number in the contents
	 *
	 * @return {@code Integer}
	 */
	public int nextInt()
	{
		int c;
		do
		{
			c = nextByte();
		} while (c <= ' ');
		int sgn = 1;
		if (c == '-')
		{
			sgn = -1;
			c = nextByte();
		}
		int res = 0;
		do
		{
			int d = c - '0';
			if (d < 0 || d > 9)
			{
				throw new InputMismatchException();
			}
			res = res * 10 + d;
			c = nextByte();
		} while (c > ' ');
		return res * sgn;
	}

	/**
	 * Next found number in the contents
	 *
	 * @return {@code Long}
	 */
	public long nextLong()
	{
		int c;
		do
		{
			c = nextByte();
		} while (c <= ' ');
		int sgn = 1;
		if (c == '-')
		{
			sgn = -1;
			c = nextByte();
		}
		long res = 0;
		do
		{
			int d = c - '0';
			if (d < 0 || d > 9)
			{
				throw new InputMismatchException();
			}
			res = res * 10 + d;
			c = nextByte();
		} while (c > ' ');
		return res * sgn;
	}

	/**
	 * Next found number in the contents
	 *
	 * @apiNote This method is under-optimized and should be avoided when
	 * dealing with non FP-data.
	 * @return {@code Double}
	 */
	public double nextDouble()
	{
		return Double.parseDouble(next());
	}

	// zero-string, lock-free output fast paths; all PrintWriter methods that are not
	// overridden still funnel into the same sink, so ordering is always preserved
	@Override
	public void write(int c)
	{
		try
		{
			sink.charOut((char) c);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void write(char[] b, int off, int len)
	{
		try
		{
			sink.write(b, off, len);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void write(String s, int off, int len)
	{
		try
		{
			sink.strOut(s, off, len);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void print(boolean x)
	{
		try
		{
			sink.bytesOut(x ? TRUE : FALSE);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void print(char x)
	{
		try
		{
			sink.charOut(x);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void print(int x)
	{
		try
		{
			sink.intOut(x);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void print(long x)
	{
		try
		{
			sink.longOut(x);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void print(String s)
	{
		try
		{
			if (s == null)
			{
				sink.bytesOut(NULL);
			}
			else
			{
				sink.strOut(s, 0, s.length());
			}
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void println()
	{
		try
		{
			sink.bytesOut(LF);
		}
		catch (IOException e)
		{
			setError();
		}
	}

	@Override
	public void println(boolean x)
	{
		print(x);
		println();
	}

	@Override
	public void println(char x)
	{
		print(x);
		println();
	}

	@Override
	public void println(int x)
	{
		print(x);
		println();
	}

	@Override
	public void println(long x)
	{
		print(x);
		println();
	}

	@Override
	public void println(String s)
	{
		print(s);
		println();
	}

	/**
	 * Byte-buffered UTF-8 sink; replaces PrintWriter's synchronized
	 * BufferedWriter -> OutputStreamWriter -> StreamEncoder chain.
	 */
	private static final class Sink extends Writer
	{
		private final OutputStream os;
		private final byte[] b = new byte[1 << 16];
		private final byte[] t = new byte[24];  // number scratch, filled backwards
		private int p;
		private char pendingHi;  // pending high surrogate (0 = none)

		Sink(OutputStream os)
		{
			this.os = os;
		}

		private void drain() throws IOException
		{
			if (p > 0)
			{
				os.write(b, 0, p);
				p = 0;
			}
		}

		void bytesOut(byte[] a) throws IOException
		{
			if (p > b.length - a.length)
			{
				drain();
			}
			System.arraycopy(a, 0, b, p, a.length);
			p += a.length;
		}

		void charOut(int c) throws IOException
		{
			if (pendingHi != 0)
			{
				char hi = pendingHi;
				pendingHi = 0;
				if (Character.isLowSurrogate((char) c))
				{
					supplementaryOut(Character.toCodePoint(hi, (char) c));
					return;
				}
				rawOut('?');  // orphaned high surrogate
			}
			if (c < 0x80)
			{
				rawOut(c);
			}
			else if (Character.isHighSurrogate((char) c))
			{
				pendingHi = (char) c;
			}
			else
			{
				multiByteOut(c);
			}
		}

		private void rawOut(int c) throws IOException
		{
			if (p == b.length)
			{
				drain();
			}
			b[p++] = (byte) c;
		}

		private void multiByteOut(int c) throws IOException
		{
			if (p > b.length - 3)
			{
				drain();
			}
			if (c < 0x800)
			{
				b[p++] = (byte) (0xC0 | c >> 6);
			}
			else
			{
				if (Character.isLowSurrogate((char) c))
				{
					rawOut('?');  // orphaned low surrogate
					return;
				}
				b[p++] = (byte) (0xE0 | c >> 12);
				b[p++] = (byte) (0x80 | c >> 6 & 0x3F);
			}
			b[p++] = (byte) (0x80 | c & 0x3F);
		}

		private void supplementaryOut(int cp) throws IOException
		{
			if (p > b.length - 4)
			{
				drain();
			}
			b[p++] = (byte) (0xF0 | cp >> 18);
			b[p++] = (byte) (0x80 | cp >> 12 & 0x3F);
			b[p++] = (byte) (0x80 | cp >> 6 & 0x3F);
			b[p++] = (byte) (0x80 | cp & 0x3F);
		}

		void strOut(String s, int off, int len) throws IOException
		{
			byte[] b = this.b;
			for (int i = off, end = off + len; i < end; i++)
			{
				int c = s.charAt(i);
				if (c < 0x80 && pendingHi == 0)
				{
					if (p == b.length)
					{
						drain();
					}
					b[p++] = (byte) c;
				}
				else
				{
					charOut(c);
				}
			}
		}

		void intOut(int v) throws IOException
		{
			if (p > b.length - 12)
			{
				drain();
			}
			byte[] t = this.t;
			int i = 24;
			if (v < 0)
			{
				b[p++] = '-';
			}
			else
			{
				v = -v;  // negative domain: no overflow on Integer.MIN_VALUE
			}
			while (v <= -100)
			{
				int q = v / 100;
				int r = (q * 100 - v) << 1;
				t[--i] = DIGITS[r + 1];
				t[--i] = DIGITS[r];
				v = q;
			}
			int r = -v;
			if (r >= 10)
			{
				int k = r << 1;
				t[--i] = DIGITS[k + 1];
				t[--i] = DIGITS[k];
			}
			else
			{
				t[--i] = (byte) ('0' + r);
			}
			System.arraycopy(t, i, b, p, 24 - i);
			p += 24 - i;
		}

		void longOut(long v) throws IOException
		{
			if (p > b.length - 21)
			{
				drain();
			}
			byte[] t = this.t;
			int i = 24;
			if (v < 0)
			{
				b[p++] = '-';
			}
			else
			{
				v = -v;  // negative domain: no overflow on Long.MIN_VALUE
			}
			while (v <= -100)
			{
				long q = v / 100;
				int r = (int) (q * 100 - v) << 1;
				t[--i] = DIGITS[r + 1];
				t[--i] = DIGITS[r];
				v = q;
			}
			int r = (int) -v;
			if (r >= 10)
			{
				int k = r << 1;
				t[--i] = DIGITS[k + 1];
				t[--i] = DIGITS[k];
			}
			else
			{
				t[--i] = (byte) ('0' + r);
			}
			System.arraycopy(t, i, b, p, 24 - i);
			p += 24 - i;
		}

		@Override
		public void write(char[] c, int off, int len) throws IOException
		{
			for (int i = off, end = off + len; i < end; i++)
			{
				charOut(c[i]);
			}
		}

		@Override
		public void write(int c) throws IOException
		{
			charOut((char) c);
		}

		@Override
		public void write(String s, int off, int len) throws IOException
		{
			strOut(s, off, len);
		}

		@Override
		public void flush() throws IOException
		{
			drain();
			os.flush();
		}

		@Override
		public void close() throws IOException
		{
			drain();
			os.flush();
			os.close();
		}
	}
}
// EndCodeSnip{FastIO}
