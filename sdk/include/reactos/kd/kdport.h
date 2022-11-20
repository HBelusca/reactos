/*
 * PROJECT:         ReactOS kernel
 * COPYRIGHT:       See COPYING in the top level directory
 * PURPOSE:         NT 5.0-style Serial Port Kernel Debugging Transport Library
 *                  Kernel Debugger Port Definition
 * PROGRAMMER:      Me
 */

#ifndef _KDPORT_
#define _KDPORT_

#pragma once

/* Port Information for the Serial Native Mode */
extern ULONG  ComPortNumber;
extern CPPORT KdComPort;
extern ULONG  KdComPortIrq; // Not used at the moment.
// extern NTHALAPI PUCHAR KdComPortInUse; // In ndk/haltypes.h

typedef struct _KD_PORT_INFORMATION
{
    ULONG ComPort;
    ULONG BaudRate;
} KD_PORT_INFORMATION, *PKD_PORT_INFORMATION;

#ifdef KDDEBUG
ULONG KdpDbgPrint(const char *Format, ...);
#else
#define KdpDbgPrint(Format, ...)    ((ULONG)0)
#endif

#if 0
typedef enum
{
    KDP_PACKET_RECEIVED = 0,
    KDP_PACKET_TIMEOUT  = 1,
    KDP_PACKET_RESEND   = 2
} KDP_STATUS;

// In kddll.h
typedef ULONG KDSTATUS;
#define KdPacketReceived     0
#define KdPacketTimedOut     1
#define KdPacketNeedsResend  2
#endif

BOOLEAN
NTAPI
KdPortInitialize(
    _In_ PKD_PORT_INFORMATION PortInformation,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ BOOLEAN Initialize);

// ReactOS-specific
BOOLEAN
NTAPI
KdPortInitializeEx(
    PCPPORT PortInformation,
    ULONG ComPortNumber
);

ULONG
NTAPI
KdPortGetByte(
    _Out_ PUCHAR Byte);

ULONG
NTAPI
KdPortPollByte(
    _Out_ PUCHAR Byte);

VOID
NTAPI
KdPortPutByte(
    _In_ UCHAR Byte);

VOID
NTAPI
KdPortRestore(VOID);

VOID
NTAPI
KdPortSave(VOID);

#endif /* _KDPORT_ */
