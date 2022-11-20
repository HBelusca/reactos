/*
 * PROJECT:     ReactOS KDBG Kernel Debugger Terminal Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Serial driver
 * COPYRIGHT:   Copyright 2004 Art Yerkes <ayerkes@speakeasy.net>
 *              Copyright 2005 Gregor Anich <blight@blight.eu.org>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "kd.h"

/* FUNCTIONS *****************************************************************/

VOID
KdbpSendCommandSerial(
    _In_ PCSTR Command)
{
    while (*Command)
        KdPortPutByte(*Command++);
}

CHAR
KdbpTryGetCharSerial(
    _In_ ULONG Retry)
{
    CHAR Result = -1;

    if (Retry == 0)
        while (!KdPortGetByte((PUCHAR)&Result)); // KdPortPollByte
    else
        while (!KdPortGetByte((PUCHAR)&Result) && Retry-- > 0); // KdPortPollByte

    return Result;
}

/* EOF */
