/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kd/i386/kdbg.c
 * PURPOSE:         NT 5.0-style Serial Port Kernel Debugging Transport Library
 * PROGRAMMER:      Alex Ionescu
 *                  Hervé Poussineau
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Serial debug connection */
#if defined(SARCH_PC98)
#define DEFAULT_DEBUG_PORT      2 /* COM2 */
#define DEFAULT_DEBUG_COM1_IRQ  4
#define DEFAULT_DEBUG_COM2_IRQ  5
#define DEFAULT_DEBUG_BAUD_RATE 9600
#define DEFAULT_BAUD_RATE       9600
#else
#define DEFAULT_DEBUG_PORT      2 /* COM2 */
#define DEFAULT_DEBUG_COM1_IRQ  4
#define DEFAULT_DEBUG_COM2_IRQ  3
#define DEFAULT_DEBUG_BAUD_RATE 115200
#define DEFAULT_BAUD_RATE       19200
#endif

#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>


/* STATIC VARIABLES ***********************************************************/

ULONG  ComPortNumber;
CPPORT KdComPort;
ULONG  KdComPortIrq = 0; // Not used at the moment.
#ifdef KDDEBUG
static CPPORT KdDebugComPort;
#endif
// PUCHAR KdComPortInUse = NULL; // In the HAL

/* The COM port must only be initialized once! */
// static BOOLEAN PortInitialized = FALSE;

/* DEBUGGING ******************************************************************/

#ifdef KDDEBUG
ULONG KdpDbgPrint(const char *Format, ...)
{
    va_list ap;
    int Length;
    char* ptr;
    CHAR Buffer[512];

    va_start(ap, Format);
    Length = _vsnprintf(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    /* Check if we went past the buffer */
    if (Length == -1)
    {
        /* Terminate it if we went over-board */
        Buffer[sizeof(Buffer) - 1] = '\n';

        /* Put maximum */
        Length = sizeof(Buffer);
    }

    ptr = Buffer;
    while (Length--)
    {
        if (*ptr == '\n')
            CpPutByte(&KdDebugComPort, '\r');

        CpPutByte(&KdDebugComPort, *ptr++);
    }

    return 0;
}
#endif

/* REACTOS FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
KdPortInitialize(
    _In_ PKD_PORT_INFORMATION PortInformation,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ BOOLEAN Initialize)
{
    NTSTATUS Status;
    ULONG ComPort = PortInformation->ComPort;

    KDDBGPRINT("KdPortInitialize, Port = COM%ld\n", ComPort);

    // TODO: Do some minor things.

    if (!Initialize)
        return TRUE;

    /* Initialize the serial port proper */
    Status = CpInitialize(&KdComPort,
                          UlongToPtr(BaseArray[ComPort]),
                          PortInformation->BaudRate);
    if (!NT_SUCCESS(Status))
        return FALSE;

    ComPortNumber = ComPort;
    KdComPortInUse = KdComPort.Address;
    return TRUE;
}

// ReactOS-specific
BOOLEAN
NTAPI
KdPortInitializeEx(
    IN PCPPORT PortInformation,
    IN ULONG ComPortNumber)
{
    NTSTATUS Status;

#if 0 // Deactivated because never used in fact (was in KdPortInitialize but we use KdPortInitializeEx)
    /*
     * Find the port if needed
     */

    if (!PortInitialized)
    {
        KdComPort.BaudRate = PortInformation->BaudRate;

        if (ComPortNumber == 0)
        {
            /*
             * Start enumerating COM ports from the last one to the first one,
             * and break when we find a valid port.
             * If we reach the first element of the list, the invalid COM port,
             * then it means that no valid port was found.
             */
            for (ComPortNumber = MAX_COM_PORTS; ComPortNumber > 0; ComPortNumber--)
            {
                if (CpDoesPortExist(UlongToPtr(BaseArray[ComPortNumber])))
                {
                    PortInformation->Address = KdComPort.Address = BaseArray[ComPortNumber];
                    break;
                }
            }
            if (ComPortNumber == 0)
            {
                HalDisplayString("\r\nKernel Debugger: No COM port found!\r\n\r\n");
                return FALSE;
            }
        }

        PortInitialized = TRUE;
    }
#endif

    /*
     * Initialize the port
     */
    Status = CpInitialize(PortInformation,
                          (ComPortNumber == 0 ? PortInformation->Address
                                              : UlongToPtr(BaseArray[ComPortNumber])),
                          (PortInformation->BaudRate == 0 ? DEFAULT_BAUD_RATE
                                                          : PortInformation->BaudRate));
    if (!NT_SUCCESS(Status))
    {
        HalDisplayString("\r\nKernel Debugger: Serial port not found!\r\n\r\n");
        return FALSE;
    }
    else
    {
#ifndef NDEBUG
        CHAR buffer[80];

        /* Print message to blue screen */
        sprintf(buffer,
                "\r\nKernel Debugger: Serial port found: COM%ld (Port 0x%p) BaudRate %ld\r\n\r\n",
                ComPortNumber,
                PortInformation->Address,
                PortInformation->BaudRate);
        HalDisplayString(buffer);
#endif /* NDEBUG */

        /* Set global info */
        KdComPortInUse = KdComPort.Address;
        return TRUE;
    }
}


ULONG
NTAPI
KdPortGetByte(
    _Out_ PUCHAR Byte)
{
    return CpGetByte(&KdComPort, Byte, TRUE, FALSE);
}

#if 0
// ReactOS-specific
BOOLEAN
NTAPI
KdPortGetByteEx(
    IN PCPPORT Port,
    OUT PUCHAR Byte)
{
    return (CpGetByte(Port, Byte, FALSE, FALSE) == CP_GET_SUCCESS);
}
#endif

ULONG
NTAPI
KdPortPollByte(
    _Out_ PUCHAR Byte)
{
    return CpGetByte(&KdComPort, Byte, FALSE, FALSE);
}

VOID
NTAPI
KdPortPutByte(
    _In_ UCHAR Byte)
{
    CpPutByte(&KdComPort, Byte);
}

VOID
NTAPI
KdPortRestore(VOID)
{
    NOTHING;
}

VOID
NTAPI
KdPortSave(VOID)
{
    NOTHING;
}

/* EOF */
