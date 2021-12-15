// SPDX-License-Identifier: GPL-2.0
// Andrea Brunato <andrea.brunato@arm.com>, 2021
// Example taken from: https://gcc.gnu.org/wiki/AutoFDO/Tutorial

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int count_lines(FILE *fp)
{
	int lines_n = 0;
	char c;

	for (c = getc(fp); !feof(fp); c = getc(fp)) {
		if (c == '\n')
			lines_n = lines_n + 1;
	}
	fseek(fp, 0, SEEK_SET);
#ifdef DEBUG
	printf("Number of lines: %d\n", lines_n);
#endif
	return lines_n;
}

#ifdef DEBUG
void print_array(int *arr, int size)
{
	int i;

	assert(arr != NULL);
	for (i = 0; i < size; i++)
		printf("%d\n", arr[i]);
}
#endif

void bubble_sort(int *a, int n)
{
	int i, t, s = 1;

	while (s) {
		s = 0;
		for (i = 1; i < n; i++) {
			if (a[i] < a[i - 1]) {
				t = a[i];
				a[i] = a[i - 1];
				a[i - 1] = t;
				s = 1;
			}
		}
	}
}

void init_array(int *arr, int size, FILE *fp)
{
	int i;

	for (i = 0; i < size; i++)
		fscanf(fp, "%d", &arr[i]);
}

int main(int argc, char **argv)
{
	int lines_n = 0, *arr = NULL;
	FILE *fp;

	assert((argc == 2) && "Please specify an input file\n");

	fp = fopen(argv[1], "r");
	assert((fp != NULL) && "ERROR: Couldn't open the specified file\n");

	// Input file expected formar: one number per line
	lines_n = count_lines(fp);

	arr = malloc(sizeof(int) * lines_n);
	init_array(arr, lines_n, fp);

	bubble_sort(arr, lines_n);

#ifdef DEBUG
	print_array(arr, lines_n);
#endif

	free(arr);
	fclose(fp);

	return 0;
}


