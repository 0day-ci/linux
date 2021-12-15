// SPDX-License-Identifier: GPL-2.0
// Andrea Brunato <andrea.brunato@arm.com>, 2021
// Example taken from: https://gcc.gnu.org/wiki/AutoFDO/Tutorial

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <string.h> // memcpy
#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)

typedef struct payload_t {
	int *array;
	int size;
} payload_t;


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

void *bubble_sort(void *payload)
{
	payload_t *p = payload;
	int *a = p->array;
	int n = p->size;
	int i, t, s = 1;

	printf("Sorting from thread %ld\n", gettid());
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
	return NULL;
}

void init_array(int *arr, int size, FILE *fp)
{
	int i;

	for (i = 0; i < size; i++)
		fscanf(fp, "%d", &arr[i]);
}

int main(int argc, char **argv)
{
	int lines_n = 0, *arr = NULL, *arr2 = NULL;
	pthread_t thread1, thread2;
	FILE *fp;
	payload_t *p1, *p2;

	p1 = malloc(sizeof(payload_t));
	p2 = malloc(sizeof(payload_t));
	assert((p1 != NULL) && "Couldn't allocate payload\n");
	assert((p2 != NULL) && "Couldn't allocate payload\n");

	assert((argc == 2) && "Please specify an input file\n");

	fp = fopen(argv[1], "r");
	assert((fp != NULL) && "ERROR: Couldn't open the specified file\n");

	// Input file expected formar: one number per line
	lines_n = count_lines(fp);

	// Allocate memory for the arrays
	arr = malloc(sizeof(int) * lines_n);
	arr2 = malloc(sizeof(int) * lines_n);
	assert((arr2 != NULL) && "Couldn't allocate array\n");
	assert((arr != NULL) && "Couldn't allocate array\n");

	init_array(arr, lines_n, fp);
	memcpy(arr2, arr, sizeof(int) * lines_n);

	// Init the payload
	p1->array = arr;
	p1->size = lines_n;
	p2->array = arr2;
	p2->size = lines_n;

	printf("Main thread tid is: %ld\n", gettid());

	/* Create independent threads each of which will sort its own array */
	pthread_create(&thread1, NULL, bubble_sort, p1);
	pthread_create(&thread2, NULL, bubble_sort, p2);

	// Let's wait for the threads to finish
	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);

#ifdef DEBUG
	print_array(p1->array, lines_n);
	print_array(p2->array, lines_n);
#endif

	free(arr);
	free(arr2);
	free(p1);
	free(p2);
	fclose(fp);

	return 0;
}
