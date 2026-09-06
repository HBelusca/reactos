/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     I/O Stub HAL Routines for the MINIHAL library
 * COPYRIGHT:   Copyright 2010 Hervé Poussineau <hpoussin@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"

/* FUNCTIONS *****************************************************************/

NTSTATUS
FASTCALL
xHalIoReadPartitionTable(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN BOOLEAN ReturnRecognizedPartitions,
    OUT PDRIVE_LAYOUT_INFORMATION *PartitionBuffer)
{
    return IoReadPartitionTable(DeviceObject,
                                SectorSize,
                                ReturnRecognizedPartitions,
                                PartitionBuffer);
}

UCHAR
NTAPI
xHalVectorToIDTEntry(IN ULONG Vector)
{
    /* Return the vector */
    return (UCHAR)Vector;
}

VOID
NTAPI
xHalHaltSystem(VOID)
{
    /* Halt execution */
    while (TRUE);
}

/* GLOBALS *******************************************************************/

HAL_DISPATCH HalDispatchTable =
{
    HAL_DISPATCH_VERSION,
    (pHalQuerySystemInformation)NULL,
    (pHalSetSystemInformation)NULL,
    (pHalQueryBusSlots)NULL,
    0,
    (pHalExamineMBR)NULL,
    (pHalIoAssignDriveLetters)NULL,
    (pHalIoReadPartitionTable)xHalIoReadPartitionTable,
    (pHalIoSetPartitionInformation)NULL,
    (pHalIoWritePartitionTable)NULL,
    (pHalHandlerForBus)NULL,
    (pHalReferenceBusHandler)NULL,
    (pHalReferenceBusHandler)NULL,
    (pHalInitPnpDriver)NULL,
    (pHalInitPowerManagement)NULL,
    (pHalGetDmaAdapter)NULL,
    (pHalGetInterruptTranslator)NULL,
    (pHalStartMirroring)NULL,
    (pHalEndMirroring)NULL,
    (pHalMirrorPhysicalMemory)NULL,
    (pHalEndOfBoot)NULL,
    (pHalMirrorVerify)NULL,
    (pHalGetAcpiTable)NULL,
    (pHalSetPciErrorHandlerCallback)NULL
};

HAL_PRIVATE_DISPATCH HalPrivateDispatchTable =
{
    HAL_PRIVATE_DISPATCH_VERSION,
    (pHalHandlerForBus)NULL,
    (pHalHandlerForConfigSpace)NULL,
    (pHalLocateHiberRanges)NULL,
    (pHalRegisterBusHandler)NULL,
    (pHalSetWakeEnable)NULL,
    (pHalSetWakeAlarm)NULL,
    (pHalTranslateBusAddress)NULL,
    (pHalAssignSlotResources)NULL,
    (pHalHaltSystem)xHalHaltSystem,
    (pHalFindBusAddressTranslation)NULL,
    (pHalResetDisplay)NULL,
    (pHalAllocateMapRegisters)NULL,
    (pKdSetupPciDeviceForDebugging)NULL,
    (pKdReleasePciDeviceForDebugging)NULL,
    (pKdGetAcpiTablePhase0)NULL,
    (pKdCheckPowerButton)NULL,
    (pHalVectorToIDTEntry)xHalVectorToIDTEntry,
    (pKdMapPhysicalMemory64)NULL,
    (pKdUnmapVirtualAddress)NULL
};
