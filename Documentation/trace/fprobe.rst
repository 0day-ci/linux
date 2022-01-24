.. SPDX-License-Identifier: GPL-2.0

==================================
Fprobe - Function entry/exit probe
==================================

.. Author: Masami Hiramatsu <mhiramat@kernel.org>

Introduction
============

Instead of using ftrace full feature, if you only want to attach callbacks
on function entry and exit, similar to the kprobes and kretprobes, you can
use fprobe. Compared with kprobes and kretprobes, fprobe gives faster
instrumentation for multiple functions with single handler. This document
describes how to use fprobe.

The usage of fprobe
===================

The fprobe is a wrapper of ftrace (+ kretprobe-like return callback) to
attach callbacks to multiple function entry and exit. User needs to set up
the `struct fprobe` and pass it to `register_fprobe()`.

Typically, `fprobe` data structure is initialized with the `syms`, `nentry`
and `entry_handler` and/or `exit_handler` as below.

.. code-block:: c

 char targets[] = {"func1", "func2", "func3"};
 struct fprobe fp = {
        .syms           = targets,
        .nentry         = ARRAY_SIZE(targets),
        .entry_handler  = my_entry_callback,
        .exit_handler   = my_exit_callback,
 };

The ftrace_ops in the fprobe is automatically set. The FTRACE_OPS_FL_SAVE_REGS
and FTRACE_OPS_FL_RECURSION
flag will be set. If you need other flags, please set it by yourself.

.. code-block:: c

 fp.ops.flags |= FTRACE_OPS_FL_RCU;

To enable this fprobe, call::

  register_fprobe(&fp);

To disable (remove from functions) this fprobe, call::

  unregister_fprobe(&fp);

You can temporally (soft) disable the fprobe by::

  disable_fprobe(&fp);

and resume by::

  enable_fprobe(&fp);

The above is defined by including the header::

  #include <linux/fprobe.h>

Same as ftrace, the registered callback will start being called some time
after the register_fprobe() is called and before it returns. See
:file:`Documentation/trace/ftrace.rst`.


The fprobe entry/exit handler
=============================

The prototype of the entry/exit callback function is as follows:

.. code-block:: c

 void callback_func(struct fprobe *fp, unsigned long entry_ip, struct pt_regs *regs);

Note that both entry and exit callback has same ptototype. The @entry_ip is
saved at function entry and passed to exit handler.

@fp
        This is the address of `fprobe` data structure related to this handler.
        You can embed the `fprobe` to your data structure and get it by
        container_of() macro from @fp. The @fp must not be NULL.

@entry_ip
        This is the entry address of the traced function (both entry and exit).

@regs
        This is the `pt_regs` data structure at the entry and exit. Note that
        the instruction pointer of @regs may be different from the @entry_ip
        in the entry_handler. If you need traced instruction pointer, you need
        to use @entry_ip. On the other hand, in the exit_handler, the instruction
        pointer of @regs is set to the currect return address.


Use fprobe with raw address list
================================

Instead of passing the array of symbols, you can pass a array of raw
function addresses via `fprobe::addrs`. In this case, the value of
this array will be changed automatically to the dynamic ftrace NOP
location addresses in the given kernel function. So please take care
if you share this array with others.


The missed counter
==================

The `fprobe` data structure has `fprobe::nmissed` counter field as same as
kprobes.
This counter counts up when;

 - fprobe fails to take ftrace_recursion lock. This usually means that a function
   which is traced by other ftrace users is called from the entry_handler.

 - fprobe fails to setup the function exit because of the shortage of rethook
   (the shadow stack for hooking the function return.)

Note that `fprobe::nmissed` field is counted up in both case. The former case
will skip both of entry and exit callback, and the latter case will skip exit
callback, but in both case the counter is just increased by 1.

Functions and structures
========================

.. kernel-doc:: include/linux/fprobe.h
.. kernel-doc:: kernel/trace/fprobe.c

