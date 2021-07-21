// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/lib/string.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * stupid library routines.. The optimized versions should generally be found
 * as inline code in <asm-xx/string.h>
 *
 * These are buggy as well..
 *
 * * Fri Jun 25 1999, Ingo Oeser <ioe@informatik.tu-chemnitz.de>
 * -  Added strsep() which will replace strtok() soon (because strsep() is
 *    reentrant and should be faster). Use only strsep() in new code, please.
 *
 * * Sat Feb 09 2002, Jason Thomas <jason@topic.com.au>,
 *                    Matthew Hawkins <matt@mh.dropbear.id.au>
 * -  Kissed strtok() goodbye
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/word-at-a-time.h>
#include <asm/page.h>

#ifndef __HAVE_ARCH_STRNCASECMP
/**
 * strncasecmp - Case insensitive, length-limited string comparison
 * @s1: One string
 * @s2: The other string
 * @len: the maximum number of characters to compare
 */
int strncasecmp(const char *s1, const char *s2, size_t len)
{
	/* Yes, Virginia, it had better be unsigned */
	unsigned char c1, c2;

	if (!len)
		return 0;

	do {
		c1 = *s1++;
		c2 = *s2++;
		if (!c1 || !c2)
			break;
		if (c1 == c2)
			continue;
		c1 = tolower(c1);
		c2 = tolower(c2);
		if (c1 != c2)
			break;
	} while (--len);
	return (int)c1 - (int)c2;
}
EXPORT_SYMBOL(strncasecmp);
#endif

#ifndef __HAVE_ARCH_STRCASECMP
int strcasecmp(const char *s1, const char *s2)
{
	int c1, c2;

	do {
		c1 = tolower(*s1++);
		c2 = tolower(*s2++);
	} while (c1 == c2 && c1 != 0);
	return c1 - c2;
}
EXPORT_SYMBOL(strcasecmp);
#endif

#ifndef __HAVE_ARCH_STRCPY
/**
 * strcpy - Copy a %NUL terminated string
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 */
char *strcpy(char *dest, const char *src)
{
	char *tmp = dest;

	while ((*dest++ = *src++) != '\0')
		/* nothing */;
	return tmp;
}
EXPORT_SYMBOL(strcpy);
#endif

#ifndef __HAVE_ARCH_STRNCPY
/**
 * strncpy - Copy a length-limited, C-string
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: The maximum number of bytes to copy
 *
 * The result is not %NUL-terminated if the source exceeds
 * @count bytes.
 *
 * In the case where the length of @src is less than  that  of
 * count, the remainder of @dest will be padded with %NUL.
 *
 */
char *strncpy(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	while (count) {
		if ((*tmp = *src) != 0)
			src++;
		tmp++;
		count--;
	}
	return dest;
}
EXPORT_SYMBOL(strncpy);
#endif

#ifndef __HAVE_ARCH_STRLCPY
/**
 * strlcpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with ``*BSD``: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
EXPORT_SYMBOL(strlcpy);
#endif

#ifndef __HAVE_ARCH_STRSCPY
/**
 * strscpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: Size of destination buffer
 *
 * Copy the string, or as much of it as fits, into the dest buffer.  The
 * behavior is undefined if the string buffers overlap.  The destination
 * buffer is always NUL terminated, unless it's zero-sized.
 *
 * Preferred to strlcpy() since the API doesn't require reading memory
 * from the src string beyond the specified "count" bytes, and since
 * the return value is easier to error-check than strlcpy()'s.
 * In addition, the implementation is robust to the string changing out
 * from underneath it, unlike the current strlcpy() implementation.
 *
 * Preferred to strncpy() since it always returns a valid string, and
 * doesn't unnecessarily force the tail of the destination buffer to be
 * zeroed.  If zeroing is desired please use strscpy_pad().
 *
 * Returns:
 * * The number of characters copied (not including the trailing %NUL)
 * * -E2BIG if count is 0 or @src was truncated.
 */
ssize_t strscpy(char *dest, const char *src, size_t count)
{
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
	size_t max = count;
	long res = 0;

	if (count == 0 || WARN_ON_ONCE(count > INT_MAX))
		return -E2BIG;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	/*
	 * If src is unaligned, don't cross a page boundary,
	 * since we don't know if the next page is mapped.
	 */
	if ((long)src & (sizeof(long) - 1)) {
		size_t limit = PAGE_SIZE - ((long)src & (PAGE_SIZE - 1));
		if (limit < max)
			max = limit;
	}
#else
	/* If src or dest is unaligned, don't do word-at-a-time. */
	if (((long) dest | (long) src) & (sizeof(long) - 1))
		max = 0;
#endif

	while (max >= sizeof(unsigned long)) {
		unsigned long c, data;

		c = read_word_at_a_time(src+res);
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			*(unsigned long *)(dest+res) = c & zero_bytemask(data);
			return res + find_zero(data);
		}
		*(unsigned long *)(dest+res) = c;
		res += sizeof(unsigned long);
		count -= sizeof(unsigned long);
		max -= sizeof(unsigned long);
	}

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (res)
		dest[res-1] = '\0';

	return -E2BIG;
}
EXPORT_SYMBOL(strscpy);
#endif

/**
 * strscpy_pad() - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: Size of destination buffer
 *
 * Copy the string, or as much of it as fits, into the dest buffer.  The
 * behavior is undefined if the string buffers overlap.  The destination
 * buffer is always %NUL terminated, unless it's zero-sized.
 *
 * If the source string is shorter than the destination buffer, zeros
 * the tail of the destination buffer.
 *
 * For full explanation of why you may want to consider using the
 * 'strscpy' functions please see the function docstring for strscpy().
 *
 * Returns:
 * * The number of characters copied (not including the trailing %NUL)
 * * -E2BIG if count is 0 or @src was truncated.
 */
ssize_t strscpy_pad(char *dest, const char *src, size_t count)
{
	ssize_t written;

	written = strscpy(dest, src, count);
	if (written < 0 || written == count - 1)
		return written;

	memset(dest + written + 1, 0, count - written - 1);

	return written;
}
EXPORT_SYMBOL(strscpy_pad);

/**
 * stpcpy - copy a string from src to dest returning a pointer to the new end
 *          of dest, including src's %NUL-terminator. May overrun dest.
 * @dest: pointer to end of string being copied into. Must be large enough
 *        to receive copy.
 * @src: pointer to the beginning of string being copied from. Must not overlap
 *       dest.
 *
 * stpcpy differs from strcpy in a key way: the return value is a pointer
 * to the new %NUL-terminating character in @dest. (For strcpy, the return
 * value is a pointer to the start of @dest). This interface is considered
 * unsafe as it doesn't perform bounds checking of the inputs. As such it's
 * not recommended for usage. Instead, its definition is provided in case
 * the compiler lowers other libcalls to stpcpy.
 */
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src);
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src)
{
	while ((*dest++ = *src++) != '\0')
		/* nothing */;
	return --dest;
}
EXPORT_SYMBOL(stpcpy);

#ifndef __HAVE_ARCH_STRCAT
/**
 * strcat - Append one %NUL-terminated string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 */
char *strcat(char *dest, const char *src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++) != '\0')
		;
	return tmp;
}
EXPORT_SYMBOL(strcat);
#endif

#ifndef __HAVE_ARCH_STRNCAT
/**
 * strncat - Append a length-limited, C-string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 * @count: The maximum numbers of bytes to copy
 *
 * Note that in contrast to strncpy(), strncat() ensures the result is
 * terminated.
 */
char *strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++) != 0) {
			if (--count == 0) {
				*dest = '\0';
				break;
			}
		}
	}
	return tmp;
}
EXPORT_SYMBOL(strncat);
#endif

#ifndef __HAVE_ARCH_STRLCAT
/**
 * strlcat - Append a length-limited, C-string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 * @count: The size of the destination buffer.
 */
size_t strlcat(char *dest, const char *src, size_t count)
{
	size_t dsize = strlen(dest);
	size_t len = strlen(src);
	size_t res = dsize + len;

	/* This would be a bug */
	BUG_ON(dsize >= count);

	dest += dsize;
	count -= dsize;
	if (len >= count)
		len = count-1;
	memcpy(dest, src, len);
	dest[len] = 0;
	return res;
}
EXPORT_SYMBOL(strlcat);
#endif

#ifndef __HAVE_ARCH_STRCMP
/**
 * strcmp - Compare two strings
 * @cs: One string
 * @ct: Another string
 */
int strcmp(const char *cs, const char *ct)
{
	unsigned char c1, c2;

	while (1) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
	}
	return 0;
}
EXPORT_SYMBOL(strcmp);
#endif

#ifndef __HAVE_ARCH_STRNCMP
/**
 * strncmp - Compare two length-limited strings
 * @cs: One string
 * @ct: Another string
 * @count: The maximum number of bytes to compare
 */
int strncmp(const char *cs, const char *ct, size_t count)
{
	unsigned char c1, c2;

	while (count) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
		count--;
	}
	return 0;
}
EXPORT_SYMBOL(strncmp);
#endif

#ifndef __HAVE_ARCH_STRCHR
/**
 * strchr - Find the first occurrence of a character in a string
 * @s: The string to be searched
 * @c: The character to search for
 *
 * Note that the %NUL-terminator is considered part of the string, and can
 * be searched for.
 */
char *strchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}
EXPORT_SYMBOL(strchr);
#endif

#ifndef __HAVE_ARCH_STRCHRNUL
/**
 * strchrnul - Find and return a character in a string, or end of string
 * @s: The string to be searched
 * @c: The character to search for
 *
 * Returns pointer to first occurrence of 'c' in s. If c is not found, then
 * return a pointer to the null byte at the end of s.
 */
char *strchrnul(const char *s, int c)
{
	while (*s && *s != (char)c)
		s++;
	return (char *)s;
}
EXPORT_SYMBOL(strchrnul);
#endif

/**
 * strnchrnul - Find and return a character in a length limited string,
 * or end of string
 * @s: The string to be searched
 * @count: The number of characters to be searched
 * @c: The character to search for
 *
 * Returns pointer to the first occurrence of 'c' in s. If c is not found,
 * then return a pointer to the last character of the string.
 */
char *strnchrnul(const char *s, size_t count, int c)
{
	while (count-- && *s && *s != (char)c)
		s++;
	return (char *)s;
}

#ifndef __HAVE_ARCH_STRRCHR
/**
 * strrchr - Find the last occurrence of a character in a string
 * @s: The string to be searched
 * @c: The character to search for
 */
char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	do {
		if (*s == (char)c)
			last = s;
	} while (*s++);
	return (char *)last;
}
EXPORT_SYMBOL(strrchr);
#endif

#ifndef __HAVE_ARCH_STRNCHR
/**
 * strnchr - Find a character in a length limited string
 * @s: The string to be searched
 * @count: The number of characters to be searched
 * @c: The character to search for
 *
 * Note that the %NUL-terminator is considered part of the string, and can
 * be searched for.
 */
char *strnchr(const char *s, size_t count, int c)
{
	while (count--) {
		if (*s == (char)c)
			return (char *)s;
		if (*s++ == '\0')
			break;
	}
	return NULL;
}
EXPORT_SYMBOL(strnchr);
#endif

/**
 * skip_spaces - Removes leading whitespace from @str.
 * @str: The string to be stripped.
 *
 * Returns a pointer to the first non-whitespace character in @str.
 */
char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}
EXPORT_SYMBOL(skip_spaces);

/**
 * strim - Removes leading and trailing whitespace from @s.
 * @s: The string to be stripped.
 *
 * Note that the first trailing whitespace is replaced with a %NUL-terminator
 * in the given string @s. Returns a pointer to the first non-whitespace
 * character in @s.
 */
char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}
EXPORT_SYMBOL(strim);

#ifndef __HAVE_ARCH_STRLEN
/**
 * strlen - Find the length of a string
 * @s: The string to be sized
 */
size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
EXPORT_SYMBOL(strlen);
#endif

#ifndef __HAVE_ARCH_STRNLEN
/**
 * strnlen - Find the length of a length-limited string
 * @s: The string to be sized
 * @count: The maximum number of bytes to search
 */
size_t strnlen(const char *s, size_t count)
{
	const char *sc;

	for (sc = s; count-- && *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
EXPORT_SYMBOL(strnlen);
#endif

#ifndef __HAVE_ARCH_STRSPN
/**
 * strspn - Calculate the length of the initial substring of @s which only contain letters in @accept
 * @s: The string to be searched
 * @accept: The string to search for
 */
size_t strspn(const char *s, const char *accept)
{
	const char *p;
	const char *a;
	size_t count = 0;

	for (p = s; *p != '\0'; ++p) {
		for (a = accept; *a != '\0'; ++a) {
			if (*p == *a)
				break;
		}
		if (*a == '\0')
			return count;
		++count;
	}
	return count;
}

EXPORT_SYMBOL(strspn);
#endif

#ifndef __HAVE_ARCH_STRCSPN
/**
 * strcspn - Calculate the length of the initial substring of @s which does not contain letters in @reject
 * @s: The string to be searched
 * @reject: The string to avoid
 */
size_t strcspn(const char *s, const char *reject)
{
	const char *p;
	const char *r;
	size_t count = 0;

	for (p = s; *p != '\0'; ++p) {
		for (r = reject; *r != '\0'; ++r) {
			if (*p == *r)
				return count;
		}
		++count;
	}
	return count;
}
EXPORT_SYMBOL(strcspn);
#endif

#ifndef __HAVE_ARCH_STRPBRK
/**
 * strpbrk - Find the first occurrence of a set of characters
 * @cs: The string to be searched
 * @ct: The characters to search for
 */
char *strpbrk(const char *cs, const char *ct)
{
	const char *sc1, *sc2;

	for (sc1 = cs; *sc1 != '\0'; ++sc1) {
		for (sc2 = ct; *sc2 != '\0'; ++sc2) {
			if (*sc1 == *sc2)
				return (char *)sc1;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(strpbrk);
#endif

#ifndef __HAVE_ARCH_STRSEP
/**
 * strsep - Split a string into tokens
 * @s: The string to be searched
 * @ct: The characters to search for
 *
 * strsep() updates @s to point after the token, ready for the next call.
 *
 * It returns empty tokens, too, behaving exactly like the libc function
 * of that name. In fact, it was stolen from glibc2 and de-fancy-fied.
 * Same semantics, slimmer shape. ;)
 */
char *strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;

	if (sbegin == NULL)
		return NULL;

	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}
EXPORT_SYMBOL(strsep);
#endif

/**
 * sysfs_streq - return true if strings are equal, modulo trailing newline
 * @s1: one string
 * @s2: another string
 *
 * This routine returns true iff two strings are equal, treating both
 * NUL and newline-then-NUL as equivalent string terminations.  It's
 * geared for use with sysfs input strings, which generally terminate
 * with newlines but are compared against values without newlines.
 */
bool sysfs_streq(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}

	if (*s1 == *s2)
		return true;
	if (!*s1 && *s2 == '\n' && !s2[1])
		return true;
	if (*s1 == '\n' && !s1[1] && !*s2)
		return true;
	return false;
}
EXPORT_SYMBOL(sysfs_streq);

/**
 * match_string - matches given string in an array
 * @array:	array of strings
 * @n:		number of strings in the array or -1 for NULL terminated arrays
 * @string:	string to match with
 *
 * This routine will look for a string in an array of strings up to the
 * n-th element in the array or until the first NULL element.
 *
 * Historically the value of -1 for @n, was used to search in arrays that
 * are NULL terminated. However, the function does not make a distinction
 * when finishing the search: either @n elements have been compared OR
 * the first NULL element was found.
 *
 * Return:
 * index of a @string in the @array if matches, or %-EINVAL otherwise.
 */
int match_string(const char * const *array, size_t n, const char *string)
{
	int index;
	const char *item;

	for (index = 0; index < n; index++) {
		item = array[index];
		if (!item)
			break;
		if (!strcmp(item, string))
			return index;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(match_string);

/**
 * __sysfs_match_string - matches given string in an array
 * @array: array of strings
 * @n: number of strings in the array or -1 for NULL terminated arrays
 * @str: string to match with
 *
 * Returns index of @str in the @array or -EINVAL, just like match_string().
 * Uses sysfs_streq instead of strcmp for matching.
 *
 * This routine will look for a string in an array of strings up to the
 * n-th element in the array or until the first NULL element.
 *
 * Historically the value of -1 for @n, was used to search in arrays that
 * are NULL terminated. However, the function does not make a distinction
 * when finishing the search: either @n elements have been compared OR
 * the first NULL element was found.
 */
int __sysfs_match_string(const char * const *array, size_t n, const char *str)
{
	const char *item;
	int index;

	for (index = 0; index < n; index++) {
		item = array[index];
		if (!item)
			break;
		if (sysfs_streq(item, str))
			return index;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(__sysfs_match_string);

#ifndef __HAVE_ARCH_MEMSET
/**
 * memset - Fill a region of memory with the given value
 * @s: Pointer to the start of the area.
 * @c: The byte to fill the area with
 * @count: The size of the area.
 *
 * Do not use memset() to access IO space, use memset_io() instead.
 */
void *memset(void *s, int c, size_t count)
{
	char *xs = s;

	while (count--)
		*xs++ = c;
	return s;
}
EXPORT_SYMBOL(memset);
#endif

#ifndef __HAVE_ARCH_MEMSET16
/**
 * memset16() - Fill a memory area with a uint16_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint16_t instead
 * of a byte.  Remember that @count is the number of uint16_ts to
 * store, not the number of bytes.
 */
void *memset16(uint16_t *s, uint16_t v, size_t count)
{
	uint16_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
EXPORT_SYMBOL(memset16);
#endif

#ifndef __HAVE_ARCH_MEMSET32
/**
 * memset32() - Fill a memory area with a uint32_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint32_t instead
 * of a byte.  Remember that @count is the number of uint32_ts to
 * store, not the number of bytes.
 */
void *memset32(uint32_t *s, uint32_t v, size_t count)
{
	uint32_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
EXPORT_SYMBOL(memset32);
#endif

#ifndef __HAVE_ARCH_MEMSET64
/**
 * memset64() - Fill a memory area with a uint64_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint64_t instead
 * of a byte.  Remember that @count is the number of uint64_ts to
 * store, not the number of bytes.
 */
void *memset64(uint64_t *s, uint64_t v, size_t count)
{
	uint64_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
EXPORT_SYMBOL(memset64);
#endif

#ifndef __HAVE_ARCH_MEMCPY
/**
 * memcpy - Copy one area of memory to another
 * @dest: Where to copy to
 * @src: Where to copy from
 * @count: The size of the area.
 *
 * You should not use this function to access IO space, use memcpy_toio()
 * or memcpy_fromio() instead.
 */
void *memcpy(void *dest, const void *src, size_t count)
{
	char *tmp = dest;
	const char *s = src;

	while (count--)
		*tmp++ = *s++;
	return dest;
}
EXPORT_SYMBOL(memcpy);
#endif

#ifndef __HAVE_ARCH_MEMMOVE
/**
 * memmove - Copy one area of memory to another
 * @dest: Where to copy to
 * @src: Where to copy from
 * @count: The size of the area.
 *
 * Unlike memcpy(), memmove() copes with overlapping areas.
 */
void *memmove(void *dest, const void *src, size_t count)
{
	char *tmp;
	const char *s;

	if (dest <= src) {
		tmp = dest;
		s = src;
		while (count--)
			*tmp++ = *s++;
	} else {
		tmp = dest;
		tmp += count;
		s = src;
		s += count;
		while (count--)
			*--tmp = *--s;
	}
	return dest;
}
EXPORT_SYMBOL(memmove);
#endif

#ifndef __HAVE_ARCH_MEMCMP

/* Threshold value for when to enter the unrolled loops.  */
#define MEMCMP_THRESH 16

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define MERGE(w0, sh_1, w1, sh_2) (((w0) >> (sh_1)) | ((w1) << (sh_2)))
# define CMP_LT_OR_GT(a, b) memcmp_bytes((a), (b))
/*
 * Compare A and B bytewise in the byte order of the machine.
 * A and B are known to be different. This is needed only on little-endian
 * machines.
 */
static inline int memcmp_bytes(unsigned long a, unsigned long b)
{
	long srcp1 = (long) &a;
	long srcp2 = (long) &b;
	unsigned long a0, b0;

	do {
		a0 = ((uint8_t *) srcp1)[0];
		b0 = ((uint8_t *) srcp2)[0];
		srcp1 += 1;
		srcp2 += 1;
	} while (a0 == b0);
	return a0 - b0;
}

#else
# define MERGE(w0, sh_1, w1, sh_2) (((w0) << (sh_1)) | ((w1) >> (sh_2)))
# define CMP_LT_OR_GT(a, b) ((a) > (b) ? 1 : -1)
#endif

/*
 * The strategy of this memcmp is:
 *  1. Compare bytes until one of the block pointers is aligned.
 *
 *  2. Compare using memcmp_common_alignment or memcmp_not_common_alignment,
 *  regarding the alignment of the other block after the initial byte operations.
 *  The maximum number of full words (of type unsigned long) are compared in
 *  this way.
 *
 *  3. Compare the few remaining bytes.
 */

/*
 * Compare blocks at SRCP1 and SRCP2 with LEN `unsigned long' objects (not LEN
 * bytes!). Both SRCP1 and SRCP2 should be aligned for memory operations on
 * `unsigned long's.
 */
static int memcmp_common_alignment(long srcp1, long srcp2, size_t len)
{
	unsigned long a0, a1;
	unsigned long b0, b1;

	switch (len % 4) {
	default: /* Avoid warning about uninitialized local variables.  */
	case 2:
		a0 = ((unsigned long *) srcp1)[0];
		b0 = ((unsigned long *) srcp2)[0];
		srcp1 -= 2 * sizeof(unsigned long);
		srcp2 -= 2 * sizeof(unsigned long);
		len += 2;
		goto do1;
	case 3:
		a1 = ((unsigned long *) srcp1)[0];
		b1 = ((unsigned long *) srcp2)[0];
		srcp1 -= sizeof(unsigned long);
		srcp2 -= sizeof(unsigned long);
		len += 1;
		goto do2;
	case 0:
		if (MEMCMP_THRESH <= 3 * sizeof(unsigned long) && len == 0)
			return 0;
		a0 = ((unsigned long *) srcp1)[0];
		b0 = ((unsigned long *) srcp2)[0];
		goto do3;
	case 1:
		a1 = ((unsigned long *) srcp1)[0];
		b1 = ((unsigned long *) srcp2)[0];
		srcp1 += sizeof(unsigned long);
		srcp2 += sizeof(unsigned long);
		len -= 1;
		if (MEMCMP_THRESH <= 3 * sizeof(unsigned long) && len == 0)
			goto do0;
		/* Fallthrough */
	}

	do {
		a0 = ((unsigned long *) srcp1)[0];
		b0 = ((unsigned long *) srcp2)[0];
		if (a1 != b1)
			return CMP_LT_OR_GT(a1, b1);

do3:
		a1 = ((unsigned long *) srcp1)[1];
		b1 = ((unsigned long *) srcp2)[1];
		if (a0 != b0)
			return CMP_LT_OR_GT(a0, b0);

do2:
		a0 = ((unsigned long *) srcp1)[2];
		b0 = ((unsigned long *) srcp2)[2];
		if (a1 != b1)
			return CMP_LT_OR_GT(a1, b1);

do1:
		a1 = ((unsigned long *) srcp1)[3];
		b1 = ((unsigned long *) srcp2)[3];
		if (a0 != b0)
			return CMP_LT_OR_GT(a0, b0);

		srcp1 += 4 * sizeof(unsigned long);
		srcp2 += 4 * sizeof(unsigned long);
		len -= 4;
	}  while (len != 0);

	/*
	 * This is the right position for do0. Please don't move it into the
	 * loop.
	 */
do0:
	if (a1 != b1)
		return CMP_LT_OR_GT(a1, b1);
	return 0;
}

/*
 * Compare blocks at SRCP1 and SRCP2 with LEN `unsigned long' objects (not LEN
 * bytes!). SRCP2 should be aligned for memory operations on `unsigned long',
 * but SRCP1 *should be unaligned*.
 */
static int memcmp_not_common_alignment(long srcp1, long srcp2, size_t len)
{
	unsigned long a0, a1, a2, a3;
	unsigned long b0, b1, b2, b3;
	unsigned long x;
	int shl, shr;

	/*
	 * Calculate how to shift a word read at the memory operation
	 * aligned srcp1 to make it aligned for comparison.
	 */

	shl = 8 * (srcp1 % sizeof(unsigned long));
	shr = 8 * sizeof(unsigned long) - shl;

	/*
	 * Make SRCP1 aligned by rounding it down to the beginning of the
	 * `unsigned long' it points in the middle of.
	 */
	srcp1 &= -sizeof(unsigned long);

	switch (len % 4) {
	default: /* Avoid warning about uninitialized local variables.  */
	case 2:
		a1 = ((unsigned long *) srcp1)[0];
		a2 = ((unsigned long *) srcp1)[1];
		b2 = ((unsigned long *) srcp2)[0];
		srcp1 -= 1 * sizeof(unsigned long);
		srcp2 -= 2 * sizeof(unsigned long);
		len += 2;
		goto do1;
	case 3:
		a0 = ((unsigned long *) srcp1)[0];
		a1 = ((unsigned long *) srcp1)[1];
		b1 = ((unsigned long *) srcp2)[0];
		srcp2 -= 1 * sizeof(unsigned long);
		len += 1;
		goto do2;
	case 0:
		if (MEMCMP_THRESH <= 3 * sizeof(unsigned long) && len == 0)
			return 0;
		a3 = ((unsigned long *) srcp1)[0];
		a0 = ((unsigned long *) srcp1)[1];
		b0 = ((unsigned long *) srcp2)[0];
		srcp1 += 1 * sizeof(unsigned long);
		goto do3;
	case 1:
		a2 = ((unsigned long *) srcp1)[0];
		a3 = ((unsigned long *) srcp1)[1];
		b3 = ((unsigned long *) srcp2)[0];
		srcp1 += 2 * sizeof(unsigned long);
		srcp2 += 1 * sizeof(unsigned long);
		len -= 1;
		if (MEMCMP_THRESH <= 3 * sizeof(unsigned long) && len == 0)
			goto do0;
		/* Fallthrough */
	}

	do {
		a0 = ((unsigned long *) srcp1)[0];
		b0 = ((unsigned long *) srcp2)[0];
		x = MERGE(a2, shl, a3, shr);
		if (x != b3)
			return CMP_LT_OR_GT(x, b3);

do3:
		a1 = ((unsigned long *) srcp1)[1];
		b1 = ((unsigned long *) srcp2)[1];
		x = MERGE(a3, shl, a0, shr);
		if (x != b0)
			return CMP_LT_OR_GT(x, b0);

do2:
		a2 = ((unsigned long *) srcp1)[2];
		b2 = ((unsigned long *) srcp2)[2];
		x = MERGE(a0, shl, a1, shr);
		if (x != b1)
			return CMP_LT_OR_GT(x, b1);

do1:
		a3 = ((unsigned long *) srcp1)[3];
		b3 = ((unsigned long *) srcp2)[3];
		x = MERGE(a1, shl, a2, shr);
		if (x != b2)
			return CMP_LT_OR_GT(x, b2);

		srcp1 += 4 * sizeof(unsigned long);
		srcp2 += 4 * sizeof(unsigned long);
		len -= 4;
	} while (len != 0);

	/*
	 * This is the right position for do0. Please don't move it into
	 * the loop.
	 */
do0:
	x = MERGE(a2, shl, a3, shr);
	if (x != b3)
		return CMP_LT_OR_GT(x, b3);
	return 0;
}

#undef memcmp
/**
 * memcmp - Compare two areas of memory
 * @s1: One area of memory
 * @s2: Another area of memory
 * @count: The size of the area.
 */
__visible int memcmp(const void *s1, const void *s2, size_t len)
{
	unsigned long a0;
	unsigned long b0;
	long srcp1 = (long) s1;
	long srcp2 = (long) s2;
	unsigned long res;

	if (len >= MEMCMP_THRESH) {
		/*
		 * There are at least some bytes to compare. No need to test
		 * for LEN == 0 in this alignment loop.
		 */
		while (!IS_ALIGNED(srcp2, sizeof(unsigned long))) {
			a0 = ((uint8_t *) srcp1)[0];
			b0 = ((uint8_t *) srcp2)[0];
			srcp1 += 1;
			srcp2 += 1;
			res = a0 - b0;
			if (res != 0)
				return res;
			len -= 1;
		}

		/*
		 * SRCP2 is now aligned for memory operations on `unsigned long'.
		 * SRCP1 alignment determines if we can do a simple, aligned
		 * compare or need to shuffle bits.
		 */

		if (IS_ALIGNED(srcp1, sizeof(unsigned long)))
			res = memcmp_common_alignment(srcp1, srcp2,
						len / sizeof(unsigned long));
		else
			res = memcmp_not_common_alignment(srcp1, srcp2,
						len / sizeof(unsigned long));

		if (res != 0)
			return res;

		/* Number of bytes remaining in the interval [0..sizeof(unsigned long)-1].  */
		srcp1 += len & -sizeof(unsigned long);
		srcp2 += len & -sizeof(unsigned long);
		len %= sizeof(unsigned long);
	}

	/* There are just a few bytes to compare. Use byte memory operations.  */
	while (len != 0) {
		a0 = ((uint8_t *) srcp1)[0];
		b0 = ((uint8_t *) srcp2)[0];
		srcp1 += 1;
		srcp2 += 1;
		res = a0 - b0;
		if (res != 0)
			return res;
		len -= 1;
	}

	return 0;
}
EXPORT_SYMBOL(memcmp);
#endif

#ifndef __HAVE_ARCH_BCMP
/**
 * bcmp - returns 0 if and only if the buffers have identical contents.
 * @a: pointer to first buffer.
 * @b: pointer to second buffer.
 * @len: size of buffers.
 *
 * The sign or magnitude of a non-zero return value has no particular
 * meaning, and architectures may implement their own more efficient bcmp(). So
 * while this particular implementation is a simple (tail) call to memcmp, do
 * not rely on anything but whether the return value is zero or non-zero.
 */
int bcmp(const void *a, const void *b, size_t len)
{
	return memcmp(a, b, len);
}
EXPORT_SYMBOL(bcmp);
#endif

#ifndef __HAVE_ARCH_MEMSCAN
/**
 * memscan - Find a character in an area of memory.
 * @addr: The memory area
 * @c: The byte to search for
 * @size: The size of the area.
 *
 * returns the address of the first occurrence of @c, or 1 byte past
 * the area if @c is not found
 */
void *memscan(void *addr, int c, size_t size)
{
	unsigned char *p = addr;

	while (size) {
		if (*p == c)
			return (void *)p;
		p++;
		size--;
	}
  	return (void *)p;
}
EXPORT_SYMBOL(memscan);
#endif

#ifndef __HAVE_ARCH_STRSTR
/**
 * strstr - Find the first substring in a %NUL terminated string
 * @s1: The string to be searched
 * @s2: The string to search for
 */
char *strstr(const char *s1, const char *s2)
{
	size_t l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	l1 = strlen(s1);
	while (l1 >= l2) {
		l1--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
EXPORT_SYMBOL(strstr);
#endif

#ifndef __HAVE_ARCH_STRNSTR
/**
 * strnstr - Find the first substring in a length-limited string
 * @s1: The string to be searched
 * @s2: The string to search for
 * @len: the maximum number of characters to search
 */
char *strnstr(const char *s1, const char *s2, size_t len)
{
	size_t l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	while (len >= l2) {
		len--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
EXPORT_SYMBOL(strnstr);
#endif

#ifndef __HAVE_ARCH_MEMCHR
/**
 * memchr - Find a character in an area of memory.
 * @s: The memory area
 * @c: The byte to search for
 * @n: The size of the area.
 *
 * returns the address of the first occurrence of @c, or %NULL
 * if @c is not found
 */
void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	while (n-- != 0) {
        	if ((unsigned char)c == *p++) {
			return (void *)(p - 1);
		}
	}
	return NULL;
}
EXPORT_SYMBOL(memchr);
#endif

static void *check_bytes8(const u8 *start, u8 value, unsigned int bytes)
{
	while (bytes) {
		if (*start != value)
			return (void *)start;
		start++;
		bytes--;
	}
	return NULL;
}

/**
 * memchr_inv - Find an unmatching character in an area of memory.
 * @start: The memory area
 * @c: Find a character other than c
 * @bytes: The size of the area.
 *
 * returns the address of the first character other than @c, or %NULL
 * if the whole buffer contains just @c.
 */
void *memchr_inv(const void *start, int c, size_t bytes)
{
	u8 value = c;
	u64 value64;
	unsigned int words, prefix;

	if (bytes <= 16)
		return check_bytes8(start, value, bytes);

	value64 = value;
#if defined(CONFIG_ARCH_HAS_FAST_MULTIPLIER) && BITS_PER_LONG == 64
	value64 *= 0x0101010101010101ULL;
#elif defined(CONFIG_ARCH_HAS_FAST_MULTIPLIER)
	value64 *= 0x01010101;
	value64 |= value64 << 32;
#else
	value64 |= value64 << 8;
	value64 |= value64 << 16;
	value64 |= value64 << 32;
#endif

	prefix = (unsigned long)start % 8;
	if (prefix) {
		u8 *r;

		prefix = 8 - prefix;
		r = check_bytes8(start, value, prefix);
		if (r)
			return r;
		start += prefix;
		bytes -= prefix;
	}

	words = bytes / 8;

	while (words) {
		if (*(u64 *)start != value64)
			return check_bytes8(start, value, 8);
		start += 8;
		words--;
	}

	return check_bytes8(start, value, bytes % 8);
}
EXPORT_SYMBOL(memchr_inv);

/**
 * strreplace - Replace all occurrences of character in string.
 * @s: The string to operate on.
 * @old: The character being replaced.
 * @new: The character @old is replaced with.
 *
 * Returns pointer to the nul byte at the end of @s.
 */
char *strreplace(char *s, char old, char new)
{
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s;
}
EXPORT_SYMBOL(strreplace);

void fortify_panic(const char *name)
{
	pr_emerg("detected buffer overflow in %s\n", name);
	BUG();
}
EXPORT_SYMBOL(fortify_panic);
