/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kd/i386/kdbg.c
 * PURPOSE:         Serial i/o functions for the kernel debugger.
 *                  Serial Port Kernel Debugging Transport Library
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

// static CPPORT DefaultPort = {0, 0, 0};

/* The COM port must only be initialized once! */
// static BOOLEAN PortInitialized = FALSE;

/* REACTOS FUNCTIONS **********************************************************/

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
        DefaultPort.BaudRate = PortInformation->BaudRate;

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
                    PortInformation->Address = DefaultPort.Address = BaseArray[ComPortNumber];
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

#if 0
        /* Set global info */
        KdComPortInUse = DefaultPort.Address;
#endif
        return TRUE;
    }
}

BOOLEAN
NTAPI
KdPortGetByteEx(
    IN PCPPORT PortInformation,
    OUT PUCHAR ByteReceived)
{
    return (CpGetByte(PortInformation, ByteReceived, FALSE, FALSE) == CP_GET_SUCCESS);
}

VOID
NTAPI
KdPortPutByteEx(
    IN PCPPORT PortInformation,
    IN UCHAR ByteToSend)
{
    CpPutByte(PortInformation, ByteToSend);
}

/* EOF */
