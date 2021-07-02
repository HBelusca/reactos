/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Assembly file for loading kernel values into AP
 * COPYRIGHT:   Copyright 2021 Justin Miller <justinmiller100@gmail.com>
 */

#include <asm.inc>
#include <ks386.inc>

PUBLIC _APFinal
PUBLIC _APFinalEnd
PUBLIC _APProcessorStateStruct
PUBLIC _TempPageTableLoc

.code32
_APFinal:
    cli
    mov eax, HEX(23)
    mov ds, eax
    mov eax, HEX(23)
    mov es, eax
    mov eax, HEX(30)
    mov fs, eax
    mov eax, HEX(00)
    mov gs, eax
    mov eax, HEX(10)
    mov ss, eax

	mov eax, ([ebx] + [_TempPageTableLoc - _APFinal])
    mov cr3, eax
    hlt
    mov eax, cr0
    or  eax, CR0_PG
    mov cr0, eax
    
    //TODO in morning, figure out why processorstate doesn't write correctly in high stub
    
    hlt
    /* Load segment registers from ProcessorState values */
    mov eax, ([ebx] + [APSegSs - _APFinal])
    mov ss, eax
    mov eax, ([ebx] + [APSegDs - _APFinal])
    mov ds, eax
    mov eax, ([ebx] + [APSegEs - _APFinal])
    mov es, eax
    mov eax, ([ebx] + [APSegGs - _APFinal])
    mov gs, eax
    mov eax, ([ebx] + [APSegFs - _APFinal])
    mov fs, eax

    /* Write CR registers with ProcessorState values */
    xor eax, eax
    mov eax, ([ebx] + [APCr2 - _APFinal])
    mov cr2, eax
    mov eax, ([ebx] + [APCr3 - _APFinal])
    mov cr3, eax
    mov eax, ([ebx] + [APCr4 - _APFinal])
    mov cr4, eax

    /* Load AP Stack*/
    mov ecx, ([ebx] + [APEsp - _APFinal])
    mov esp, ecx

    /* Enable Paging */
    mov eax, cr0
    or  eax, CR0_PG
    mov cr0, eax

    /* Load LoaderBlock and the location of KiSystemStartup */
    xor eax, eax
    xor ecx, ecx
    mov eax, ([ebx] + [APEip - _APFinal])
    mov ecx, ([ebx] + [APEcx - _APFinal])

    /* Push Loaderblock onto the stack as paramter */
    //TODO: Ask about this... It's inside ECX at this stage

    /* make the jump to KiSystemStartup */
    //TODO: Ask about this... It's inside EIP at this stage

_TempPageTableLoc:
    .long HEX(0)
/* ProcessorState "Struct" */
_APProcessorStateStruct:
    APCr0:
        .long HEX(0)
    APCr2:
        .long HEX(0)
    APCr3:
        .long HEX(0) 
    APCr4:
        .long HEX(0)   
    APSegCs:
        .long HEX(0)
    APSegSs:
        .long HEX(0)
    APSegDs:
        .long HEX(0)
    APSegEs:
        .long HEX(0)
    APSegGs:
        .long HEX(0)
    APSegFs:
        .long HEX(0)
    APTr:
        .long HEX(0)
    APEip:
        .long HEX(0)
    APEsp:
        .long HEX(0)
    APEcx:
        .long HEX(0)

_APFinalEnd:
END
