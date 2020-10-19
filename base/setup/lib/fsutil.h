/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Filesystem Format and ChkDsk support functions.
 * COPYRIGHT:   Copyright 2003-2019 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2017-2020 Hermes Belusca-Maito
 */

#pragma once

#include <fmifs/fmifs.h>

/** QueryAvailableFileSystemFormat() **/
BOOLEAN
GetRegisteredFileSystems(
    IN ULONG Index,
    OUT PCWSTR* FileSystemName);


/** ChkdskEx() **/
NTSTATUS
ChkdskFileSystem_UStr(
    IN PUNICODE_STRING DriveRoot,
    IN PCWSTR FileSystemName,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PFMIFSCALLBACK Callback);

NTSTATUS
ChkdskFileSystem(
    IN PCWSTR DriveRoot,
    IN PCWSTR FileSystemName,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PFMIFSCALLBACK Callback);


/** FormatEx() **/
NTSTATUS
FormatFileSystem_UStr(
    IN PUNICODE_STRING DriveRoot,
    IN PCWSTR FileSystemName,
    IN FMIFS_MEDIA_FLAG MediaFlag,
    IN PUNICODE_STRING Label,
    IN BOOLEAN QuickFormat,
    IN ULONG ClusterSize,
    IN PFMIFSCALLBACK Callback);

NTSTATUS
FormatFileSystem(
    IN PCWSTR DriveRoot,
    IN PCWSTR FileSystemName,
    IN FMIFS_MEDIA_FLAG MediaFlag,
    IN PCWSTR Label,
    IN BOOLEAN QuickFormat,
    IN ULONG ClusterSize,
    IN PFMIFSCALLBACK Callback);


//
// Bootsector routines
//

#define FAT_BOOTSECTOR_SIZE     (1 * SECTORSIZE)
#define FAT32_BOOTSECTOR_SIZE   (1 * SECTORSIZE) // Counts only the primary sector.
#define BTRFS_BOOTSECTOR_SIZE   (3 * SECTORSIZE)

typedef NTSTATUS
(/*NTAPI*/ *PFS_INSTALL_BOOTCODE)(
    IN PCWSTR SrcPath,          // Bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition);   // Partition holding the (old) bootsector data information

NTSTATUS
InstallFatBootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE RootPartition);

#define InstallFat12BootCode    InstallFatBootCode
#define InstallFat16BootCode    InstallFatBootCode

NTSTATUS
InstallFat32BootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE RootPartition);

NTSTATUS
InstallBtrfsBootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE RootPartition);


//
// Formatting routines
//

NTSTATUS // ERROR_NUMBER
FormatPartition(
    IN PPARTENTRY PartEntry,
    IN PCWSTR FileSystemName,
    IN FMIFS_MEDIA_FLAG MediaFlag,
    IN PCWSTR Label,
    IN BOOLEAN QuickFormat,
    IN ULONG ClusterSize,
    IN PFMIFSCALLBACK Callback);

NTSTATUS // ERROR_NUMBER
// ChkdskPartition
CheckPartition(
    IN PPARTENTRY PartEntry,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PFMIFSCALLBACK Callback);


//
// FileSystem Volume Operations Queue
//

// Like the SPFILENOTIFY_xxxx values
typedef enum _FSVOLNOTIFY
{
    FSVOLNOTIFY_STARTQUEUE = 0,
    FSVOLNOTIFY_ENDQUEUE,
    FSVOLNOTIFY_STARTSUBQUEUE,
    FSVOLNOTIFY_ENDSUBQUEUE,
    FSVOLNOTIFY_PREPAREFORMAT,
    FSVOLNOTIFY_STARTFORMAT,
    FSVOLNOTIFY_ENDFORMAT,
    ChangeSystemPartition, // FIXME: Deprecate!
    SystemPartitionError,  // FIXME: Deprecate!
    FSVOLNOTIFY_FORMATERROR,
    // FSVOLNOTIFY_PREPARECHECK,
    FSVOLNOTIFY_STARTCHECK,
    FSVOLNOTIFY_ENDCHECK,
    FSVOLNOTIFY_CHECKERROR
} FSVOLNOTIFY;

// Like the FILEOP_xxxx values
typedef enum _FSVOL_OP
{
/* Operations ****/
    FSVOL_FORMAT = 0,
    FSVOL_CHECK,
/* Response actions ****/
    FSVOL_ABORT = 0,
    FSVOL_DOIT,
    FSVOL_RETRY = FSVOL_DOIT,
    FSVOL_SKIP,
} FSVOL_OP;

#define ERROR_SYSTEM_PARTITION_NOT_FOUND    (ERROR_LAST_ERROR_CODE + 1)

typedef struct _FORMAT_PARTITION_INFO
{
    PPARTENTRY PartEntry;
    // PCWSTR NtPathPartition;
    NTSTATUS ErrorStatus;

/* Input information given by the 'FSVOLNOTIFY_PREPAREFORMAT' step ****/
    PCWSTR FileSystemName;
    FMIFS_MEDIA_FLAG MediaFlag;
    PCWSTR Label;
    BOOLEAN QuickFormat;
    ULONG ClusterSize;
    PFMIFSCALLBACK Callback;

} FORMAT_PARTITION_INFO, *PFORMAT_PARTITION_INFO;

typedef struct _CHECK_PARTITION_INFO
{
    PPARTENTRY PartEntry;
    // PCWSTR NtPathPartition;
    NTSTATUS ErrorStatus;

/* Input information given by the 'FSVOLNOTIFY_STARTCHECK' step ****/
    // PCWSTR FileSystemName; // Obtained from PartEntry!
    BOOLEAN FixErrors;
    BOOLEAN Verbose;
    BOOLEAN CheckOnlyIfDirty;
    BOOLEAN ScanDrive;
    PFMIFSCALLBACK Callback;

} CHECK_PARTITION_INFO, *PCHECK_PARTITION_INFO;

typedef FSVOL_OP
(CALLBACK *PFSVOL_CALLBACK)(
    IN PVOID Context OPTIONAL,
    IN FSVOLNOTIFY FormatStatus,
    IN ULONG_PTR Param1,
    IN ULONG_PTR Param2);

BOOLEAN
CommitFsVolOpsQueue(
    IN PPARTLIST PartitionList,
    IN PPARTENTRY InstallPartition,
    IN PPARTENTRY SystemPartition,
    IN PFSVOL_CALLBACK FsVolCallback OPTIONAL,
    IN PVOID Context OPTIONAL);

/*
 * FIXME: TODO: Setup-specific stuff; find a better place to put it!
 */
BOOLEAN
InitSystemPartition(
    IN PPARTLIST PartitionList,
    IN PPARTENTRY InstallPartition,
    OUT PPARTENTRY* pSystemPartition,
    IN PFSVOL_CALLBACK FsVolCallback OPTIONAL,
    IN PVOID Context OPTIONAL);

/* EOF */
