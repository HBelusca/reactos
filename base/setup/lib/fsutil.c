/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Filesystem Format and ChkDsk support functions.
 * COPYRIGHT:   Copyright 2003-2019 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2017-2020 Hermes Belusca-Maito
 */

//
// See also: https://git.reactos.org/?p=reactos.git;a=blob;f=reactos/dll/win32/fmifs/init.c;h=e895f5ef9cae4806123f6bbdd3dfed37ec1c8d33;hb=b9db9a4e377a2055f635b2fb69fef4e1750d219c
// for how to get FS providers in a dynamic way. In the (near) future we may
// consider merging some of this code with us into a fmifs / fsutil / fslib library...
//

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include "partlist.h"
#include "fsrec.h"
#include "bootcode.h"
#include "fsutil.h"

#include <fslib/vfatlib.h>
#include <fslib/btrfslib.h>
// #include <fslib/ext2lib.h>
// #include <fslib/ntfslib.h>

#define NDEBUG
#include <debug.h>


/* TYPEDEFS *****************************************************************/

#include <pshpack1.h>
typedef struct _FAT_BOOTSECTOR
{
    UCHAR       JumpBoot[3];                // Jump instruction to boot code
    CHAR        OemName[8];                 // "MSWIN4.1" for MS formatted volumes
    USHORT      BytesPerSector;             // Bytes per sector
    UCHAR       SectorsPerCluster;          // Number of sectors in a cluster
    USHORT      ReservedSectors;            // Reserved sectors, usually 1 (the bootsector)
    UCHAR       NumberOfFats;               // Number of FAT tables
    USHORT      RootDirEntries;             // Number of root directory entries (fat12/16)
    USHORT      TotalSectors;               // Number of total sectors on the drive, 16-bit
    UCHAR       MediaDescriptor;            // Media descriptor byte
    USHORT      SectorsPerFat;              // Sectors per FAT table (fat12/16)
    USHORT      SectorsPerTrack;            // Number of sectors in a track
    USHORT      NumberOfHeads;              // Number of heads on the disk
    ULONG       HiddenSectors;              // Hidden sectors (sectors before the partition start like the partition table)
    ULONG       TotalSectorsBig;            // This field is the new 32-bit total count of sectors on the volume
    UCHAR       DriveNumber;                // Int 0x13 drive number (e.g. 0x80)
    UCHAR       Reserved1;                  // Reserved (used by Windows NT). Code that formats FAT volumes should always set this byte to 0.
    UCHAR       BootSignature;              // Extended boot signature (0x29). This is a signature byte that indicates that the following three fields in the boot sector are present.
    ULONG       VolumeSerialNumber;         // Volume serial number
    CHAR        VolumeLabel[11];            // Volume label. This field matches the 11-byte volume label recorded in the root directory
    CHAR        FileSystemType[8];          // One of the strings "FAT12   ", "FAT16   ", or "FAT     "

    UCHAR       BootCodeAndData[448];       // The remainder of the boot sector

    USHORT      BootSectorMagic;            // 0xAA55

} FAT_BOOTSECTOR, *PFAT_BOOTSECTOR;
C_ASSERT(sizeof(FAT_BOOTSECTOR) == FAT_BOOTSECTOR_SIZE);

typedef struct _FAT32_BOOTSECTOR
{
    UCHAR       JumpBoot[3];                // Jump instruction to boot code
    CHAR        OemName[8];                 // "MSWIN4.1" for MS formatted volumes
    USHORT      BytesPerSector;             // Bytes per sector
    UCHAR       SectorsPerCluster;          // Number of sectors in a cluster
    USHORT      ReservedSectors;            // Reserved sectors, usually 1 (the bootsector)
    UCHAR       NumberOfFats;               // Number of FAT tables
    USHORT      RootDirEntries;             // Number of root directory entries (fat12/16)
    USHORT      TotalSectors;               // Number of total sectors on the drive, 16-bit
    UCHAR       MediaDescriptor;            // Media descriptor byte
    USHORT      SectorsPerFat;              // Sectors per FAT table (fat12/16)
    USHORT      SectorsPerTrack;            // Number of sectors in a track
    USHORT      NumberOfHeads;              // Number of heads on the disk
    ULONG       HiddenSectors;              // Hidden sectors (sectors before the partition start like the partition table)
    ULONG       TotalSectorsBig;            // This field is the new 32-bit total count of sectors on the volume
    ULONG       SectorsPerFatBig;           // This field is the FAT32 32-bit count of sectors occupied by ONE FAT. BPB_FATSz16 must be 0
    USHORT      ExtendedFlags;              // Extended flags (fat32)
    USHORT      FileSystemVersion;          // File system version (fat32)
    ULONG       RootDirStartCluster;        // Starting cluster of the root directory (fat32)
    USHORT      FsInfo;                     // Sector number of FSINFO structure in the reserved area of the FAT32 volume. Usually 1.
    USHORT      BackupBootSector;           // If non-zero, indicates the sector number in the reserved area of the volume of a copy of the boot record. Usually 6.
    UCHAR       Reserved[12];               // Reserved for future expansion
    UCHAR       DriveNumber;                // Int 0x13 drive number (e.g. 0x80)
    UCHAR       Reserved1;                  // Reserved (used by Windows NT). Code that formats FAT volumes should always set this byte to 0.
    UCHAR       BootSignature;              // Extended boot signature (0x29). This is a signature byte that indicates that the following three fields in the boot sector are present.
    ULONG       VolumeSerialNumber;         // Volume serial number
    CHAR        VolumeLabel[11];            // Volume label. This field matches the 11-byte volume label recorded in the root directory
    CHAR        FileSystemType[8];          // Always set to the string "FAT32   "

    UCHAR       BootCodeAndData[420];       // The remainder of the boot sector

    USHORT      BootSectorMagic;            // 0xAA55

} FAT32_BOOTSECTOR, *PFAT32_BOOTSECTOR;
C_ASSERT(sizeof(FAT32_BOOTSECTOR) == FAT32_BOOTSECTOR_SIZE);

typedef struct _BTRFS_BOOTSECTOR
{
    UCHAR JumpBoot[3];
    UCHAR ChunkMapSize;
    UCHAR BootDrive;
    ULONGLONG PartitionStartLBA;
    UCHAR Fill[1521]; // 1536 - 15
    USHORT BootSectorMagic;
} BTRFS_BOOTSECTOR, *PBTRFS_BOOTSECTOR;
C_ASSERT(sizeof(BTRFS_BOOTSECTOR) == BTRFS_BOOTSECTOR_SIZE);

// TODO: Add more bootsector structures!

#include <poppack.h>


/* LOCALS *******************************************************************/

/** IFS_PROVIDER **/
typedef struct _FILE_SYSTEM
{
    PCWSTR FileSystemName;
    PULIB_FORMAT FormatFunc;
    PULIB_CHKDSK ChkdskFunc;
} FILE_SYSTEM, *PFILE_SYSTEM;

/* The list of file systems on which we can install ReactOS */
static FILE_SYSTEM RegisteredFileSystems[] =
{
    /* NOTE: The FAT formatter will automatically
     * determine whether to use FAT12/16 or FAT32. */
    { L"FAT"  , VfatFormat, VfatChkdsk },
    { L"FAT32", VfatFormat, VfatChkdsk },
#if 0
    { L"FATX" , VfatxFormat, VfatxChkdsk },
    { L"NTFS" , NtfsFormat, NtfsChkdsk },
#endif
    { L"BTRFS", BtrfsFormat, BtrfsChkdsk },
#if 0
    { L"EXT2" , Ext2Format, Ext2Chkdsk },
    { L"EXT3" , Ext2Format, Ext2Chkdsk },
    { L"EXT4" , Ext2Format, Ext2Chkdsk },
    { L"FFS"  , FfsFormat , FfsChkdsk  },
    { L"REISERFS", ReiserfsFormat, ReiserfsChkdsk },
#endif
};


/* FUNCTIONS ****************************************************************/

/** QueryAvailableFileSystemFormat() **/
BOOLEAN
GetRegisteredFileSystems(
    IN ULONG Index,
    OUT PCWSTR* FileSystemName)
{
    if (Index >= ARRAYSIZE(RegisteredFileSystems))
        return FALSE;

    *FileSystemName = RegisteredFileSystems[Index].FileSystemName;

    return TRUE;
}


/** GetProvider() **/
static PFILE_SYSTEM
GetFileSystemByName(
    IN PCWSTR FileSystemName)
{
#if 0 // Reenable when the list of registered FSes will again be dynamic

    PLIST_ENTRY ListEntry;
    PFILE_SYSTEM_ITEM Item;

    ListEntry = List->ListHead.Flink;
    while (ListEntry != &List->ListHead)
    {
        Item = CONTAINING_RECORD(ListEntry, FILE_SYSTEM_ITEM, ListEntry);
        if (Item->FileSystemName &&
            (wcsicmp(FileSystemName, Item->FileSystemName) == 0))
        {
            return Item;
        }

        ListEntry = ListEntry->Flink;
    }

#else

    ULONG Count = ARRAYSIZE(RegisteredFileSystems);
    PFILE_SYSTEM FileSystems = RegisteredFileSystems;

    ASSERT(FileSystems && Count != 0);

    while (Count--)
    {
        if (FileSystems->FileSystemName &&
            (wcsicmp(FileSystemName, FileSystems->FileSystemName) == 0))
        {
            return FileSystems;
        }

        ++FileSystems;
    }

#endif

    return NULL;
}


/** ChkdskEx() **/
NTSTATUS
ChkdskFileSystem_UStr(
    IN PUNICODE_STRING DriveRoot,
    IN PCWSTR FileSystemName,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PFMIFSCALLBACK Callback)
{
    PFILE_SYSTEM FileSystem;
    NTSTATUS Status;
    BOOLEAN Success;

    FileSystem = GetFileSystemByName(FileSystemName);

    if (!FileSystem || !FileSystem->ChkdskFunc)
    {
        // Success = FALSE;
        // Callback(DONE, 0, &Success);
        return STATUS_NOT_SUPPORTED;
    }

    Status = STATUS_SUCCESS;
    Success = FileSystem->ChkdskFunc(DriveRoot,
                                     Callback,
                                     FixErrors,
                                     Verbose,
                                     CheckOnlyIfDirty,
                                     ScanDrive,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     (PULONG)&Status);
    if (!Success)
        DPRINT1("ChkdskFunc() failed with Status 0x%lx\n", Status);

    // Callback(DONE, 0, &Success);

    return Status;
}

NTSTATUS
ChkdskFileSystem(
    IN PCWSTR DriveRoot,
    IN PCWSTR FileSystemName,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PFMIFSCALLBACK Callback)
{
    UNICODE_STRING DriveRootU;

    RtlInitUnicodeString(&DriveRootU, DriveRoot);
    return ChkdskFileSystem_UStr(&DriveRootU,
                                 FileSystemName,
                                 FixErrors,
                                 Verbose,
                                 CheckOnlyIfDirty,
                                 ScanDrive,
                                 Callback);
}


/** FormatEx() **/
NTSTATUS
FormatFileSystem_UStr(
    IN PUNICODE_STRING DriveRoot,
    IN PCWSTR FileSystemName,
    IN FMIFS_MEDIA_FLAG MediaFlag,
    IN PUNICODE_STRING Label,
    IN BOOLEAN QuickFormat,
    IN ULONG ClusterSize,
    IN PFMIFSCALLBACK Callback)
{
    PFILE_SYSTEM FileSystem;
    BOOLEAN Success;
    BOOLEAN BackwardCompatible = FALSE; // Default to latest FS versions.
    MEDIA_TYPE MediaType;

    FileSystem = GetFileSystemByName(FileSystemName);

    if (!FileSystem || !FileSystem->FormatFunc)
    {
        // Success = FALSE;
        // Callback(DONE, 0, &Success);
        return STATUS_NOT_SUPPORTED;
    }

    /* Set the BackwardCompatible flag in case we format with older FAT12/16 */
    if (wcsicmp(FileSystemName, L"FAT") == 0)
        BackwardCompatible = TRUE;
    // else if (wcsicmp(FileSystemName, L"FAT32") == 0)
        // BackwardCompatible = FALSE;

    /* Convert the FMIFS MediaFlag to a NT MediaType */
    // FIXME: Actually covert all the possible flags.
    switch (MediaFlag)
    {
    case FMIFS_FLOPPY:
        MediaType = F5_320_1024; // FIXME: This is hardfixed!
        break;
    case FMIFS_REMOVABLE:
        MediaType = RemovableMedia;
        break;
    case FMIFS_HARDDISK:
        MediaType = FixedMedia;
        break;
    default:
        DPRINT1("Unknown FMIFS MediaFlag %d, converting 1-to-1 to NT MediaType\n",
                MediaFlag);
        MediaType = (MEDIA_TYPE)MediaFlag;
        break;
    }

    Success = FileSystem->FormatFunc(DriveRoot,
                                     Callback,
                                     QuickFormat,
                                     BackwardCompatible,
                                     MediaType,
                                     Label,
                                     ClusterSize);
    if (!Success)
        DPRINT1("FormatFunc() failed\n");

    // Callback(DONE, 0, &Success);

    return (Success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL);
}

NTSTATUS
FormatFileSystem(
    IN PCWSTR DriveRoot,
    IN PCWSTR FileSystemName,
    IN FMIFS_MEDIA_FLAG MediaFlag,
    IN PCWSTR Label,
    IN BOOLEAN QuickFormat,
    IN ULONG ClusterSize,
    IN PFMIFSCALLBACK Callback)
{
    UNICODE_STRING DriveRootU;
    UNICODE_STRING LabelU;

    RtlInitUnicodeString(&DriveRootU, DriveRoot);
    RtlInitUnicodeString(&LabelU, Label);

    return FormatFileSystem_UStr(&DriveRootU,
                                 FileSystemName,
                                 MediaFlag,
                                 &LabelU,
                                 QuickFormat,
                                 ClusterSize,
                                 Callback);
}


//
// Bootsector routines
//

NTSTATUS
InstallFatBootCode(
    IN PCWSTR SrcPath,          // FAT12/16 bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition)    // Partition holding the (old) FAT12/16 information
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    BOOTCODE OrigBootSector = {0};
    BOOTCODE NewBootSector  = {0};

    /* Allocate and read the current original partition bootsector */
    Status = ReadBootCodeByHandle(&OrigBootSector,
                                  RootPartition,
                                  FAT_BOOTSECTOR_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Allocate and read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    Status = ReadBootCodeFromFile(&NewBootSector,
                                  &Name,
                                  FAT_BOOTSECTOR_SIZE);
    if (!NT_SUCCESS(Status))
    {
        FreeBootCode(&OrigBootSector);
        return Status;
    }

    /* Adjust the bootsector (copy a part of the FAT12/16 BPB) */
    RtlCopyMemory(&((PFAT_BOOTSECTOR)NewBootSector.BootCode)->OemName,
                  &((PFAT_BOOTSECTOR)OrigBootSector.BootCode)->OemName,
                  FIELD_OFFSET(FAT_BOOTSECTOR, BootCodeAndData) -
                  FIELD_OFFSET(FAT_BOOTSECTOR, OemName));

    /* Free the original bootsector */
    FreeBootCode(&OrigBootSector);

    /* Write the new bootsector to DstPath */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector.BootCode,
                         NewBootSector.Length,
                         &FileOffset,
                         NULL);

    /* Free the new bootsector */
    FreeBootCode(&NewBootSector);

    return Status;
}

NTSTATUS
InstallFat32BootCode(
    IN PCWSTR SrcPath,          // FAT32 bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition)    // Partition holding the (old) FAT32 information
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    USHORT BackupBootSector = 0;
    BOOTCODE OrigBootSector = {0};
    BOOTCODE NewBootSector  = {0};

    /* Allocate and read the current original partition bootsector */
    Status = ReadBootCodeByHandle(&OrigBootSector,
                                  RootPartition,
                                  FAT32_BOOTSECTOR_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Allocate and read the new bootsector (2 sectors) from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    Status = ReadBootCodeFromFile(&NewBootSector,
                                  &Name,
                                  2 * FAT32_BOOTSECTOR_SIZE);
    if (!NT_SUCCESS(Status))
    {
        FreeBootCode(&OrigBootSector);
        return Status;
    }

    /* Adjust the bootsector (copy a part of the FAT32 BPB) */
    RtlCopyMemory(&((PFAT32_BOOTSECTOR)NewBootSector.BootCode)->OemName,
                  &((PFAT32_BOOTSECTOR)OrigBootSector.BootCode)->OemName,
                  FIELD_OFFSET(FAT32_BOOTSECTOR, BootCodeAndData) -
                  FIELD_OFFSET(FAT32_BOOTSECTOR, OemName));

    /*
     * We know we copy the boot code to a file only when DstPath != RootPartition,
     * otherwise the boot code is copied to the specified root partition.
     */
    if (DstPath != RootPartition)
    {
        /* Copy to a file: Disable the backup bootsector */
        ((PFAT32_BOOTSECTOR)NewBootSector.BootCode)->BackupBootSector = 0;
    }
    else
    {
        /* Copy to a disk: Get the location of the backup bootsector */
        BackupBootSector = ((PFAT32_BOOTSECTOR)OrigBootSector.BootCode)->BackupBootSector;
    }

    /* Free the original bootsector */
    FreeBootCode(&OrigBootSector);

    /* Write the first sector of the new bootcode to DstPath sector 0 */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector.BootCode,
                         FAT32_BOOTSECTOR_SIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
        FreeBootCode(&NewBootSector);
        return Status;
    }

    if (DstPath == RootPartition)
    {
        /* Copy to a disk: Write the backup bootsector */
        if ((BackupBootSector != 0x0000) && (BackupBootSector != 0xFFFF))
        {
            FileOffset.QuadPart = (ULONGLONG)((ULONG)BackupBootSector * FAT32_BOOTSECTOR_SIZE);
            Status = NtWriteFile(DstPath,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 NewBootSector.BootCode,
                                 FAT32_BOOTSECTOR_SIZE,
                                 &FileOffset,
                                 NULL);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
                FreeBootCode(&NewBootSector);
                return Status;
            }
        }
    }

    /* Write the second sector of the new bootcode to boot disk sector 14 */
    // FileOffset.QuadPart = (ULONGLONG)(14 * FAT32_BOOTSECTOR_SIZE);
    FileOffset.QuadPart = 14 * FAT32_BOOTSECTOR_SIZE;
    Status = NtWriteFile(DstPath,   // or really RootPartition ???
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         ((PUCHAR)NewBootSector.BootCode + FAT32_BOOTSECTOR_SIZE),
                         FAT32_BOOTSECTOR_SIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed (Status %lx)\n", Status);
    }

    /* Free the new bootsector */
    FreeBootCode(&NewBootSector);

    return Status;
}

NTSTATUS
InstallBtrfsBootCode(
    IN PCWSTR SrcPath,          // BTRFS bootsector source file (on the installation medium)
    IN HANDLE DstPath,          // Where to save the bootsector built from the source + partition information
    IN HANDLE RootPartition)    // Partition holding the (old) BTRFS information
{
    NTSTATUS Status;
    NTSTATUS LockStatus;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    PARTITION_INFORMATION_EX PartInfo;
    BOOTCODE NewBootSector = {0};

    /* Allocate and read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    Status = ReadBootCodeFromFile(&NewBootSector,
                                  &Name,
                                  BTRFS_BOOTSECTOR_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

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

    /* Obtain partition info and write it to the bootsector */
    Status = NtDeviceIoControlFile(RootPartition,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_PARTITION_INFO_EX,
                                   NULL,
                                   0,
                                   &PartInfo,
                                   sizeof(PartInfo));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_PARTITION_INFO_EX failed (Status %lx)\n", Status);
        goto Quit;
    }

    /* Write new bootsector to RootPath */
    ((PBTRFS_BOOTSECTOR)NewBootSector.BootCode)->PartitionStartLBA =
        PartInfo.StartingOffset.QuadPart / SECTORSIZE;

    /* Write sector 0 */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector.BootCode,
                         NewBootSector.Length,
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

    /* Free the new bootsector */
    FreeBootCode(&NewBootSector);

    return Status;
}


//
// Formatting routines
//

NTSTATUS
ChkdskPartition(
    IN PPARTENTRY PartEntry,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PFMIFSCALLBACK Callback)
{
    NTSTATUS Status;
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    // UNICODE_STRING PartitionRootPath;
    WCHAR PartitionRootPath[MAX_PATH]; // PathBuffer

    ASSERT(PartEntry->IsPartitioned && PartEntry->PartitionNumber != 0);

    /* HACK: Do not try to check a partition with an unknown filesystem */
    if (!*PartEntry->FileSystem)
    {
        PartEntry->NeedsCheck = FALSE;
        return STATUS_SUCCESS;
    }

    /* Set PartitionRootPath */
    RtlStringCchPrintfW(PartitionRootPath, ARRAYSIZE(PartitionRootPath),
                        L"\\Device\\Harddisk%lu\\Partition%lu",
                        DiskEntry->DiskNumber,
                        PartEntry->PartitionNumber);
    DPRINT("PartitionRootPath: %S\n", PartitionRootPath);

    /* Check the partition */
    Status = ChkdskFileSystem(PartitionRootPath,
                              PartEntry->FileSystem,
                              FixErrors,
                              Verbose,
                              CheckOnlyIfDirty,
                              ScanDrive,
                              Callback);
    if (!NT_SUCCESS(Status))
        return Status;

    PartEntry->NeedsCheck = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
FormatPartition(
    IN PPARTENTRY PartEntry,
    IN PCWSTR FileSystemName,
    IN FMIFS_MEDIA_FLAG MediaFlag,
    IN PCWSTR Label,
    IN BOOLEAN QuickFormat,
    IN ULONG ClusterSize,
    IN PFMIFSCALLBACK Callback)
{
    NTSTATUS Status;
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    UCHAR PartitionType;
    // UNICODE_STRING PartitionRootPath;
    WCHAR PartitionRootPath[MAX_PATH]; // PathBuffer

    ASSERT(PartEntry->IsPartitioned && PartEntry->PartitionNumber != 0);

    if (!FileSystemName || !*FileSystemName)
    {
        DPRINT1("No file system specified?\n");
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    /*
     * Prepare the partition for formatting (for MBR disks, reset the
     * partition type), and adjust the filesystem name in case of FAT
     * vs. FAT32, depending on the geometry of the partition.
     */

// FIXME: Do this only if QuickFormat == FALSE? What about FAT handling?

    /*
     * Retrieve a partition type as a hint only. It will be used to determine
     * whether to actually use FAT12/16 or FAT32 filesystem, depending on the
     * geometry of the partition. If the partition resides on an MBR disk,
     * the partition style will be reset to this value as well, unless the
     * partition is OEM.
     */
    PartitionType = FileSystemToMBRPartitionType(FileSystemName,
                                                 PartEntry->StartSector.QuadPart,
                                                 PartEntry->SectorCount.QuadPart);
    if (PartitionType == PARTITION_ENTRY_UNUSED)
    {
        /* Unknown file system */
        DPRINT1("Unknown file system '%S'\n", FileSystemName);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    /* Reset the MBR partition type, unless this is an OEM partition */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
    {
        if (!IsOEMPartition(PartEntry->PartitionType))
            SetMBRPartitionType(PartEntry, PartitionType);
    }

    /*
     * Adjust the filesystem name in case of FAT vs. FAT32, according to
     * the type of partition returned by FileSystemToMBRPartitionType().
     */
    if (wcsicmp(FileSystemName, L"FAT") == 0)
    {
        if ((PartitionType == PARTITION_FAT32) ||
            (PartitionType == PARTITION_FAT32_XINT13))
        {
            FileSystemName = L"FAT32";
        }
    }

    /* Commit the partition changes to the disk */
    Status = WritePartitions(DiskEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WritePartitions(disk %lu) failed, Status 0x%08lx\n",
                DiskEntry->DiskNumber, Status);
        return STATUS_PARTITION_FAILURE;
    }

    /* Set PartitionRootPath */
    RtlStringCchPrintfW(PartitionRootPath, ARRAYSIZE(PartitionRootPath),
                        L"\\Device\\Harddisk%lu\\Partition%lu",
                        DiskEntry->DiskNumber,
                        PartEntry->PartitionNumber);
    DPRINT("PartitionRootPath: %S\n", PartitionRootPath);

    /* Format the partition */
    Status = FormatFileSystem(PartitionRootPath,
                              FileSystemName,
                              MediaFlag,
                              Label,
                              QuickFormat,
                              ClusterSize,
                              Callback);
    if (!NT_SUCCESS(Status))
        return Status;

//
// TODO: Here, call a partlist.c function that update the actual
// FS name and the label fields of the volume.
//
    PartEntry->FormatState = Formatted;

    /* Set the new partition's file system proper */
    RtlStringCbCopyW(PartEntry->FileSystem,
                     sizeof(PartEntry->FileSystem),
                     FileSystemName);

    PartEntry->New = FALSE;

    return STATUS_SUCCESS;
}


//
// FileSystem Volume Operations Queue
//

static FSVOL_OP
DoFormatting(
    IN PPARTENTRY PartEntry,
    IN PVOID Context OPTIONAL,
    IN PFSVOL_CALLBACK FsVolCallback OPTIONAL)
{
    FSVOL_OP Result;
    NTSTATUS Status;
    FORMAT_PARTITION_INFO PartInfo;

    ASSERT(PartEntry->IsPartitioned && PartEntry->PartitionNumber != 0);

    PartInfo.PartEntry = PartEntry;

RetryFormat:
    Result = FsVolCallback(Context,
                           FSVOLNOTIFY_PREPAREFORMAT,
                           (ULONG_PTR)&PartInfo,
                           FSVOL_FORMAT);
    if (Result != FSVOL_DOIT)
        return Result;

    ASSERT(PartInfo.FileSystemName && *PartInfo.FileSystemName);

    Result = FsVolCallback(Context,
                           FSVOLNOTIFY_STARTFORMAT,
                           (ULONG_PTR)&PartInfo,
                           FSVOL_FORMAT);
    if (Result != FSVOL_DOIT)
        goto EndFormat;

    /* Format the partition */
    Status = FormatPartition(PartEntry,
                             PartInfo.FileSystemName,
                             PartInfo.MediaFlag,
                             PartInfo.Label,
                             PartInfo.QuickFormat,
                             PartInfo.ClusterSize,
                             PartInfo.Callback);
    if (!NT_SUCCESS(Status))
    {
        // PartInfo.NtPathPartition = PathBuffer;
        PartInfo.ErrorStatus = Status;

        Result = FsVolCallback(Context,
                               FSVOLNOTIFY_FORMATERROR,
                               (ULONG_PTR)&PartInfo,
                               0);
        if (Result == FSVOL_RETRY)
            goto RetryFormat;
        // else if (Result == FSVOL_ABORT || Result == FSVOL_SKIP), stop.
    }

EndFormat:
    /* This notification is always sent, even in case of error */
    PartInfo.ErrorStatus = Status;
    FsVolCallback(Context,
                  FSVOLNOTIFY_ENDFORMAT,
                  (ULONG_PTR)&PartInfo,
                  0);
    return Result;
}

static FSVOL_OP
DoChecking(
    IN PPARTENTRY PartEntry,
    IN PVOID Context OPTIONAL,
    IN PFSVOL_CALLBACK FsVolCallback OPTIONAL)
{
    FSVOL_OP Result;
    NTSTATUS Status = STATUS_SUCCESS;
    CHECK_PARTITION_INFO PartInfo;

    ASSERT(PartEntry->IsPartitioned && PartEntry->PartitionNumber != 0);

    PartInfo.PartEntry = PartEntry;

RetryCheck:
    // Result = FsVolCallback(Context,
                           // FSVOLNOTIFY_PREPARECHECK,
                           // (ULONG_PTR)&PartInfo,
                           // FSVOL_CHECK);
    // if (Result != FSVOL_DOIT)
        // return Result;

    Result = FsVolCallback(Context,
                           FSVOLNOTIFY_STARTCHECK,
                           (ULONG_PTR)&PartInfo,
                           FSVOL_CHECK);
    if (Result != FSVOL_DOIT)
        goto EndCheck;

    /* Check the partition */
    Status = ChkdskPartition(PartEntry,
                             PartInfo.FixErrors,
                             PartInfo.Verbose,
                             PartInfo.CheckOnlyIfDirty,
                             PartInfo.ScanDrive,
                             PartInfo.Callback);
    if (!NT_SUCCESS(Status))
    {
        // PartInfo.NtPathPartition = PathBuffer;
        PartInfo.ErrorStatus = Status;

        Result = FsVolCallback(Context,
                               FSVOLNOTIFY_CHECKERROR,
                               (ULONG_PTR)&PartInfo,
                               0);
        if (Result == FSVOL_RETRY)
            goto RetryCheck;
        // else if (Result == FSVOL_ABORT || Result == FSVOL_SKIP), stop.
    }

EndCheck:
    /* This notification is always sent, even in case of error */
    PartInfo.ErrorStatus = Status;
    FsVolCallback(Context,
                  FSVOLNOTIFY_ENDCHECK,
                  (ULONG_PTR)&PartInfo,
                  0);
    return Result;
}

BOOLEAN
CommitFsVolOpsQueue(
    IN PPARTLIST PartitionList,
    IN PPARTENTRY InstallPartition,
    IN PPARTENTRY SystemPartition,
    IN PFSVOL_CALLBACK FsVolCallback OPTIONAL,
    IN PVOID Context OPTIONAL)
{
    BOOLEAN Success = TRUE; // Suppose success.
    FSVOL_OP Result;
    PPARTENTRY PartEntry;

    /* Machine state for the format step */
    typedef enum _FORMATMACHINESTATE
    {
        Start,
        FormatSystemPartition,
        FormatInstallPartition,
        FormatOtherPartition,
        FormatDone
    } FORMATMACHINESTATE;
    FORMATMACHINESTATE FormatState = Start;

    ASSERT(PartitionList && InstallPartition && SystemPartition);

    Result = FsVolCallback(Context,
                           FSVOLNOTIFY_STARTQUEUE,
                           0, 0);
    if (Result == FSVOL_ABORT)
        return FALSE;

    /*
     * Commit the Format queue
     */

    /* Reset the formatter machine state */
    FormatState = Start;

    Result = FsVolCallback(Context,
                           FSVOLNOTIFY_STARTSUBQUEUE,
                           FSVOL_FORMAT,
                           0);
    if (Result == FSVOL_ABORT)
        return FALSE;
    /** HACK!! **/
    if (Result == FSVOL_SKIP)
        goto StartCheckQueue;
    /** END HACK!! **/

    // ASSERT(SystemPartition->IsPartitioned);

NextFormat:
    PartEntry = NULL;
    switch (FormatState)
    {
        case Start:
        {
            /*
             * We start by formatting the system partition in case it is new
             * (it didn't exist before) and is not the same as the installation
             * partition. Otherwise we just require a filesystem check on it,
             * and start by formatting the installation partition instead.
             */

            ASSERT(SystemPartition->IsPartitioned);

            if ((SystemPartition != InstallPartition) &&
                (SystemPartition->FormatState == Unformatted))
            {
                PartEntry = SystemPartition;
                PartEntry->NeedsCheck = TRUE;

                // TODO: Should we let the user use a custom file-system,
                // or should we always use FAT(32) for it?
                // For "compatibility", FAT(32) would be best indeed.

                DPRINT1("FormatState: Start --> FormatSystemPartition\n");
                FormatState = FormatSystemPartition;
            }
            else
            {
                PartEntry = InstallPartition;
                PartEntry->NeedsCheck = TRUE;

                if (SystemPartition != InstallPartition)
                {
                    /* The system partition is separate, so it had better be formatted! */
                    ASSERT((SystemPartition->FormatState == Preformatted) ||
                           (SystemPartition->FormatState == Formatted));

                    /* Require a filesystem check on the system partition too */
                    SystemPartition->NeedsCheck = TRUE;
                }

                DPRINT1("FormatState: Start --> FormatInstallPartition\n");
                FormatState = FormatInstallPartition;
            }
            break;
        }

        case FormatSystemPartition:
        {
            PartEntry = InstallPartition;
            PartEntry->NeedsCheck = TRUE;

            DPRINT1("FormatState: FormatSystemPartition --> FormatInstallPartition\n");
            FormatState = FormatInstallPartition;
            break;
        }

        case FormatInstallPartition:
        case FormatOtherPartition:
        {
            if (GetNextUnformattedPartition(PartitionList,
                                            NULL,
                                            &PartEntry))
            {
                ASSERT(PartEntry);
                PartEntry->NeedsCheck = TRUE;

                if (FormatState == FormatInstallPartition)
                    DPRINT1("FormatState: FormatInstallPartition --> FormatOtherPartition\n");
                else
                    DPRINT1("FormatState: FormatOtherPartition --> FormatOtherPartition\n");

                FormatState = FormatOtherPartition;
            }
            else
            {
                if (FormatState == FormatInstallPartition)
                    DPRINT1("FormatState: FormatInstallPartition --> FormatDone\n");
                else
                    DPRINT1("FormatState: FormatOtherPartition --> FormatDone\n");

                FormatState = FormatDone;
                Success = TRUE;
                goto EndFormat;
            }
            break;
        }

        case FormatDone:
        {
            DPRINT1("FormatState: FormatDone\n");
            Success = TRUE;
            goto EndFormat;
        }

        default:
        {
            DPRINT1("FormatState: Invalid value %ld\n", FormatState);
            ASSERT(FALSE);
            Success = FALSE;
            goto Quit;
        }
    }

    ASSERT(PartEntry);
    Result = DoFormatting(PartEntry, Context, FsVolCallback);
    if (Result == FSVOL_ABORT)
    {
        Success = FALSE;
        goto Quit;
    }
    else
    {
        /* Go to the next partition to be formatted */
        goto NextFormat;
    }

EndFormat:
    FsVolCallback(Context,
                  FSVOLNOTIFY_ENDSUBQUEUE,
                  FSVOL_FORMAT,
                  0);


    /*
     * Commit the CheckFS queue
     */

StartCheckQueue:
    Result = FsVolCallback(Context,
                           FSVOLNOTIFY_STARTSUBQUEUE,
                           FSVOL_CHECK,
                           0);
    if (Result == FSVOL_ABORT)
        return FALSE;

NextCheck:
    if (!GetNextUncheckedPartition(PartitionList, NULL, &PartEntry))
    {
        Success = TRUE;
        goto EndCheck;
    }

    ASSERT(PartEntry);
    Result = DoChecking(PartEntry, Context, FsVolCallback);
    if (Result == FSVOL_ABORT)
    {
        Success = FALSE;
        goto Quit;
    }
    else
    {
        /* Go to the next partition to be checked */
        goto NextCheck;
    }

EndCheck:
    FsVolCallback(Context,
                  FSVOLNOTIFY_ENDSUBQUEUE,
                  FSVOL_CHECK,
                  0);


Quit:
    /* All the queues have been committed */
    FsVolCallback(Context,
                  FSVOLNOTIFY_ENDQUEUE,
                  Success,
                  0);
    return Success;
}


/*
 * FIXME: TODO: Setup-specific stuff; find a better place to put it!
 */
BOOLEAN
InitSystemPartition(
    IN PPARTLIST PartitionList,
    IN PPARTENTRY InstallPartition,
    OUT PPARTENTRY* pSystemPartition,
    IN PFSVOL_CALLBACK FsVolCallback OPTIONAL,
    IN PVOID Context OPTIONAL)
{
    FSVOL_OP Result;
    PPARTENTRY SystemPartition;
    PPARTENTRY OldActivePart;

    /*
     * If we install on a fixed disk, try to find a supported system
     * partition on the system. Otherwise if we install on a removable disk
     * use the install partition as the system partition.
     */
    if (InstallPartition->DiskEntry->MediaType == FixedMedia)
    {
        SystemPartition = FindSupportedSystemPartition(PartitionList,
                                                       FALSE,
                                                       InstallPartition->DiskEntry,
                                                       InstallPartition);
        /* Use the original system partition as the old active partition hint */
        OldActivePart = PartitionList->SystemPartition;

        if ( SystemPartition && PartitionList->SystemPartition &&
            (SystemPartition != PartitionList->SystemPartition) )
        {
            DPRINT1("We are using a different system partition!!\n");

            Result = FsVolCallback(Context,
                                   ChangeSystemPartition,
                                   (ULONG_PTR)SystemPartition,
                                   0);
            if (Result != FSVOL_DOIT)
                return FALSE;
        }
    }
    else // if (InstallPartition->DiskEntry->MediaType == RemovableMedia)
    {
        SystemPartition = InstallPartition;
        /* Don't specify any old active partition hint */
        OldActivePart = NULL;
    }

    if (!SystemPartition)
    {
        FsVolCallback(Context,
                      SystemPartitionError,
                      ERROR_SYSTEM_PARTITION_NOT_FOUND,
                      0);
        return FALSE;
    }

    *pSystemPartition = SystemPartition;

    /*
     * If the system partition can be created in some
     * non-partitioned space, create it now.
     */
    if (!SystemPartition->IsPartitioned)
    {
        CreatePrimaryPartition(PartitionList,
                               SystemPartition,
                               0LL, // SystemPartition->SectorCount.QuadPart,
                               TRUE);
        ASSERT(SystemPartition->IsPartitioned);
    }

    /* Set it as such */
    if (!SetActivePartition(PartitionList, SystemPartition, OldActivePart))
    {
        DPRINT1("SetActivePartition(0x%p) failed?!\n", SystemPartition);
        ASSERT(FALSE);
    }

    /* Commit all partition changes to all the disks */
    if (!WritePartitionsToDisk(PartitionList))
    {
        DPRINT("WritePartitionsToDisk() failed\n");
        /* Result = */ FsVolCallback(Context,
                            SystemPartitionError,
                            STATUS_PARTITION_FAILURE,
                            0);
        return FALSE;
    }

//
// FIXME: TODO: Do we do the following here, or in the caller ??
//
    /*
     * In all cases, whether or not we are going to perform a formatting,
     * we must perform a filesystem check of both the system and the
     * installation partitions.
     */
    InstallPartition->NeedsCheck = TRUE;
    if (SystemPartition != InstallPartition)
        SystemPartition->NeedsCheck = TRUE;

    return TRUE;
}

/* EOF */
