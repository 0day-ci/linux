// SPDX-License-Identifier: GPL-2.0-only
/*
 * Example user of the DOE userspace interface.
 *
 * Jonathan Cameron <Jonathan.Cameron@huawei.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <linux/types.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <string.h>

struct pci_doe_uexchange {
	__u16 vid;
	__u8 protocol;
	__u8 rsvd;
	__u32 retval;
	struct {
		__s32 size;
		__u32 rsvd;
		__u64 payload;
	} in;
	struct {
		__s32 size;
		__u32 rsvd;
		__u64 payload;
	} out;
};

#define PCI_DOE_EXCHANGE _IOWR(0xDA, 1, struct pci_doe_uexchange)

int doe_list_protocols(int fd)
{
	__u32 outbuf = 0;
	__u32 inbuf; /* Start with index 0 */
	struct pci_doe_uexchange ex = {
		.vid = 33,
		.protocol = 1,
		.in.size = sizeof(inbuf),
		.in.payload = (__u64)&inbuf,
		.out.size = sizeof(outbuf),
		.out.payload = (__u64)&outbuf,
		.vid = 0x01, /* PCI SIG */
		.protocol = 0x00,
	};
	int rc;
	uint8_t index = 0;

	do {
		inbuf = index;
		rc = ioctl(fd, PCI_DOE_EXCHANGE, &ex);
		if (rc) {
			printf("IOCTL error: %d\n", rc);
			return rc;
		}
		if (ex.retval) {
			printf("DOE return value indicates failure: %d\n", ex.retval);
			return ex.retval;
		}
		index = outbuf >> 24;

		printf("VID: %#x, Protocol: %#x\n", outbuf & 0xffff, (outbuf >> 16) & 0xff);
	} while (index);

	return 0;
}

static const struct option longopts[] = {
	{ "filename",		1, 0, 'f' },
	{ }
};

static void print_usage(void)
{
	fprintf(stderr, "Usage: doe [options]...\n"
		"Example userspace access to a PCI DOE mailbox\n"
		"  -f <filename>	Path to chardev /dev/pcidoe/...\n"
		"  -l			List supported protocols\n");
}

int main(int argc, char **argv)
{
	char *filename = NULL;
	bool run_discovery = false;
	int fd, c;
	int rc = 0;

	while ((c = getopt_long(argc, argv, "?f:l", longopts, NULL)) != -1) {
		switch (c) {
		case 'f':
			filename = strdup(optarg);
			break;
		case 'l':
			run_discovery = true;
			break;
		case '?':
			print_usage();
			goto free_filename;
		}
	}
	if (!filename) {
		fprintf(stderr, "Filename must be supplied using -f FILENAME\n");
		rc = -1;
		/* No need to actually free the filename, but keep exit path simple */
		goto free_filename;
	}

	fd = open(filename, 0);
	if (fd == -1) {
		fprintf(stderr, "Could not open file %s\n", filename);
		rc = -1;
		goto free_filename;
	}
	if (run_discovery) {
		rc = doe_list_protocols(fd);
		if (rc)
			goto close_fd;
	}
close_fd:
	close(fd);
free_filename:
	free(filename);

	return rc;
}
