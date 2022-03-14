============
Load Average
============

Load average is a basic statistic provided by almost all operating systems that
aims to report the usage of system hardware resources. In Linux kernel, the
load average is calculated via the following expression::

                / 0                                      , if t = 0
    load_{t} = |
                \ laod_{t - 1} * exp + active * (1 - exp), otherwise

The expression represents the exponential moving average of the historical
loading of the system. There are several reasons that Linux kernel chooses
exponential moving average from other similar average equations such as simple
moving average or cumulative moving average:

#. The exponential moving average consumes fixed memory space, while the simple
   moving average has O(n) space complexity where n is the number of timeslice
   within a given interval.
#. The exponential moving average not only applies a higher weight to the most
   recent record but also declines the weight exponentially, which makes the
   resulting load average reflect the situation of the current system. Neither
   the simple moving average nor cumulative moving average has this feature.

In the expression, the load_{t} in the expression indicates the calculated load
average at the given time t.
The active is the most recent recorded system load. In Linux, the system load
means the number of tasks in the state of TASK_RUNNING or TASK_UNINTERRUPTIBLE
of the entire system. Tasks with TASK_UNINTERRUPTIBLE state are usually waiting
for disk I/O or holding an uninterruptible lock, which is considered as a part
of system resource, thus, Linux kernel covers them while calculating the load
average.
The exp means the weight applied to the previous report of load average, while
(1 - exp) is the weight applied to the most recently recorded system load.
There are three different weights defined in the Linux kernel, in
include/linux/sched/loadavg.h, to perform statistics in various timescales::

    // include/linux/sched/loadavg.h
    ...
    #define EXP_1    1884    /* 1/exp(5sec/1min) as fixed-point */
    #define EXP_5    2014    /* 1/exp(5sec/5min) */
    #define EXP_15   2037    /* 1/exp(5sec/15min) */
    ...

According to the expression shown on the top of this page, the weight (exp)
controls how much of the last load load_{t - 1} will take place in the
calculation of current load, while (1 - exp) is the weight applied to the most
recent record of system load active.

Due to the security issue, the weights are defined as fixed-point numbers based
on the unsigned integer rather than floating-pointing numbers. The introduction
of the fixed-point number keeps the FPU away from the calculation process. Since
the precession of the fixed-point used in the Linux kernel is 11 bits, a
fixed-point can be converted to a floating-point by dividing it by 2048, as the
expression shown below::

    EXP_1  = 1884 / 2048 = 0.919922
    EXP_5  = 2014 / 2048 = 0.983398
    EXP_15 = 2037 / 2048 = 0.994629

Which indicates the weights applied to active are::

    (1 - EXP_1)  = (1 - 0.919922) = 0.080078
    (1 - EXP_5)  = (1 - 0.983398) = 0.016602
    (1 - EXP_15) = (1 - 0.994629) = 0.005371

The load average will be updated every 5 seconds. Each time the scheduler_tick()
be called, the function calc_global_load_tick() will also be invoked, which
makes the active of each CPU core be calculated and be merged globally, finally,
the load average will be updated with that global active.

As a user, the load average can be observed via top, htop, or other system
monitor application, or more directly, by the following command::

    $ cat /proc/laodavg

