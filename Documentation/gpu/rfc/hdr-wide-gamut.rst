==============================
HDR & Wide Color Gamut Support
==============================

.. role:: wy-text-strike

ToDo
====

* :wy-text-strike:`Reformat as RST kerneldoc` - done
* :wy-text-strike:`Don't use color_encoding for color_space definitions` - done
* :wy-text-strike:`Update SDR luminance description and reasoning` - done
* :wy-text-strike:`Clarify 3D LUT required for some color space transformations` - done
* :wy-text-strike:`Highlight need for named color space and EOTF definitions` - done
* :wy-text-strike:`Define transfer function API` - done
* :wy-text-strike:`Draft upstream plan` - done
* :wy-text-strike:`Reference to wayland plan` - done
* Reference to Chrome plans
* Sketch view of HW pipeline for couple of HW implementations


Upstream Plan
=============

* Reach consensus on DRM/KMS API
* Implement support in amdgpu
* Implement IGT tests
* Add API support to Weston, ChromiumOS, or other canonical open-source project interested in HDR
* Merge user-space
* Merge kernel patches


History
=======

v3:

* Add sections on single-plane and multi-plane HDR
* Describe approach to define HW details vs approach to define SW intentions
* Link Jeremy Cline's excellent HDR summaries
* Outline intention behind overly verbose doc
* Describe FP16 use-case
* Clean up links

v2: create this doc

v1: n/a


Introduction
============

We are looking to enable HDR support for a couple of single-plane and
multi-plane scenarios. To do this effectively we recommend new interfaces
to drm_plane. Below I'll give a bit of background on HDR and why we
propose these interfaces.

As an RFC doc this document is more verbose than what we would want from
an eventual uAPI doc. This is intentional in order to ensure interested
parties are all on the same page and to facilitate discussion if there
is disagreement on aspects of the intentions behind the proposed uAPI.


Overview and background
=======================

I highly recommend you read `Jeremy Cline's HDR primer`_

Jeremy Cline did a much better job describing this. I highly recommend
you read it at [1]:

.. _Jeremy Cline's HDR primer: https://www.jcline.org/blog/fedora/graphics/hdr/2021/05/07/hdr-in-linux-p1.html

Defining a pixel's luminance
----------------------------

The luminance space of pixels in a framebuffer/plane presented to the
display is not well defined in the DRM/KMS APIs. It is usually assumed to
be in a 2.2 or 2.4 gamma space and has no mapping to an absolute luminance
value; it is interpreted in relative terms.

Luminance can be measured and described in absolute terms as candela
per meter squared, or cd/m2, or nits. Even though a pixel value can be
mapped to luminance in a linear fashion to do so without losing a lot of
detail requires 16-bpc color depth. The reason for this is that human
perception can distinguish roughly between a 0.5-1% luminance delta. A
linear representation is suboptimal, wasting precision in the highlights
and losing precision in the shadows.

A gamma curve is a decent approximation to a human's perception of
luminance, but the `PQ (perceptual quantizer) function`_ improves on
it. It also defines the luminance values in absolute terms, with the
highest value being 10,000 nits and the lowest 0.0005 nits.

Using a content that's defined in PQ space we can approximate the real
world in a much better way.

Here are some examples of real-life objects and their approximate
luminance values:


.. _PQ (perceptual quantizer) function: https://en.wikipedia.org/wiki/High-dynamic-range_video#Perceptual_Quantizer

.. flat-table::
   :header-rows: 1

   * - Object
     - Luminance in nits

   *  - Fluorescent light
      - 10,000

   *  - Highlights
      - 1,000 - sunlight

   *  - White Objects
      - 250 - 1,000

   *  - Typical Objects
      - 1 - 250

   *  - Shadows
      - 0.01 - 1

   *  - Ultra Blacks
      - 0 - 0.0005


Transfer functions
------------------

Traditionally we used the terms gamma and de-gamma to describe the
encoding of a pixel's luminance value and the operation to transfer from
a linear luminance space to the non-linear space used to encode the
pixels. Since some newer encodings don't use a gamma curve I suggest
we refer to non-linear encodings using the terms `EOTF, and OETF`_, or
simply as transfer function in general.

The EOTF (Electro-Optical Transfer Function) describes how to transfer
from an electrical signal to an optical signal. This was traditionally
done by the de-gamma function.

The OETF (Opto Electronic Transfer Function) describes how to transfer
from an optical signal to an electronic signal. This was traditionally
done by the gamma function.

More generally we can name the transfer function describing the transform
between scanout and blending space as the **input transfer function**, and
the transfer function describing the transform from blending space to the
output space as **output transfer function**.


.. _EOTF, and OETF: https://en.wikipedia.org/wiki/Transfer_functions_in_imaging

Mastering Luminances
--------------------

Even though we are able to describe the absolute luminance of a pixel
using the PQ 2084 EOTF we are presented with physical limitations of the
display technologies on the market today. Here are a few examples of
luminance ranges of displays.

.. flat-table::
   :header-rows: 1

   * - Display
     - Luminance range in nits

   *  - Typical PC display
      - 0.3 - 200

   *  - Excellent LCD HDTV
      - 0.3 - 400

   *  - HDR LCD w/ local dimming
      - 0.05 - 1,500

Since no display can currently show the full 0.0005 to 10,000 nits
luminance range of PQ the display will need to tone-map the HDR content,
i.e to fit the content within a display's capabilities. To assist
with tone-mapping HDR content is usually accompanied by a metadata
that describes (among other things) the minimum and maximum mastering
luminance, i.e. the maximum and minimum luminance of the display that
was used to master the HDR content.

The HDR metadata is currently defined on the drm_connector via the
hdr_output_metadata blob property.

It might be useful to define per-plane hdr metadata, as different planes
might have been mastered differently.

.. _SDR Luminance:

SDR Luminance
-------------

Traditional SDR content's maximum white luminance is not well defined.
Some like to define it at 80 nits, others at 200 nits. It also depends
to a large extent on the environmental viewing conditions. In practice
this means that we need to define the maximum SDR white luminance, either
in nits, or as a ratio.

`One Windows API`_ defines it as a ratio against 80 nits.

`Another Windows API`_ defines it as a nits value.

The `Wayland color management proposal`_ uses Apple's definition of EDR as a
ratio of the HDR range vs SDR range.

If a display's maximum HDR white level is correctly reported it is trivial
to convert between all of the above representations of SDR white level. If
it is not, defining SDR luminance as a nits value, or a ratio vs a fixed
nits value is preferred, assuming we are blending in linear space.

It is our experience that many HDR displays do not report maximum white
level correctly

.. _One Windows API: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_dxgkarg_settargetadjustedcolorimetry2
.. _Another Windows API: https://docs.microsoft.com/en-us/uwp/api/windows.graphics.display.advancedcolorinfo.sdrwhitelevelinnits?view=winrt-20348
.. _Wayland color management proposal: https://gitlab.freedesktop.org/swick/wayland-protocols/-/blob/color/unstable/color-management/color.rst#id8

Let There Be Color
------------------

So far we've only talked about luminance, ignoring colors altogether. Just
like in the luminance space, traditionally the color space of display
outputs has not been well defined. Similar to how an EOTF defines a
mapping of pixel data to an absolute luminance value, the color space
maps color information for each pixel onto the CIE 1931 chromaticity
space. This can be thought of as a mapping to an absolute, real-life,
color value.

A color space is defined by its primaries and white point. The primaries
and white point are expressed as coordinates in the CIE 1931 color
space. Think of the red primary as the reddest red that can be displayed
within the color space. Same for green and blue.

Examples of color spaces are:

.. flat-table::
   :header-rows: 1

   * - Color Space
     - Description

   *  - BT 601
      - similar to BT 709

   *  - BT 709
      - used by sRGB content; ~53% of BT 2020

   *  - DCI-P3
      - used by most HDR displays; ~72% of BT 2020

   *  - BT 2020
      - standard for most HDR content



Color Primaries and White Point
-------------------------------

Just like displays can currently not represent the entire 0.0005 -
10,000 nits HDR range of the PQ 2084 EOTF, they are currently not capable
of representing the entire BT.2020 color Gamut. For this reason video
content will often specify the color primaries and white point used to
master the video, in order to allow displays to be able to map the image
as best as possible onto the display's gamut.


Displays and Tonemapping
------------------------

External displays are able to do their own tone and color mapping, based
on the mastering luminance, color primaries, and white space defined in
the HDR metadata.

Some internal panels might not include the complex HW to do tone and color
mapping on their own and will require the display driver to perform
appropriate mapping.


How are we solving the problem?
===============================

Single-plane
------------

If a single drm_plane is used no further work is required. The compositor
will provide one HDR plane alongside a drm_connector's hdr_output_metadata
and the display HW will output this plane without further processing if
no CRTC LUTs are provided.

If desired a compositor can use the CRTC LUTs for HDR content but without
support for PWL or multi-segmented LUTs the quality of the operation is
expected to be subpar for HDR content.


Multi-plane
-----------

In multi-plane configurations we need to solve the problem of blending
HDR and SDR content. This blending should be done in linear space and
therefore requires framebuffer data that is presented in linear space
or a way to convert non-linear data to linear space. Additionally
we need a way to define the luminance of any SDR content in relation
to the HDR content.

In order to present framebuffer data in linear space without losing a
lot of precision it needs to be presented using 16 bpc precision.


Defining HW Details
-------------------

One way to take full advantage of modern HW's color pipelines is by
defining a "generic" pipeline that matches all capable HW. Something
like this, which I took `from Uma Shankar`_ and expanded on:

.. _from Uma Shankar: https://patchwork.freedesktop.org/series/90826/

.. kernel-figure::  colorpipe.svg

I intentionally put de-Gamma, and Gamma in parentheses in my graph
as they describe the intention of the block but not necessarily a
strict definition of how a userspace implementation is required to
use them.

De-Gamma and Gamma blocks are named LUT, but they could be non-programmable
LUTs in some HW implementations with no programmable LUT available. See
the definitions for AMD's `latest dGPU generation`_ as an example.

.. _latest dGPU generation: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/gpu/drm/amd/display/dc/dcn30/dcn30_resource.c?h=v5.13#n2586

I renamed the "Plane Gamma LUT" and "CRTC De-Gamma LUT" to "Tonemapping"
as we generally don't want to re-apply gamma before blending, or do
de-gamma post blending. These blocks tend generally to be intended for
tonemapping purposes.

Tonemapping in this case could be a simple nits value or `EDR`_ to describe
how to scale the :ref:`SDR luminance`.

Tonemapping could also include the ability to use a 3D LUT which might be
accompanied by a 1D shaper LUT. The shaper LUT is required in order to
ensure a 3D LUT with limited entries (e.g. 9x9x9, or 17x17x17) operates
in perceptual (non-linear) space, so as to evenly spread the limited
entries evenly across the perceived space.

.. _EDR: https://gitlab.freedesktop.org/swick/wayland-protocols/-/blob/color/unstable/color-management/color.rst#id8

Creating a model that is flexible enough to define color pipelines for
a wide variety of HW is challenging, though not impossible. Implementing
support for such a flexible definition in userspace, though, amounts
to essentially writing color pipeline drivers for each HW.


Defining SW Intentions
----------------------

An alternative to describing the HW color pipeline in enough detail to
be useful for color management and HDR purposes is to instead define
SW intentions.

.. kernel-figure::  color_intentions.svg

This greatly simplifies the API and lets the driver do what a driver
does best: figure out how to program the HW to achieve the desired
effect.

The above diagram could include white point, primaries, and maximum
peak and average white levels in order to facilitate tone mapping.

At this point I suggest to keep tonemapping (other than an SDR luminance
adjustment) out of the current DRM/KMS API. Most HDR displays are capable
of tonemapping. If for some reason tonemapping is still desired on
a plane, a shader might be a better way of doing that instead of relying
on display HW.

In some ways this mirrors how various userspace APIs treat HDR:
 * Gstreamer's `GstVideoTransferFunction`_
 * EGL's `EGL_EXT_gl_colorspace_bt2020_pq`_ extension
 * Vulkan's `VkColorSpaceKHR`_

.. _GstVideoTransferFunction: https://gstreamer.freedesktop.org/documentation/video/video-color.html?gi-language=c#GstVideoTransferFunction
.. _EGL_EXT_gl_colorspace_bt2020_pq: https://www.khronos.org/registry/EGL/extensions/EXT/EGL_EXT_gl_colorspace_bt2020_linear.txt
.. _VkColorSpaceKHR: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VkColorSpaceKHR


A hybrid approach to the API
----------------------------

Our current approach attempts a hybrid approach, defining API to specify
input and output transfer functions, as well as an SDR boost, and a
input color space definition.

We would like to solicit feedback and encourage discussion around the
merits and weaknesses of these approaches. This question is at the core
of defining a good API and we'd like to get it right.


Input and Output Transfer functions
-----------------------------------

We define an input transfer function on drm_plane to describe the
transform from framebuffer to blending space.

We define an output transfer function on drm_crtc to describe the
transform from blending space to display space.

The transfer function can be a pre-defined function, such as PQ EOTF, or
a custom LUT. A driver will be able to specify support for specific
transfer functions, including custom ones.

Defining the transfer function in this way allows us to support in on HW
that uses ROMs to support these transforms, as well as on HW that use
LUT definitions that are complex and don't map easily onto a standard LUT
definition.

We will not define per-plane LUTs in this patchset as the scope of our
current work only deals with pre-defined transfer functions. This API has
the flexibility to add custom 1D or 3D LUTs at a later date.

In order to support the existing 1D de-gamma and gamma LUTs on the drm_crtc
we will include a "custom 1D" enum value to indicate that the custom gamma and
de-gamma 1D LUTs should be used.

Possible transfer functions:

.. flat-table::
   :header-rows: 1

   * - Transfer Function
     - Description

   *  - Gamma 2.2
      - a simple 2.2 gamma function

   *  - sRGB
      - 2.4 gamma with small initial linear section

   *  - PQ 2084
      - SMPTE ST 2084; used for HDR video and allows for up to 10,000 nit support

   *  - Linear
      - Linear relationship between pixel value and luminance value

   *  - Custom 1D
      - Custom 1D de-gamma and gamma LUTs; one LUT per color

   *  - Custom 3D
      - Custom 3D LUT (to be defined)


Describing SDR Luminance
------------------------------

Since many displays do no correctly advertise the HDR white level we
propose to define the SDR white level in nits.

We define a new drm_plane property to specify the white level of an SDR
plane.


Defining the color space
------------------------

We propose to add a new color space property to drm_plane to define a
plane's color space.

While some color space conversions can be performed with a simple color
transformation matrix (CTM) others require a 3D LUT.


Defining mastering color space and luminance
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

ToDo



Pixel Formats
~~~~~~~~~~~~~

The pixel formats, such as ARGB8888, ARGB2101010, P010, or FP16 are
unrelated to color space and EOTF definitions. HDR pixels can be formatted
in different ways but in order to not lose precision HDR content requires
at least 10 bpc precision. For this reason ARGB2101010, P010, and FP16 are
the obvious candidates for HDR. ARGB2101010 and P010 have the advantage
of requiring only half the bandwidth as FP16, while FP16 has the advantage
of enough precision to operate in a linear space, i.e. without EOTF.


Use Cases
=========

RGB10 HDR plane - composited HDR video & desktop
------------------------------------------------

A single, composited plane of HDR content. The use-case is a video player
on a desktop with the compositor owning the composition of SDR and HDR
content. The content shall be PQ BT.2020 formatted. The drm_connector's
hdr_output_metadata shall be set.


P010 HDR video plane + RGB8 SDR desktop plane
---------------------------------------------
A normal 8bpc desktop plane, with a P010 HDR video plane underlayed. The
HDR plane shall be PQ BT.2020 formatted. The desktop plane shall specify
an SDR boost value. The drm_connector's hdr_output_metadata shall be set.


One XRGB8888 SDR Plane - HDR output
-----------------------------------

In order to support a smooth transition we recommend an OS that supports
HDR output to provide the hdr_output_metadata on the drm_connector to
configure the output for HDR, even when the content is only SDR. This will
allow for a smooth transition between SDR-only and HDR content. In this
use-case the SDR max luminance value should be provided on the drm_plane.

In DCN we will de-PQ or de-Gamma all input in order to blend in linear
space. For SDR content we will also apply any desired boost before
blending. After blending we will then re-apply the PQ EOTF and do RGB
to YCbCr conversion if needed.

FP16 HDR linear planes
----------------------

These will require a transformation into the display's encoding (e.g. PQ)
using the CRTC LUT. Current CRTC LUTs are lacking the precision in the
dark areas to do the conversion without losing detail.

One of the newly defined output transfer functions or a PWL or `multi-segmented
LUT`_ can be used to facilitate the conversion to PQ, HLG, or another
encoding supported by displays.

.. _multi-segmented LUT: https://patchwork.freedesktop.org/series/90822/


User Space
==========

Gnome & GStreamer
-----------------

See Jeremy Cline's `HDR in Linux\: Part 2`_.

.. _HDR in Linux\: Part 2: https://www.jcline.org/blog/fedora/graphics/hdr/2021/06/28/hdr-in-linux-p2.html


Wayland
-------

See `Wayland Color Management and HDR Design Goals`_.

.. _Wayland Color Management and HDR Design Goals: https://gitlab.freedesktop.org/swick/wayland-protocols/-/blob/color/unstable/color-management/color.rst


ChromeOS Ozone
--------------

ToDo


HW support
==========

ToDo, describe pipeline on a couple different HW platforms


Further Reading
===============

* https://gitlab.freedesktop.org/swick/wayland-protocols/-/blob/color/unstable/color-management/color.rst
* http://downloads.bbc.co.uk/rd/pubs/whp/whp-pdf-files/WHP309.pdf
* https://app.spectracal.com/Documents/White%20Papers/HDR_Demystified.pdf
* https://www.jcline.org/blog/fedora/graphics/hdr/2021/05/07/hdr-in-linux-p1.html
* https://www.jcline.org/blog/fedora/graphics/hdr/2021/06/28/hdr-in-linux-p2.html


