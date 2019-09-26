/*
 * PROJECT:     FreeLoader - StartUp Module
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 *
 * PURPOSE:     Entry point code for the 16-bit StartUp Module.
 *              Also contains: the FAT12 second-part stub, the 16 <-> 32 bits
 *              trampoline code, as well as the 16-bit parts of the Int386
 *              mechanism, PXE and PnP support, and the Linux x86 loader.
 *
 * COPYRIGHT:   Copyright 2011-2019 Timo Kreuzer
 */

#include <asm.inc>
#include "../../include/arch/pc/x86common.h"

.code16

/* FAT helper code */
#include "fathelp.inc"

.org 512
RealModeEntryPoint:

    cli

    /* Setup segment registers */
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Setup the stack */
    mov sp, word ptr ds:[stack16]

    /* Enable A20 address line */
    call EnableA20

    /* Save real mode entry point in shared memory */
    mov dword ptr ds:[BSS_RealModeEntry], offset switch_to_real16

    /*
     * The remaining part of the StartUp module is the 32-bit code that
     * is appended (and which entry-point is) just at FREELDR_PE_BASE.
     *
     * NOTE: It serves as the 16 -> 32 bit re-entry point.
     *
     * NOTE 2: I think this may be simplified since now we know that
     * the position of our 16 -> 32 bit entry point is fixed
     */
    /* Save the entry point to protected mode */
    mov dword ptr ds:[pm_entrypoint], FREELDR_PE_BASE

    jmp exit_to_protected


/* This is the entry point from protected mode */
switch_to_real16:

    /* Restore segment registers to correct limit */
    mov ax, RMODE_DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Disable Protected Mode */
    mov eax, cr0
    and eax, CR0_PE_CLR
    mov cr0, eax

    /* Clear prefetch queue & correct CS */
    ljmp16 0, inrmode

inrmode:
    /* Set real mode segments */
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Clear out the high 16-bits of ESP */
    /* This is needed because I have one */
    /* machine that hangs when booted to dos if */
    /* anything other than 0x0000 is in the high */
    /* 16-bits of ESP. Even though real-mode */
    /* code should only use SP and not ESP. */
    xor esp, esp

    /* Restore real mode stack */
    mov sp, word ptr ds:[stack16]

    /* Load IDTR with real mode value */
#ifdef _USE_ML
    lidt fword ptr rmode_idtptr
#else
    lidt rmode_idtptr
#endif

    sti /* These are ok now */

    /* Do the callback, specified by bx */
    shl bx, 1
    call word ptr ds:CallbackTable[bx]


/*
 * Switches the processor to protected mode
 * it destroys eax
 */
exit_to_protected:

    cli

    /* Save current stack pointer */
    mov word ptr ds:[stack16], sp

    /* Load the GDT */
#ifdef _USE_ML
    lgdt fword ptr gdtptr
#else
    lgdt gdtptr
#endif

    /* Enable Protected Mode */
    mov eax, cr0
    or eax, CR0_PE_SET
    mov cr0, eax

    /* Clear prefetch queue & correct CS */
    ljmp16 PMODE_CS, inpmode
inpmode:
    .byte HEX(0ff), HEX(25) // opcode of indirect jump
    .word pm_entrypoint, 0
pm_entrypoint:
    .long 0 // receives address of PE entry point -- Now it is the fixed address of the 32-bit portion of StartUp.
    nop

CallbackTable:
    .word Int386
    .word Reboot
    .word Relocator16Boot
    .word PxeCallApi
    .word PnpBiosGetDeviceNodeCount
    .word PnpBiosGetDeviceNode


    /* 16-bit stack pointer */
stack16:
    .word    STACK16ADDR


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
    .word gdt, 0        /* Base Address */

/* Real-mode IDT pointer */
rmode_idtptr:
    .word HEX(3FF)      /* Limit */
    .long 0             /* Base Address */

#include "int386.inc"
#if defined(SARCH_PC98)
#include "helpers_pc98.inc"
#else
#include "helpers.inc"
#endif
#include "pxe.inc"
#include "pnp.inc"

/*
 * The 32-bit portion (and its entry-point) of the StartUp module
 * is appended just after.
 */
.org (FREELDR_PE_BASE - FREELDR_BASE - 1)
.byte 0
.endcode16

END
