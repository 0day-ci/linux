#!/bin/sh
# Copyright (c) 2021 Alexey Dobriyan <adobriyan@gmail.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# Generate Qt Creator project files.
set -eo pipefail

test -n "$1" || { echo >&2 "usage: $0 project-name"; exit 1; }
p="$1"

filename="$p.cflags"
echo "$filename"
echo '-std=gnu89' >"$filename"

filename="$p.config"
echo $filename
cat <<EOF >$filename
#define __KERNEL__
#include <linux/kconfig.h>
EOF

filename="$p.creator"
echo $filename
echo '[General]' >$filename

filename="$p.cxxflags"
echo $filename
echo '-std=gnu++17' >$filename

filename="$p.files"
echo $filename
# ibmvnic.c: workaround https://bugzilla.redhat.com/show_bug.cgi?id=1886548
git ls-tree -r HEAD --name-only | LC_ALL=C sort | grep -v -e 'ibmvnic.c' >$filename

filename="$p.includes"
echo $filename
echo 'include' >$filename
for i in arch/*/include; do echo $i; done | LC_ALL=C sort >>$filename
