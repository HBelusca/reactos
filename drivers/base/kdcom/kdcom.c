/*
 * COPYRIGHT:       GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            drivers/base/kddll/kdcom.c
 * PURPOSE:         COM port functions for the kernel debugger.
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include "kddll.h"

#include <arc/arc.h>
#include <stdio.h>
#include <stdlib.h>
#include <ndk/halfuncs.h>

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


/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
KdD0Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdSave(IN BOOLEAN SleepTransition)
{
    /* Nothing to do on COM ports */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdRestore(IN BOOLEAN SleepTransition)
{
    /* Nothing to do on COM ports */
    return STATUS_SUCCESS;
}

/******************************************************************************
 * \name KdDebuggerInitialize0
 * \brief Phase 0 initialization.
 * \param [opt] LoaderBlock Pointer to the Loader parameter block. Can be NULL.
 * \return Status
 */
NTSTATUS
NTAPI
KdDebuggerInitialize0(IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    KD_PORT_INFORMATION PortInfo = {DEFAULT_DEBUG_PORT, DEFAULT_DEBUG_BAUD_RATE};
    PCHAR CommandLine, PortString, BaudString, IrqString;
    ULONG Value;

    /* Check if we have a LoaderBlock */
    if (LoaderBlock)
    {
        /* Get the Command Line */
        CommandLine = LoaderBlock->LoadOptions;

        /* Upcase it */
        _strupr(CommandLine);

        /* Get the port and baud rate */
        PortString = strstr(CommandLine, "DEBUGPORT");
        BaudString = strstr(CommandLine, "BAUDRATE");
        IrqString  = strstr(CommandLine, "IRQ");

        /* Check if we got the /DEBUGPORT parameter */
        if (PortString)
        {
            /* Move past the actual string, to reach the port*/
            PortString += strlen("DEBUGPORT");

            /* Now get past any spaces and skip the equal sign */
            while (*PortString == ' ') PortString++;
            PortString++;

            /* Do we have a serial port? */
            if (strncmp(PortString, "COM", 3) != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }

            /* Check for a valid Serial Port */
            PortString += 3;
            Value = atol(PortString);
            if (Value >= sizeof(BaseArray) / sizeof(BaseArray[0]))
            {
                return STATUS_INVALID_PARAMETER;
            }

            /* Set the port to use */
            PortInfo.ComPort = Value;
        }

        /* Check if we got a baud rate */
        if (BaudString)
        {
            /* Move past the actual string, to reach the rate */
            BaudString += strlen("BAUDRATE");

            /* Now get past any spaces */
            while (*BaudString == ' ') BaudString++;

            /* And make sure we have a rate */
            if (*BaudString)
            {
                /* Read and set it */
                Value = atol(BaudString + 1);
                if (Value) PortInfo.BaudRate = Value;
            }
        }

        /* Check Serial Port Settings [IRQ] */
        if (IrqString)
        {
            /* Move past the actual string, to reach the rate */
            IrqString += strlen("IRQ");

            /* Now get past any spaces */
            while (*IrqString == ' ') IrqString++;

            /* And make sure we have an IRQ */
            if (*IrqString)
            {
                /* Read and set it */
                Value = atol(IrqString + 1);
                if (Value) KdComPortIrq = Value;
            }
        }
    }

#ifdef KDDEBUG
    /*
     * Try to find a free COM port and use it as the KD debugging port.
     * NOTE: Inspired by reactos/boot/freeldr/freeldr/comm/rs232.c, Rs232PortInitialize(...)
     */
    {
    /*
     * Start enumerating COM ports from the last one to the first one,
     * and break when we find a valid port.
     * If we reach the first element of the list, the invalid COM port,
     * then it means that no valid port was found.
     */
    ULONG ComPort;
    for (ComPort = MAX_COM_PORTS; ComPort > 0; ComPort--)
    {
        /* Check if the port exist; skip the KD port */
        if ((ComPort != PortInfo.ComPort) && CpDoesPortExist(UlongToPtr(BaseArray[ComPort])))
            break;
    }
    if (ComPort != 0)
        CpInitialize(&KdDebugComPort, UlongToPtr(BaseArray[ComPort]), DEFAULT_BAUD_RATE);
    }
#endif

    KDDBGPRINT("KdDebuggerInitialize0\n");

    /* Initialize the port */
    return (KdPortInitialize(&PortInfo, LoaderBlock, TRUE)
                ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL);
}

/******************************************************************************
 * \name KdDebuggerInitialize1
 * \brief Phase 1 initialization.
 * \param [opt] LoaderBlock Pointer to the Loader parameter block. Can be NULL.
 * \return Status
 */
NTSTATUS
NTAPI
KdDebuggerInitialize1(IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    return STATUS_SUCCESS;
}


VOID
NTAPI
KdpSendByte(IN UCHAR Byte)
{
    /* Send the byte */
    KdPortPutByte(Byte);
}

KDSTATUS
NTAPI
KdpPollByte(OUT PUCHAR OutByte)
{
    ULONG CpStatus;

    /* Poll the byte */
    CpStatus = KdPortPollByte(OutByte);
    switch (CpStatus)
    {
        case CP_GET_SUCCESS:
            return KdPacketReceived;

        case CP_GET_NODATA:
            return KdPacketTimedOut;

        case CP_GET_ERROR:
        default:
            return KdPacketNeedsResend;
    }
}

KDSTATUS
NTAPI
KdpReceiveByte(OUT PUCHAR OutByte)
{
    ULONG CpStatus;

    /* Get the byte */
    CpStatus = KdPortGetByte(OutByte);
    switch (CpStatus)
    {
        case CP_GET_SUCCESS:
            return KdPacketReceived;

        case CP_GET_NODATA:
            return KdPacketTimedOut;

        case CP_GET_ERROR:
        default:
            return KdPacketNeedsResend;
    }
}

KDSTATUS
NTAPI
KdpPollBreakIn(VOID)
{
    KDSTATUS KdStatus;
    UCHAR Byte;

    KdStatus = KdpPollByte(&Byte);
    if ((KdStatus == KdPacketReceived) && (Byte == BREAKIN_PACKET_BYTE))
    {
        return KdPacketReceived;
    }
    return KdPacketTimedOut;
}

/* EOF */
