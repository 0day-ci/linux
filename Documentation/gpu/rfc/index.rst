===============
GPU RFC Section
===============

For complex work, especially new uapi, it is often good to nail the high level
design issues before getting lost in the code details. This section is meant to
host such documentation:

* Each RFC should be a section in this file, explaining the goal and main design
  considerations. Especially for uapi make sure you Cc: all relevant project
  mailing lists and involved people outside of dri-devel.

* For uapi structures add a file to this directory with and then pull the
  kerneldoc in like with real uapi headers.

* Once the code has landed move all the documentation to the right places in
  the main core, helper or driver sections.

I915 DG1/LMEM RFC Section
=========================

Object placement and region query
=================================
Starting from DG1 we need to give userspace the ability to allocate buffers from
device local-memory. Currently the driver supports gem_create, which can place
buffers in system memory via shmem, and the usual assortment of other
interfaces, like dumb buffers and userptr.

To support this new capability, while also providing a uAPI which will work
beyond just DG1, we propose to offer three new bits of uAPI:

DRM_I915_QUERY_MEMORY_REGIONS
-----------------------------
Query mechanism which allows userspace to discover the list of supported memory
regions(like system-memory and local-memory) for a given device. We identify
each region with a class and instance pair, which should be unique. The class
here would be DEVICE or SYSTEM, and the instance would be zero, on platforms
like DG1.

Side note: The class/instance design is borrowed from our existing engine uAPI,
where we describe every physical engine in terms of its class, and the
particular instance, since we can have more than one per class.

In the future we also want to expose more information which can further
describe the capabilities of a region.

.. literalinclude:: i915_region_query.c

GEM_CREATE_EXT
--------------
New ioctl which is basically just gem_create but now allows userspace to
provide a chain of possible extensions. Note that if we don't provide any
extensions then we get the exact same behaviour as gem_create.

.. literalinclude:: i915_create_ext.c

Side note: We also need to support PXP[1] in the near future, which is also
applicable to integrated platforms, and adds its own gem_create_ext extension,
which basically lets userspace mark a buffer as "protected".

I915_OBJECT_PARAM_MEMORY_REGIONS
--------------------------------
Implemented as an extension for gem_create_ext, we would now allow userspace to
optionally provide an immutable list of preferred placements at creation time,
in priority order, for a given buffer object.  For the placements we expect
them each to use the class/instance encoding, as per the output of the regions
query. Having the list in priority order will be useful in the future when
placing an object, say during eviction.

.. literalinclude:: i915_create_ext_placements.c

As an example, on DG1 if we wish to set the placement as local-memory we can do
something like:

.. code-block::

        struct drm_i915_gem_memory_class_instance region_param = {
                .memory_class = I915_MEMORY_CLASS_DEVICE,
                .memory_instance = 0,
        };
        struct drm_i915_gem_create_ext_setparam setparam_region = {
                .base = { .name = I915_GEM_CREATE_EXT_SETPARAM },
                .param = {
                        .param = I915_OBJECT_PARAM_MEMORY_REGIONS,
                        .data = (uintptr_t)&region_param,
                        .size = 1,
                },
        };

        struct drm_i915_gem_create_ext create_ext = {
                .size = 16 * PAGE_SIZE,
                .extensions = (uintptr_t)&setparam_region,
        };
        int err = ioctl(fd, DRM_IOCTL_I915_GEM_CREATE_EXT, &create_ext);
        if (err) ...

One fair criticism here is that this seems a little over-engineered[2]. If we
just consider DG1 then yes, a simple gem_create.flags or something is totally
all that's needed to tell the kernel to allocate the buffer in local-memory or
whatever. However looking to the future we need uAPI which can also support
upcoming Xe HP multi-tile architecture in a sane way, where there can be
multiple local-memory instances for a given device, and so using both class and
instance in our uAPI to describe regions is desirable, although specifically
for DG1 it's uninteresting, since we only have a single local-memory instance.

[1] https://patchwork.freedesktop.org/series/86798/

[2] https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/5599#note_553791
