// SPDX-License-Identifier: GPL-2.0-only
///
/// Check for opencoded sputchar() implementation.
///
// Confidence: High
// Copyright: (C) 2021 Yury Norov
// Options: --no-includes --include-headers
//
// Keywords: sputchar
//

virtual patch
virtual org
virtual report
virtual context

@rpostfix depends on !patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
*	if ((buf) < (end)) {
*		*(buf) = (c);
*	}
*	(buf)++;@p
	...>
}

@rprefix depends on !patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
*	if ((buf) < (end))
*		*(buf) = (c);
*	++(buf);@p
	...>
}

@rinc1 depends on !patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
*	if ((buf) < (end)) {
*		*(buf) = (c);
*	}
*	(buf) += 1;@p
	...>
}

@rinc2 depends on !patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
*	if ((buf) < (end)) {
*		*(buf) = (c);
*	}
*	(buf) = (buf) + 1;@p
	...>
}

@ppostfix depends on patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
-	if ((buf) < (end)) {
-		*(buf) = (c);
-	}
-	(buf)++;@p
+	buf = sputchar(buf, end, c);
	...>
}

// @pprefix depends on patch@
// identifier func;
// expression buf, end, c;
// position p;
// @@
//
// func(...)
// {
// 	<...
// -	if ((buf) < (end)) {
// -		*(buf) = (c);
// -	}
// -	++(buf);
// +	buf = sputchar(buf, end, c);
// 	...>
// }

@pinc1 depends on patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
-	if ((buf) < (end)) {
-		*(buf) = (c);
-	}
-	(buf) += 1;
+	buf = sputchar(buf, end, c);
	...>
}

@pinc2 depends on patch@
identifier func;
expression buf, end, c;
position p;
@@

func(...)
{
	<...
-	if ((buf) < (end)) {
-		*(buf) = (c);
-	}
-	(buf) = (buf) + 1;
+	buf = sputchar(buf, end, c);
	...>
}

@script:python depends on report@
p << rpostfix.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for sputchar()")

@script:python depends on org@
p << rpostfix.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for sputchar()")


@script:python depends on report@
p << rprefix.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for sputchar()")

@script:python depends on org@
p << rprefix.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for sputchar()")

@script:python depends on report@
p << rinc1.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for sputchar()")

@script:python depends on org@
p << rinc1.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for sputchar()")

