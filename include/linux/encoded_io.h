/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ENCODED_IO_H
#define _LINUX_ENCODED_IO_H

#include <uapi/linux/encoded_io.h>

struct encoded_iov;
struct iov_iter;
struct kiocb;
extern int generic_encoded_write_checks(struct kiocb *,
					const struct encoded_iov *);
extern int copy_encoded_iov_from_iter(struct encoded_iov *, struct iov_iter *);
extern ssize_t generic_encoded_read_checks(struct kiocb *, struct iov_iter *);
extern int copy_encoded_iov_to_iter(const struct encoded_iov *,
				    struct iov_iter *);

#endif /* _LINUX_ENCODED_IO_H */
