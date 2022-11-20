/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kdbg/kdb_serial.c
 * PURPOSE:         Serial driver
 *
 * PROGRAMMERS:     Victor Kirhenshtein (sauros@iname.com)
 *                  Jason Filby (jasonfilby@yahoo.com)
 *                  arty
 */

/* INCLUDES ****************************************************************/

#include <ntoskrnl.h>

CHAR
KdbpTryGetCharSerial(ULONG Retry)
{
    CHAR Result = -1;

    if (Retry == 0)
        while (!KdPortGetByte((PUCHAR)&Result)); // KdPortPollByte
    else
        while (!KdPortGetByte((PUCHAR)&Result) && Retry-- > 0); // KdPortPollByte

    return Result;
}
