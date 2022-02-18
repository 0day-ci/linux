==========================
I915 Small BAR RFC Section
==========================
Starting from DG2 we will have resizable BAR support for device local-memory,
but in some cases the final BAR size might still be smaller than the total
local-memory size. In such cases only part of local-memory will be CPU
accessible, while the remainder is only accessible via the GPU.

I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS flag
----------------------------------------------
New gem_create_ext flag to tell the kernel that a BO will require CPU access.
The becomes important when placing an object in LMEM, where underneath the
device has a small BAR, meaning only part of it is CPU accessible. Without this
flag the kernel will assume that CPU access is not required, and prioritize
using the non-CPU visible portion of LMEM(if present on the device).

Related to this, we now also reject any objects marked with
EXEC_OBJECT_CAPTURE, which are also not tagged with NEEDS_CPU_ACCESS. This only
impacts DG2+.

XXX: One open here is whether we should extend the memory region query to return
the CPU visible size of the region. For now the IGTs just use debugfs to query
the size. However, if userspace sees a real need for this then extending the
region query would be a lot nicer.

.. kernel-doc:: Documentation/gpu/rfc/i915_small_bar.h
   :functions: __drm_i915_gem_create_ext

DRM_I915_QUERY_VMA_INFO query
-----------------------------
Query the attributes of some vma. Given a vm and GTT offset, find the
respective vma, and return its set of attrubutes. For now we only support
DRM_I915_QUERY_VMA_INFO_CPU_VISIBLE, which is set if the object/vma is
currently placed in memory that is accessible by the CPU. This should always be
set on devices where the CPU visible size of LMEM matches the probed size. If
this is not set then CPU faulting the object will first require migrating the
pages.

.. kernel-doc:: Documentation/gpu/rfc/i915_small_bar.h
   :functions: __drm_i915_query_vma_info
