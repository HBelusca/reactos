/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Provides a serial port library for KDCOM, INBV, and FREELDR
 * COPYRIGHT:   Copyright 20xx ReactOS Portable Systems Group
 *              Copyright 2012-2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *
 * NOTE: This code is used by Headless Support (Ntoskrnl.exe and Osloader.exe)
 * and Kdcom.dll in Windows. It may be that WinDBG depends on some of these quirks.
 *
 * NOTE: The CPortLib has been updated in Windows 10 and is now documented:
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/uart/
 *
 * FIXMEs:
 * - Get x64 KDCOM, KDBG, FREELDR, and other current code to use this.
 */

/* INCLUDES *******************************************************************/

// #include <intrin.h>
// #include <ioaccess.h>
#include <ntstatus.h>
#include <cportlib/cportlib.h>

/* GLOBALS ********************************************************************/

// Wait timeout value for CpGetByte().
#define TIMEOUT_COUNT   (1024 * 200)


/*
 * Supported UART devices, ordered by serial subtype as specified
 * in the Microsoft Debug Port Table 2 (DBG2) specification.
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table#debug-device-information-structure
 * Backward-compatible with the Serial Port Console Redirection Table (SPCR).
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/serports/serial-port-console-redirection-table
 */
PUART_HARDWARE_DRIVER UartHardwareDrivers[] =
{
/* Specific non-ACPI PC-derived architectures */
#if defined(_X86_)
#if defined(SARCH_PC98)
    Pc98HardwareDriver,
#elif defined(SARCH_XBOX)
#endif
#endif

#if defined(_X86_) || defined(_AMD64_)

    Legacy16550HardwareDrive,   // "Legacy" Fully 16550-compatible
    Uart16550HardwareDriver,    // 16550 subset (16450) compatible with DBGP Revision 1
    NULL,                       // FIXME: MAX311xE SPI UART
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL,                       // FIXME: Intel USIF
    NULL, NULL, NULL, NULL, NULL, NULL,
    MM16550HardwareDriver       // 16550-compatible with parameters defined in Generic Address Structure.

#elif defined(_ARM_) || defined(_ARM64_)

    // TODO: Implement other ones - FIXME: Sort it out correctly!
    BeagleHardwareDriver,
    Omap3HardwareDriver,
    VersaHardwareDriver,

#else

#error "Unknown Processor Architecture"

#endif
};

// extern PUART_HARDWARE_DRIVER UartHardwareDrivers[];


// FIXME: To be determined at runtime.
ENABLE_FIFO UartEnableFifo = NULL;
DOES_PORT_EXIST UartDoesPortExist = NULL;
PUART_HARDWARE_DRIVER UartHardwareDriver = NULL;


/* FUNCTIONS ******************************************************************/

VOID
NTAPI
CpEnableFifo(
    _In_ PUCHAR Address,
    _In_ BOOLEAN Enable)
{
    UartEnableFifo(Address, Enable);
}

VOID
NTAPI
CpSetBaud(
    _In_ PCPPORT Port,
    _In_ ULONG BaudRate)
{
    (VOID)UartHardwareDriver->SetBaud(Port, BaudRate);
}

NTSTATUS
NTAPI
CpInitialize(
    _Inout_ PCPPORT Port,
    _In_ PUCHAR Address,
    _In_ ULONG BaudRate)
{
    /* Validity checks */
    if (Port == NULL || Address == NULL || BaudRate == 0)
        return STATUS_INVALID_PARAMETER;

#if 0
    if (!UartDoesPortExist(Address))
        return STATUS_NOT_FOUND;
#endif

    /* Initialize port data */
    Port->Address  = Address;
    Port->BaudRate = BaudRate;

    return UartHardwareDriver->InitializePort(NULL, Port, FALSE, AccessSize, BitWidth) ? STATUS_SUCCESS : STATUS_NOT_FOUND; // STATUS_INVALID_PARAMETER;
}

BOOLEAN
NTAPI
CpDoesPortExist(
    _In_ PUCHAR Address)
{
    return UartDoesPortExist(Address);
}

USHORT
NTAPI
CpGetByte(
    _In_ PCPPORT Port,
    _Out_ PUCHAR Byte,
    _In_ BOOLEAN Wait,
    _In_ BOOLEAN Poll)
{
    UART_STATUS Status;
    ULONG LimitCount = (Wait ? TIMEOUT_COUNT : 1);

    /* Handle early read-before-init */
    if (!Port->Address)
        return CP_GET_NODATA;

    /* If "wait" mode enabled, spin many times, otherwise attempt just once */
    while (LimitCount--)
    {
        // FIXME: Use UartHardwareDriver->RxReady(Port); if we just want to Poll ??
        Status = UartHardwareDriver->GetByte(Port, Byte);

        /* If an error happened, clear the byte and fail */
        if (Status == UartError)
        {
            *Byte = 0;
            return CP_GET_ERROR;
        }

        if (Status == UartSuccess)
        {
            /* If only polling was requested by caller, return now */
            // FIXME: Do **NOT** erase the out byte!
            if (Poll) return CP_GET_SUCCESS;

            /* Byte was read */
            return CP_GET_SUCCESS;
        }
    }

    /* No data was found */
    return CP_GET_NODATA;
}

VOID
NTAPI
CpPutByte(
    _In_ PCPPORT Port,
    _In_ UCHAR Byte)
{
    /* Send the byte, busy-waiting */
    (VOID)UartHardwareDriver->PutByte(Port, Byte, TRUE);
}

/* EOF */
