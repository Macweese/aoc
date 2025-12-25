#include <stdio.h>

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("Provide rotation input string as argument.\n");
		return 1;
	}

	int pos = 50, c = 0;
	const unsigned char *p = (const unsigned char *) argv[1];

	while (*p)
	{
		int dir = 1 - ((*p++ & 8) >> 2);
		int v = 0;
		unsigned d;
		while ((d = (unsigned) (*p - '0')) <= 9)
		{
			v = v * 10 + (int) d;
			p++;
		}
		if (*p)
		{
			p++; /* skip LF */
		}

		const int q = (v * 656) >> 16;
		const int q100 = (q << 6) + (q << 5) + (q << 2);
		v = v - q100;
		pos += dir * v;
		pos += 100 & (pos >> 31);        /* wrap negative into [0,100) */
		pos -= 100 & ((99 - pos) >> 31); /* wrap overflow into [0,100) */
		c += ((pos | -pos) >> 31) + 1;   /* +1 iff pos == 0 */
	}

	printf("Password:\n%d\n", c);
	return 0;
}
