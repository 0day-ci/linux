==========================================
I915 VM_BIND feature design and use cases
==========================================

VM_BIND feature
================
DRM_I915_GEM_VM_BIND/UNBIND ioctls allows UMD to bind/unbind GEM buffer
objects (BOs) or sections of a BOs at specified GPU virtual addresses on
a specified address space (VM).

These mappings (also referred to as persistent mappings) will be persistent
across multiple GPU submissions (execbuff) issued by the UMD, making execbuff
path leaner with fast path submission latency of O(1) w.r.t the number of
objects required for that submission.

UMDs can still send BOs of these persistent mappings in execlist of execbuff
for specifying BO dependencies (implicit fencing) and to use BO as a batch.

The persistent mappings are not individually tracked, instead the address
space (VM) they are mapped in is tracked to determine if the mappings are
being referred by GPU job (active) or not.

VM_BIND features include:
- Different VA mappings can map to the same physical pages of an object
  (aliasing).
- VA mapping can map to a partial section of the BO (partial binding).
- Support capture of mapping in the dump upon GPU error.
- TLB is flushed upon unbind completion.
- Asynchronous vm_bind and vm_unbind support.
- VM_BIND uses user/memory fence mechanism (explained below) for signaling
  bind completion.


User/Memory Fence
==================
The idea is to take a user process virtual address and install an interrupt
handler to wake up the current task when the memory location passes the user
supplied filter.

It also allows the user to emit their own MI_FLUSH/PIPE_CONTROL notify
interrupt within their batches after updating the value on the GPU to
have sub-batch precision on the wakeup.

User/Memory fence <user address and value pair> can also be supplied to the
kernel driver to signal/wake up the user process after completion of an
asynchronous operation.

This feature will be derived from the below original work:
https://patchwork.freedesktop.org/patch/349417/

When VM_BIND ioctl was provided with a user/memory fence via SYNC_FENCE
extension, it will be signaled upon the completion of binding of that
mapping. All async binds/unbinds are serialized, hence signaling of
user/memory fence also indicate the completion of all previous binds/unbinds.


TODOs
======
- Rebase VM_BIND on top of ongoing i915 TTM adoption changes including
  eviction support.
- Various optimizations like around LRU ordering of persistent mappings,
  batching of TLB flushes etc.


Intended use cases
===================

Debugger
---------
With debug event interface user space process (debugger) is able to keep track
of and act upon resources created by another process (debuggee) and attached
to GPU via vm_bind interface.

Mesa/Valkun
------------
VM_BIND can potentially reduce the CPU-overhead in Mesa thus improving
performance. For Vulkan it should be straightforward to use VM_BIND.
For Iris implicit buffer tracking must be implemented before we can harness
VM_BIND benefits. With increasing GPU hardware performance reducing CPU
overhead becomes more important.

Page level hints settings
--------------------------
VM_BIND allows any hints setting per mapping instead of per BO.
Possible hints include read-only, placement and atomicity.
Sub-BO level placement hint will be even more relevant with
upcoming GPU on-demand page fault support.

Page level Cache/CLOS settings
-------------------------------
VM_BIND allows cache/CLOS settings per mapping instead of per BO.

Compute
--------
Usage of dma-fence expects that they complete in reasonable amount of time.
Compute on the other hand can be long running. Hence it is appropriate for
compute to use user/memory fence (explained above) and dma-fence usage will
be limited to in kernel consumption only. Compute must opt-in for this
mechanism during context creation time with a 'compute_ctx' flag.

Where GPU page faults are not available, kernel driver upon buffer invalidation
must initiate a compute context suspend with a dma-fence attached to it.
And upon completion of that suspend fence, finish the invalidation and then
resume the compute context.

This is much easier to support with VM_BIND instead of the current heavier
execbuff path resource attachment.

Low Latency Submission
-----------------------
Allow compute UMDs to directly submit GPU jobs instead of through execbuff
ioctl. VM_BIND allows map/unmap of BOs required for directly submitted jobs.

Shared Virtual Memory (SVM) support
------------------------------------
VM_BIND interface can be used to map system memory directly (without gem BO
abstraction) using the HMM interface.


UAPI
=====
Uapi definiton can be found here:
.. kernel-doc:: Documentation/gpu/rfc/i915_vm_bind.h


Links:
======
- Reference WIP VM_BIND implementation can be found here.
  https://gitlab.freedesktop.org/nvishwa1/nvishwa1-drm-tip

  NOTE: It is WIP and not fully functional. There are known issues which
  are being worked upon.
