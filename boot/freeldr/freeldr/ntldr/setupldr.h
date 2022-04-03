/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Setup Loader.
 * COPYRIGHT:   Copyright 2022 Hermès Bélusca-Maïto
 */

#pragma once

// #include <arc/setupblk.h>

/*static*/ VOID
SetupLdrLoadNlsData(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ HINF InfHandle,
    _In_ PCSTR SearchPath);

/*static*/
BOOLEAN
SetupLdrInitErrataInf(
    IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN HINF InfHandle,
    IN PCSTR SystemRoot);

/*static*/ VOID
SetupLdrScanBootDrivers(
    _Inout_ PLIST_ENTRY BootDriverListHead,
    _In_ HINF InfHandle,
    _In_ PCSTR SearchPath);

VOID
SetupLdrPostProcessBootOptions(
    _Out_z_bytecap_(BootOptionsSize)
         PSTR BootOptions,
    _In_ SIZE_T BootOptionsSize,
    _In_ PCSTR ArgsBootOptions,
    // _In_ ULONG Argc,
    // _In_ PCHAR Argv[],
    _In_ HINF InfHandle);

ARC_STATUS
SetupLdrFindConfigSource(
    _Out_ PHINF InfHandle,
    _Inout_z_bytecap_(BootPathSize)
         PSTR BootPath,
    _In_ SIZE_T BootPathSize,
    _Out_z_bytecap_(FilePathSize)
         PSTR FilePath,
    _In_ SIZE_T FilePathSize);

/* EOF */
