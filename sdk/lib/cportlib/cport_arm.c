/*
 * PROJECT:         ReactOS ComPort Library for NEC PC-98 series
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Provides a serial port library for KDCOM, INBV, and FREELDR
 * COPYRIGHT:       Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <intrin.h>
#include <ioaccess.h>
#include <ntstatus.h>
#include <cportlib/cportlib.h>
// #include <drivers/pc98/serial.h>

/* GLOBALS ********************************************************************/

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
CpEnableFifo(
    IN PUCHAR Address,
    IN BOOLEAN Enable)
{
}

VOID
NTAPI
CpSetBaud(
    IN PCPPORT Port,
    IN ULONG BaudRate)
{
    return STATUS_NOT_FOUND;
}

BOOLEAN
NTAPI
CpDoesPortExist(IN PUCHAR Address)
{
    return FALSE;
}

UCHAR
NTAPI
CpReadLsr(
    IN PCPPORT Port,
    IN UCHAR ExpectedValue)
{
    return 0;
}

USHORT
NTAPI
CpGetByte(
    IN PCPPORT Port,
    OUT PUCHAR Byte,
    IN BOOLEAN Wait,
    IN BOOLEAN Poll)
{
    return CP_GET_NODATA;
}

VOID
NTAPI
CpPutByte(
    IN PCPPORT Port,
    IN UCHAR Byte)
{
}
