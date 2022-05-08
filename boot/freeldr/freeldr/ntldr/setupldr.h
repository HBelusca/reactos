/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Setup Loader.
 * COPYRIGHT:   Copyright 2022 Hermès Bélusca-Maïto
 */

#pragma once

// #include <arc/setupblk.h>

BOOLEAN
SetupLdrGetNLSNames(
    _In_ PNT_CONFIG_SOURCES ConfigSources,
    _Out_ PUNICODE_STRING AnsiFileName,
    _Out_ PUNICODE_STRING OemFileName,
    _Out_ PUNICODE_STRING LangFileName, // CaseTable
    _Out_ PUNICODE_STRING OemHalFileName);

BOOLEAN
SetupLdrGetErrataInf(
    _In_ USHORT OperatingSystemVersion,
    _In_ PNT_CONFIG_SOURCES ConfigSources,
    _In_ PCSTR SystemRoot,
    _Out_z_bytecap_(FilePathSize)
         PSTR FilePath,
    _In_ SIZE_T FilePathSize);

BOOLEAN
SetupLdrScanBootDrivers(
    _In_ PNT_CONFIG_SOURCES ConfigSources,
    _Inout_ PLIST_ENTRY DriverListHead);

VOID
SetupLdrPostProcessBootOptions(
    _Out_z_bytecap_(BootOptionsSize)
         PSTR BootOptions,
    _In_ SIZE_T BootOptionsSize,
    _In_ PCSTR ArgsBootOptions,
    _In_ PNT_CONFIG_SOURCES ConfigSources);

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
