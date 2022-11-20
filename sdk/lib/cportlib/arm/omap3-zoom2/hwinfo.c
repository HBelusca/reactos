/*
 * PROJECT:         ReactOS Boot Loader
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            boot/armllb/hw/omap3-zoom2/hwinfo.c
 * PURPOSE:         LLB Hardware Info Routines for OMAP3 ZOOM2
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#include "precomp.h"

TIMEINFO LlbTime;

#define BCD_INT(bcd) (((bcd & 0xf0) >> 4) * 10 + (bcd &0x0f))

ULONG
NTAPI
LlbHwGetSerialUart(VOID)
{
    return 0;
}

/* EOF */
