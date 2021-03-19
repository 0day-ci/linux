// SPDX-License-Identifier: GPL-2.0-only
/// Mixing signed and unsigned types in bitwise operations risks problems when
/// the 'Usual arithmetic conversions' are applied.
/// For example:
/// https://lore.kernel.org/lkml/20210317013758.GA134033@roeck-us.net/
/// When a signed int and an unsigned int are compared there is no problem.
/// But if the unsigned is changed to a unsigned long, for example by using BIT
/// the signed value will be sign-extended and could result in incorrect logic.
// Confidence:
// Copyright: (C) 2021 Evan Benn <evanbenn@chromium.org>
// Comments:
// Options:

virtual context
virtual org
virtual report

@r@
position p;
{int} s;
{unsigned long} u;
@@
    s@p & u

@script:python depends on org@
p << r.p;
@@

cocci.print_main("sign extension when comparing bits of signed and unsigned values", p)

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0],"sign extension when comparing bits of signed and unsigned values")
