// SPDX-License-Identifier: GPL-2.0-only
///
/// Use pm_runtime_resume_and_get.
/// pm_runtime_get_sync keeps a reference count on failure,
/// which can lead to leaks.  pm_runtime_resume_and_get
/// drops the reference count in the failure case.
/// This rule addresses the cases where the reference count
/// is unlikely to be needed in the failure case.
///
// Confidence: High
// Copyright: (C) 2021 Julia Lawall, Inria
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Options: --include-headers --no-includes
// Keywords: pm_runtime_get_sync

virtual patch
virtual context
virtual org
virtual report

@r0 depends on patch && !context && !org && !report@
expression ret,e;
@@

-     ret = pm_runtime_get_sync(e);
+     ret = pm_runtime_resume_and_get(e);
-     if (ret < 0)
-             pm_runtime_put_noidle(e);

@r1 depends on patch && !context && !org && !report@
expression ret,e;
statement S1,S2;
@@

-     ret = pm_runtime_get_sync(e);
+     ret = pm_runtime_resume_and_get(e);
      if (ret < 0)
-     {
-             pm_runtime_put_noidle(e);
	      S1
-     }
      else S2

@r2 depends on patch && !context && !org && !report@
expression ret,e;
statement S;
@@

-     ret = pm_runtime_get_sync(e);
+     ret = pm_runtime_resume_and_get(e);
      if (ret < 0) {
-             pm_runtime_put_noidle(e);
	      ...
      } else S

@r3 depends on patch && !context && !org && !report@
expression ret,e;
identifier f;
constant char[] c;
statement S;
@@

-     ret = pm_runtime_get_sync(e);
+     ret = pm_runtime_resume_and_get(e);
      if (ret < 0)
-     {
              f(...,c,...);
-             pm_runtime_put_noidle(e);
-     }
      else S

@r4 depends on patch && !context && !org && !report@
expression ret,e;
identifier f;
constant char[] c;
statement S;
@@

-     ret = pm_runtime_get_sync(e);
+     ret = pm_runtime_resume_and_get(e);
      if (ret < 0) {
              f(...,c,...);
-             pm_runtime_put_noidle(e);
	      ...
      } else S

// ----------------------------------------------------------------------------

@r2_context depends on !patch && (context || org || report)@
statement S;
expression e, ret;
position j0, j1;
@@

*     ret@j0 = pm_runtime_get_sync(e);
      if (ret < 0) {
*             pm_runtime_put_noidle@j1(e);
	      ...
      } else S

@r3_context depends on !patch && (context || org || report)@
identifier f;
statement S;
constant char []c;
expression e, ret;
position j0, j1;
@@

*     ret@j0 = pm_runtime_get_sync(e);
      if (ret < 0) {
              f(...,c,...);
*             pm_runtime_put_noidle@j1(e);
	      ...
      } else S

// ----------------------------------------------------------------------------

@script:python r2_org depends on org@
j0 << r2_context.j0;
j1 << r2_context.j1;
@@

msg = "WARNING: opportunity for pm_runtime_get_sync"
coccilib.org.print_todo(j0[0], msg)
coccilib.org.print_link(j1[0], "")

@script:python r3_org depends on org@
j0 << r3_context.j0;
j1 << r3_context.j1;
@@

msg = "WARNING: opportunity for pm_runtime_get_sync"
coccilib.org.print_todo(j0[0], msg)
coccilib.org.print_link(j1[0], "")

// ----------------------------------------------------------------------------

@script:python r2_report depends on report@
j0 << r2_context.j0;
j1 << r2_context.j1;
@@

msg = "WARNING: opportunity for pm_runtime_get_sync on line %s." % (j0[0].line)
coccilib.report.print_report(j0[0], msg)

@script:python r3_report depends on report@
j0 << r3_context.j0;
j1 << r3_context.j1;
@@

msg = "WARNING: opportunity for pm_runtime_get_sync on %s." % (j0[0].line)
coccilib.report.print_report(j0[0], msg)

