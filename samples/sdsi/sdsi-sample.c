// SPDX-License-Identifier: GPL-2.0
/*
 * sdsi_test: Example program using the sysfs interface of the
 * Intel Software Defined Silicon Linux driver.
 *
 * See https://github.com/intel/intel-sdsi/blob/master/os-interface.rst
 * for register descriptions.
 *
 * Copyright (C) 2021 Intel Corporation. All rights reserved.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SDSI_DIR		"/sys/bus/auxiliary/devices/intel_vsec.sdsi"
#define GUID			"0x6dd191"
#define REGISTERS_MIN_SIZE	72

struct enabled_features {
	uint64_t reserved:3;
	uint64_t sdsi:1;
	uint64_t reserved1:60;
};

struct auth_fail_count {
	uint64_t key_failure_count:3;
	uint64_t key_failure_threshold:3;
	uint64_t auth_failure_count:3;
	uint64_t auth_failure_threshold:3;
	uint64_t reserved:52;
};

struct availability {
	uint64_t reserved:58;
	uint64_t updates_available:3;
	uint64_t updates_threshold:3;
};

struct sdsi_reg_6dd191 {
	uint64_t ppin;
	uint64_t reserved;
	struct enabled_features en_features;
	uint64_t reserved1;
	struct auth_fail_count auth_fail_count;
	struct availability prov_avail;
	uint64_t reserved2;
	uint64_t reserved3;
	uint64_t socket_id;
};

enum command {
	CMD_NONE,
	CMD_READ_LIC,
	CMD_READ_REG,
	CMD_PROV_AKC,
	CMD_PROV_CAP,
};

static int get_file_size(FILE *stream, char *name)
{
	long size;
	int ret;

	ret = fseek(stream, 0L, SEEK_END);
	if (ret == -1) {
		fprintf(stderr, "...Could not seek to EOF %s: %s\n", name, strerror(errno));
		return ret;
	}

	size = ftell(stream);
	if (size == -1) {
		fprintf(stderr, "...Could not get size of file %s: %s\n", name, strerror(errno));
		return size;
	}

	rewind(stream);

	return size;
}

static int sdsi_read_reg(char *socket)
{
	FILE *regs_ptr, *guid_ptr;
	struct sdsi_reg_6dd191 registers;
	char guid_val[20], *buf;
	char regs_file[70];
	char guid_file[70];
	int ret, i;
	long size;

	snprintf(regs_file, sizeof(regs_file), "%s%s%s%s",
		 SDSI_DIR, ".", socket, "/registers");

	snprintf(guid_file, sizeof(guid_file), "%s%s%s%s",
		 SDSI_DIR, ".", socket, "/guid");

	memset(&registers, 0, sizeof(registers));

	/* Open the guid file */
	guid_ptr = fopen(guid_file, "r");
	if (!guid_ptr) {
		fprintf(stderr, "...Could not open file %s: %s\n", guid_file, strerror(errno));
		return -1;
	}

	fscanf(guid_ptr, "%20s", guid_val);
	fclose(guid_ptr);

	/* Open the registers file */
	regs_ptr = fopen(regs_file, "r");
	if (!regs_ptr) {
		fprintf(stderr, "...Could not open file %s: %s\n", regs_file, strerror(errno));
		return -1;
	}

	/* Get size of the registers file */
	size = get_file_size(regs_ptr, regs_file);
	if (size < 0) {
		ret = size;
		goto close_regs_ptr;
	}

	/* Unknown guid. Just dump raw data */
	if (strcmp(GUID, guid_val)) {
		printf("Unrecognized guid, %s\n", guid_val);

		buf = (char *)malloc(sizeof(char) * size);
		if (!buf) {
			perror("malloc");
			goto close_regs_ptr;
		}

		ret = fread(buf, sizeof(uint8_t), size, regs_ptr);
		if (!ret) {
			fprintf(stderr, "...Could not read file %s: %s\n", regs_file,
				strerror(errno));
			free(buf);
			goto close_regs_ptr;
		}

		for (i = 0; i < size; i += sizeof(uint64_t))
			printf("%3d: 0x%lx\n", i, *(uint64_t *)&buf[i]);

		free(buf);
		goto close_regs_ptr;
	}

	/* Print register info for this guid */
	ret = fread(&registers, sizeof(uint8_t), sizeof(registers), regs_ptr);
	if (!ret) {
		fprintf(stderr, "...Could not read file %s: %s\n", regs_file, strerror(errno));
		goto close_regs_ptr;
	}

	printf("\n");
	printf("Info for device %s.%s\n", "intel_vsec.sdsi", socket);
	printf("\n");
	printf("PPIN:                           0x%lx\n", registers.ppin);
	printf("Enabled Features\n");
	printf("    SDSi:                       %s\n", !!registers.en_features.sdsi ? "Enabled" : "Disabled");
	printf("Authorization Failure Count\n");
	printf("    Key Failure Count:          %d\n", registers.auth_fail_count.key_failure_count);
	printf("    Key Failure Count:          %d\n", registers.auth_fail_count.key_failure_threshold);
	printf("    Auth Failure Count:         %d\n", registers.auth_fail_count.auth_failure_count);
	printf("    Auth Failure Count:         %d\n", registers.auth_fail_count.key_failure_threshold);
	printf("Provisioning Availability\n");
	printf("    Updates Available:          %d\n", registers.prov_avail.updates_available);
	printf("    Updates Threshold:          %d\n", registers.prov_avail.updates_threshold);
	printf("Socket ID:                      0x%lx\n", registers.socket_id);

close_regs_ptr:
	fclose(regs_ptr);

	return 0;
}

static int sdsi_certificate_dump(char *socket)
{
	uint64_t state_certificate[512] = {0};
	bool first_instance;
	char cert_file[70];
	uint64_t previous;
	FILE *cert_ptr;
	int i, ret;

	snprintf(cert_file, sizeof(cert_file), "%s%s%s%s",
		 SDSI_DIR, ".", socket, "/state_certificate");

	/* Open the registers file */
	cert_ptr = fopen(cert_file, "r");
	if (!cert_ptr) {
		fprintf(stderr, "...Could not open file %s: %s\n", cert_file, strerror(errno));
		return -1;
	}

	/* Read registers */
	ret = fread(state_certificate, sizeof(uint8_t), sizeof(state_certificate), cert_ptr);
	if (!ret) {
		fprintf(stderr, "...Could not read file %s: %s\n", cert_file, strerror(errno));
		goto close_cert_ptr;
	}

	printf("%3d: 0x%lx\n", 0, state_certificate[0]);
	previous = state_certificate[0];
	first_instance = true;

	for (i = 1; i < (sizeof(state_certificate)/sizeof(uint64_t)); i++) {
		if (state_certificate[i] == previous) {
			if (first_instance) {
				puts("*");
				first_instance = false;
			}
			continue;
		}
		printf("%3d: 0x%lx\n", i, state_certificate[i]);
		previous = state_certificate[i];
		first_instance = true;
	}
	printf("%3d\n", i);

close_cert_ptr:
	fclose(cert_ptr);

	return 0;
}

static int sdsi_provision(char *prov_file, char *bin_file)
{
	char buf[4096] = { 0 };
	int bin_fd, prov_fd, size, ret = 0;

	if (!bin_file) {
		fprintf(stderr, "...No binary file provided\n");
		return -1;
	}

	/* Open the provision file */
	prov_fd = open(prov_file, O_WRONLY);
	if (prov_fd == -1) {
		fprintf(stderr, "...Could not open file %s: %s\n", prov_file, strerror(errno));
		return prov_fd;
	}

	/* Open the binary */
	bin_fd = open(bin_file, O_RDONLY);
	if (bin_fd == -1) {
		fprintf(stderr, "...Could not open file %s: %s\n", bin_file, strerror(errno));
		ret = bin_fd;
		goto close_provision_fd;
	}

	/* Read the binary file into the buffer */
	ret = read(bin_fd, buf, 4096);
	if (ret == -1)
		goto close_bin_fd;

	size = ret;
	ret = write(prov_fd, buf, size);
	if (ret < size) {
		fprintf(stderr, "...Could not write file %s: %s\n", prov_file, strerror(errno));
		goto close_bin_fd;
	}

	printf("Provisioned %s file %s successfully\n", prov_file, bin_file);

close_bin_fd:
	close(bin_fd);
close_provision_fd:
	close(prov_fd);

	return ret;
}

static int sdsi_provision_akc(char *socket, char *bin_file)
{
	char akc_file[70];

	snprintf(akc_file, sizeof(akc_file), "%s%s%s%s",
		 SDSI_DIR, ".", socket, "/provision_akc");

	return sdsi_provision(akc_file, bin_file);
}

static int sdsi_provision_cap(char *socket, char *bin_file)
{
	char cap_file[70];

	snprintf(cap_file, sizeof(cap_file), "%s%s%s%s",
		 SDSI_DIR, ".", socket, "/provision_cap");

	return sdsi_provision(cap_file, bin_file);
}

static void print_help(char *prog)
{
	printf("Usage:\n");

	printf("\t%s -s socket [-r [lic] [reg]] [-a file] [-c file]\n", prog);

	printf("Options:\n");
	printf("%-13s\t%s\n", "-s <socket>", "socket number to open");
	printf("%-13s\t%s\n", "-r lic", "read licence data");
	printf("%-13s\t%s\n", "-r reg", "read SDSi register data");
	printf("%-13s\t%s\n", "-a <file>", "provision socket with AKC file");
	printf("%-13s\t%s\n", "-c <file>", "provision socket with CAP file");
}

int main(int argc, char *argv[])
{
	char *bin_file = NULL, *socket = NULL;
	enum command command = CMD_NONE;
	int ret, opt, cmd_count = 0;

	while ((opt = getopt(argc, argv, "hs:ra:c:t:")) != -1) {
		switch (opt) {
		case 's':
			socket = optarg;
			break;
		case 'r':
			if (!argv[optind]) {
				print_help(argv[0]);
				return -1;
			}

			if (strlen(argv[optind]) != strlen("lic")) {
				print_help(argv[0]);
				return -1;
			}

			if (!strcmp(argv[optind], "lic")) {
				command = CMD_READ_LIC;
				++cmd_count;
				break;
			} else if (!strcmp(argv[optind], "reg")) {
				command = CMD_READ_REG;
				++cmd_count;
				break;
			}

			print_help(argv[0]);
			return -1;
		case 'a':
			command = CMD_PROV_AKC;
			bin_file = optarg;
			++cmd_count;
			break;
		case 'c':
			command = CMD_PROV_CAP;
			bin_file = optarg;
			++cmd_count;
			break;
		case 'h':
			print_help(argv[0]);
			break;
		default:
			print_help(argv[0]);
			return 0;
		}
	}

	if (!socket) {
		fprintf(stderr, "socket is required\n");
		print_help(argv[0]);
		return -1;
	}

	if (!cmd_count) {
		fprintf(stderr, "need to specify a command\n");
		print_help(argv[0]);
		return -1;
	}

	/* If applicable, check file exists */
	if (bin_file) {
		if (!access(bin_file, F_OK) == 0) {
			fprintf(stderr, "...Could not open file %s: %s\n", bin_file, strerror(errno));
			return -1;
		}
	}

	/* Run the command */
	if (command == CMD_READ_LIC)
		ret = sdsi_certificate_dump(socket);
	else if (command == CMD_READ_REG)
		ret = sdsi_read_reg(socket);
	else if (command == CMD_PROV_AKC)
		ret = sdsi_provision_akc(socket, bin_file);
	else
		ret = sdsi_provision_cap(socket, bin_file);

	return ret;
}
