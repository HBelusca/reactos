/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     UART Initialization Routines for OMAP3 Beagle
 * COPYRIGHT:   Copyright 20xx ReactOS Portable Systems Group
 *              Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <cportlib/cportlib.h>

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
BeagleEnableFifo(
    _In_ PUCHAR Address,
    _In_ BOOLEAN Enable)
{
}

// LlbHwOmap3UartInitialize(VOID)
BOOLEAN
NTAPI
BeagleInitializePort(
    _In_opt_ PCSTR LoadOptions,
    _Inout_ PCPPORT Port,
    _In_ BOOLEAN MemoryMapped,
    _In_ UCHAR AccessSize,
    _In_ UCHAR BitWidth)
{
    return TRUE; // FALSE;
}

BOOLEAN
NTAPI
BeagleDoesPortExist(
    _In_ PUCHAR Address)
{
    return TRUE; // FALSE;
}

// LlbHwUartSendChar(IN CHAR Char)
UART_STATUS
NTAPI
BeagleGetByte(
    _In_ /*_Inout_*/ PCPPORT Port,
    _Out_ PUCHAR Byte)
{
    return UartNotReady;
}

UART_STATUS
NTAPI
BeaglePutByte(
    _In_ /*_Inout_*/ PCPPORT Port,
    _In_ UCHAR Byte,
    _In_ BOOLEAN BusyWait)
{
    return UartNotReady;
}

BOOLEAN
NTAPI
BeagleRxReady(
    _In_ /*_Inout_*/ PCPPORT Port)
{
    return FALSE;
}


ULONG
NTAPI
LlbHwGetUartBase(IN ULONG Port)
{
    // FIXME: Should be 0-based instead!
    if (Port == 1)
    {
        return 0x4806A000;
    }
    else if (Port == 2)
    {
        return 0x4806C000;
    }
    else if (Port == 3) // Console UART
    {
        return 0x49020000;
    }
    else if (Port == 4)
    {
        return 0x49042000;
    }

    return 0;
}

ENABLE_FIFO xxx = BeagleEnableFifo;
DOES_PORT_EXIST xxx = BeagleDoesPortExist;

UART_HARDWARE_DRIVER
BeagleHardwareDriver =
{
    BeagleInitializePort,
    BeagleSetBaud,
    BeagleGetByte,
    BeaglePutByte,
    BeagleRxReady
};

/* EOF */
