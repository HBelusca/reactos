/*
 * PROJECT:         ReactOS Boot Loader
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            boot/armllb/hw/omap3-beagle/hwinfo.c
 * PURPOSE:         LLB Hardware Info Routines for OMAP3 Beagle
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#include "precomp.h"

ULONG
NTAPI
LlbHwGetSerialUart(VOID)
{
    return 3;
}

/* EOF */
