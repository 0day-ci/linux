RTRS (RDMA Transport) Fault Injection
=====================================
This document introduces how to enable and use the error injection of RTRS
via debugfs in the /sys/kernel/debug directory. When enabled, users can
enable specific error injection point and change the default status code
via the debugfs.

Following examples show how to inject an error into the RTRS.

First, enable CONFIG_FAULT_INJECTION_DEBUG_FS kernel config,
recompile the kernel. After booting up the kernel, map a target device.

After mapping, /sys/kernel/debug/<session-name> directory is created
on both of the client and the server.

Example 1: Inject an error into request processing of rtrs-client
-----------------------------------------------------------------

Generate an error on one session of rtrs-client::

  echo 100 > /sys/kernel/debug/ip\:192.168.123.144@ip\:192.168.123.190/fault_inject/probability
  echo 1 > /sys/kernel/debug/ip\:192.168.123.144@ip\:192.168.123.190/fault_inject/times
  echo 1 > /sys/kernel/debug/ip\:192.168.123.144@ip\:192.168.123.190/fault_inject/fail-request
  dd if=/dev/rnbd0 of=./dd bs=1k count=10

Expected Result::

  dd succeeds but generates an IO error

Message from dmesg::

  FAULT_INJECTION: forcing a failure.
  name fault_inject, interval 1, probability 100, space 0, times 1
  CPU: 0 PID: 799 Comm: dd Tainted: G           O      5.4.77-pserver+ #169
  Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.13.0-1ubuntu1.1 04/01/2014
  Call Trace:
    dump_stack+0x97/0xe0
    should_fail.cold+0x5/0x11
    rtrs_clt_should_fail_request+0x2f/0x50 [rtrs_client]
    rtrs_clt_request+0x223/0x540 [rtrs_client]
    rnbd_queue_rq+0x347/0x800 [rnbd_client]
    __blk_mq_try_issue_directly+0x268/0x380
    blk_mq_request_issue_directly+0x9a/0xe0
    blk_mq_try_issue_list_directly+0xa3/0x170
    blk_mq_sched_insert_requests+0x1de/0x340
    blk_mq_flush_plug_list+0x488/0x620
    blk_flush_plug_list+0x20f/0x250
    blk_finish_plug+0x3c/0x54
    read_pages+0x104/0x2b0
    __do_page_cache_readahead+0x28b/0x2b0
    ondemand_readahead+0x2cc/0x610
    generic_file_read_iter+0xde0/0x11f0
    new_sync_read+0x246/0x360
    vfs_read+0xc1/0x1b0
    ksys_read+0xc3/0x160
    do_syscall_64+0x68/0x260
    entry_SYSCALL_64_after_hwframe+0x49/0xbe
  RIP: 0033:0x7f7ff4296461
  Code: fe ff ff 50 48 8d 3d fe d0 09 00 e8 e9 03 02 00 66 0f 1f 84 00 00 00 00 00 48 8d 05 99 62 0d 00 8b 00 85 c0 75 13 31 c0 0f 05 <48> 3d 00 f0 ff ff 77 57 c3 66 0f 1f 44 00 00 41 54 49 89 d4 55 48
  RSP: 002b:00007fffdceca5b8 EFLAGS: 00000246 ORIG_RAX: 0000000000000000
  RAX: ffffffffffffffda RBX: 000055c5eab6e3e0 RCX: 00007f7ff4296461
  RDX: 0000000000000400 RSI: 000055c5ead27000 RDI: 0000000000000000
  RBP: 0000000000000400 R08: 0000000000000003 R09: 00007f7ff4368260
  R10: ffffffffffffff3b R11: 0000000000000246 R12: 000055c5ead27000
  R13: 0000000000000000 R14: 0000000000000000 R15: 000055c5ead27000

Example 2: rtrs-server does not send ACK to the heart-beat of rtrs-client
-------------------------------------------------------------------------

::

  echo 100 > /sys/kernel/debug/ip\:192.168.123.190@ip\:192.168.123.144/fault_inject/probability
  echo 5 > /sys/kernel/debug/ip\:192.168.123.190@ip\:192.168.123.144/fault_inject/times
  echo 1 > /sys/kernel/debug/ip\:192.168.123.190@ip\:192.168.123.144/fault_inject/fail-hb-ack

Expected Result::

  If rtrs-server does not send ACK more than 5 times, rtrs-client tries reconnection.

Check how many times rtrs-client did reconnection::

  cat /sys/devices/virtual/rtrs-client/bla/paths/ip\:192.168.122.142@ip\:192.168.122.130/stats/reconnects
  1 0
