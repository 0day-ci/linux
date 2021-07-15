.. SPDX-License-Identifier: GPL-2.0

STM32 Chrom-Art 2D Graphics Accelerator unit (DMA2D) driver
================================================

The DMA2D driver implements the following driver-specific controls:

``V4L2_CID_DMA2D_R2M_MODE``
-------------------------
    Enable/Disable the Register-To-Memory mode, filling a part or the
    whole of a destination image with a specific color.

    1 for enable, 0 for disable.

``V4L2_CID_DMA2D_R2M_COLOR``
-------------------------------
    Set the color to fill a part or the whole of a destination image.
    only used under Register-To-Memory mode, to set the DMA2D_OCOLR register
    (RED, GREEN, BLUE) which is:

    31 .. 24    23 .. 16  15 .. 8     7 .. 0
    ALPHA[7:0]  RED[7:0]  GREEN[7:0]  BLUE[7:0]
