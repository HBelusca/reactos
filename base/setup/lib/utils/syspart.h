/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Firmware System Partition helpers
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

#if 0
// See ex/sysinfo.c
/* The maximum size of an environment value (in bytes) */
#define MAX_ENVVAL_SIZE 1024
#endif

/* FUNCTIONS ****************************************************************/

#if 0

//static
PDISKENTRY
GetSystemDisk(
    _In_ PPARTLIST List);

//static
PPARTENTRY
GetActiveDiskPartition(
    _In_ PDISKENTRY DiskEntry);

#endif


NTSTATUS
SpArcGetEnvironmentValues(
    _In_ PCWSTR VariableName,
    _Out_writes_(BufferLength) PZZWSTR ValuesBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength);


inline
BOOLEAN
MBRIsSystemPartition(
    _In_ PPARTITION_INFORMATION PartitionInfo);

inline
BOOLEAN
GPTIsSystemPartition(
    _In_ PPARTITION_INFORMATION PartitionInfo);


VOID
MBRSetSystemPartition(
    _In_ PPARTENTRY PartEntry,
    _In_ BOOLEAN SetSystem);

VOID
GPTSetSystemPartition(
    _In_ PPARTENTRY PartEntry,
    _In_ BOOLEAN SetSystem);


#if 0
VOID
PtBiosSetSystemPartition(
    _In_ PPARTENTRY PartEntry);

VOID
PtEfiSetSystemPartition(
    _In_ PPARTENTRY PartEntry);

VOID
PtArcSetSystemPartition(
    _In_ PPARTENTRY PartEntry);
#endif

VOID
PtSetSystemPartition(
    _In_ PPARTENTRY PartEntry);


#if 0
NTSTATUS
PtBiosMarkSystemPartitions(
    _In_ PPARTLIST PartList);

NTSTATUS
PtEfiMarkSystemPartitions(
    _In_ PPARTLIST PartList);

NTSTATUS
PtArcMarkSystemPartitions(
    _In_ PPARTLIST PartList);
#endif

NTSTATUS
PtMarkSystemPartitions(
    _In_ PPARTLIST PartList);

/* EOF */
