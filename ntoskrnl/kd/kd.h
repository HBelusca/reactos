
#ifndef _KDTERM_H_
#define _KDTERM_H_

#pragma once

#ifndef _NTOSKRNL_

#include <stdio.h>
#include <stdlib.h>

/* KD Support */
#define NOEXTAPI
#include <ntifs.h>
#include <windbgkd.h>

#include <arc/arc.h>
#include <kddll.h>

#include <cportlib/cportlib.h>
#include <drivers/bootvid/bootvid.h>
#include <ndk/halfuncs.h>
//#include <ndk/haltypes.h>
#include <ndk/inbvfuncs.h>
#include <ndk/iofuncs.h>

#else

#include <ntoskrnl.h>

#undef KdDebuggerInitialize0
#undef KdDebuggerInitialize1
#undef KdD0Transition
#undef KdD3Transition
#undef KdSave
#undef KdRestore
#undef KdSendPacket
#undef KdReceivePacket

#endif /* _NTOSKRNL_ */

#include <cportlib/cportlib.h>

//
// Kernel Debugger Port Definition
//

BOOLEAN
NTAPI
KdPortInitializeEx(
    PCPPORT PortInformation,
    ULONG ComPortNumber
);

BOOLEAN
NTAPI
KdPortGetByteEx(
    PCPPORT PortInformation,
    PUCHAR ByteReceived);

VOID
NTAPI
KdPortPutByteEx(
    PCPPORT PortInformation,
    UCHAR ByteToSend
);


/* KD IO ROUTINES ************************************************************/

struct _KD_DISPATCH_TABLE;

typedef
NTSTATUS
(NTAPI *PKDP_INIT_ROUTINE)(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

typedef
VOID
(NTAPI *PKDP_PRINT_ROUTINE)(
    _In_ PCCH String,
    _In_ ULONG Length);

VOID
KdIoPuts(
    _In_ PCSTR String);

VOID
__cdecl
KdIoPrintf(
    _In_ PCSTR Format,
    ...);

SIZE_T
KdIoReadLine(
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size);


/* KD TERMINAL ROUTINES ******************************************************/

BOOLEAN
KdpTermInit(VOID);

VOID
NTAPI
KdpTermSetState(
    _In_ BOOLEAN Enable);

VOID
NTAPI
KdpTermFlushInput(VOID);

CHAR
NTAPI
KdpTermReadKey(
    _Out_ PULONG ScanCode);


/* INIT ROUTINES *************************************************************/

KIRQL
NTAPI
KdbpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock);

VOID
NTAPI
KdbpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql);

VOID
KdpScreenAcquire(VOID);

VOID
KdpScreenRelease(VOID);


NTSTATUS
NTAPI
KdpScreenInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

NTSTATUS
NTAPI
KdpSerialInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

NTSTATUS
NTAPI
KdpDebugLogInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);


/* KD GLOBALS ****************************************************************/

/* Serial debug connection */
#define DEFAULT_DEBUG_PORT      2 /* COM2 */
#define DEFAULT_DEBUG_COM1_IRQ  4 /* COM1 IRQ */
#define DEFAULT_DEBUG_COM2_IRQ  3 /* COM2 IRQ */
#define DEFAULT_DEBUG_BAUD_RATE 115200 /* 115200 Baud */

/* KD Native Modes */
#define KdScreen    0
#define KdSerial    1
#define KdFile      2
#define KdMax       3

/* KD Private Debug Modes */
typedef struct _KDP_DEBUG_MODE
{
    union
    {
        struct
        {
            /* Native Modes */
            UCHAR Screen :1;
            UCHAR Serial :1;
            UCHAR File   :1;
        };

        /* Generic Value */
        ULONG Value;
    };
} KDP_DEBUG_MODE;

/* Dispatch Table for Wrapper Functions */
typedef struct _KD_DISPATCH_TABLE
{
    PKDP_INIT_ROUTINE KdpInitRoutine;
    PKDP_PRINT_ROUTINE KdpPrintRoutine;
    NTSTATUS InitStatus;
} KD_DISPATCH_TABLE, *PKD_DISPATCH_TABLE;

/* The current Debugging Mode */
extern KDP_DEBUG_MODE KdpDebugMode;

/* Port Information for the Serial Native Mode */
extern ULONG  SerialPortNumber;
extern CPPORT SerialPortInfo;

/* Logging file path */
extern ANSI_STRING KdpLogFileName;

/* Dispatch Tables for Native Providers */
extern KD_DISPATCH_TABLE DispatchTable[KdMax];

/* Support for function pointers thorough runtime relocation */
extern char __ImageBase;
#define RVA_TO_VA(Base, Offset) ((PVOID)((ULONG_PTR)(Base) + (ULONG_PTR)(Offset)))
#define VA_TO_RVA(Base, Ptr)    ((PVOID)((ULONG_PTR)(Ptr)  - (ULONG_PTR)(Base)))
#define REL_TO_ADDR(Offset)     RVA_TO_VA(&__ImageBase, Offset)
#define ADDR_TO_REL(Addr)       VA_TO_RVA(&__ImageBase, Addr)

#endif /* _KDTERM_H_ */
