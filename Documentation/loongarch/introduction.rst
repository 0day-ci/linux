.. SPDX-License-Identifier: GPL-2.0

=========================
Introduction of LoongArch
=========================

LoongArch is a new RISC ISA, which is a bit like MIPS or RISC-V. LoongArch
includes a reduced 32-bit version (LA32R), a standard 32-bit version (LA32S)
and a 64-bit version (LA64). LoongArch has 4 privilege levels (PLV0~PLV3),
PLV0 is the highest level which used by kernel, and PLV3 is the lowest level
which used by applications. This document introduces the registers, basic
instruction set, virtual memory and some other topics of LoongArch.

Registers
=========

LoongArch registers include general purpose registers (GPRs), floating point
registers (FPRs), vector registers (VRs) and control status registers (CSRs)
used in privileged mode (PLV0).

GPRs
----

LoongArch has 32 GPRs ($r0 - $r31), each one is 32bit wide in LA32 and 64bit
wide in LA64. $r0 is always zero, and other registers has no special feature,
but we actually have an ABI register conversion as below.

================= =============== =================== ============
Name              Alias           Usage               Preserved
                                                      across calls
================= =============== =================== ============
``$r0``           ``$zero``       Constant zero       Unused
``$r1``           ``$ra``         Return address      No
``$r2``           ``$tp``         TLS                 Unused
``$r3``           ``$sp``         Stack pointer       Yes
``$r4``-``$r11``  ``$a0``-``$a7`` Argument registers  No
``$r4``-``$r5``   ``$v0``-``$v1`` Return value        No
``$r12``-``$r20`` ``$t0``-``$t8`` Temp registers      No
``$r21``          ``$x``          Reserved            Unused
``$r22``          ``$fp``         Frame pointer       Yes
``$r23``-``$r31`` ``$s0``-``$s8`` Static registers    Yes
================= =============== =================== ============

FPRs
----

LoongArch has 32 FPRs ($f0 - $f31), each one is 64bit wide. We also have an
ABI register conversion as below.

================= ================== =================== ============
Name              Alias              Usage               Preserved
                                                         across calls
================= ================== =================== ============
``$f0``-``$f7``   ``$fa0``-``$fa7``  Argument registers  No
``$f0``-``$f1``   ``$fv0``-``$fv1``  Return value        No
``$f8``-``$f23``  ``$ft0``-``$ft15`` Temp registers      No
``$f24``-``$f31`` ``$fs0``-``$fs7``  Static registers    Yes
================= ================== =================== ============

VRs
----

LoongArch has 128bit vector extension (LSX, short for Loongson SIMD eXtention)
and 256bit vector extension (LASX, short for Loongson Advanced SIMD eXtension).
There are also 32 vector registers, for LSX is $v0 - $v31, and for LASX is $x0
- $x31. FPRs and VRs are reused, e.g. the lower 128bits of $x0 is $v0, and the
lower 64bits of $v0 is $f0, etc.

CSRs
----

CSRs can only be used in privileged mode (PLV0):

================= ===================================== ==============
Address           Full Name                             Abbrev Name
================= ===================================== ==============
0x0               Current Mode information              CRMD
0x1               Pre-exception Mode information        PRMD
0x2               Extended Unit Enable                  EUEN
0x3               Miscellaneous controller              MISC
0x4               Exception Configuration               ECFG
0x5               Exception Status                      ESTAT
0x6               Exception Return Address              ERA
0x7               Bad Virtual Address                   BADV
0x8               Bad Instruction                       BADI
0xC               Exception Entry Base address          EENTRY
0x10              TLB Index                             TLBIDX
0x11              TLB Entry High-order bits             TLBEHI
0x12              TLB Entry Low-order bits 0            TLBELO0
0x13              TLB Entry Low-order bits 1            TLBELO1
0x18              Address Space Identifier              ASID
0x19              Page Global Directory address for     PGDL
                  Lower half address space
0x1A              Page Global Directory address for     PGDH
                  Higher half address space
0x1B              Page Global Directory address         PGD
0x1C              Page Walk Controller for Lower        PWCL
                  half address space
0x1D              Page Walk Controller for Higher       PWCH
                  half address space
0x1E              STLB Page Size                        STLBPS
0x1F              Reduced Virtual Address Configuration RVACFG
0x20              CPU Identifier                        CPUID
0x21              Privileged Resource Configuration 1   PRCFG1
0x22              Privileged Resource Configuration 2   PRCFG2
0x23              Privileged Resource Configuration 3   PRCFG3
0x30+n (0≤n≤15)   Data Save register                    SAVEn
0x40              Timer Identifier                      TID
0x41              Timer Configuration                   TCFG
0x42              Timer Value                           TVAL
0x43              Compensation of Timer Count           CNTC
0x44              Timer Interrupt Clearing              TICLR
0x60              LLBit Controller                      LLBCTL
0x80              Implementation-specific Controller 1  IMPCTL1
0x81              Implementation-specific Controller 2  IMPCTL2
0x88              TLB Refill Exception Entry Base       TLBRENTRY
                  address
0x89              TLB Refill Exception BAD Virtual      TLBRBADV
                  address
0x8A              TLB Refill Exception Return Address   TLBRERA
0x8B              TLB Refill Exception data SAVE        TLBRSAVE
                  register
0x8C              TLB Refill Exception Entry Low-order  TLBRELO0
                  bits 0
0x8D              TLB Refill Exception Entry Low-order  TLBRELO1
                  bits 1
0x8E              TLB Refill Exception Entry High-order TLBEHI
                  bits
0x8F              TLB Refill Exception Pre-exception    TLBRPRMD
                  Mode information
0x90              Machine Error Controller              MERRCTL
0x91              Machine Error Information 1           MERRINFO1
0x92              Machine Error Information 2           MERRINFO2
0x93              Machine Error Exception Entry Base    MERRENTRY
                  address
0x94              Machine Error Exception Return        MERRERA
                  address
0x95              Machine Error Exception data SAVE     MERRSAVE
                  register
0x98              Cache TAGs                            CTAG
0x180+n (0≤n≤3)   Direct Mapping configuration Window n DMWn
0x200+2n (0≤n≤31) Performance Monitor Configuration n   PMCFGn
0x201+2n (0≤n≤31) Performance Monitor overall Counter n PMCNTn
0x300             Memory load/store WatchPoint          MWPC
                  overall Controller
0x301             Memory load/store WatchPoint          MWPS
                  overall Status
0x310+8n (0≤n≤7)  Memory load/store WatchPoint n        MWPnCFG1
                  Configuration 1
0x311+8n (0≤n≤7)  Memory load/store WatchPoint n        MWPnCFG2
                  Configuration 2
0x312+8n (0≤n≤7)  Memory load/store WatchPoint n        MWPnCFG3
                  Configuration 3
0x313+8n (0≤n≤7)  Memory load/store WatchPoint n        MWPnCFG4
                  Configuration 4
0x380             Fetch WatchPoint overall Controller   FWPC
0x381             Fetch WatchPoint overall Status       FWPS
0x390+8n (0≤n≤7)  Fetch WatchPoint n Configuration 1    FWPnCFG1
0x391+8n (0≤n≤7)  Fetch WatchPoint n Configuration 2    FWPnCFG2
0x392+8n (0≤n≤7)  Fetch WatchPoint n Configuration 3    FWPnCFG3
0x393+8n (0≤n≤7)  Fetch WatchPoint n Configuration 4    FWPnCFG4
0x500             Debug register                        DBG
0x501             Debug Exception Return address        DERA
0x502             Debug data SAVE register              DSAVE
================= ===================================== ==============

Basic Instruction Set
=====================

Instruction formats
-------------------

LoongArch has 32-bit wide instructions, and there are 9 instruction formats::

  2R-type:    Opcode + Rj + Rd
  3R-type:    Opcode + Rk + Rj + Rd
  4R-type:    Opcode + Ra + Rk + Rj + Rd
  2RI8-type:  Opcode + I8 + Rj + Rd
  2RI12-type: Opcode + I12 + Rj + Rd
  2RI14-type: Opcode + I14 + Rj + Rd
  2RI16-type: Opcode + I16 + Rj + Rd
  1RI21-type: Opcode + I21L + Rj + I21H
  I26-type:   Opcode + I26L + I26H

Rj and Rk are source operands (register), Rd is destination operand (register),
and Ra is the additional operand (register) in 4R-type. I8/I12/I16/I21/I26 are
8-bits/12-bits/16-bits/21-bits/26bits immediate data. 21bits/26bits immediate
data are split into higher bits and lower bits in an instruction word, so you
can see I21L/I21H and I26L/I26H here.

Instruction names (Mnemonics)
-----------------------------

We only list the instruction names here, for details please read the references.

Arithmetic Operation Instructions::

  ADD.W SUB.W ADDI.W ADD.D SUB.D ADDI.D
  SLT SLTU SLTI SLTUI
  AND OR NOR XOR ANDN ORN ANDI ORI XORI
  MUL.W MULH.W MULH.WU DIV.W DIV.WU MOD.W MOD.WU
  MUL.D MULH.D MULH.DU DIV.D DIV.DU MOD.D MOD.DU
  PCADDI PCADDU12I PCADDU18I
  LU12I.W LU32I.D LU52I.D ADDU16I.D

Bit-shift Instructions::

  SLL.W SRL.W SRA.W ROTR.W SLLI.W SRLI.W SRAI.W ROTRI.W
  SLL.D SRL.D SRA.D ROTR.D SLLI.D SRLI.D SRAI.D ROTRI.D

Bit-manipulation Instructions::

  EXT.W.B EXT.W.H CLO.W CLO.D SLZ.W CLZ.D CTO.W CTO.D CTZ.W CTZ.D
  BYTEPICK.W BYTEPICK.D BSTRINS.W BSTRINS.D BSTRPICK.W BSTRPICK.D
  REVB.2H REVB.4H REVB.2W REVB.D REVH.2W REVH.D BITREV.4B BITREV.8B BITREV.W BITREV.D
  MASKEQZ MASKNEZ

Branch Instructions::

  BEQ BNE BLT BGE BLTU BGEU BEQZ BNEZ B BL JIRL

Load/Store Instructions::

  LD.B LD.BU LD.H LD.HU LD.W LD.WU LD.D ST.B ST.H ST.W ST.D
  LDX.B LDX.BU LDX.H LDX.HU LDX.W LDX.WU LDX.D STX.B STX.H STX.W STX.D
  LDPTR.W LDPTR.D STPTR.W STPTR.D
  PRELD PRELDX

Atomic Operation Instructions::

  LL.W SC.W LL.D SC.D
  AMSWAP.W AMSWAP.D AMADD.W AMADD.D AMAND.W AMAND.D AMOR.W AMOR.D AMXOR.W AMXOR.D
  AMMAX.W AMMAX.D AMMIN.W AMMIN.D

Barrier Instructions::

  IBAR DBAR

Special Instructions::

  SYSCALL BREAK CPUCFG NOP IDLE ERTN DBCL RDTIMEL.W RDTIMEH.W RDTIME.D ASRTLE.D ASRTGT.D

Privileged Instructions::

  CSRRD CSRWR CSRXCHG
  IOCSRRD.B IOCSRRD.H IOCSRRD.W IOCSRRD.D IOCSRWR.B IOCSRWR.H IOCSRWR.W IOCSRWR.D
  CACOP TLBP(TLBSRCH) TLBRD TLBWR TLBFILL TLBCLR TLBFLUSH INVTLB LDDIR LDPTE

Virtual Memory
==============

LoongArch can use direct-mapped virtual memory and page-mapped virtual memory.

Direct-mapped virtual memory is configured by CSR.DMWn (n=0~3), it has a simple
relationship between virtual address (VA) and physical address (PA)::

 VA = PA + FixedOffset

Page-mapped virtual memory has arbitrary relationship between VA and PA, which
is recorded in TLB and page tables. LoongArch's TLB includes a fully-associative
MTLB (Multiple Page Size TLB) and set-associative STLB (Single Page Size TLB).

By default, the whole virtual address space of LA32 is configured like this:

============ =========================== =============================
Name         Address Range               Attributes
============ =========================== =============================
``UVRANGE``  ``0x00000000 - 0x7FFFFFFF`` Page-mapped, Cached, PLV0~3
``KPRANGE0`` ``0x80000000 - 0x9FFFFFFF`` Direct-mapped, Uncached, PLV0
``KPRANGE1`` ``0xA0000000 - 0xBFFFFFFF`` Direct-mapped, Cached, PLV0
``KVRANGE``  ``0xC0000000 - 0xFFFFFFFF`` Page-mapped, Cached, PLV0
============ =========================== =============================

User mode (PLV3) can only access UVRANGE. For direct-mapped KPRANGE0 and
KPRANGE1, PA is equal to VA with bit30~31 cleared. For example, the uncached
direct-mapped VA of 0x00001000 is 0x80001000, and the cached direct-mapped
VA of 0x00001000 is 0xA0001000.

By default, the whole virtual address space of LA64 is configured like this:

============ ====================== ======================================
Name         Address Range          Attributes
============ ====================== ======================================
``XUVRANGE`` ``0x0000000000000000 - Page-mapped, Cached, PLV0~3
             0x3FFFFFFFFFFFFFFF``
``XSPRANGE`` ``0x4000000000000000 - Direct-mapped, Cached / Uncached, PLV0
             0x7FFFFFFFFFFFFFFF``
``XKPRANGE`` ``0x8000000000000000 - Direct-mapped, Cached / Uncached, PLV0
             0xBFFFFFFFFFFFFFFF``
``XKVRANGE`` ``0xC000000000000000 - Page-mapped, Cached, PLV0
             0xFFFFFFFFFFFFFFFF``
============ ====================== ======================================

User mode (PLV3) can only access XUVRANGE. For direct-mapped XSPRANGE and XKPRANGE,
PA is equal to VA with bit60~63 cleared, and the cache attributes is configured by
bit60~61 (0 is strongly-ordered uncached, 1 is coherent cached, and 2 is weakly-
ordered uncached) in VA. Currently we only use XKPRANGE for direct mapping and
XSPRANGE is reserved. As an example, the strongly-ordered uncached direct-mapped VA
(in XKPRANGE) of 0x00000000 00001000 is 0x80000000 00001000, the coherent cached
direct-mapped VA (in XKPRANGE) of 0x00000000 00001000 is 0x90000000 00001000, and
the weakly-ordered uncached direct-mapped VA (in XKPRANGE) of 0x00000000 00001000
is 0xA0000000 00001000.

Relationship of Loongson and LoongArch
======================================

LoongArch is a RISC ISA which is different from any other existing ones, while
Loongson is a family of processors. Loongson includes 3 series: Loongson-1 is
32-bit processors, Loongson-2 is low-end 64-bit processors, and Loongson-3 is
high-end 64-bit processors. Old Loongson is based on MIPS, and New Loongson is
based on LoongArch. Take Loongson-3 as an example: Loongson-3A1000/3B1500/3A2000
/3A3000/3A4000 are MIPS-compatible, while Loongson-3A5000 (and future revisions)
are all based on LoongArch.

References
==========

Official web site of Loongson and LoongArch (Loongson Technology Corp. Ltd.):

  http://www.loongson.cn/index.html

Developer web site of Loongson and LoongArch (Software and Documentations):

  http://www.loongnix.cn/index.php

  https://github.com/loongson

Documentations of LoongArch ISA:

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-Vol1-v1.00-CN.pdf (in Chinese)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-Vol1-v1.00-EN.pdf (in English)

Documentations of LoongArch ABI:

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-ABI-v1.00-CN.pdf (in Chinese)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-ABI-v1.00-EN.pdf (in English)

Linux kernel repository of Loongson and LoongArch:

  https://git.kernel.org/pub/scm/linux/kernel/git/chenhuacai/linux-loongson.git
