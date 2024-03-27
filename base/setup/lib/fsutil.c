/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Filesystem support functions
 * COPYRIGHT:   Copyright 2003-2018 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

//
// See also: https://git.reactos.org/?p=reactos.git;a=blob;f=reactos/dll/win32/fmifs/init.c;h=e895f5ef9cae4806123f6bbdd3dfed37ec1c8d33;hb=b9db9a4e377a2055f635b2fb69fef4e1750d219c
// for how to get FS providers in a dynamic way. In the (near) future we may
// consider merging some of this code with us into a fmifs / fsutil / fslib library...
//

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include "bootcode.h"
#include "fsutil.h"
#include "partlist.h"

#include <fslib/vfatlib.h>
#include <fslib/btrfslib.h>
// #include <fslib/ext2lib.h>
// #include <fslib/ntfslib.h>

#define NDEBUG
#include <debug.h>


/* GLOBALS ******************************************************************/

static
NTSTATUS
InstallFatBootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE RootPartition);

static
NTSTATUS
InstallBtrfsBootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE RootPartition);

/* The list of file systems on which we can install ReactOS */
// NOTE: From Rufus' documentation, the FMIFS.DLL' FormatEx allows one to
// specify the precise FS to use; we may consider using it too in the future.
FILE_SYSTEM RegisteredFileSystems[] =
{
    /* NOTE: The FAT formatter automatically determines
     * whether it will use FAT-16 or FAT-32. */
    { L"FAT"  , VfatFormat, VfatChkdsk, InstallFatBootCode, {NULL, NULL, 0} },
#if 0
    { L"FAT32", VfatFormat, VfatChkdsk, InstallFatBootCode, {L"\\loader\\fat32.bin", NULL, 0} },
    { L"FATX" , VfatxFormat, VfatxChkdsk, {NULL, NULL, 0} },
    { L"NTFS" , NtfsFormat, NtfsChkdsk, {NULL, NULL, 0} },
#endif
    { L"BTRFS", BtrfsFormatEx, BtrfsChkdskEx, InstallBtrfsBootCode, {L"\\loader\\btrfs.bin", NULL, 0} },
#if 0
    { L"EXT2" , Ext2Format, Ext2Chkdsk, {NULL, NULL, 0} },
    { L"EXT3" , Ext2Format, Ext2Chkdsk, {NULL, NULL, 0} },
    { L"EXT4" , Ext2Format, Ext2Chkdsk, {NULL, NULL, 0} },
    { L"FFS"  , FfsFormat , FfsChkdsk , {NULL, NULL, 0} },
    { L"REISERFS", ReiserfsFormat, ReiserfsChkdsk, {NULL, NULL, 0} },
#endif
};

NTSTATUS
LoadBootCode(
    IN PFILE_SYSTEM FileSystem,
    IN PCWSTR RootPath OPTIONAL,
    IN PCWSTR BootCodeFileName OPTIONAL)
{
    NTSTATUS Status;
    PVOID BootCode;
    WCHAR SrcPath[MAX_PATH];

    //
    // TODO: Check whether both BootCodeFileName and FileSystem->BootCodeFileName
    // are NULL. If so this means we cannot do anything more...
    //

    /* Allocate buffer for new bootsector */
    BootCode = RtlAllocateHeap(ProcessHeap, 0,SECTORSIZE);
    if (BootCode == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Read the new bootsector from SrcPath */
    CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, RootPath,
                 (BootCodeFileName ? BootCodeFileName
                                   : FileSystem->BootCodeFileName));
    RtlInitUnicodeString(&Name, SrcPath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, BootCode);
        return Status;
    }

    FileOffset.QuadPart = 0ULL;
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        BootCode,
                        SECTORSIZE,
                        &FileOffset,
                        NULL);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, BootCode);
        return Status;
    }

    /* Update the bootcode cache */
    if (FileSystem->BootCode)
        RtlFreeHeap(ProcessHeap, 0, FileSystem->BootCode);
    FileSystem->BootCode = BootCode;
    /**/ FileSystem->BootCodeLength = SECTORSIZE; /**/

    return STATUS_SUCCESS;
}



/* FILESYSTEM-SPECIFIC FUNCTIONS ********************************************/

//
// Bootsector routines
//

static
NTSTATUS
InstallFat12BootCode(
    IN PCWSTR SrcPath,          // FAT12/16 bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition)    // Partition holding the (old) FAT12/16 information
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    LARGE_INTEGER FileOffset;
    PFAT_BOOTSECTOR OrigBootSector;
    PFAT_BOOTSECTOR NewBootSector;

    /* Allocate buffer for original bootsector */
    OrigBootSector = RtlAllocateHeap(ProcessHeap, 0, SECTORSIZE);
    if (OrigBootSector == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Read current boot sector into buffer */
    /* Read the current partition boot sector into the buffer */
    FileOffset.QuadPart = 0ULL;
    Status = NtReadFile(RootPartition,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        OrigBootSector,
                        SECTORSIZE,
                        &FileOffset,
                        NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        return Status;
    }

    /* Allocate buffer for new bootsector */
    NewBootSector = RtlAllocateHeap(ProcessHeap, 0,SECTORSIZE);
    if (NewBootSector == NULL)
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    FileOffset.QuadPart = 0ULL;
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        NewBootSector,
                        SECTORSIZE,
                        &FileOffset,
                        NULL);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    /* Adjust bootsector (copy a part of the FAT12/16 BPB) */
    RtlCopyMemory(&NewBootSector->OemName,
                  &OrigBootSector->OemName,
                  FIELD_OFFSET(FAT_BOOTSECTOR, BootCodeAndData) -
                  FIELD_OFFSET(FAT_BOOTSECTOR, OemName));

    /* Free the original boot sector */
    RtlFreeHeap(ProcessHeap, 0, OrigBootSector);

    /* Write new bootsector to RootPath */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector,
                         SECTORSIZE,
                         &FileOffset,
                         NULL);

    /* Free the new boot sector */
    RtlFreeHeap(ProcessHeap, 0, NewBootSector);

    return Status;
}

static
NTSTATUS
InstallFat16BootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE RootPartition)
{
    /* We use the same function as for FAT12 */
    return InstallFat12BootCode(SrcPath, DstPath, RootPartition);
}

static
NTSTATUS
InstallFat32BootCode(
    IN PCWSTR SrcPath,          // FAT32 bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition)    // Partition holding the (old) FAT32 information
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    LARGE_INTEGER FileOffset;
    PFAT32_BOOTSECTOR OrigBootSector;
    PFAT32_BOOTSECTOR NewBootSector;
    USHORT BackupBootSector;

    /* Allocate a buffer for the original bootsector */
    OrigBootSector = RtlAllocateHeap(ProcessHeap, 0, SECTORSIZE);
    if (OrigBootSector == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Read the current boot sector into the buffer */
    FileOffset.QuadPart = 0ULL;
    Status = NtReadFile(RootPartition,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        OrigBootSector,
                        SECTORSIZE,
                        &FileOffset,
                        NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        return Status;
    }

    /* Allocate a buffer for the new bootsector (2 sectors) */
    NewBootSector = RtlAllocateHeap(ProcessHeap, 0, 2 * SECTORSIZE);
    if (NewBootSector == NULL)
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    FileOffset.QuadPart = 0ULL;
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        NewBootSector,
                        2 * SECTORSIZE,
                        &FileOffset,
                        NULL);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, OrigBootSector);
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    /* Adjust the bootsector (copy a part of the FAT32 BPB) */
    RtlCopyMemory(&NewBootSector->OemName,
                  &OrigBootSector->OemName,
                  FIELD_OFFSET(FAT32_BOOTSECTOR, BootCodeAndData) -
                  FIELD_OFFSET(FAT32_BOOTSECTOR, OemName));

    /*
     * We know we copy the boot code to a file only when DstPath != RootPartition,
     * otherwise the boot code is copied to the specified root partition.
     */
    if (DstPath != RootPartition)
    {
        /* Copy to a file: Disable the backup boot sector */
        NewBootSector->BackupBootSector = 0;
    }
    else
    {
        /* Copy to a disk: Get the location of the backup boot sector */
        BackupBootSector = OrigBootSector->BackupBootSector;
    }

    /* Free the original boot sector */
    RtlFreeHeap(ProcessHeap, 0, OrigBootSector);

    /* Write the first sector of the new bootcode to DstPath sector 0 */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector,
                         SECTORSIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    if (DstPath == RootPartition)
    {
        /* Copy to a disk: Write the backup boot sector */
        if ((BackupBootSector != 0x0000) && (BackupBootSector != 0xFFFF))
        {
            FileOffset.QuadPart = (ULONGLONG)((ULONG)BackupBootSector * SECTORSIZE);
            Status = NtWriteFile(DstPath,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 NewBootSector,
                                 SECTORSIZE,
                                 &FileOffset,
                                 NULL);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
                RtlFreeHeap(ProcessHeap, 0, NewBootSector);
                return Status;
            }
        }
    }

    /* Write the second sector of the new bootcode to boot disk sector 14 */
    // FileOffset.QuadPart = (ULONGLONG)(14 * SECTORSIZE);
    FileOffset.QuadPart = 14 * SECTORSIZE;
    Status = NtWriteFile(DstPath,   // or really RootPartition ???
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         ((PUCHAR)NewBootSector + SECTORSIZE),
                         SECTORSIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
    }

    /* Free the new boot sector */
    RtlFreeHeap(ProcessHeap, 0, NewBootSector);

    return Status;
}

static
NTSTATUS
InstallBtrfsBootCode(
    IN PCWSTR SrcPath,          // BTRFS bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition)    // Partition holding the (old) BTRFS information
{
    NTSTATUS Status;
    NTSTATUS LockStatus;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    LARGE_INTEGER FileOffset;
    PBTRFS_BOOTSECTOR NewBootSector;
    PARTITION_INFORMATION PartInfo;

    // if (SrcPath)
        // LoadBootCode(...., ..., SrcPath);

    /* Allocate buffer for new bootsector */
    NewBootSector = RtlAllocateHeap(ProcessHeap, 0, sizeof(BTRFS_BOOTSECTOR));
    if (NewBootSector == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        NewBootSector,
                        sizeof(BTRFS_BOOTSECTOR),
                        NULL,
                        NULL);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, NewBootSector);
        return Status;
    }

    /*
     * The BTRFS driver requires the volume to be locked in order to modify
     * the first sectors of the partition, even though they are outside the
     * file-system space / in the reserved area (they are situated before
     * the super-block at 0x1000) and is in principle allowed by the NT
     * storage stack.
     * So we lock here in order to write the bootsector at sector 0.
     * If locking fails, we ignore and continue nonetheless.
     */
    LockStatus = NtFsControlFile(DstPath,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_LOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("WARNING: Failed to lock BTRFS volume for writing bootsector! Operations may fail! (Status 0x%lx)\n", LockStatus);
    }

    /* Obtaining partition info and writing it to bootsector */
    Status = NtDeviceIoControlFile(RootPartition,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_PARTITION_INFO,
                                   NULL,
                                   0,
                                   &PartInfo,
                                   sizeof(PartInfo));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_PARTITION_INFO failed (Status %lx)\n", Status);
        goto Quit;
    }

    /* Write new bootsector to RootPath */

    NewBootSector->PartitionStartLBA = PartInfo.StartingOffset.QuadPart / SECTORSIZE;

    /* Write sector 0 */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector,
                         sizeof(BTRFS_BOOTSECTOR),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
        goto Quit;
    }

Quit:
    /* Unlock the volume */
    LockStatus = NtFsControlFile(DstPath,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_UNLOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Failed to unlock BTRFS volume (Status 0x%lx)\n", LockStatus);
    }

    /* Close the volume */
    NtClose(FileHandle);

    /* Free the new boot sector */
    RtlFreeHeap(ProcessHeap, 0, NewBootSector);

    return Status;
}


/* FUNCTIONS ****************************************************************/

PFILE_SYSTEM
GetRegisteredFileSystems(OUT PULONG Count)
{
    *Count = ARRAYSIZE(RegisteredFileSystems);
    return RegisteredFileSystems;
}

PFILE_SYSTEM
GetFileSystemByName(
    // IN PFILE_SYSTEM_LIST List,
    IN PCWSTR FileSystemName)
{
#if 0 // Reenable when the list of registered FSes will again be dynamic

    PLIST_ENTRY ListEntry;
    PFILE_SYSTEM_ITEM Item;

    ListEntry = List->ListHead.Flink;
    while (ListEntry != &List->ListHead)
    {
        Item = CONTAINING_RECORD(ListEntry, FILE_SYSTEM_ITEM, ListEntry);
        if (Item->FileSystemName && wcsicmp(FileSystemName, Item->FileSystemName) == 0)
            return Item;

        ListEntry = ListEntry->Flink;
    }

#else

    ULONG Count;
    PFILE_SYSTEM FileSystems;

    FileSystems = GetRegisteredFileSystems(&Count);
    if (!FileSystems || Count == 0)
        return NULL;

    while (Count--)
    {
        if (FileSystems->FileSystemName && wcsicmp(FileSystemName, FileSystems->FileSystemName) == 0)
            return FileSystems;

        ++FileSystems;
    }

#endif

    return NULL;
}


//
// FileSystem recognition (using NT OS functionality)
//

/* NOTE: Ripped & adapted from base/system/autochk/autochk.c */
static NTSTATUS
_MyGetFileSystem(
    IN struct _PARTENTRY* PartEntry,
    IN OUT PWSTR FileSystemName,
    IN SIZE_T FileSystemNameSize)
{
    NTSTATUS Status;
    UNICODE_STRING PartitionRootPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR PathBuffer[MAX_PATH];
    UCHAR Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    PFILE_FS_ATTRIBUTE_INFORMATION FileFsAttribute = (PFILE_FS_ATTRIBUTE_INFORMATION)Buffer;

    /* Set PartitionRootPath */
    RtlStringCchPrintfW(PathBuffer, ARRAYSIZE(PathBuffer),
                        L"\\Device\\Harddisk%lu\\Partition%lu",
                        PartEntry->DiskEntry->DiskNumber,
                        PartEntry->PartitionNumber);
    RtlInitUnicodeString(&PartitionRootPath, PathBuffer);
    DPRINT("PartitionRootPath: %wZ\n", &PartitionRootPath);

    /* Open the partition */
    InitializeObjectAttributes(&ObjectAttributes,
                               &PartitionRootPath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&FileHandle, // PartitionHandle,
                        FILE_GENERIC_READ /* | SYNCHRONIZE */,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        0 /* FILE_SYNCHRONOUS_IO_NONALERT */);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open partition '%wZ', Status 0x%08lx\n", &PartitionRootPath, Status);
        return Status;
    }

    /* Retrieve the FS attributes */
    Status = NtQueryVolumeInformationFile(FileHandle,
                                          &IoStatusBlock,
                                          FileFsAttribute,
                                          sizeof(Buffer),
                                          FileFsAttributeInformation);
    NtClose(FileHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtQueryVolumeInformationFile failed for partition '%wZ', Status 0x%08lx\n",
                &PartitionRootPath, Status);
        return Status;
    }

    if (FileSystemNameSize < FileFsAttribute->FileSystemNameLength + sizeof(WCHAR))
        return STATUS_BUFFER_TOO_SMALL;

    return RtlStringCbCopyNW(FileSystemName, FileSystemNameSize,
                             FileFsAttribute->FileSystemName,
                             FileFsAttribute->FileSystemNameLength);
}

PFILE_SYSTEM
GetFileSystem(
    // IN PFILE_SYSTEM_LIST FileSystemList,
    IN struct _PARTENTRY* PartEntry)
{
    PFILE_SYSTEM CurrentFileSystem;
    NTSTATUS Status;
    PWSTR FileSystemName = NULL;
    WCHAR FsRecFileSystemName[MAX_PATH];

    CurrentFileSystem = PartEntry->FileSystem;

    /* We have a file system, return it */
    if (CurrentFileSystem != NULL && CurrentFileSystem->FileSystemName != NULL)
        return CurrentFileSystem;

    DPRINT1("File system not found, try to guess one...\n");

    CurrentFileSystem = NULL;

    /*
     * We don't have one...
     *
     * Try to infer a file system using NT file system recognition.
     */
    Status = _MyGetFileSystem(PartEntry, FsRecFileSystemName, sizeof(FsRecFileSystemName));
    if (NT_SUCCESS(Status) && *FsRecFileSystemName)
    {
        /* Temporary HACK: map FAT32 back to FAT */
        if (wcscmp(FsRecFileSystemName, L"FAT32") == 0)
            RtlStringCbCopyW(FsRecFileSystemName, sizeof(FsRecFileSystemName), L"FAT");

        FileSystemName = FsRecFileSystemName;
        goto Quit;
    }

    /*
     * We don't have one...
     *
     * Try to infer a preferred file system for this partition, given its ID.
     *
     * WARNING: This is partly a hack, since partitions with the same ID can
     * be formatted with different file systems: for example, usual Linux
     * partitions that are formatted in EXT2/3/4, ReiserFS, etc... have the
     * same partition ID 0x83.
     *
     * The proper fix is to make a function that detects the existing FS
     * from a given partition (not based on the partition ID).
     * On the contrary, for unformatted partitions with a given ID, the
     * following code is OK.
     */
    if ((PartEntry->PartitionType == PARTITION_FAT_12) ||
        (PartEntry->PartitionType == PARTITION_FAT_16) ||
        (PartEntry->PartitionType == PARTITION_HUGE  ) ||
        (PartEntry->PartitionType == PARTITION_XINT13) ||
        (PartEntry->PartitionType == PARTITION_FAT32 ) ||
        (PartEntry->PartitionType == PARTITION_FAT32_XINT13))
    {
        FileSystemName = L"FAT";
    }
    else if (PartEntry->PartitionType == PARTITION_LINUX)
    {
        // WARNING: See the warning above.
        /* Could also be EXT2/3/4, ReiserFS, ... */
        FileSystemName = L"BTRFS";
    }
    else if (PartEntry->PartitionType == PARTITION_IFS)
    {
        // WARNING: See the warning above.
        /* Could also be HPFS */
        FileSystemName = L"NTFS";
    }

Quit:
    // HACK: WARNING: We cannot write on this FS yet!
    if (FileSystemName)
    {
        if (PartEntry->PartitionType == PARTITION_IFS)
            DPRINT1("Recognized file system %S that doesn't support write support yet!\n", FileSystemName);
    }

    DPRINT1("GetFileSystem -- PartitionType: 0x%02X ; FileSystemName (guessed): %S\n",
            PartEntry->PartitionType, FileSystemName ? FileSystemName : L"None");

    if (FileSystemName)
        CurrentFileSystem = GetFileSystemByName(FileSystemName);

    return CurrentFileSystem;
}


//
// Formatting routines
//

BOOLEAN
PreparePartitionForFormatting(
    IN struct _PARTENTRY* PartEntry,
    IN PFILE_SYSTEM FileSystem)
{
    UCHAR PartitionType;

    if (!FileSystem)
    {
        DPRINT1("No file system specified?\n");
        return FALSE;
    }

    if (wcscmp(FileSystem->FileSystemName, L"FAT") == 0)
    {
        if (PartEntry->SectorCount.QuadPart < 8192)
        {
            /* FAT12 CHS partition (disk is smaller than 4.1MB) */
            PartitionType = PARTITION_FAT_12;
        }
        else if (PartEntry->StartSector.QuadPart < 1450560)
        {
            /* Partition starts below the 8.4GB boundary ==> CHS partition */

            if (PartEntry->SectorCount.QuadPart < 65536)
            {
                /* FAT16 CHS partition (partition size < 32MB) */
                PartitionType = PARTITION_FAT_16;
            }
            else if (PartEntry->SectorCount.QuadPart < 1048576)
            {
                /* FAT16 CHS partition (partition size < 512MB) */
                PartitionType = PARTITION_HUGE;
            }
            else
            {
                /* FAT32 CHS partition (partition size >= 512MB) */
                PartitionType = PARTITION_FAT32;
            }
        }
        else
        {
            /* Partition starts above the 8.4GB boundary ==> LBA partition */

            if (PartEntry->SectorCount.QuadPart < 1048576)
            {
                /* FAT16 LBA partition (partition size < 512MB) */
                PartitionType = PARTITION_XINT13;
            }
            else
            {
                /* FAT32 LBA partition (partition size >= 512MB) */
                PartitionType = PARTITION_FAT32_XINT13;
            }
        }
    }
    else if (wcscmp(FileSystem->FileSystemName, L"BTRFS") == 0)
    {
        PartitionType = PARTITION_LINUX;
    }
#if 0
    else if (wcscmp(FileSystem->FileSystemName, L"EXT2") == 0)
    {
        PartitionType = PARTITION_LINUX;
    }
    else if (wcscmp(FileSystem->FileSystemName, L"NTFS") == 0)
    {
        PartitionType = PARTITION_IFS;
    }
#endif
    else
    {
        /* Unknown file system? */
        DPRINT1("Unknown file system \"%S\"?\n", FileSystem->FileSystemName);
        return FALSE;
    }

    SetPartitionType(PartEntry, PartitionType);

//
// FIXME: Do this now, or after the partition is actually formatted??
//
    /* Set the new partition's file system proper */
    PartEntry->FormatState = Formatted; // Well... This may be set after the real formatting takes place (in which case we should change the FormatState to another value)
    PartEntry->FileSystem  = FileSystem;

    return TRUE;
}

/* EOF */
