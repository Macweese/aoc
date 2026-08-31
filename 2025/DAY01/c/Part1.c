#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("Provide rotation input string as argument.\n");
		return 1;
	}

	int i = 50, c = 0;
	char *input = argv[1];
	char *saveptr = NULL;
	char *op = strtok_r(input, "\n", &saveptr);

	while (op != NULL)
	{
		i = (i + atoi(op + 1) * (op[0] / 3 - 26)) % 100;
		c += ((i | -i) >> 31) + 1;
		op = strtok_r(NULL, "\n", &saveptr);
	}

	printf("Password:\n%d\n", c);
	return 0;
}