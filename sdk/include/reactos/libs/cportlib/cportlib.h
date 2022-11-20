/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Header for the ComPort Library
 * COPYRIGHT:   Copyright 20xx ReactOS Portable Systems Group
 *              Copyright 2012-2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *
 * NOTE: The CPortLib has been updated in Windows 10 and is now documented:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/uart/
 */

/* INCLUDES *******************************************************************/

#ifndef _CPORTLIB_
#define _CPORTLIB_

#pragma once

#include <ntdef.h>
#include "uart.h"

//
// Return error codes.
//
#define CP_GET_SUCCESS  0
#define CP_GET_NODATA   1
#define CP_GET_ERROR    2

//
// COM port flags.
//
#define CPPORT_FLAG_MODEM_CONTROL   PORT_RING_INDICATOR

typedef struct _CPPORT
{
    PUCHAR Address;
    ULONG  BaudRate;
    USHORT Flags;
} CPPORT, *PCPPORT;

/* ReactOS-specific callback */
typedef BOOLEAN
(NTAPI *ENABLE_FIFO(
    _In_ PUCHAR Address,
    _In_ BOOLEAN Enable);

VOID
NTAPI
CpEnableFifo(
    _In_ PUCHAR Address,
    _In_ BOOLEAN Enable);

VOID
NTAPI
CpSetBaud(
    _In_ PCPPORT Port,
    _In_ ULONG BaudRate);

NTSTATUS
NTAPI
CpInitialize(
    _Inout_ PCPPORT Port,
    _In_ PUCHAR Address,
    _In_ ULONG BaudRate);

/* ReactOS-specific callback */
typedef BOOLEAN
(NTAPI *DOES_PORT_EXIST(
    _In_ PUCHAR Address);

BOOLEAN
NTAPI
CpDoesPortExist(
    _In_ PUCHAR Address);

USHORT
NTAPI
CpGetByte(
    _In_ PCPPORT Port,
    _Out_ PUCHAR Byte,
    _In_ BOOLEAN Wait,
    _In_ BOOLEAN Poll);

VOID
NTAPI
CpPutByte(
    _In_ PCPPORT Port,
    _In_ UCHAR Byte);

#endif /* _CPORTLIB_ */

/* EOF */
