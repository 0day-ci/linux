// SPDX-License-Identifier: GPL-2.0-only
/// Find missing dma_free_coherent for every dma_alloc_coherent.
///
// Confidence: Moderate
// Copyright: (C) 2013 Petr Strnad.
// URL: http://coccinelle.lip6.fr/
// Keywords: dma_free_coherent, dma_alloc_coherent
// Options: --no-includes --include-headers

virtual report
virtual org

@search@
local idexpression id;
expression x,y,z,e;
position p1,p2;
type T;
@@

id = dma_alloc_coherent@p1(x,y,&z)
... when != e = id
if (id == NULL || ...) { ... return ...; }
... when != dma_free_coherent(x,y,id,z)
    when != if (id) { ... dma_free_coherent(x,y,id,z) ... }
    when != if (y) { ... dma_free_coherent(x,y,id,z) ... }
    when != e = (T)id
    when exists
(
return 0;
|
return 1;
|
return id;
|
return@p2 ...;
)

@script:python depends on report@
p1 << search.p1;
p2 << search.p2;
@@

msg = "ERROR: missing dma_free_coherent; dma_alloc_coherent on line %s and return without freeing on line %s" % (p1[0].line,p2[0].line)
coccilib.report.print_report(p2[0],msg)

@script:python depends on org@
p1 << search.p1;
p2 << search.p2;
@@

msg = "ERROR: missing dma_free_coherent; dma_alloc_coherent on line %s and return without freeing on line %s" % (p1[0].line,p2[0].line)
cocci.print_main(msg,p1)
cocci.print_secs("",p2)
