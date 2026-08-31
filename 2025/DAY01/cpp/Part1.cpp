#include <iostream>
#include <iterator>
#include <string>

// Slurp stdin once, then a zero-allocation pointer scan: no getline, no per-line
// string, branchless direction decode / wrap / zero-detect.
int main()
{
	std::ios_base::sync_with_stdio(false);
	const std::string in(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
	const unsigned char *p = reinterpret_cast<const unsigned char *>(in.data());
	const unsigned char *const end = p + in.size();

	int pos{50}, c{0};
	while (p < end)
	{
		const int dir = 1 - ((*p++ & 8) >> 2); // 'L'/'R' -> -1/+1; coef determinant
		int v{0};
		unsigned d;
		while (p < end && (d = static_cast<unsigned>(*p - '0')) <= 9)
		{
			v = v * 10 + static_cast<int>(d);
			++p;
		}
		if (p < end)
		{
			++p; // skip LF
		}

		pos += dir * (v % 100);
		pos += 100 & (pos >> 31);        // wrap negative into [0,100)
		pos -= 100 & ((99 - pos) >> 31); // wrap overflow into [0,100)
		c += ((pos | -pos) >> 31) + 1;   // +1 iff pos == 0
	}

	std::cout << c;
	return 0;
}
