/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     File support functions.
 * COPYRIGHT:   Eric Kohl
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

#pragma once

NTSTATUS
SetupDeleteFile(
    IN PCWSTR FileName,
    IN BOOLEAN ForceDelete); // ForceDelete can be used to delete read-only files

NTSTATUS
SetupCopyFile(
    IN PCWSTR SourceFileName,
    IN PCWSTR DestinationFileName,
    IN BOOLEAN FailIfExists);

#ifndef _WINBASE_

#define MOVEFILE_REPLACE_EXISTING   1
#define MOVEFILE_COPY_ALLOWED       2
#define MOVEFILE_WRITE_THROUGH      8

#endif

NTSTATUS
SetupMoveFile(
    IN PCWSTR ExistingFileName,
    IN PCWSTR NewFileName,
    IN ULONG Flags);

NTSTATUS
ConcatPathsV(
    IN OUT PWSTR PathBuffer,
    IN SIZE_T cchPathSize,
    IN ULONG NumberOfPathComponents,
    IN va_list PathComponentsList);

NTSTATUS
CombinePathsV(
    OUT PWSTR PathBuffer,
    IN SIZE_T cchPathSize,
    IN ULONG NumberOfPathComponents,
    IN va_list PathComponentsList);

NTSTATUS
ConcatPaths(
    IN OUT PWSTR PathBuffer,
    IN SIZE_T cchPathSize,
    IN ULONG NumberOfPathComponents,
    IN /* PCWSTR */ ...);

NTSTATUS
CombinePaths(
    OUT PWSTR PathBuffer,
    IN SIZE_T cchPathSize,
    IN ULONG NumberOfPathComponents,
    IN /* PCWSTR */ ...);

BOOLEAN
DoesPathExistEx(
    IN HANDLE RootDirectory OPTIONAL,
    IN PCWSTR PathName,
    IN BOOLEAN IsDirectory);

#define DoesPathExist(RootDirectory, PathName)  \
    DoesPathExistEx((RootDirectory), (PathName), TRUE)

#define DoesFileExist(RootDirectory, FileName)  \
    DoesPathExistEx((RootDirectory), (FileName), FALSE)

// FIXME: DEPRECATED! HACKish function that needs to be deprecated!
BOOLEAN
DoesFileExist_2(
    IN PCWSTR PathName OPTIONAL,
    IN PCWSTR FileName);

BOOLEAN
NtPathToDiskPartComponents(
    IN PCWSTR NtPath,
    OUT PULONG pDiskNumber,
    OUT PULONG pPartNumber,
    OUT PCWSTR* PathComponent OPTIONAL);

NTSTATUS
OpenAndMapFile(
    IN  HANDLE RootDirectory OPTIONAL,
    IN  PCWSTR PathNameToFile,
    OUT PHANDLE FileHandle,         // IN OUT PHANDLE OPTIONAL
    OUT PHANDLE SectionHandle,
    OUT PVOID* BaseAddress,
    OUT PULONG FileSize OPTIONAL,
    IN  BOOLEAN ReadWriteAccess);

BOOLEAN
UnMapFile(
    IN HANDLE SectionHandle,
    IN PVOID BaseAddress);

/* EOF */
