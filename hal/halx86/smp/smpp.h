/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private Header File for SMP
 * COPYRIGHT:   Copyright 2021 Justin Miller <justinmiller100@gmail.com>
 */

VOID
HalpInitializeAPStub(PVOID APStubLocation, 
                     ULONG32 GdtBase, UINT16 GdtLimit,
                     ULONG32 IdtBase, UINT16 IdtLimit);

VOID
HalpWriteProcessorState(PVOID APStubLocation,
                        PKPROCESSOR_STATE ProcessorState,
                        UINT32 LoaderBlock);

VOID
HalpWriteTempPageTable(PVOID APStubLocation, 
                       PVOID PageTableLocationPhysical, 
                       PVOID PageTableLocationBase);
