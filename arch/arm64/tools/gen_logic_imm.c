// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 ARM Limited */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef uint32_t u32;
typedef uint64_t u64;

static u64 gen_mask(u32 num_bits)
{
	if (num_bits == 64)
		return ~0ULL;

	return (1ULL<<num_bits) - 1;
}

static u64 ror(u64 bits, u64 count, u64 esize)
{
	u64 ret;
	u64 bottom_bits = bits & gen_mask(count);

	if (!count)
		return bits;

	ret = bottom_bits << (esize - count) | (bits >> count);

	return ret;
}

static u64 replicate(u64 bits, u32 esize)
{
	int i;
	u64 ret = 0;

	bits &= gen_mask(esize);
	for (i = 0; i < 64; i += esize)
		ret |= (u64)bits << i;

	return ret;
}

static u32 fls(u32 x)
{
	/* If x is 0, the result is undefined */
	if (!x)
		return 32;

	return 32 - __builtin_clz(x);
}

#define PIPE_READ	0
#define PIPE_WRITE	1
/*
 * Use objdump to decode the encoded instruction, and compare the immediate.
 * On error, returns the bad instruction, otherwise returns 0.
 */
static int validate(u64 val, u32 immN, u32 imms, u32 immr, char *objdump)
{
	pid_t child;
	char *immediate;
	char val_str[32];
	u32 insn = 0x12000000;
	char output[1024] = {0};
	int fd, pipefd[2], bytes;
	char filename[] = "validate_gen_logic_imm.XXXXXX";

	insn |= 1 << 31;
	insn |= (immN & 0x1)<<22;
	insn |= (immr & 0x3f)<<16;
	insn |= (imms & 0x3f)<<10;

	fd = mkstemp(filename);
	if (fd < 0)
		abort();

	write(fd, &insn, sizeof(insn));
	close(fd);

	if (pipe(pipefd))
		return 0;

	child = vfork();
	if (child) {
		close(pipefd[PIPE_WRITE]);
		waitpid(child, NULL, 0);

		bytes = read(pipefd[PIPE_READ], output, sizeof(output));
		close(pipefd[PIPE_READ]);
		if (!bytes || bytes == sizeof(output))
			return insn;

		immediate = strstr(output, "x0, x0, #");
		if (!immediate)
			return insn;
		immediate += strlen("x0, x0, #");

		/*
		 * strtoll() has its own ideas about overflow and underflow.
		 * Do a string comparison. immediate ends in a newline.
		 */
		snprintf(val_str, sizeof(val_str), "0x%lx", val);
		if (strncmp(val_str, immediate, strlen(val_str))) {
			fprintf(stderr, "Unexpected decode from objdump: %s\n",
				immediate);
			return insn;
		}
	} else {
		close(pipefd[PIPE_READ]);
		close(1);
		dup2(pipefd[PIPE_WRITE], 1);
		execl(objdump, objdump, "-b", "binary", "-m", "aarch64", "-D",
		      filename, (char *) NULL);
		abort();
	}

	unlink(filename);
	return 0;
}

static int decode_bit_masks(u32 immN, u32 imms, u32 immr, char *objdump)
{
	u32 esize, len, S, R;
	u64 levels, welem, wmask;

	imms &= 0x3f;
	immr &= 0x3f;

	len = fls((immN << 6) | (~imms & 0x3f));
	if (!len || len > 7)
		return 0;

	esize = 1 << (len - 1);
	levels = gen_mask(len);
	S = imms & levels;
	if (S + 1 >= esize)
		return 0;
	R = immr & levels;
	if (immr >= esize)
		return 0;

	welem = gen_mask(S + 1);
	wmask = replicate(ror(welem, R, esize), esize);

	printf("\t{0x%.16lx, %u, %2u, %2u},\n", wmask, immN, immr, imms);

	if (objdump) {
		u32 bad_insn = validate(wmask, immN, imms, immr, objdump);

		if (bad_insn) {
			fprintf(stderr,
				"Failed to validate encoding of 0x%.16lx as 0x%x\n",
				wmask, bad_insn);
			exit(1);
		}
	}

	return 1;
}

int main(int argc, char **argv)
{
	u32 immN, imms, immr, count = 0;
	char *objdump = NULL;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [/path/to/objdump]\n", argv[0]);
		exit(0);
	} else if (argc == 2) {
		objdump = argv[1];
	}

	for (immN = 0; immN <= 1; immN++) {
		for (imms = 0; imms <= 0x3f; imms++) {
			for (immr = 0; immr <= 0x3f; immr++)
				count += decode_bit_masks(immN, imms, immr, objdump);
		}
	}

	if (count != 5334) {
		printf("#error Wrong number of encodings generated.\n");
		exit(1);
	}

	return 0;
}
