#include <iostream>
#include <string>

int main()
{
	int c{0}, r{50};
	std::string l{};
	while (std::getline(std::cin, l))
	{
		int m{0};
		for (std::size_t i{1}; i < l.size(); ++i) m = (m * 10 + l[i] - '0');
		r = ((m * (l[0] / 3 - 26 /* the difference between 'L' and 'R' divided by 3 - 26 makes +-1; use as sign/coef determinant */)) + r) % 100;
		c += ((r | -r) >> 31) + 1;
	}

	std::cout << c;
	return 0;
}
