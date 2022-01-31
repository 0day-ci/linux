#ifndef _LINUX_CONST_H
#define _LINUX_CONST_H

#include <vdso/const.h>

/*
 * This returns a constant expression while determining if an argument is
 * a constant expression, most importantly without evaluating the argument.
 * Glory to Martin Uecker <Martin.Uecker@med.uni-goettingen.de>
 *
 * Details:
 * - sizeof() is an integer constant expression, and does not evaluate the
 *   value of its operand; it only examines the type of its operand.
 * - The results of comparing two integer constant expressions is also
 *   an integer constant expression.
 * - The use of literal "8" is to avoid warnings about unaligned pointers;
 *   these could otherwise just be "1"s.
 * - (long)(x) is used to avoid warnings about 64-bit types on 32-bit
 *   architectures.
 * - The C standard defines an "integer constant expression" as different
 *   from a "null pointer constant" (an integer constant 0 pointer).
 * - The conditional operator ("... ? ... : ...") returns the type of the
 *   operand that isn't a null pointer constant. This behavior is the
 *   central mechanism of the macro.
 * - If (x) is an integer constant expression, then the "* 0l" resolves it
 *   into a null pointer constant, which forces the conditional operator
 *   to return the type of the last operand: "(int *)".
 * - If (x) is not an integer constant expression, then the type of the
 *   conditional operator is from the first operand: "(void *)".
 * - sizeof(int) == 4 and sizeof(void) == 1.
 * - The ultimate comparison to "sizeof(int)" chooses between either:
 *     sizeof(*((int *) (8)) == sizeof(int)   (x was a constant expression)
 *     sizeof(*((void *)(8)) == sizeof(void)  (x was not a constant expression)
 */
#define __is_constexpr(x) \
	(sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

#endif /* _LINUX_CONST_H */
