/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ARC path to-and-from NT path resolver.
 * COPYRIGHT:   Copyright 2017-2018 Hermes Belusca-Maito
 */

#pragma once

/* Supported adapter types */
typedef enum _ADAPTER_TYPE
{
    EisaAdapter,
    ScsiAdapter,
    MultiAdapter,
    NetAdapter,
    RamdiskAdapter,
    AdapterTypeMax
} ADAPTER_TYPE, *PADAPTER_TYPE;

/* Supported controller types */
typedef enum _CONTROLLER_TYPE
{
    DiskController,
    CdRomController,
    ControllerTypeMax
} CONTROLLER_TYPE, *PCONTROLLER_TYPE;

/* Supported peripheral types */
typedef enum _PERIPHERAL_TYPE
{
//  VDiskPeripheral,
    RDiskPeripheral,
    FDiskPeripheral,
    CdRomPeripheral,
    PeripheralTypeMax
} PERIPHERAL_TYPE, *PPERIPHERAL_TYPE;

BOOLEAN
ArcPathNormalize(
    OUT PUNICODE_STRING NormalizedArcPath,
    IN  PCWSTR ArcPath);

NTSTATUS
ParseArcName(
    IN OUT PCWSTR* ArcNamePath,
    OUT PULONG pAdapterKey,
    OUT PULONG pControllerKey,
    OUT PULONG pPeripheralKey,
    OUT PULONG pPartitionNumber,
    OUT PADAPTER_TYPE pAdapterType,
    OUT PCONTROLLER_TYPE pControllerType,
    OUT PPERIPHERAL_TYPE pPeripheralType,
    OUT PBOOLEAN pUseSignature);

BOOLEAN
ArcPathToNtPath(
    OUT PUNICODE_STRING NtPath,
    IN  PCWSTR ArcPath,
    IN  PPARTLIST PartList OPTIONAL);

/* EOF */
