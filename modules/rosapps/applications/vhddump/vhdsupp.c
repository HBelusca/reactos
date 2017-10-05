
#include "vhddump.h"
#include "vhdlib.h"
#include "virtdisk_driver.h"

#include <ndk/kefuncs.h>

#define NDEBUG
#include <debug.h>

/* Ripped from RPC headers */
#define RPCRTAPI DECLSPEC_IMPORT
#ifndef __ROS_LONG64__
typedef long RPC_STATUS;
#else
typedef int RPC_STATUS;
#endif
#define RPC_ENTRY __stdcall // WINAPI
RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreate(UUID*);

#if 0

// #include <uuid/uuid.h>
// void uuid_generate(uuid_t out);
// void uuid_generate_random(uuid_t out);
// void uuid_generate_time(uuid_t out);
// int uuid_generate_time_safe(uuid_t out);

typedef GUID UUID;
#define uuid_t UUID
// typedef UUID uuid_t;
void uuid_generate(uuid_t* guid);


/**
 * FIXME: This should be inside a library, the latter being platform-dependent
 * and/or provide a fallback default implementation.
 **/
// Taken from reactos/sdk/lib/rtl/actctx.c, line l3690.
// See also reactos/ntoskrnl/ex/uuid.c,
// reactos/dll/win32/rpcrt4/rpcrt4_main.c (UuidCreate)
void uuid_generate(uuid_t* guid)
{
    static ULONG seed = 0;

    ULONG *ptr = (ULONG*)guid;
    int i;

    /* GUID is 16 bytes long */
    for (i = 0; i < sizeof(GUID)/sizeof(ULONG); i++, ptr++)
        *ptr = RtlUniform(&seed);

    ++seed;

    guid->Data3 &= 0x0fff;
    guid->Data3 |= (4 << 12);
    guid->Data4[0] &= 0x3f;
    guid->Data4[0] |= 0x80;
}

#endif


typedef struct _VHD_BACKEND
{
    VHDFILE VhdFile;
    // TODO: Other VHD-specific stuff
    PVIRTUAL_DISK Disk;
    // PVIRTUAL_DISK_IMAGE Image; // Back-pointer to the corresponding VDisk image
} VHD_BACKEND, *PVHD_BACKEND;

VIRTUAL_DISK_VTBL VhdVtbl;

// PVHD_ALLOCATE_ROUTINE
// PVHD_FREE_ROUTINE

// PVHD_FILE_READ_ROUTINE
static
NTSTATUS NTAPI
VhdpReadFile(IN  PVHDFILE Disk,
             IN  PLARGE_INTEGER FileOffset,
             OUT PVOID   Buffer,
             IN  SIZE_T  Length,
             OUT PSIZE_T ReadLength OPTIONAL)
{
    PVHD_BACKEND pDisk = (PVHD_BACKEND)Disk;
    /* Fallback to the VirtDisk function */
    return VdpReadFile(pDisk->Disk->FileHandle, FileOffset, Buffer, Length, ReadLength);
}

// PVHD_FILE_WRITE_ROUTINE
static
NTSTATUS NTAPI
VhdpWriteFile(IN  PVHDFILE Disk,
              IN  PLARGE_INTEGER FileOffset,
              IN  PVOID   Buffer,
              IN  SIZE_T  Length,
              OUT PSIZE_T WrittenLength OPTIONAL)
{
    PVHD_BACKEND pDisk = (PVHD_BACKEND)Disk;
    /* Fallback to the VirtDisk function */
    return VdpWriteFile(pDisk->Disk->FileHandle, FileOffset, Buffer, Length, WrittenLength);
}

// PVHD_FILE_SET_SIZE_ROUTINE
static
NTSTATUS NTAPI
VhdpSetFileSize(IN PVHDFILE Disk,
                IN ULONG FileSize,    // SIZE_T
                IN ULONG OldFileSize) // SIZE_T
{
    PVHD_BACKEND pDisk = (PVHD_BACKEND)Disk;
    /* Fallback to the VirtDisk function */
    return VdpSetFileSize(pDisk->Disk->FileHandle, FileSize, OldFileSize);
}

// PVHD_FILE_FLUSH_ROUTINE
static
NTSTATUS NTAPI
VhdpFlushFile(IN PVHDFILE Disk,
              IN PLARGE_INTEGER FileOffset,
              IN ULONG Length)
{
    PVHD_BACKEND pDisk = (PVHD_BACKEND)Disk;
    /* Fallback to the VirtDisk function */
    return VdpFlushFile(pDisk->Disk->FileHandle, FileOffset, Length);
}


static NTSTATUS NTAPI
VhdProbeFile(
    IN HANDLE FileHandle,
    IN ULONG  FileSize) // FIXME!
{
    NTSTATUS Status;
    LARGE_INTEGER FileOffset;
    ULONG BytesToRead;
    VHD_FOOTER Footer;

    /*
     * Go to the end of the file and read the footer.
     * As the footer might be 511-byte large only (as found in VHD disks
     * made with versions prior to MS Virtual PC 2004) instead of 512 bytes,
     * check for 511-byte only & round it down to a VHD sector size.
     */
    FileOffset.QuadPart = ROUND_DOWN(FileSize - sizeof(VHD_FOOTER) + 1, VHD_SECTOR_SIZE);
    BytesToRead = sizeof(VHD_FOOTER);
    Status = VdpReadFile(FileHandle,
                         &FileOffset,
                         &Footer,
                         BytesToRead,
                         &BytesToRead);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("VdpReadFile() failed when reading HDD footer (Status 0x%08lx)\n", Status);
        return Status;
    }

    if (RtlCompareMemory(Footer.creator, "conectix",
                         sizeof(Footer.creator)) == sizeof(Footer.creator))
    {
        return STATUS_SUCCESS;
    }

    DPRINT1("VHD possibly corrupted, try again...\n");

    /*
     * The file image footer may be corrupted, try to find one at the beginning.
     */
    FileOffset.QuadPart = 0LL;
    BytesToRead = sizeof(VHD_FOOTER);
    Status = VdpReadFile(FileHandle,
                         &FileOffset,
                         &Footer,
                         BytesToRead,
                         &BytesToRead);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("VdpReadFile() failed when reading HDD header (Status 0x%08lx)\n", Status);
        return Status;
    }

    if (RtlCompareMemory(Footer.creator, "conectix",
                         sizeof(Footer.creator)) == sizeof(Footer.creator))
    {
        /* We succeeded, but this means the footer was corrupted */
        return STATUS_SUCCESS;
    }

    DPRINT1("OpenHDD: Invalid HDD image (expected VHD).");
    return STATUS_UNRECOGNIZED_MEDIA; // STATUS_INVALID_IMAGE_FORMAT;
}

static NTSTATUS NTAPI
VhdCreateFile(
    IN OUT PVIRTUAL_DISK VirtualDisk,
    // IN VHD_TYPE Type,
    IN UINT64 FileSize,
    IN ULONG    BlockSizeInBytes,  // UINT32
    IN ULONG    SectorSizeInBytes // UINT32 // I think it's useless, because for VHDs it can only be == 512 bytes == VHD_SECTOR_SIZE
    // IN PGUID    UniqueId,
    // IN ULONG    TimeStamp,
    )
{
    NTSTATUS Status;
    PVHD_BACKEND Backend;
    LARGE_INTEGER SystemTime;
    GUID  UniqueId;
    ULONG TimeStamp;

    ASSERT(VirtualDisk);

    Backend = VdpAlloc(sizeof(*Backend), HEAP_ZERO_MEMORY, 0);
    if (!Backend)
        return STATUS_NO_MEMORY;

    Backend->Disk = VirtualDisk;

    // ExUuidCreate
    UuidCreate(&UniqueId);

    /*
     * Obtain a timestamp in number of seconds since Jan 1, 2000 0:00:00 (UTC)
     */
    NtQuerySystemTime(&SystemTime);
    RtlTimeToSecondsSince1970(&SystemTime, &TimeStamp);
    TimeStamp -= VHD_TIMESTAMP_BASE;

    Status = VhdCreateDisk(&Backend->VhdFile,
                           VHD_DYNAMIC, // VHD_FIXED,
                           FALSE,
                           FileSize, /// TotalSize , or MaximumSize
                           BlockSizeInBytes,
                           SectorSizeInBytes,
                           &UniqueId,
                           TimeStamp,
                           NULL, 0, NULL, NULL, 0,
                           "vpc sd",
                           MAKELONG(3, 5), // Version of Virtual PC 2007 (5.3)
                           "Wi2k",
                           VdpAlloc,  // Use the VirtDisk function
                           VdpFree,   // Use the VirtDisk function
                           VhdpSetFileSize,
                           VhdpWriteFile,
                           VhdpReadFile,
                           VhdpFlushFile);
    if (!NT_SUCCESS(Status))
    {
        VdpFree(Backend, 0, 0);
        return Status;
    }

    VirtualDisk->BackendData = Backend;
    VirtualDisk->Backend = &VhdVtbl;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
VhdOpenFile(
    IN OUT PVIRTUAL_DISK VirtualDisk,
    IN UINT64 FileSize,
    // IN BOOLEAN  CreateNew,
    IN BOOLEAN  ReadOnly)
{
    NTSTATUS Status;
    PVHD_BACKEND Backend;

    ASSERT(VirtualDisk);

    Backend = VdpAlloc(sizeof(*Backend), HEAP_ZERO_MEMORY, 0);
    if (!Backend)
        return STATUS_NO_MEMORY;

    Backend->Disk = VirtualDisk;

    Status = VhdOpenDisk(&Backend->VhdFile,
                         FileSize,
                         ReadOnly,
                         VdpAlloc,  // Use the VirtDisk function
                         VdpFree,   // Use the VirtDisk function
                         VhdpSetFileSize,
                         VhdpWriteFile,
                         VhdpReadFile,
                         VhdpFlushFile);
    if (!NT_SUCCESS(Status))
    {
        VdpFree(Backend, 0, 0);
        return Status;
    }

    VirtualDisk->BackendData = Backend;
    VirtualDisk->Backend = &VhdVtbl;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
VhdFlushFile(
    IN PVIRTUAL_DISK VirtualDisk)
{
    PVHD_BACKEND pDisk = VirtualDisk->BackendData;
    return VhdFlushDisk(&pDisk->VhdFile);
}

static VOID    // or NTSTATUS, to return whether or not the disk is still used??
NTAPI
VhdCloseFile(
    IN PVIRTUAL_DISK VirtualDisk)
{
    PVHD_BACKEND pDisk = VirtualDisk->BackendData;
    VhdCloseDisk(&pDisk->VhdFile);
    VdpFree(pDisk, 0, 0);
    VirtualDisk->BackendData = NULL;
    VirtualDisk->Backend = NULL;
    // return STATUS_SUCCESS;
}

static ULONG NTAPI
VhdGetFileAlignment(
    IN  PVIRTUAL_DISK VirtualDisk)
{
    return VhdGetAlignmentBytes();
}

static NTSTATUS NTAPI
VhdReadFileAligned(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    PVHD_BACKEND pDisk = VirtualDisk->BackendData;
    return VhdReadDiskAligned(&pDisk->VhdFile,
                              ByteOffset,
                              Buffer,
                              Length,
                              ReadLength);
}

static NTSTATUS NTAPI
VhdWriteFileAligned(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    PVHD_BACKEND pDisk = VirtualDisk->BackendData;
    return VhdWriteDiskAligned(&pDisk->VhdFile,
                               ByteOffset,
                               Buffer,
                               Length,
                               WrittenLength);
}

static UINT64 NTAPI
_VhdGetVirtTotalSize(
    IN PVIRTUAL_DISK VirtualDisk)
{
    PVHD_BACKEND pDisk = VirtualDisk->BackendData;
    return VhdGetVirtTotalSize(&pDisk->VhdFile);
}

#if 0

static UINT64 NTAPI
VhdGetFileSize(
    IN PVIRTUAL_DISK VirtualDisk)
{
    PVHD_BACKEND pDisk = VirtualDisk->BackendData;
    return VhdGetFileSize(&pDisk->VhdFile);
}

static NTSTATUS NTAPI
VdCompactDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS NTAPI
VdExpandDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS NTAPI
VdRepairDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    return STATUS_NOT_IMPLEMENTED;
}

#endif

VIRTUAL_DISK_VTBL VhdVtbl =
{
    VhdProbeFile,

    VhdCreateFile,
    VhdOpenFile,
    VhdFlushFile,
    VhdCloseFile,

    VhdGetFileAlignment,

    VhdReadFileAligned,
    VhdWriteFileAligned,

    _VhdGetVirtTotalSize,
    // VhdGetFileSize,

    NULL,   // VhdCompactDisk
    NULL,   // VhdExpandDisk
    NULL,   // VhdRepairDisk
};
