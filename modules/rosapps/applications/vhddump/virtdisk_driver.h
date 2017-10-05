
#ifndef __VIRTDISK_XXXXXX_H__
#define __VIRTDISK_XXXXXX_H__

#pragma once

/* PSDK/NDK Headers */
// #define WIN32_NO_STATUS
// #include <windef.h>
// #include <winbase.h>
// #include <winnt.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
// #include <ndk/rtltypes.h>





/*** See e.g. vhdlib.h ... ***/

// FIXME everywhere: do NOT use EXPLICIT casts to ULONG!
#ifndef ROUND_DOWN
// #define ROUND_DOWN(n, align) (((ULONG)n) & ~((align) - 1l))
#define ROUND_DOWN(n, align) ((n) & ~((align) - 1))
#endif

#ifndef ROUND_UP
// #define ROUND_UP(n, align) ROUND_DOWN(((ULONG)n) + (align) - 1, (align))
#define ROUND_UP(n, align) ROUND_DOWN((n) + (align) - 1, (align))
#endif

// FIXME in ntoskrnl: use ULONG_PTR instead of ULONG64!
#define IS_ALIGNED(n, align) (((n) & ((align) - 1)) == 0)







// A virtual disk support provider for the specified file was not found.
#ifndef STATUS_VIRTDISK_PROVIDER_NOT_FOUND
#define STATUS_VIRTDISK_PROVIDER_NOT_FOUND              0xC03A0014
#endif

// The specified disk is not a virtual disk.
#ifndef STATUS_VIRTDISK_NOT_VIRTUAL_DISK
#define STATUS_VIRTDISK_NOT_VIRTUAL_DISK                0xC03A0015
#endif

// The chain of virtual hard disks is inaccessible. The process has not been
// granted access rights to the parent virtual hard disk for the differencing disk.
#ifndef STATUS_VHD_PARENT_VHD_ACCESS_DENIED
#define STATUS_VHD_PARENT_VHD_ACCESS_DENIED             0xC03A0016
#endif

// The chain of virtual hard disks is corrupted. There is a mismatch
// in the virtual sizes of the parent virtual hard disk and differencing disk.
#ifndef STATUS_VHD_CHILD_PARENT_SIZE_MISMATCH
#define STATUS_VHD_CHILD_PARENT_SIZE_MISMATCH           0xC03A0017
#endif

// The chain of virtual hard disks is corrupted. A differencing disk
// is indicated in its own parent chain.
#ifndef STATUS_VHD_DIFFERENCING_CHAIN_CYCLE_DETECTED
#define STATUS_VHD_DIFFERENCING_CHAIN_CYCLE_DETECTED    0xC03A0018
#endif

// The chain of virtual hard disks is inaccessible. There was an error
// opening a virtual hard disk further up the chain.
#ifndef STATUS_VHD_DIFFERENCING_CHAIN_ERROR_IN_PARENT
#define STATUS_VHD_DIFFERENCING_CHAIN_ERROR_IN_PARENT   0xC03A0019
#endif

typedef struct _VIRTUAL_DISK *PVIRTUAL_DISK;

typedef struct _VIRTUAL_DISK_VTBL
{
    NTSTATUS (NTAPI *Probe)(
        IN HANDLE FileHandle,
        IN ULONG  FileSize); // FIXME!

    NTSTATUS (NTAPI *CreateDisk)(
        IN OUT PVIRTUAL_DISK VirtualDisk,
        // IN VHD_TYPE Type,
        IN UINT64 FileSize,
        IN ULONG    BlockSizeInBytes,  // UINT32
        IN ULONG    SectorSizeInBytes // UINT32 // I think it's useless, because for VHDs it can only be == 512 bytes == VHD_SECTOR_SIZE
        // IN PGUID    UniqueId,
        // IN ULONG    TimeStamp,
        // IN CHAR  CreatorApp[4],
        // IN ULONG CreatorVersion,
        // IN CHAR  CreatorHostOS[4]
        );

    NTSTATUS (NTAPI *OpenDisk)(
        IN OUT PVIRTUAL_DISK VirtualDisk,
        IN UINT64 FileSize,
        // IN BOOLEAN  CreateNew,
        IN BOOLEAN  ReadOnly);
    NTSTATUS (NTAPI *FlushDisk)(
        IN PVIRTUAL_DISK VirtualDisk);
    VOID    // or NTSTATUS, to return whether or not the disk is still used??
    (NTAPI *CloseDisk)(
        IN PVIRTUAL_DISK VirtualDisk);

    ULONG (NTAPI *GetAlignment)(
        IN PVIRTUAL_DISK VirtualDisk);

    NTSTATUS (NTAPI *ReadDiskAligned)(
        IN PVIRTUAL_DISK VirtualDisk,
        IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
        OUT PVOID   Buffer,
        IN  SIZE_T  Length,
        OUT PSIZE_T ReadLength OPTIONAL);
    NTSTATUS (NTAPI *WriteDiskAligned)(
        IN PVIRTUAL_DISK VirtualDisk,
        IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
        IN  PVOID   Buffer,
        IN  SIZE_T  Length,
        OUT PSIZE_T WrittenLength OPTIONAL);

    UINT64 (NTAPI *GetVirtTotalSize)(
        IN PVIRTUAL_DISK VirtualDisk);
    // UINT64 (NTAPI *GetFileSize)(
        // IN PVIRTUAL_DISK VirtualDisk);

    NTSTATUS (NTAPI *CompactDisk)(
        IN PVIRTUAL_DISK VirtualDisk);
    NTSTATUS (NTAPI *ExpandDisk)(
        IN PVIRTUAL_DISK VirtualDisk);
    NTSTATUS (NTAPI *RepairDisk)(
        IN PVIRTUAL_DISK VirtualDisk);
} VIRTUAL_DISK_VTBL, *PVIRTUAL_DISK_VTBL;


#if 0

typedef struct _VIRTUAL_DISK_IMAGE
{
    PVIRTUAL_DISK_IMAGE ParentImage; // Can be NULL; the last image in the chain has it NULL.
    // LIST_ENTRY ChildrenImagesList;

    UNICODE_STRING ImageFileName;
    HANDLE FileHandle;

    // GUIDs, FILETIMEs ...
    // PVIRTUAL_DISK VirtualDisk; // Back-pointer to the VDisk maintaining this image

    // ImageType == Base (fixed, dynamic, any other stuff that is not a diff/snapshot) ; Differencing (or snapshot)
    // General rule: a 'Base' image cannot have parents. A 'Differencing' / snapshot must have parents...

    ULONG Alignment;

    PVOID BackendData;
    PVIRTUAL_DISK_VTBL Backend;
} VIRTUAL_DISK_IMAGE, *PVIRTUAL_DISK_IMAGE;

#endif


typedef struct _VIRTUAL_DISK
{
    // PVIRTUAL_DISK_IMAGE PrincipalImage; // BaseImage;
    // LIST_ENTRY ImagesList;   // hmmm... ?
    // ULONG Alignment;
    // Geometry ?
    // UINT64 VirtualTotalSize;


    PVIRTUAL_DISK ParentDisk; // Can be NULL; the last disk in the chain has it NULL.
    // LIST_ENTRY ChildDisksList;

    UNICODE_STRING DiskFileName;
    HANDLE FileHandle;

    ULONG Alignment;

    PVOID BackendData;
    PVIRTUAL_DISK_VTBL Backend;
} VIRTUAL_DISK, *PVIRTUAL_DISK;


PVOID NTAPI
VdpAlloc(IN SIZE_T Size,
         IN ULONG Flags,
         IN ULONG Tag);

VOID NTAPI
VdpFree(IN PVOID Ptr,
        IN ULONG Flags,
        IN ULONG Tag);


NTSTATUS NTAPI
VdpReadFile(IN  HANDLE FileHandle,
            IN  PLARGE_INTEGER FileOffset,
            OUT PVOID   Buffer,
            IN  SIZE_T  Length,
            OUT PSIZE_T ReadLength OPTIONAL);

NTSTATUS NTAPI
VdpWriteFile(IN  HANDLE FileHandle,
             IN  PLARGE_INTEGER FileOffset,
             IN  PVOID   Buffer,
             IN  SIZE_T  Length,
             OUT PSIZE_T WrittenLength OPTIONAL);

NTSTATUS NTAPI
VdpSetFileSize(IN HANDLE FileHandle,
               IN ULONG FileSize,     // SIZE_T
               IN ULONG OldFileSize); // SIZE_T

NTSTATUS NTAPI
VdpFlushFile(IN HANDLE FileHandle,
             IN PLARGE_INTEGER FileOffset,
             IN ULONG Length);


NTSTATUS
NTAPI
VdCreateDisk(
    IN OUT PVIRTUAL_DISK* VirtualDisk,
    IN PUNICODE_STRING DiskFileName,
    IN UINT64 FileSize);

NTSTATUS
NTAPI
VdOpenDisk(
    IN OUT PVIRTUAL_DISK* VirtualDisk,
    IN PUNICODE_STRING DiskFileName,
    // IN BOOLEAN  CreateNew,
    IN BOOLEAN  ReadOnly);

NTSTATUS
NTAPI
VdFlushDisk(
    IN PVIRTUAL_DISK VirtualDisk);

VOID    // or NTSTATUS, to return whether or not the disk is still used??
NTAPI
VdCloseDisk(
    IN PVIRTUAL_DISK VirtualDisk);

NTSTATUS
NTAPI
VdReadDiskAligned(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL);

NTSTATUS
NTAPI
VdReadDisk(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL);

NTSTATUS
NTAPI
VdWriteDiskAligned(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL);

NTSTATUS
NTAPI
VdWriteDisk(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL);

UINT64
NTAPI
VdGetVirtTotalSize(
    IN PVIRTUAL_DISK VirtualDisk);

// UINT64
// NTAPI
// VdGetFileSize(
    // IN PVIRTUAL_DISK VirtualDisk);

NTSTATUS
NTAPI
VdCompactDisk(
    IN PVIRTUAL_DISK VirtualDisk);

NTSTATUS
NTAPI
VdExpandDisk(
    IN PVIRTUAL_DISK VirtualDisk);

NTSTATUS
NTAPI
VdMergeDisks(
    IN PVIRTUAL_DISK VirtualDisk1,
    IN PVIRTUAL_DISK VirtualDisk2);

NTSTATUS
NTAPI
VdRepairDisk(
    IN PVIRTUAL_DISK VirtualDisk);

#endif /* __VIRTDISK_XXXXXX_H__ */
