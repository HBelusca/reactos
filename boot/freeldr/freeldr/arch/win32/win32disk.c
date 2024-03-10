/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     Dual-licensed:
 *              GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Disk Access Functions
 * COPYRIGHT:   Copyright 2024-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <win32ldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define FIRST_BIOS_DISK 0x80
#define FIRST_PARTITION 1

typedef struct tagDISKCONTEXT
{
    UCHAR DriveNumber;
    ULONG SectorSize;
    ULONGLONG SectorOffset;
    ULONGLONG SectorCount;
    ULONGLONG SectorNumber;
} DISKCONTEXT;

/* GLOBALS *******************************************************************/

/* Made to match BIOS */
static UCHAR PcBiosDiskCount;
static CHAR PcDiskIdentifier[32][20];

PVOID DiskReadBuffer;
SIZE_T DiskReadBufferSize;

UCHAR FrldrBootDrive;
ULONG FrldrBootPartition;

/* FUNCTIONS *****************************************************************/

PCHAR
GetHarddiskIdentifier(UCHAR DriveNumber)
{
    TRACE("GetHarddiskIdentifier: DriveNumber: %d\n", DriveNumber);
    return PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK];
}

/* For disk.c!DiskError() */
PCSTR
DiskGetErrorCodeString(
    _In_ ULONG ErrorCode)
{
    return NULL;
}

/* FUNCTIONS *****************************************************************/

static
ARC_STATUS
Win32DiskClose(
    _In_ ULONG FileId)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    FrLdrTempFree(Context, TAG_HW_DISK_CONTEXT);
    return ESUCCESS;
}

static
ARC_STATUS
Win32DiskGetFileInformation(
    _In_ ULONG FileId,
    _Out_ FILEINFORMATION* Information)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    RtlZeroMemory(Information, sizeof(*Information));

    /*
     * The ARC specification mentions that for partitions, StartingAddress and
     * EndingAddress are the start and end positions of the partition in terms
     * of byte offsets from the start of the disk.
     * CurrentAddress is the current offset into (i.e. relative to) the partition.
     */
    Information->StartingAddress.QuadPart = Context->SectorOffset * Context->SectorSize;
    Information->EndingAddress.QuadPart   = (Context->SectorOffset + Context->SectorCount) * Context->SectorSize;
    Information->CurrentAddress.QuadPart  = Context->SectorNumber * Context->SectorSize;

    return ESUCCESS;
}

static
ARC_STATUS
Win32DiskOpen(
    _In_ PCHAR Path,
    _In_ OPENMODE OpenMode,
    _Out_ PULONG FileId)
{
    DISKCONTEXT* Context;
    UCHAR DriveNumber;
    ULONG DrivePartition;
    ULONG SectorSize = 0;
    ULONGLONG SectorOffset = 0;
    ULONGLONG SectorCount = 0;

    TRACE("Win32DiskOpen: File ID: %d, Path: %s\n", FileId, Path);

    if (!DissectArcPath(Path, NULL, &DriveNumber, &DrivePartition))
        return EINVAL;

    TRACE("Opening disk: DriveNumber: %d, DrivePartition: %d\n", DriveNumber, DrivePartition);

    //
    // Win32: We only support drive 0x80, partition 1, that
    // redirects to the current directory of the freeldr process.
    //
    if ((DriveNumber != FIRST_BIOS_DISK) && (DrivePartition != 1))
        return EINVAL;

    Context = FrLdrTempAlloc(sizeof(DISKCONTEXT), TAG_HW_DISK_CONTEXT);
    if (!Context)
        return ENOMEM;

    Context->DriveNumber = DriveNumber;
    Context->SectorSize = SectorSize;
    Context->SectorOffset = SectorOffset;
    Context->SectorCount = SectorCount;
    Context->SectorNumber = 0;
    FsSetDeviceSpecific(*FileId, Context);
    return ESUCCESS;
}

static
ARC_STATUS
Win32DiskRead(
    _In_ ULONG FileId,
    _Out_ PVOID Buffer,
    _In_ ULONG N,
    _Out_ PULONG Count)
{
    // DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    //
    // Win32: We do not support direct disk reads.
    //
    return EIO;
}

static
ARC_STATUS
Win32DiskSeek(
    _In_ ULONG FileId,
    _In_ LARGE_INTEGER* Position,
    _In_ SEEKMODE SeekMode)
{
    // DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    //
    // Win32: We do not support direct disk seeks.
    //
    return EINVAL;
}

static const DEVVTBL Win32DiskVtbl =
{
    Win32DiskClose,
    Win32DiskGetFileInformation,
    Win32DiskOpen,
    Win32DiskRead,
    Win32DiskSeek,
};

static VOID
GetHarddiskInformation(
    _In_ UCHAR DriveNumber)
{
    PCHAR Identifier = PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK];
    //ARC_STATUS Status;
    ULONG Checksum, Signature;
    BOOLEAN ValidPartitionTable;
    //CHAR DiskName[64];
    CHAR ArcName[MAX_PATH];

    //RtlStringCbPrintfA(DiskName, sizeof(DiskName),
    RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                       "multi(0)disk(0)rdisk(%u)",
                       DriveNumber - FIRST_BIOS_DISK);

    DiskReportError(FALSE);
    //Status = DiskInitialize(DriveNumber, DiskName, DiskPeripheral, &Win32DiskVtbl,
    //                        &Checksum, &Signature, &ValidPartitionTable);
    Checksum  = (ULONG)-1;
    Signature = (ULONG)-1;
    ValidPartitionTable = TRUE;
    AddReactOSArcDiskInfo(ArcName, NULL, Signature, Checksum, ValidPartitionTable);

    RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                       "multi(0)disk(0)rdisk(%u)partition(0)",
                       DriveNumber - FIRST_BIOS_DISK);
    FsRegisterDevice(ArcName, &Win32DiskVtbl);

    /* Add partitions */
    RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                       "multi(0)disk(0)rdisk(%u)partition(%lu)",
                       DriveNumber - FIRST_BIOS_DISK, FIRST_PARTITION);
    FsRegisterDevice(ArcName, &Win32DiskVtbl);

    DiskReportError(TRUE);

    //if (Status != ESUCCESS)
    //{
    //    /* The disk failed to be initialized, use a default identifier */
    //    RtlStringCbPrintfA(Identifier, 20, "BIOSDISK%u", DriveNumber - FIRST_BIOS_DISK + 1);
    //    return;
    //}

    /* Win32: use a default identifier */
    RtlStringCbPrintfA(Identifier, 20, "BIOSDISK%u", DriveNumber - FIRST_BIOS_DISK + 1);
}

BOOLEAN
Win32InitializeBootDevices(VOID)
{
    DiskReadBufferSize = PAGE_SIZE;
    DiskReadBuffer = MmAllocateMemoryWithType(DiskReadBufferSize,
                                              LoaderFirmwareTemporary);
    if (!DiskReadBuffer)
    {
        ERR("Failed to allocate disk read buffer (buffer size = 0x%X)\n",
            DiskReadBufferSize);
        return FALSE;
    }

    /* Hardcode our boot device, that we will redirect to
     * the current directory for the current process */
    FrldrBootDrive = FIRST_BIOS_DISK;
    FrldrBootPartition = FIRST_PARTITION;

    PcBiosDiskCount = 0;

    //
    // Win32: We support only one single virtual hard disk
    //
    PcBiosDiskCount++;
    GetHarddiskInformation(0 + FIRST_BIOS_DISK);

    /* Initialize FrLdrBootPath, the boot path we're booting from (the "SystemPartition") */
    RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                       "multi(0)disk(0)rdisk(%u)partition(%lu)",
                       FrldrBootDrive - FIRST_BIOS_DISK, FrldrBootPartition);

    return TRUE;
}

UCHAR
Win32GetFloppyCount(VOID)
{
    /* No floppy support for now... */
    return 0;
}

BOOLEAN
Win32DiskReadLogicalSectors(
    _In_ UCHAR DriveNumber,
    _In_ ULONGLONG SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer)
{
    ERR("Win32DiskReadLogicalSectors(%u, %I64u, %lu, 0x%p) is UNIMPLEMENTED\n",
        DriveNumber, SectorNumber, SectorCount, Buffer);

    if (DriveNumber != FIRST_BIOS_DISK)
        return FALSE;

    /* Only support sector 0 reading (1 sector) */
    if (SectorNumber != 0ULL || SectorCount > 1)
        return FALSE;

    /* Zero out the output buffer */
    if (SectorCount > 0)
        RtlZeroMemory(Buffer, SectorCount * 512); // 512-byte sector
    return TRUE;
}

BOOLEAN
Win32DiskGetDriveGeometry(
    _In_ UCHAR DriveNumber,
    _Out_ PGEOMETRY Geometry)
{
    ERR("Win32DiskGetDriveGeometry(%u, 0x%p) is UNIMPLEMENTED\n",
        DriveNumber, Geometry);

    if (DriveNumber != FIRST_BIOS_DISK)
        return FALSE;

    /* Return a fake hardcoded geometry */
    Geometry->Cylinders = 1;
    Geometry->Heads = 1;
    Geometry->SectorsPerTrack = (ULONG)(4 * 1024 * 1024 / 512); // 4 MB in 512-byte sectors
    Geometry->BytesPerSector = 512;
    Geometry->Sectors = Geometry->SectorsPerTrack;
    return TRUE;
}

ULONG
Win32DiskGetCacheableBlockCount(
    _In_ UCHAR DriveNumber)
{
    ERR("Win32DiskGetCacheableBlockCount(%u) is UNIMPLEMENTED\n", DriveNumber);
    return 0;
}

/* EOF */
