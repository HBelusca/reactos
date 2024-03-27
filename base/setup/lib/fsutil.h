/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Filesystem support functions
 * COPYRIGHT:   Copyright 2003-2018 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

#pragma once

#include <fmifs/fmifs.h>

typedef NTSTATUS
(NTAPI *PFS_INSTALL_BOOTCODE)(
    IN PCWSTR SrcPath,          // FAT12 bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition);   // Partition holding the (old) FAT12 information

typedef struct _FILE_SYSTEM
{
    PCWSTR FileSystemName;
    FORMATEX FormatFunc;
    CHKDSKEX ChkdskFunc;
    PFS_INSTALL_BOOTCODE InstallBootCode;
    BOOTCODE BootCode;
} FILE_SYSTEM, *PFILE_SYSTEM;

PFILE_SYSTEM
GetRegisteredFileSystems(OUT PULONG Count);

PFILE_SYSTEM
GetFileSystemByName(
    // IN PFILE_SYSTEM_LIST List,
    IN PCWSTR FileSystemName);

struct _PARTENTRY; // Defined in partlist.h

PFILE_SYSTEM
GetFileSystem(
    // IN PFILE_SYSTEM_LIST FileSystemList,
    IN struct _PARTENTRY* PartEntry);


BOOLEAN
PreparePartitionForFormatting(
    IN struct _PARTENTRY* PartEntry,
    IN PFILE_SYSTEM FileSystem);

//
// Bootsector routines
//

// HACK: The following defines are temporary hacks!
#define SECTORSIZE 512
#define FAT_BOOTSECTOR_SIZE     (1 * SECTORSIZE)
#define FAT32_BOOTSECTOR_SIZE   (1 * SECTORSIZE)
#define BTRFS_BOOTSECTOR_SIZE   (3 * SECTORSIZE)

/* EOF */
