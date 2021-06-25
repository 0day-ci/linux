// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 * Copyright (C) 2017-2021 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: selftest.c
 *      Functions to test Huawei Digest Lists.
 */

#include <sys/random.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/hash_info.h>

#include <openssl/evp.h>

#include "common.h"
#include "../kselftest_harness.h"

#define HDR_ASCII_FMT \
	"actions: %d, version: %d, algo: %s, type: %d, modifiers: %d, count: %d, datalen: %d\n"
#define QUERY_RESULT_FMT \
	"%s (actions: %d): version: %d, algo: %s, type: %d, modifiers: %d, count: %d, datalen: %d\n"
#define QUERY_RESULT_DIGEST_LIST_FMT "%s (actions: %d): type: %d, size: %lld\n"

enum compact_types { COMPACT_KEY, COMPACT_PARSER, COMPACT_FILE,
		     COMPACT_METADATA, COMPACT_DIGEST_LIST, COMPACT__LAST };

enum compact_modifiers { COMPACT_MOD_IMMUTABLE, COMPACT_MOD__LAST };

enum compact_actions { COMPACT_ACTION_IMA_MEASURED,
		       COMPACT_ACTION_IMA_APPRAISED,
		       COMPACT_ACTION_IMA_APPRAISED_DIGSIG,
		       COMPACT_ACTION__LAST };

enum ops { DIGEST_LIST_ADD, DIGEST_LIST_DEL, DIGEST_LIST_OP__LAST };

struct compact_list_hdr {
	__u8 version;
	__u8 _reserved;
	__le16 type;
	__le16 modifiers;
	__le16 algo;
	__le32 count;
	__le32 datalen;
} __packed;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define MD5_DIGEST_SIZE 16
#define SHA1_DIGEST_SIZE 20
#define RMD160_DIGEST_SIZE 20
#define SHA256_DIGEST_SIZE 32
#define SHA384_DIGEST_SIZE 48
#define SHA512_DIGEST_SIZE 64
#define SHA224_DIGEST_SIZE 28
#define RMD128_DIGEST_SIZE 16
#define RMD256_DIGEST_SIZE 32
#define RMD320_DIGEST_SIZE 40
#define WP256_DIGEST_SIZE 32
#define WP384_DIGEST_SIZE 48
#define WP512_DIGEST_SIZE 64
#define TGR128_DIGEST_SIZE 16
#define TGR160_DIGEST_SIZE 20
#define TGR192_DIGEST_SIZE 24
#define SM3256_DIGEST_SIZE 32
#define STREEBOG256_DIGEST_SIZE 32
#define STREEBOG512_DIGEST_SIZE 64

#define DIGEST_LIST_PATH_TEMPLATE "/tmp/digest_list.XXXXXX"
#define PARSER_BASENAME "manage_digest_lists"

#define INTEGRITY_DIR "/sys/kernel/security/integrity"

#define DIGEST_LIST_DIR INTEGRITY_DIR "/digest_lists"
#define DIGEST_QUERY_PATH DIGEST_LIST_DIR "/digest_query"
#define DIGEST_LABEL_PATH DIGEST_LIST_DIR "/digest_label"
#define DIGEST_LIST_ADD_PATH DIGEST_LIST_DIR "/digest_list_add"
#define DIGEST_LIST_DEL_PATH DIGEST_LIST_DIR "/digest_list_del"
#define DIGEST_LISTS_LOADED_PATH DIGEST_LIST_DIR "/digest_lists_loaded"
#define DIGESTS_COUNT DIGEST_LIST_DIR "/digests_count"

#define IMA_POLICY_PATH INTEGRITY_DIR "/ima/policy"
#define IMA_MEASUREMENTS_PATH INTEGRITY_DIR "/ima/ascii_runtime_measurements"

#define DIGEST_LIST_DEBUGFS_DIR "/sys/kernel/debug/fail_digest_lists"
#define DIGEST_LIST_DEBUGFS_TASK_FILTER DIGEST_LIST_DEBUGFS_DIR "/task-filter"
#define DIGEST_LIST_DEBUGFS_PROBABILITY DIGEST_LIST_DEBUGFS_DIR "/probability"
#define DIGEST_LIST_DEBUGFS_TIMES DIGEST_LIST_DEBUGFS_DIR "/times"
#define DIGEST_LIST_DEBUGFS_VERBOSE DIGEST_LIST_DEBUGFS_DIR "/verbose"
#define PROCFS_SELF_FAULT "/proc/self/make-it-fail"

#define MAX_LINE_LENGTH 512
#define LABEL_LEN 32
#define MAX_DIGEST_COUNT 100
#define MAX_DIGEST_LISTS 100
#define MAX_DIGEST_BLOCKS 10
#define MAX_DIGEST_VALUE 10
#define MAX_SEARCH_ATTEMPTS 10
#define NUM_QUERIES 1000
#define MAX_DIGEST_LIST_SIZE 10000
#define NUM_ITERATIONS 100000

enum upload_types { UPLOAD_FILE, UPLOAD_BUFFER };

const char *const hash_algo_name[HASH_ALGO__LAST] = {
	[HASH_ALGO_MD4]		= "md4",
	[HASH_ALGO_MD5]		= "md5",
	[HASH_ALGO_SHA1]	= "sha1",
	[HASH_ALGO_RIPE_MD_160]	= "rmd160",
	[HASH_ALGO_SHA256]	= "sha256",
	[HASH_ALGO_SHA384]	= "sha384",
	[HASH_ALGO_SHA512]	= "sha512",
	[HASH_ALGO_SHA224]	= "sha224",
	[HASH_ALGO_RIPE_MD_128]	= "rmd128",
	[HASH_ALGO_RIPE_MD_256]	= "rmd256",
	[HASH_ALGO_RIPE_MD_320]	= "rmd320",
	[HASH_ALGO_WP_256]	= "wp256",
	[HASH_ALGO_WP_384]	= "wp384",
	[HASH_ALGO_WP_512]	= "wp512",
	[HASH_ALGO_TGR_128]	= "tgr128",
	[HASH_ALGO_TGR_160]	= "tgr160",
	[HASH_ALGO_TGR_192]	= "tgr192",
	[HASH_ALGO_SM3_256]	= "sm3",
	[HASH_ALGO_STREEBOG_256] = "streebog256",
	[HASH_ALGO_STREEBOG_512] = "streebog512",
};

const int hash_digest_size[HASH_ALGO__LAST] = {
	[HASH_ALGO_MD4]		= MD5_DIGEST_SIZE,
	[HASH_ALGO_MD5]		= MD5_DIGEST_SIZE,
	[HASH_ALGO_SHA1]	= SHA1_DIGEST_SIZE,
	[HASH_ALGO_RIPE_MD_160]	= RMD160_DIGEST_SIZE,
	[HASH_ALGO_SHA256]	= SHA256_DIGEST_SIZE,
	[HASH_ALGO_SHA384]	= SHA384_DIGEST_SIZE,
	[HASH_ALGO_SHA512]	= SHA512_DIGEST_SIZE,
	[HASH_ALGO_SHA224]	= SHA224_DIGEST_SIZE,
	[HASH_ALGO_RIPE_MD_128]	= RMD128_DIGEST_SIZE,
	[HASH_ALGO_RIPE_MD_256]	= RMD256_DIGEST_SIZE,
	[HASH_ALGO_RIPE_MD_320]	= RMD320_DIGEST_SIZE,
	[HASH_ALGO_WP_256]	= WP256_DIGEST_SIZE,
	[HASH_ALGO_WP_384]	= WP384_DIGEST_SIZE,
	[HASH_ALGO_WP_512]	= WP512_DIGEST_SIZE,
	[HASH_ALGO_TGR_128]	= TGR128_DIGEST_SIZE,
	[HASH_ALGO_TGR_160]	= TGR160_DIGEST_SIZE,
	[HASH_ALGO_TGR_192]	= TGR192_DIGEST_SIZE,
	[HASH_ALGO_SM3_256]	= SM3256_DIGEST_SIZE,
	[HASH_ALGO_STREEBOG_256] = STREEBOG256_DIGEST_SIZE,
	[HASH_ALGO_STREEBOG_512] = STREEBOG512_DIGEST_SIZE,
};

struct digest_list_item {
	loff_t size;
	u8 *buf;
	u8 actions;
	char digest_str[64 * 2 + 1];
	enum hash_algo algo;
	char filename_suffix[6 + 1];
};

static const char hex_asc[] = "0123456789abcdef";

#define hex_asc_lo(x)	hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)	hex_asc[((x) & 0xf0) >> 4]

static inline char *hex_byte_pack(char *buf, unsigned char byte)
{
	*buf++ = hex_asc_hi(byte);
	*buf++ = hex_asc_lo(byte);
	return buf;
}

/* from lib/hexdump.c (Linux kernel) */
static int hex_to_bin(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	ch = tolower(ch);
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	return -1;
}

int _hex2bin(unsigned char *dst, const char *src, size_t count)
{
	while (count--) {
		int hi = hex_to_bin(*src++);
		int lo = hex_to_bin(*src++);

		if ((hi < 0) || (lo < 0))
			return -1;

		*dst++ = (hi << 4) | lo;
	}
	return 0;
}

char *_bin2hex(char *dst, const void *src, size_t count)
{
	const unsigned char *_src = src;

	while (count--)
		dst = hex_byte_pack(dst, *_src++);
	return dst;
}

u32 num_max_digest_lists = MAX_DIGEST_LISTS;
u32 digest_lists_pos;
struct digest_list_item *digest_lists[MAX_DIGEST_LISTS];

enum hash_algo ima_hash_algo = HASH_ALGO__LAST;

static enum hash_algo get_ima_hash_algo(void)
{
	char *measurement_list, *measurement_list_ptr;
	size_t measurement_list_len;
	int ret, i = 0;

	if (ima_hash_algo != HASH_ALGO__LAST)
		return ima_hash_algo;

	ret = read_buffer(IMA_MEASUREMENTS_PATH, &measurement_list,
			  &measurement_list_len, true, true);
	if (ret < 0)
		return ret;

	measurement_list_ptr = measurement_list;
	while ((strsep(&measurement_list_ptr, " ")) && i++ < 2)
		;

	for (i = 0; i < HASH_ALGO__LAST; i++) {
		if (!strncmp(hash_algo_name[i], measurement_list_ptr,
			     strlen(hash_algo_name[i]))) {
			ima_hash_algo = i;
			break;
		}
	}

	free(measurement_list);
	return ima_hash_algo;
}

int calc_digest(u8 *digest, void *data, u64 len, enum hash_algo algo)
{
	EVP_MD_CTX *mdctx;
	const EVP_MD *md;
	int ret = -EINVAL;

	OpenSSL_add_all_algorithms();

	md = EVP_get_digestbyname(hash_algo_name[algo]);
	if (!md)
		goto out;

	mdctx = EVP_MD_CTX_create();
	if (!mdctx)
		goto out;

	if (EVP_DigestInit_ex(mdctx, md, NULL) != 1)
		goto out_mdctx;

	if (EVP_DigestUpdate(mdctx, data, len) != 1)
		goto out_mdctx;

	if (EVP_DigestFinal_ex(mdctx, digest, NULL) != 1)
		goto out_mdctx;

	ret = 0;
out_mdctx:
	EVP_MD_CTX_destroy(mdctx);
out:
	EVP_cleanup();
	return ret;
}

int calc_file_digest(u8 *digest, char *path, enum hash_algo algo)
{
	void *data = MAP_FAILED;
	struct stat st;
	int fd, ret = 0;

	if (stat(path, &st) == -1)
		return -EACCES;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	if (st.st_size) {
		data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (data == MAP_FAILED) {
			ret = -ENOMEM;
			goto out;
		}
	}

	ret = calc_digest(digest, data, st.st_size, algo);
out:
	if (data != MAP_FAILED)
		munmap(data, st.st_size);

	close(fd);
	return ret;
}

static struct digest_list_item *digest_list_generate(void)
{
	struct digest_list_item *digest_list;
	struct compact_list_hdr *hdr_array;
	u8 *buf_ptr;
	u32 num_digest_blocks = 0;
	u8 digest[64];
	int ret, i, j;

	digest_list = calloc(1, sizeof(*digest_list));
	if (!digest_list)
		return NULL;

	while (!num_digest_blocks) {
		ret = getrandom(&num_digest_blocks,
				sizeof(num_digest_blocks), 0);
		if (ret < 0)
			goto out;

		num_digest_blocks = num_digest_blocks % MAX_DIGEST_BLOCKS;
	}

	hdr_array = calloc(num_digest_blocks, sizeof(*hdr_array));
	if (!hdr_array)
		goto out;

	for (i = 0; i < num_digest_blocks; i++) {
		ret = getrandom(&hdr_array[i], sizeof(hdr_array[i]), 0);
		if (ret < 0)
			goto out;

		hdr_array[i].version = 1;
		/* COMPACT_DIGEST_LIST type is not allowed. */
		hdr_array[i].type = hdr_array[i].type % (COMPACT__LAST - 1);
		hdr_array[i].modifiers =
		    hdr_array[i].modifiers % (1 << COMPACT_MOD_IMMUTABLE) + 1;
		hdr_array[i].algo = hdr_array[i].algo % HASH_ALGO_RIPE_MD_128;
		hdr_array[i].count = hdr_array[i].count % MAX_DIGEST_COUNT;

		while (!hdr_array[i].count) {
			ret = getrandom(&hdr_array[i].count,
					sizeof(hdr_array[i].count), 0);
			if (ret < 0)
				goto out;

			hdr_array[i].count =
				hdr_array[i].count % MAX_DIGEST_COUNT;
		}

		hdr_array[i].datalen =
		    hdr_array[i].count * hash_digest_size[hdr_array[i].algo];

		digest_list->size += sizeof(*hdr_array) + hdr_array[i].datalen;
	}

	digest_list->buf = calloc(digest_list->size, sizeof(unsigned char));
	if (!digest_list->buf) {
		free(digest_list);
		ret = -ENOMEM;
		goto out;
	}

	buf_ptr = digest_list->buf;

	for (i = 0; i < num_digest_blocks; i++) {
		memcpy(buf_ptr, &hdr_array[i], sizeof(*hdr_array));
		buf_ptr += sizeof(*hdr_array);

		for (j = 0; j < hdr_array[i].count; j++) {
			ret = getrandom(buf_ptr, sizeof(u32), 0);
			if (ret < 0)
				goto out;

			*(u32 *)buf_ptr = *(u32 *)buf_ptr % MAX_DIGEST_VALUE;
			buf_ptr += hash_digest_size[hdr_array[i].algo];
		}
	}

	digest_list->algo = get_ima_hash_algo();
	if (digest_list->algo == HASH_ALGO__LAST) {
		ret = -ENOENT;
		goto out;
	}

	ret = calc_digest(digest, digest_list->buf, digest_list->size,
			  digest_list->algo);
	if (ret < 0)
		goto out;

	_bin2hex(digest_list->digest_str, digest,
		 hash_digest_size[digest_list->algo]);

	ret = 0;
out:
	if (ret < 0) {
		free(digest_list->buf);
		free(digest_list);
	}

	free(hdr_array);
	return !ret ? digest_list : NULL;
}

static struct digest_list_item *digest_list_generate_random(void)
{
	struct digest_list_item *digest_list;
	struct compact_list_hdr *hdr;
	u32 size = 0;
	u8 digest[64];
	int ret;

	digest_list = calloc(1, sizeof(*digest_list));
	if (!digest_list)
		return NULL;

	while (!size) {
		ret = getrandom(&size, sizeof(size), 0);
		if (ret < 0)
			goto out;

		size = size % MAX_DIGEST_LIST_SIZE;
	}

	digest_list->size = size;
	digest_list->buf = calloc(digest_list->size, sizeof(unsigned char));
	if (!digest_list->buf) {
		free(digest_list);
		ret = -ENOMEM;
		goto out;
	}

	ret = getrandom(digest_list->buf, digest_list->size, 0);
	if (ret < 0)
		goto out;

	hdr = (struct compact_list_hdr *)digest_list->buf;
	hdr->version = 1;
	hdr->type = hdr->type % (COMPACT__LAST - 1);
	hdr->algo = hdr->algo % HASH_ALGO__LAST;

	digest_list->algo = get_ima_hash_algo();
	if (digest_list->algo == HASH_ALGO__LAST) {
		ret = -ENOENT;
		goto out;
	}

	ret = calc_digest(digest, digest_list->buf, digest_list->size,
			  digest_list->algo);
	if (ret < 0)
		goto out;

	_bin2hex(digest_list->digest_str, digest,
		 hash_digest_size[digest_list->algo]);

	ret = 0;
out:
	if (ret < 0) {
		free(digest_list->buf);
		free(digest_list);
	}

	return !ret ? digest_list : NULL;
}

static int digest_list_upload(struct digest_list_item *digest_list, enum ops op,
			      enum upload_types upload_type, char *parser_path,
			      char *parser_mode)
{
	char path_template[] = DIGEST_LIST_PATH_TEMPLATE;
	char *path_upload = DIGEST_LIST_ADD_PATH, *basename;
	unsigned char *buffer = digest_list->buf;
	size_t buffer_len = digest_list->size;
	unsigned char rnd[3];
	int ret = 0, fd;

	if (op == DIGEST_LIST_ADD) {
		if (upload_type == UPLOAD_FILE) {
			fd = mkstemp(path_template);
			if (fd < 0)
				return -EPERM;

			close(fd);
			ret = write_buffer(path_template,
					   (char *)digest_list->buf,
					   digest_list->size);
			if (ret < 0)
				goto out;

			buffer = (unsigned char *)path_template;
			buffer_len = strlen(path_template);
		} else {
			ret = getrandom(rnd, sizeof(rnd), 0);
			if (ret < 0)
				goto out;

			_bin2hex(path_template +
				 sizeof(DIGEST_LIST_PATH_TEMPLATE) - 7, rnd,
				 sizeof(rnd));
		}

		memcpy(digest_list->filename_suffix,
		       path_template + sizeof(DIGEST_LIST_PATH_TEMPLATE) - 7,
		       6);
	} else {
		memcpy(path_template + sizeof(DIGEST_LIST_PATH_TEMPLATE) - 7,
		       digest_list->filename_suffix, 6);
		path_upload = DIGEST_LIST_DEL_PATH;
		if (upload_type == UPLOAD_FILE) {
			buffer = (unsigned char *)path_template;
			buffer_len = strlen(path_template);
		}
	}

	if (upload_type == UPLOAD_BUFFER) {
		basename = strrchr(path_template, '/');
		ret = write_buffer(DIGEST_LABEL_PATH, basename,
				   strlen(basename));
		if (ret < 0)
			goto out;
	}

	ret = write_buffer(path_upload, (char *)buffer, buffer_len);
out:
	if (ret < 0 || op == DIGEST_LIST_DEL)
		unlink(path_template);

	return ret;
}

static int digest_list_check(struct digest_list_item *digest_list, enum ops op)
{
	char path[PATH_MAX];
	u8 digest_list_buf[MAX_LINE_LENGTH];
	char digest_list_info[MAX_LINE_LENGTH];
	ssize_t size = digest_list->size;
	struct compact_list_hdr *hdr;
	struct stat st;
	int ret = 0, i, fd, path_len, len, read_len;

	path_len = snprintf(path, sizeof(path), "%s/%s-%s-digest_list.%s.ascii",
			    DIGEST_LISTS_LOADED_PATH,
			    hash_algo_name[digest_list->algo],
			    digest_list->digest_str,
			    digest_list->filename_suffix);

	path[path_len - 6] = '\0';

	if (op == DIGEST_LIST_DEL) {
		if (stat(path, &st) != -1)
			return -EEXIST;

		path[path_len - 6] = '.';

		if (stat(path, &st) != -1)
			return -EEXIST;

		return 0;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	while (size) {
		len = read(fd, digest_list_buf, sizeof(digest_list_buf));
		if (len <= 0) {
			ret = -errno;
			goto out;
		}

		if (memcmp(digest_list_buf,
			   digest_list->buf + digest_list->size - size, len)) {
			ret = -EINVAL;
			goto out;
		}

		size -= len;
	}

	close(fd);

	path[path_len - 6] = '.';

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	size = digest_list->size;
	while (size) {
		hdr = (struct compact_list_hdr *)(digest_list->buf +
						  digest_list->size - size);

		len = snprintf(digest_list_info, sizeof(digest_list_info),
			       HDR_ASCII_FMT, digest_list->actions,
			       hdr->version, hash_algo_name[hdr->algo],
			       hdr->type, hdr->modifiers, hdr->count,
			       hdr->datalen);

		read_len = read(fd, digest_list_buf, len);

		if (read_len != len ||
		    memcmp(digest_list_info, digest_list_buf, len)) {
			ret = -EIO;
			goto out;
		}

		size -= sizeof(*hdr);

		for (i = 0; i < hdr->count; i++) {
			_bin2hex(digest_list_info,
				 digest_list->buf + digest_list->size - size,
				 hash_digest_size[hdr->algo]);

			read_len = read(fd, digest_list_buf,
					hash_digest_size[hdr->algo] * 2 + 1);

			if (read_len != hash_digest_size[hdr->algo] * 2 + 1 ||
			    memcmp(digest_list_info, digest_list_buf,
				   read_len - 1) ||
				   digest_list_buf[read_len - 1] != '\n') {
				ret = -EIO;
				goto out;
			}

			size -= hash_digest_size[hdr->algo];
		}
	}
out:
	close(fd);
	return ret;
}

static int digest_list_query(u8 *digest, enum hash_algo algo,
			     char **query_result)
{
	ssize_t len, to_write, written;
	char query[256] = { 0 };
	size_t query_result_len;
	int ret = 0, fd;

	len = snprintf(query, sizeof(query), "%s-", hash_algo_name[algo]);

	_bin2hex(query + len, digest, hash_digest_size[algo]);
	len += hash_digest_size[algo] * 2 + 1;

	fd = open(DIGEST_QUERY_PATH, O_WRONLY);
	if (fd < 0)
		return -errno;

	to_write = len;

	while (to_write) {
		written = write(fd, query + len - to_write, to_write);
		if (written <= 0) {
			ret = -errno;
			break;
		}

		to_write -= written;
	}

	close(fd);
	if (ret < 0)
		return ret;

	return read_buffer(DIGEST_QUERY_PATH, query_result, &query_result_len,
			   true, true);
}

static int *get_count_gen_lists(u8 *digest, enum hash_algo algo)
{
	struct compact_list_hdr *hdr;
	u8 *buf_ptr;
	loff_t size;
	int i, j, *count;

	count = calloc(num_max_digest_lists, sizeof(*count));
	if (!count)
		return count;

	for (i = 0; i < num_max_digest_lists; i++) {
		if (!digest_lists[i])
			continue;

		size = digest_lists[i]->size;
		buf_ptr = digest_lists[i]->buf;

		while (size) {
			hdr = (struct compact_list_hdr *)buf_ptr;
			if (hdr->algo != algo) {
				buf_ptr += sizeof(*hdr) + hdr->datalen;
				size -= sizeof(*hdr) + hdr->datalen;
				continue;
			}

			buf_ptr += sizeof(*hdr);
			size -= sizeof(*hdr);

			for (j = 0; j < hdr->count; j++) {
				if (!memcmp(digest, buf_ptr,
				    hash_digest_size[algo]))
					count[i]++;
				buf_ptr += hash_digest_size[algo];
				size -= hash_digest_size[algo];
			}
		}
	}

	return count;
}

static int *get_count_kernel_query(u8 *digest, enum hash_algo algo)
{
	char *query_result = NULL, *query_result_ptr, *line;
	char digest_list_info[MAX_LINE_LENGTH];
	char label[256];
	struct compact_list_hdr *hdr;
	struct digest_list_item *digest_list;
	size_t size, size_info;
	int ret, i, *count = NULL;

	count = calloc(num_max_digest_lists, sizeof(*count));
	if (!count)
		return count;

	ret = digest_list_query(digest, algo, &query_result);
	if (ret < 0)
		goto out;

	query_result_ptr = query_result;

	while ((line = strsep(&query_result_ptr, "\n"))) {
		if (!strlen(line))
			continue;

		for (i = 0; i < num_max_digest_lists; i++) {
			if (!digest_lists[i])
				continue;

			digest_list = digest_lists[i];
			size = digest_list->size;

			while (size) {
				hdr =
				  (struct compact_list_hdr *)(digest_list->buf +
						digest_list->size - size);
				size -= sizeof(*hdr) + hdr->datalen;

				snprintf(label, sizeof(label),
					 "%s-%s-digest_list.%s",
					 hash_algo_name[digest_list->algo],
					 digest_list->digest_str,
					 digest_list->filename_suffix);

				/* From digest_query_show(). */
				size_info = snprintf(digest_list_info,
					 sizeof(digest_list_info),
					 QUERY_RESULT_FMT, label,
					 digest_list->actions, hdr->version,
					 hash_algo_name[hdr->algo], hdr->type,
					 hdr->modifiers, hdr->count,
					 hdr->datalen);

				/* strsep() replaced '\n' with '\0' in line. */
				digest_list_info[size_info - 1] = '\0';

				if (!strcmp(digest_list_info, line)) {
					count[i]++;
					break;
				}
			}
		}
	}
out:
	free(query_result);
	if (ret < 0)
		free(count);

	return (!ret) ? count : NULL;
}

static int compare_count(u32 value, enum hash_algo algo,
			 struct __test_metadata *_metadata)
{
	int *count_gen_list_array, *count_kernel_query_array;
	int count_gen_list = 0, count_kernel_query = 0;
	u8 digest[64] = { 0 };
	int i;

	*(u32 *)digest = value;

	count_gen_list_array = get_count_gen_lists(digest, algo);
	if (!count_gen_list_array)
		return -EINVAL;

	count_kernel_query_array = get_count_kernel_query(digest, algo);
	if (!count_kernel_query_array) {
		free(count_gen_list_array);
		return -EINVAL;
	}

	for (i = 0; i < num_max_digest_lists; i++) {
		count_gen_list += count_gen_list_array[i];
		count_kernel_query += count_kernel_query_array[i];
	}

	TH_LOG("value: %d, algo: %s, gen list digests: %d, kernel digests: %d",
	       value, hash_algo_name[algo], count_gen_list, count_kernel_query);
	free(count_gen_list_array);
	free(count_kernel_query_array);
	return (count_gen_list == count_kernel_query) ? 0 : -EINVAL;
}

static void digest_list_delete_all(struct __test_metadata *_metadata,
				   enum upload_types upload_type,
				   char *parser_path)
{
	int ret, i;

	for (i = 0; i < MAX_DIGEST_LISTS; i++) {
		if (!digest_lists[i])
			continue;

		ret = digest_list_upload(digest_lists[i], DIGEST_LIST_DEL,
					 upload_type, parser_path, "normal");
		ASSERT_EQ(0, ret) {
			TH_LOG("digest_list_upload() failed\n");
		}

		free(digest_lists[i]->buf);
		free(digest_lists[i]);
		digest_lists[i] = NULL;
	}
}

FIXTURE(test)
{
	enum upload_types upload_type;
};

FIXTURE_SETUP(test)
{
}

FIXTURE_TEARDOWN(test)
{
	digest_list_delete_all(_metadata, self->upload_type, NULL);
}

static int enable_fault_injection(void)
{
	int ret;

	ret = write_buffer(DIGEST_LIST_DEBUGFS_TASK_FILTER, "Y", 1);
	if (ret < 0)
		return ret;

	ret = write_buffer(DIGEST_LIST_DEBUGFS_PROBABILITY, "1", 1);
	if (ret < 0)
		return ret;

	ret = write_buffer(DIGEST_LIST_DEBUGFS_TIMES, "10000", 5);
	if (ret < 0)
		return ret;

	ret = write_buffer(DIGEST_LIST_DEBUGFS_VERBOSE, "1", 1);
	if (ret < 0)
		return ret;

	ret = write_buffer(PROCFS_SELF_FAULT, "1", 1);
	if (ret < 0)
		return ret;

	return 0;
}

static void digest_list_add_del_test(struct __test_metadata *_metadata,
				     int fault_injection,
				     enum upload_types upload_type)
{
	u32 value;
	enum ops op;
	enum hash_algo algo;
	int ret, i, cur_queries = 1;

	while (cur_queries <= NUM_QUERIES) {
		ret = getrandom(&op, 1, 0);
		ASSERT_EQ(1, ret) {
			TH_LOG("getrandom() failed\n");
		}

		op = op % 2;

		switch (op) {
		case DIGEST_LIST_ADD:
			TH_LOG("add digest list...");
			for (digest_lists_pos = 0;
			     digest_lists_pos < num_max_digest_lists;
			     digest_lists_pos++)
				if (!digest_lists[digest_lists_pos])
					break;

			if (digest_lists_pos == num_max_digest_lists)
				continue;

			digest_lists[digest_lists_pos] = digest_list_generate();
			ASSERT_NE(NULL, digest_lists[digest_lists_pos]) {
				TH_LOG("digest_list_generate() failed");
			}

			ret = digest_list_upload(digest_lists[digest_lists_pos],
						 op, upload_type, NULL, NULL);
			/* Handle failures from fault injection. */
			if (fault_injection && ret < 0) {
				TH_LOG("handle failure...");
				ret = digest_list_check(
						digest_lists[digest_lists_pos],
						DIGEST_LIST_DEL);
				ASSERT_EQ(0, ret) {
					TH_LOG("digest_list_check() failed");
				}

				free(digest_lists[digest_lists_pos]->buf);
				free(digest_lists[digest_lists_pos]);
				digest_lists[digest_lists_pos] = NULL;
				break;
			}

			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_upload() failed");
			}

			ret = digest_list_check(digest_lists[digest_lists_pos],
						op);
			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_check() failed");
			}

			break;
		case DIGEST_LIST_DEL:
			TH_LOG("delete digest list...");
			for (digest_lists_pos = 0;
			     digest_lists_pos < num_max_digest_lists;
			     digest_lists_pos++)
				if (digest_lists[digest_lists_pos])
					break;

			if (digest_lists_pos == num_max_digest_lists)
				continue;

			for (i = 0; i < MAX_SEARCH_ATTEMPTS; i++) {
				ret = getrandom(&digest_lists_pos,
						sizeof(digest_lists_pos), 0);
				ASSERT_EQ(sizeof(digest_lists_pos), ret) {
					TH_LOG("getrandom() failed");
				}

				digest_lists_pos =
					digest_lists_pos % num_max_digest_lists;

				if (digest_lists[digest_lists_pos])
					break;
			}

			if (i == MAX_SEARCH_ATTEMPTS) {
				for (digest_lists_pos = 0;
				     digest_lists_pos < num_max_digest_lists;
				     digest_lists_pos++)
					if (digest_lists[digest_lists_pos])
						break;

				if (digest_lists_pos == num_max_digest_lists)
					continue;
			}

			ret = digest_list_upload(digest_lists[digest_lists_pos],
						 op, upload_type, NULL, NULL);
			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_upload() failed");
			}

			ret = digest_list_check(digest_lists[digest_lists_pos],
						op);
			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_check() failed");
			}

			free(digest_lists[digest_lists_pos]->buf);
			free(digest_lists[digest_lists_pos]);
			digest_lists[digest_lists_pos] = NULL;
			break;
		default:
			break;
		}

		ret = getrandom(&value, sizeof(value), 0);
		ASSERT_EQ(sizeof(value), ret) {
			TH_LOG("getrandom() failed");
		}

		value = value % 10;

		if (value != 1)
			continue;

		ret = getrandom(&value, sizeof(value), 0);
		ASSERT_EQ(sizeof(value), ret) {
			TH_LOG("getrandom() failed");
		}

		value = value % MAX_DIGEST_VALUE;

		ret = getrandom(&algo, sizeof(algo), 0);
		ASSERT_EQ(sizeof(algo), ret) {
			TH_LOG("getrandom() failed");
		}

		algo = algo % HASH_ALGO_RIPE_MD_128;

		ret = compare_count(value, algo, _metadata);
		ASSERT_EQ(0, ret) {
			TH_LOG("count mismatch");
		}

		TH_LOG("query digest lists (%d/%d)...", cur_queries,
		       NUM_QUERIES);

		cur_queries++;
	}
}

TEST_F_TIMEOUT(test, digest_list_add_del_test_file_upload, UINT_MAX)
{
	self->upload_type = UPLOAD_FILE;
	digest_list_add_del_test(_metadata, 0, self->upload_type);
}

TEST_F_TIMEOUT(test, digest_list_add_del_test_file_upload_fault, UINT_MAX)
{
	int ret;

	self->upload_type = UPLOAD_FILE;

	ret = enable_fault_injection();
	ASSERT_EQ(0, ret) {
		TH_LOG("enable_fault_injection() failed");
	}

	digest_list_add_del_test(_metadata, 1, self->upload_type);
}

TEST_F_TIMEOUT(test, digest_list_add_del_test_buffer_upload, UINT_MAX)
{
	self->upload_type = UPLOAD_BUFFER;
	digest_list_add_del_test(_metadata, 0, self->upload_type);
}

TEST_F_TIMEOUT(test, digest_list_add_del_test_buffer_upload_fault, UINT_MAX)
{
	int ret;

	self->upload_type = UPLOAD_BUFFER;

	ret = enable_fault_injection();
	ASSERT_EQ(0, ret) {
		TH_LOG("enable_fault_injection() failed");
	}

	digest_list_add_del_test(_metadata, 1, self->upload_type);
}

FIXTURE(test_fuzzing)
{
};

FIXTURE_SETUP(test_fuzzing)
{
}

FIXTURE_TEARDOWN(test_fuzzing)
{
}

TEST_F_TIMEOUT(test_fuzzing, digest_list_fuzzing_test, UINT_MAX)
{
	char digests_count_before[256] = { 0 };
	char *digests_count_before_ptr = digests_count_before;
	char digests_count_after[256] = { 0 };
	char *digests_count_after_ptr = digests_count_after;
	size_t len = sizeof(digests_count_before) - 1;
	int ret, i;

	ret = read_buffer(DIGESTS_COUNT, &digests_count_before_ptr, &len,
			  false, true);
	ASSERT_EQ(0, ret) {
		TH_LOG("read_buffer() failed");
	}

	for (i = 1; i <= NUM_ITERATIONS; i++) {
		TH_LOG("add digest list (%d/%d)...", i, NUM_ITERATIONS);

		digest_lists[0] = digest_list_generate_random();
		ASSERT_NE(NULL, digest_lists[0]) {
			TH_LOG("digest_list_generate() failed");
		}

		ret = digest_list_upload(digest_lists[0], DIGEST_LIST_ADD,
					 UPLOAD_FILE, NULL, NULL);
		if (!ret) {
			ret = digest_list_check(digest_lists[0],
						DIGEST_LIST_ADD);
			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_check() failed");
			}

			ret = digest_list_upload(digest_lists[0],
						 DIGEST_LIST_DEL, UPLOAD_FILE,
						 NULL, NULL);
			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_upload() failed");
			}

			ret = digest_list_check(digest_lists[0],
						DIGEST_LIST_DEL);
			ASSERT_EQ(0, ret) {
				TH_LOG("digest_list_check() failed");
			}
		}

		free(digest_lists[0]->buf);
		free(digest_lists[0]);
		digest_lists[0] = NULL;
	}

	ret = read_buffer(DIGESTS_COUNT, &digests_count_after_ptr, &len, false,
			  true);
	ASSERT_EQ(0, ret) {
		TH_LOG("read_buffer() failed");
	}

	ASSERT_STREQ(digests_count_before, digests_count_after);
}

TEST_HARNESS_MAIN
