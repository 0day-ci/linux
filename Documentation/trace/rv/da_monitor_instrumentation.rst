Deterministic Automata Instrumentation
========================================

This document introduces some concepts behind the **Deterministic Automata
(DA)** monitor instrumentation.

The synthesis of automata-based models into the Linux *RV monitor* abstraction
is automatized by a tool named dot2k, and the "rv/da_monitor.h" provided
by the RV interface.

For example, given a file "wip.dot", representing a per-cpu monitor, with
this content::

  digraph state_automaton {
	center = true;
	size = "7,11";
	rankdir = LR;
	{node [shape = circle] "non_preemptive"};
	{node [shape = plaintext, style=invis, label=""] "__init_preemptive"};
	{node [shape = doublecircle] "preemptive"};
	{node [shape = circle] "preemptive"};
	"__init_preemptive" -> "preemptive";
	"non_preemptive" [label = "non_preemptive"];
	"non_preemptive" -> "non_preemptive" [ label = "sched_waking" ];
	"non_preemptive" -> "preemptive" [ label = "preempt_enable" ];
	"preemptive" [label = "preemptive"];
	"preemptive" -> "non_preemptive" [ label = "preempt_disable" ];
	{ rank = min ;
		"__init_preemptive";
		"preemptive";
	}
  }

That is the "DOT" representation of this automata model::

                       preempt_enable
          +---------------------------------+
          v                                 |
        #============#  preempt_disable   +------------------+
    --> H preemptive H -----------------> |  non_preemptive  |
        #============#                    +------------------+
                                            ^ sched_waking |
                                            +--------------+


Run the dot2k tool with the model, specifying that it is a "per-cpu"
model::

  $ dot2k -d ~/wip.dot -t per_cpu

This will create a directory named "wip/" with the following files:

- model.h: the wip in C
- wip.h: tracepoints that report the execution of the events by the
  monitor
- wip.c: the RV monitor

The monitor instrumentation should be done entirely in the RV monitor,
in the example above, in the wip.c file.

The RV monitor instrumentation section
--------------------------------------

The RV monitor file created by dot2k, with the name "$MODEL_NAME.c"
will include a section dedicated to instrumentation.

In the example of the wip.dot above, it will look like::

 /*
  * This is the instrumentation part of the monitor.
  *
  * This is the section where manual work is required. Here the kernel events
  * are translated into model's event.
  *
  */

 void handle_preempt_disable(void *data, /* XXX: fill header */)
 {
	da_handle_event_wip(preempt_disable);
 }

 void handle_preempt_enable(void *data, /* XXX: fill header */)
 {
	da_handle_event_wip(preempt_enable);
 }

 void handle_sched_waking(void *data, /* XXX: fill header */)
 {
	da_handle_event_wip(sched_waking);
 }

 #define NR_TP   3
 struct tracepoint_hook_helper tracepoints_to_hook[NR_TP] = {
	{
		.probe = handle_preempt_disable,
		.name = /* XXX: tracepoint name here */,
		.registered = 0
	},
	{
		.probe = handle_preempt_enable,
		.name = /* XXX: tracepoint name here */,
		.registered = 0
	},
	{
		.probe = handle_sched_waking,
		.name = /* XXX: tracepoint name here */,
		.registered = 0
	},
 };

The comment at the top of the section explains the general idea: the
instrumentation section translates *kernel events* into the *events
accepted by the model*.

Tracing callback functions
-----------------------------

The first three functions are skeletons for callback *handler functions* for
each of the three events from the wip model. The developer does not
necessarily need to use them: they are just starting points.

Using the example of::

 void handle_preempt_disable(void *data, /* XXX: fill header */)
 {
        da_handle_event_wip(preempt_disable);
 }

The "preempt_disable" event from the model conects directly to the
"preemptirq:preempt_disable". The "preemptirq:preempt_disable" event
has the following signature, from "include/trace/events/preemptirq.h"::

  TP_PROTO(unsigned long ip, unsigned long parent_ip)

Hence, the "handle_preempt_disable()" function will look like::

  void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)

In this case, the kernel even translates one to one with the automata event,
and indeed, no other change is needed for this function.

The next handler function, "handle_preempt_enable()" has the same argument
list from the "handle_preempt_disable()". The difference is that the
"preempt_enable" event will be used to synchronize the system to the model.

Initially, the *model* is placed in the initial state. However, the *system*
might, or might not be in the initial state. The monitor cannot start
processing events until it knows that the system reached the initial state. Otherwise the monitor and the system could be out-of-sync.

Looking at the automata definition, it is possible to see that the system
and the model are expected to return to the initial state after the
"preempt_enable" execution. Hence, it can be used to synchronize the
system and the model at the initialization of the monitoring section.

The initialization is informed via an special handle function, the
"da_handle_init_event_$(MONITOR)(event)", in this case::

  da_handle_event_wip(preempt_disable);

So, the callback function will look like::

  void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
  {
        da_handle_init_event_wip(preempt_enable);
  }

Finally, the "handle_sched_waking()" will look like::

  void handle_sched_waking(void *data, struct task_struct *task)
  {
        da_handle_event_wip(sched_waking);
  }

And the explanation is left for the reader as an exercise.

Tracepoint hook helpers
--------------------------

Still in the previous example, the next code section is the
"tracepoint_to_hook" definition, which is a structure that aims to help to
connect a monitor *handler function* with a given "tracepoint". Note that
this is just a suggestion. Indeed, the *handler functions* can hook to anything
that is possible to hook in the kernel, not even limited to the
tracing interface.

For the specific case of wip, the "tracepoints_to_hook" structure was
defined as::

  #define NR_TP   3
  struct tracepoint_hook_helper tracepoints_to_hook[NR_TP] = {
        {
                .probe = handle_preempt_disable,
                .name = "preempt_disable",
                .registered = 0
        },
        {
                .probe = handle_preempt_enable,
                .name = "preempt_enable",
                .registered = 0
        },
        {
                .probe = handle_sched_waking,
                .name = "sched_wakeup",
                .registered = 0
        },
  };

And that is the instrumentation required for the wip sample model.

Start and Stop functions
------------------------

Finally, dot2k automatically creates two special functions::

  start_$MODELNAME()
  stop_$MODELNAME()

These functions are called when the monitor is enabled and disabled,
respectivelly.
They should be used to *hook* and *unhook* the instrumentation to the running
system. The developer must add to the relative function all that is needed to
*hook* and *unhook* its monitor to the system.

For the wip case, these functions were named::

 start_wip()
 stop_wip()

But no change was required because: by default, these functions *hook* and
*unhook* the tracepoints_to_hook, which was enough for this case.
