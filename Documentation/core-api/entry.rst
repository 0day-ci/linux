Entry/exit handling for exceptions, interrupts, syscalls and KVM
================================================================

For any transition from one execution domain into another the kernel
requires update of various states. The state updates have strict rules
versus ordering.

The states which need to be updated are:

  * Lockdep
  * RCU
  * Preemption counter
  * Tracing
  * Time accounting

The update order depends on the transition type and is explained below in
the transition type sections.

Non-instrumentable code - noinstr
---------------------------------

Low level transition code cannot be instrumented before RCU is watching and
after RCU went into a non watching state (NOHZ, NOHZ_FULL) as most
instrumentation facilities depend on RCU.

Aside of that many architectures have to save register state, e.g. debug or
cause registers before another exception of the same type can happen. A
breakpoint in the breakpoint entry code would overwrite the debug registers
of the inital breakpoint.

Such code has to be marked with the 'noinstr' attribute. That places the
code into a special section which is taboo for instrumentation and debug
facilities.

In a function which is marked 'noinstr' it's only allowed to call into
non-instrumentable code except when the invocation of instrumentable code
is annotated with a instrumentation_begin()/instrumentation_end() pair::

  noinstr void entry(void)
  {
  	handle_entry();     <-- must be 'noinstr' or '__always_inline'
	...
	instrumentation_begin();
	handle_context();   <-- instrumentable code
	instrumentation_end();
	...
	handle_exit();     <-- must be 'noinstr' or '__always_inline'
  }

This allows verification of the 'noinstr' restrictions via objtool on
supported architectures.

Invoking non-instrumentable functions from instrumentable context has no
restrictions and is useful to protect e.g. state switching which would
cause malfunction if instrumented.

All non-instrumentable entry/exit code sections before and after the RCU
state transitions must run with interrupts disabled.

Syscalls
--------

Syscall entry exit code starts obviously in low level architecture specific
assembly code and calls out into C-code after establishing low level
architecture specific state and stack frames. This low level code must not
be instrumented. A typical syscall handling function invoked from low level
assembly code looks like this::

  noinstr void do_syscall(struct pt_regs \*regs, int nr)
  {
	arch_syscall_enter(regs);
	nr = syscall_enter_from_user_mode(regs, nr);

	instrumentation_begin();

	if (!invoke_syscall(regs, nr) && nr != -1)
	 	result_reg(regs) = __sys_ni_syscall(regs);

	instrumentation_end();

	syscall_exit_to_user_mode(regs);
  }

syscall_enter_from_user_mode() first invokes enter_from_user_mode() which
establishes state in the following order:

  * Lockdep
  * RCU / Context tracking
  * Tracing

and then invokes the various entry work functions like ptrace, seccomp,
audit, syscall tracing etc. After the function returns instrumentable code
can be invoked. After returning from the syscall handler the instrumentable
code section ends and syscall_exit_to_user_mode() is invoked.

syscall_exit_to_user_mode() handles all work which needs to be done before
returning to user space like tracing, audit, signals, task work etc. After
that it invokes exit_to_user_mode() which again handles the state
transition in the reverse order:

  * Tracing
  * RCU / Context tracking
  * Lockdep

syscall_enter_from_user_mode() and syscall_exit_to_user_mode() are also
available as fine grained subfunctions in cases where the architecture code
has to do extra work between the various steps. In such cases it has to
ensure that enter_from_user_mode() is called first on entry and
exit_to_user_mode() is called last on exit.


KVM
---

Entering or exiting guest mode is very similar to syscalls. From the host
kernel point of view the CPU goes off into user space when entering the
guest and returns to the kernel on exit.

kvm_guest_enter_irqoff() is a KVM specific variant of exit_to_user_mode()
and kvm_guest_exit_irqoff() is the KVM variant of enter_from_user_mode().
The state operations have the same ordering.

Task work handling is done separately for guest at the boundary of the
vcpu_run() loop via xfer_to_guest_mode_handle_work() which is a subset of
the work handled on return to user space.

Interrupts and regular exceptions
---------------------------------

Interrupts entry and exit handling is slightly more complex than syscalls
and KVM transitions.

If an interrupt is raised while the CPU executes in user space, the entry
and exit handling is exactly the same as for syscalls.

If the interrupt is raised while the CPU executes in kernel space the entry
and exit handling is slightly different. RCU state is only updated when the
interrupt was raised in context of the idle task because that's the only
kernel context where RCU can be not watching on NOHZ enabled kernels.
Lockdep and tracing have to be updated unconditionally.

irqentry_enter() and irqentry_exit() provide the implementation for this.

The architecture specific part looks similar to syscall handling::

  noinstr void do_interrupt(struct pt_regs \*regs, int nr)
  {
	arch_interrupt_enter(regs);
	state = irqentry_enter(regs);

	instrumentation_begin();

	irq_enter_rcu();
	invoke_irq_handler(regs, nr);
	irq_exit_rcu();

	instrumentation_end();

	irqentry_exit(regs, state);
  }

Note, that the invocation of the actual interrupt handler is within a
irq_enter_rcu() and irq_exit_rcu() pair.

irq_enter_rcu() updates the preemption count which makes in_hardirq()
return true, handles NOHZ tick state and interrupt time accounting. This
means that up to the point where irq_enter_rcu() is invoked in_hardirq()
returns false.

irq_exit_rcu() handles interrupt time accounting, undoes the preemption
count update and eventually handles soft interrupts and NOHZ tick state.

The preemption count could be established in irqentry_enter() already, but
there is no real value to do so. This allows the preemption count to be
traced and just puts a restriction on the early entry code up to
irq_enter_rcu().

This also keeps the handling vs. irq_exit_rcu() symmetric and
irq_exit_rcu() must undo the preempt count elevation before handling soft
interrupts and irqentry_exit() also requires that because it might
schedule.


NMI and NMI-like exceptions
---------------------------

NMIs and NMI like exceptions, e.g. Machine checks, double faults, debug
interrupts etc. can hit any context and have to be extra careful vs. the
state.

Debug exceptions can handle user space breakpoints or watchpoints in the
same way as an interrupt which was raised while executing in user space,
but kernel mode debug exceptions have to be treated like NMIs as they can
even happen in NMI context, e.g. due to code patching.

Also Machine check exceptions can handle user mode exceptions like regular
interrupts, but for kernel mode exceptions they have to be treated like
NMIs.

NMIs and the other NMI-like exceptions handle state transitions in the most
straight forward way and do not differentiate between user and kernel mode
origin.

The state update on entry is handled in irqentry_nmi_enter() which updates
state in the following order:

  * Preemption counter
  * Lockdep
  * RCU
  * Tracing

The exit counterpart irqenttry_nmi_exit() does the reverse operation in the
reverse order.

Note, that the update of the preemption counter has to be the first
operation on enter and the last operation on exit. The reason is that both
lockdep and RCU rely on in_nmi() returning true in this case. The
preemption count modification in the NMI entry/exit case can obviously not
be traced.

Architecture specific code looks like this::

  noinstr void do_nmi(struct pt_regs \*regs)
  {
	arch_nmi_enter(regs);
	state = irqentry_nmi_enter(regs);

	instrumentation_begin();

	invoke_nmi_handler(regs);

	instrumentation_end();
	irqentry_nmi_exit(regs);
  }

and for e.g. a debug exception it can look like this::

  noinstr void do_debug(struct pt_regs \*regs)
  {
	arch_nmi_enter(regs);

	debug_regs = save_debug_regs();

	if (user_mode(regs)) {
		state = irqentry_enter(regs);

		instrumentation_begin();

		user_mode_debug_handler(regs, debug_regs);

		instrumentation_end();

		irqentry_exit(regs, state);
  	} else {
  		state = irqentry_nmi_enter(regs);

		instrumentation_begin();

		kernel_mode_debug_handler(regs, debug_regs);

		instrumentation_end();

		irqentry_nmi_exit(regs, state);
	}
  }

There is no combined irqentry_nmi_if_kernel() function available as the
above cannot be handled in an exception agnostic way.
