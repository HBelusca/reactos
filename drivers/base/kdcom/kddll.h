/*
 * COPYRIGHT:       GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            drivers/base/kddll/kddll.h
 * PURPOSE:         Base definitions for the kernel debugger.
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#ifndef _KDDLL_H_
#define _KDDLL_H_

#define NOEXTAPI
#include <ntifs.h>
#include <windbgkd.h>

// #define KDDEBUG /* uncomment to enable debugging this dll */

#ifndef KDDEBUG
#define KDDBGPRINT(...)
#else
extern ULONG KdpDbgPrint(const char* Format, ...);
#define KDDBGPRINT KdpDbgPrint
#endif

VOID
NTAPI
KdpSendBuffer(
    IN PVOID Buffer,
    IN ULONG Size);

KDSTATUS
NTAPI
KdpReceiveBuffer(
    OUT PVOID Buffer,
    IN  ULONG Size);

KDSTATUS
NTAPI
KdpReceivePacketLeader(
    OUT PULONG PacketLeader);

VOID
NTAPI
KdpSendByte(IN UCHAR Byte);

KDSTATUS
NTAPI
KdpPollByte(OUT PUCHAR OutByte);

KDSTATUS
NTAPI
KdpReceiveByte(OUT PUCHAR OutByte);

KDSTATUS
NTAPI
KdpPollBreakIn(VOID);

#endif /* _KDDLL_H_ */
