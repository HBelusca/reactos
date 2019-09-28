/*
 * PROJECT:     FreeLoader - StartUp Module
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 *
 * PURPOSE:     Entry point code for the 32-bit portion of the StartUp Module.
 *              Also contains: the 32-bit part of the 16 <-> 32 bits
 *              trampoline code, as well as the 32-bit parts of the Int386
 *              mechanism, PXE and PnP support, and the Linux x86 loader.
 *
 * COPYRIGHT:   Copyright 1998-2002  Brian Palmer  <brianp@sginet.com>
 *              Copyright 2011-2019 Timo Kreuzer
 */

#include <asm.inc>
#include "../x86common.h"
#include <arch/pc/startup.h>

EXTERN _BootMain:PROC
EXTERN _InitIdt:PROC
EXTERN _i386Idt:DWORD
//EXTERN _i386idtptr:FWORD

EXTERN _DiskStopFloppyMotor:PROC

#ifdef _USE_ML
EXTERN __bss_start__:DWORD
EXTERN __bss_end__:DWORD
#endif

.code32

/*
 * 16 -> 32 bit re-entry point.
 */
PUBLIC _RealEntryPoint
_RealEntryPoint:

// Interrupts must be disabled at this point

    /* Setup segment selectors */
    mov ax, PMODE_DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Load the IDT */
#ifdef _USE_ML
    lidt fword ptr ds:[i386idtptr]
#else
    lidt i386idtptr
#endif

    /* Setup protected mode stack */
    mov esp, dword ptr ds:[stack32]

    /* Continue execution */
    jmp dword ptr ds:[ContinueAddress]

PUBLIC ContinueAddress
ContinueAddress:
    .long _FrldrStartup


/*
 * 32-bit entry point for the StartUp Module.
 */
_FrldrStartup:

    /* Store BootDrive and BootPartition */
    mov byte ptr ds:[_FrldrBootDrive], dl
    xor eax, eax
    mov al, dh
    mov dword ptr ds:[_FrldrBootPartition], eax

    /* Patch long jump with real mode entry point */
    mov eax, dword ptr ds:[BSS_RealModeEntry]
    mov dword ptr ds:[SwitchToReal16Address], eax

    /* Clean out BSS */
    xor eax, eax
    mov edi, offset __bss_start__
    mov ecx, offset __bss_end__ + 3
    sub ecx, edi
    shr ecx, 2
    rep stosd

    /* Initialize the idt */
    call _InitIdt

    /* Pass the command line to BootMain */
    mov eax, offset cmdline

    /* GO! */
    push eax
    call _BootMain

    /* We should never get here */
#if 0
stop:
    jmp short stop
#endif
    jmp _Reboot
    nop
    nop


/* Include multiboot support */
#include "../multiboot.inc"


/*
 * 32 -> 16 bit entry points for the services.
 */

PUBLIC _Reboot
_Reboot:
    /* Stop the floppy drive motor */
    call _DiskStopFloppyMotor

    /* Set the function ID and switch to real mode (we don't return) */
    mov bx, FNID_Reboot
    jmp SwitchToReal


/*
 * VOID __cdecl Relocator16Boot(
 *     IN REGS*  In,
 *     IN USHORT StackSegment,
 *     IN USHORT StackPointer,
 *     IN USHORT CodeSegment,
 *     IN USHORT CodePointer);
 *
 * RETURNS: Nothing.
 *
 * NOTE: The implementation of this function is similar to that of Int386(),
 * with the proviso that no attempt is done to save the original values of
 * the registers since we will not need them anyway, as we do not return back
 * to the caller but instead place the machine in a permanent new CPU state.
 */
PUBLIC _Relocator16Boot
_Relocator16Boot:

    /* Copy input registers */
    mov esi, dword ptr [esp + 4]
    mov edi, BSS_RegisterSet
    mov ecx, REGS_SIZE / 4
    rep movsd

    /* Set the stack segment/offset */
    // Since BSS_CallbackReturn contains a ULONG, store in its high word
    // the stack segment and in its low word the stack offset.
    mov ax, word ptr [esp + 8]
    shl eax, 16
    mov ax, word ptr [esp + 12]
    mov dword ptr ds:[BSS_CallbackReturn], eax

    /*
     * Set the code segment/offset (Copy entry point)
     * NOTE: We permanently *ERASE* the contents of ds:[BSS_RealModeEntry]
     * but it is not a problem since we are going to place the machine in
     * a permanent new CPU state.
     */
    // Since BSS_RealModeEntry contains a ULONG, store in its high word
    // the code segment and in its low word the code offset.
    mov ax, word ptr [esp + 16]
    shl eax, 16
    mov ax, word ptr [esp + 20]
    mov dword ptr ds:[BSS_RealModeEntry], eax

    /* Set the function ID and switch to real mode (we don't return) */
    mov bx, FNID_Relocator16Boot
    jmp SwitchToReal


/*
 * U16 PxeCallApi(U16 Segment, U16 Offset, U16 Service, VOID *Parameter);
 *
 * RETURNS:
 */
PUBLIC _PxeCallApi
_PxeCallApi:
    /* copy entry point */
    mov eax, [esp + 4]
    shl eax, 16
    mov ax, [esp + 8]
    mov dword ptr ds:[BSS_PxeEntryPoint], eax

    /* copy function */
    mov ax, [esp + 12]
    mov word ptr ds:[BSS_PxeFunction], ax

    /* convert pointer to data buffer to segment/offset */
    mov eax, [esp + 16]
    shr eax, 4
    and eax, HEX(0f000)
    mov word ptr ds:[BSS_PxeBufferSegment], ax
    mov eax, [esp + 16]
    and eax, HEX(0ffff)
    mov word ptr ds:[BSS_PxeBufferOffset], ax

    pusha

    /* Set the function ID and call realmode */
    mov bx, FNID_PxeCallApi
    call i386CallRealMode

    popa

    mov ax, word ptr [BSS_PxeResult]

    ret


PUBLIC i386CallRealMode
i386CallRealMode:
    /* Set continue address and switch to real mode */
    mov dword ptr ds:[ContinueAddress], offset i386CallRealMode_return
    jmp SwitchToReal
i386CallRealMode_return:
    ret


/* Entrypoint for realmode function calls
 * ContinueAddress must be set to the return point from realmode
 * bx must be set to the ID of the realmode function to call. */
PUBLIC SwitchToReal
SwitchToReal:
    /* Set sane segments */
    mov ax, PMODE_DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Save 32-bit stack pointer */
    mov dword ptr ds:[stack32], esp

    /* jmp to 16-bit segment to set the limit correctly */
    .byte HEX(0ea) // jmp far RMODE_CS:switch_to_real16
SwitchToReal16Address:
    .long 0 // receives address of switch_to_real16
    .word RMODE_CS
    nop


    /* 16-bit stack pointer */
stack16:
    .word STACK16ADDR

    /* 32-bit stack pointer */
// PUBLIC stack32
stack32:
    .long STACKADDR

    .align 4    /* force 4-byte alignment */
gdt:
    /* NULL Descriptor */
    .word HEX(0000)
    .word HEX(0000)
    .word HEX(0000)
    .word HEX(0000)

    /* 32-bit flat CS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9A00)
    .word HEX(00CF)

    /* 32-bit flat DS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9200)
    .word HEX(00CF)

    /* 16-bit real mode CS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9E00)
    .word HEX(0000)

    /* 16-bit real mode DS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9200)
    .word HEX(0000)

/* GDT table pointer */
gdtptr:
    .word HEX(27)       /* Limit */
    .long gdt           /* Base Address */

// See _i386IdtDescriptor
// PUBLIC i386idtptr
i386idtptr:
    .word 255           /* Limit */
    .long _i386Idt      /* Base Address */


//
// TODO: Move this to the DATA section
//

/*
 * Boot data structure passed to FreeLdr.
 * See "startup.h" for the complete definitions.
 */

/*
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
*/
    /* Align to 32 bits boundary */
    .align 4

// EXTERN _Reboot:PROC
EXTERN _Int386:PROC
// EXTERN _Relocator16Boot:PROC
// EXTERN _PxeCallApi:PROC
EXTERN _PnpBiosSupported:PROC
EXTERN _PnpBiosGetDeviceNodeCount:PROC
EXTERN _PnpBiosGetDeviceNode:PROC
ServicesTable:
    .long offset _Reboot
    .long offset _Int386
    .long offset _Relocator16Boot
    .long offset _PxeCallApi
    .long offset _PnpBiosSupported
    .long offset _PnpBiosGetDeviceNodeCount
    .long offset _PnpBiosGetDeviceNode
    .long 0

/*
typedef struct _BOOT_CONTEXT
{
    ULONG Signature;    // == BOOT_CONTEXT_SIGNATURE
    ULONG Size;         // == sizeof(BOOT_CONTEXT)

    ULONG Flags;

    ULONG BootDrive;        // UCHAR
    ULONG BootPartition;    // UCHAR

    ULONG MachineType;  // Either MACHINE_TYPE_ISA, MACHINE_TYPE_EISA or MACHINE_TYPE_MCA.

    PVOID ImageBase;
    ULONG ImageSize;
    ULONG ImageType;

    PSERVICES_TABLE ServicesTable;
    CHAR CommandLine[CMDLINE_SIZE];
} BOOT_CONTEXT, *PBOOT_CONTEXT;
*/
    /* Align to 32 bits boundary */
    .align 4

PUBLIC _BootData
_BootData:
    /* Signature */
    .long BOOT_CONTEXT_SIGNATURE
    /* Size */
    .long BootDataEnd - _BootData
    /* Flags */
    .long 0

PUBLIC _FrldrBootDrive
_FrldrBootDrive:
    .byte 0
    .align 4

PUBLIC _FrldrBootPartition
_FrldrBootPartition:
    .long 0

    /* MachineType */
    .long 0
    /* ImageBase */
    .long 0
    /* ImageSize */
    .long 0
    /* ImageType */
    .long 0

    /* ServicesTable */
    .long offset ServicesTable

PUBLIC cmdline
cmdline:
    .space CMDLINE_SIZE
BootDataEnd:

END
