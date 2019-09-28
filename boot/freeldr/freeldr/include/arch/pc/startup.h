/*
 * Interface with the StartUp module.
 */

#ifndef _STARTUP_H_
#define _STARTUP_H_

#ifndef __ASM__
#pragma once
#endif

#ifndef HEX
#define HEX(y) 0x##y
#endif

#define BOOT_CONTEXT_SIGNATURE  'RDLF' // HEX(52444C46) == "FLDR"
#define CMDLINE_SIZE            256

#ifndef __ASM__

typedef VOID (__cdecl* SU_REBOOT)(VOID);

struct _REGS;
typedef INT  (__cdecl* SU_INT386)(
    IN  INT IVec,
    IN  /* REGS* */struct _REGS* In,
    OUT /* REGS* */struct _REGS* Out);

typedef VOID (__cdecl* SU_RELOCATOR16_BOOT)(
    IN REGS*  In,
    IN USHORT StackSegment,
    IN USHORT StackPointer,
    IN USHORT CodeSegment,
    IN USHORT CodePointer);

typedef USHORT (__cdecl* SU_PXE_CALL_API)(
    IN USHORT Segment,
    IN USHORT Offset,
    IN USHORT Service,
    IN PVOID  Parameter);

typedef ULONG_PTR (__cdecl* SU_PNP_SUPPORTED)(VOID);

typedef ULONG (__cdecl* SU_PNP_GET_DEVICE_NODE_COUNT)(
    OUT PULONG NodeSize,
    OUT PULONG NodeCount);

typedef ULONG (__cdecl* SU_PNP_GET_DEVICE_NODE)(
    IN OUT PUCHAR NodeId,
    OUT PUCHAR NodeBuffer);

typedef ULONG_PTR (__cdecl* SU_SERVICE_CALL)(
    IN ULONG ServiceNumber,
    IN PVOID Argument1,
    IN PVOID Argument2,
    IN PVOID Argument3,
    IN PVOID Argument4);

typedef struct _SERVICES_TABLE
{
    SU_REBOOT Reboot;
    SU_INT386 Int386;
    SU_RELOCATOR16_BOOT Relocator16Boot;
    SU_PXE_CALL_API PxeCallApi;
    SU_PNP_SUPPORTED PnpBiosSupported;
    SU_PNP_GET_DEVICE_NODE_COUNT PnpBiosGetDeviceNodeCount;
    SU_PNP_GET_DEVICE_NODE PnpBiosGetDeviceNode;
    SU_SERVICE_CALL ServiceCall;
} SERVICES_TABLE, *PSERVICES_TABLE;

// NOTE: We reuse the public name of the structure, but its contents completely differs!
typedef struct _BOOT_CONTEXT
{
    ULONG Signature;    // == BOOT_CONTEXT_SIGNATURE
    ULONG Size;         // == sizeof(BOOT_CONTEXT)

    ULONG Flags;

    ULONG BootDrive;
    ULONG BootPartition;

    ULONG MachineType;  // Either MACHINE_TYPE_ISA, MACHINE_TYPE_EISA or MACHINE_TYPE_MCA.

    PVOID ImageBase;
    ULONG ImageSize;
    ULONG ImageType;

    /* Buffer to store temporary data for any Int386() call */
    PVOID BiosCallBuffer;
    ULONG BiosCallBufferSize;

    PSERVICES_TABLE ServicesTable;
    CHAR CommandLine[CMDLINE_SIZE];
} BOOT_CONTEXT, *PBOOT_CONTEXT;

#define IS_BOOT_CONTEXT_VALID(BootContextPtr)   \
    (((BootContextPtr)->Signature == BOOT_CONTEXT_SIGNATURE) && \
     ((BootContextPtr)->Size == sizeof(BOOT_CONTEXT)))

typedef VOID (NTAPI* BOOTMGR_ENTRY_POINT)(IN PBOOT_CONTEXT BootContext);

#endif // !__ASM__

#endif // _STARTUP_H_
