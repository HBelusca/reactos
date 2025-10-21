/*
 * PROJECT:     FreeLoader
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Abstraction for disk/block IO devices.
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define TAG_DISK_DEVICE     'ksiD' // 'oIkB'
#define TAG_PART_DEVICE     'traP'
#define TAG_DISK_CONTEXT    'xCkD'

/* Driver-specific disk device object extension */
typedef struct _DDISKDEVICE
{
    CONFIGURATION_TYPE DeviceType;
    GEOMETRY Geometry;
    ULONG SectorSize;       // == Geometry.BytesPerSector
    ULONGLONG SectorCount;  // == Geometry.Sectors
    ULONGLONG SectorOffset; // Always 0 (whole disk) -- FIXME: Remove?
    PVOID Context; // For "miniports"
    PBLOCK_INIT DevInit;
    PBLOCK_UNINIT DevUninit;
    PBLOCK_READ ReadBlocks;
    PBLOCK_WRITE WriteBlocks;
/** Partition support */
    PARTITION_STYLE PartitionStyle;
} DISKDEVICE, *PDISKDEVICE; // BLOCK_IO_MEDIA

#if 0
/* Partition device object extension */
typedef struct _PARTDEVICE
{
    PDISKDEVICE Device;     ///< Block device where this partition lies.
    ULONGLONG SectorCount;  ///< Partition size in sectors.
    ULONGLONG SectorOffset; ///< Disk offset (in sectors) where the partition begins.
} PARTDEVICE, *PPARTDEVICE;
// NOTE: Idea based on SFI port: have _one_ partition device for _one_ disk device,
// where this partition device describes the set of partitions found on the disk.
#endif

/* Per-handle data for reads/writes */
typedef struct _DISKCONTEXT
{
    PDISKDEVICE Device;
    ULONGLONG SectorOffset; ///< Disk offset (in sectors) where the area begins.
    ULONGLONG SectorCount;  ///< Accessible area size in sectors.
    ULONGLONG SectorNumber;
} DISKCONTEXT, *PDISKCONTEXT;


/* FUNCTIONS *****************************************************************/

static ARC_STATUS
DiskClose(
    _In_ ULONG FileId)
{
    PDISKCONTEXT Context = FsGetDeviceSpecific(FileId);
    PDISKDEVICE Device = Context->Device;
    //if (????) // RefCount reaches zero, etc.
        Device->DevUninit(Device->Context);
    FrLdrTempFree(Context, TAG_DISK_CONTEXT);
    return ESUCCESS;
}

static ARC_STATUS
DiskGetFileInformation(
    _In_ ULONG FileId,
    _Out_ FILEINFORMATION* Information)
{
    PDISKCONTEXT Context = FsGetDeviceSpecific(FileId);
    PDISKDEVICE Device = Context->Device;

    RtlZeroMemory(Information, sizeof(*Information));

    /*
     * The ARC specification mentions that for partitions, StartingAddress and
     * EndingAddress are the start and end positions of the partition in terms
     * of byte offsets from the start of the disk.
     * CurrentAddress is the current offset into (i.e. relative to) the partition.
     */
    Information->StartingAddress.QuadPart = /*Device*/Context->SectorOffset * Device->SectorSize;
    Information->EndingAddress.QuadPart   = (/*Device*/Context->SectorOffset + /*Device*/Context->SectorCount) * Device->SectorSize;
    Information->CurrentAddress.QuadPart  = Context->SectorNumber * Device->SectorSize;

    Information->Type = Device->DeviceType;

    return ESUCCESS;
}

static ARC_STATUS
DiskOpen(
    _In_ CHAR* Path,
    _In_ OPENMODE OpenMode,
    _Inout_ ULONG* FileId)
{
    PDISKDEVICE Device = FsGetDeviceData(*FileId);
    PDISKCONTEXT Context;
    ULONGLONG SectorOffset = 0;
    ULONGLONG SectorCount = 0;

    ULONG AdapterNumber, ControllerNumber, DriveNumber, DrivePartition;
    ULONG PathSyntax;

    if (!Device)
    {
        ERR("DiskOpen(): Device is NULL, something is wrong.\n");
        ASSERT(FALSE);
        return ENODEV;
    }

    /* Parse ARC path */
    if (!DissectArcPath2(Path, &AdapterNumber, &ControllerNumber,
                         &DriveNumber, &DrivePartition, &PathSyntax))
    {
        return ENODEV;
    }
ERR("DissectArcPath2(%s):\n"
    "    PathSyntax  %lu\n"
    "    Adapter     %lu\n"
    "    Controller  %lu\n"
    "    DriveNumber %lu\n"
    "    Partition   %lu\n\n",
    Path, PathSyntax, AdapterNumber, ControllerNumber, DriveNumber, DrivePartition);

    /* If we are delay-initializing the device, finish now its initialization */
    if (Device->Geometry.BytesPerSector == MAXULONG) // Uninitialized
    {
        ARC_STATUS Status = Device->DevInit(Device->Context, Path, &Device->Geometry);
        if (Status != ESUCCESS)
            return Status;

        if (Device->Geometry.BytesPerSector == 0)
        {
            WARN("DevInit/GetDriveGeometry(0x%x) failed, fall back to hardcoded values\n", DriveNumber);
            if (Device->DeviceType == CdromController)
            {
                /* This is a CD-ROM device */
                Device->Geometry.BytesPerSector = 2048;
            }
            else
            {
                /* This is either a floppy disk or a hard disk device, but it doesn't
                 * matter which one because they both have 512 bytes per sector */
                Device->Geometry.BytesPerSector = 512;
            }
            // return EIO;
        }

        Device->SectorSize = Device->Geometry.BytesPerSector;
        Device->SectorOffset = 0;
        Device->SectorCount = Device->Geometry.Sectors;
    }

#if 1 // TODO: Temporarily handle partitions here....
    if (DrivePartition != 0)
    {
        PARTITION_TABLE_ENTRY PartitionTableEntry;

        if (!DiskGetPartitionEntry(Device->Context, Device->ReadBlocks,
                                   Device->PartitionStyle, DrivePartition, &PartitionTableEntry))
        {
            return EINVAL;
        }

        SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
        SectorCount = PartitionTableEntry.PartitionSectorCount;

        // TODO: Clamp offset and count to what's allowable from the Device.
        // SectorOffset = ;
        // SectorCount = ;
    }
    else
#endif
    {
        SectorOffset = Device->SectorOffset;
        SectorCount = Device->SectorCount;
    }

ERR("-- Drive %lu:\n"
    "   DeviceType:   %lu\n"
    "   SectorSize:   %lu\n"
    "   SectorCount:  %I64u\n"
    "   SectorOffset: %I64u\n"
    "   Context:      0x%p\n",
    DriveNumber,
    Device->DeviceType,
    // GEOMETRY Geometry;
    Device->SectorSize,
    Device->SectorCount,
    Device->SectorOffset,
    Device->Context);
ERR("-- Disk region: Offset: %I64u Count: %I64u\n", SectorOffset, SectorCount);

    Context = FrLdrTempAlloc(sizeof(*Context), TAG_DISK_CONTEXT);
    if (!Context)
        return ENOMEM;

    Context->Device = Device;
    Context->SectorCount = SectorCount;
    Context->SectorOffset = SectorOffset;
    Context->SectorNumber = 0;
    FsSetDeviceSpecific(*FileId, Context);
    return ESUCCESS;
}

#if 0
// TODO: Get rid of DiskReadBuffer (this is a BIOS-specific thing)
static ARC_STATUS
DiskReadByBlocks(
    _In_ ULONG FileId,
    _Out_ VOID* Buffer,
    _In_ ULONG N,
    _Out_ ULONG* Count)
{
    PDISKCONTEXT Context = FsGetDeviceSpecific(FileId);
    PDISKDEVICE Device = Context->Device;

    UCHAR* Ptr = (UCHAR*)Buffer;
    ULONG Length, TotalSectors, MaxSectors, ReadSectors;
    ULONGLONG SectorOffset;
    ARC_STATUS Status;

    ASSERT(DiskReadBufferSize > 0);

    TotalSectors = (N + Device->SectorSize - 1) / Device->SectorSize;
    MaxSectors   = DiskReadBufferSize / Device->SectorSize;
    SectorOffset = /*Device*/Context->SectorOffset + Context->SectorNumber;

    // If MaxSectors is 0, this will lead to infinite loop.
    // In release builds assertions are disabled, however we also have sanity checks in DiskOpen()
    ASSERT(MaxSectors > 0);

    Status = ESUCCESS;

    while (TotalSectors)
    {
        ReadSectors = min(TotalSectors, MaxSectors);

        Status = Device->ReadBlocks(Device->Context,
                                    SectorOffset,
                                    ReadSectors,
                                    DiskReadBuffer);
        if (Status != ESUCCESS)
            break;

        Length = ReadSectors * Device->SectorSize;
        Length = min(Length, N);

        RtlCopyMemory(Ptr, DiskReadBuffer, Length);

        Ptr += Length;
        N -= Length;
        SectorOffset += ReadSectors;
        TotalSectors -= ReadSectors;
    }

    *Count = (ULONG)((ULONG_PTR)Ptr - (ULONG_PTR)Buffer);
    Context->SectorNumber = SectorOffset - /*Device*/Context->SectorOffset;

    // return (Status == ESUCCESS ? Status : EIO);
    return Status;
}
#endif

// TODO: Merge both DiskRead*() functions together.
static ARC_STATUS
DiskRead(
    _In_ ULONG FileId,
    _Out_ VOID* Buffer,
    _In_ ULONG N,
    _Out_ ULONG* Count)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    PDISKDEVICE Device = Context->Device;
    ARC_STATUS Status;

    ULONG FullSectors, NbSectors;
    ULONG Lba;

    *Count = 0;

    if (N == 0)
        return ESUCCESS;

    FullSectors = N / Device->SectorSize;
    NbSectors = (N + Device->SectorSize - 1) / Device->SectorSize;
    if (Context->SectorNumber + NbSectors >= /*Device*/Context->SectorCount)
        return EINVAL;
    if (FullSectors > 0xffff)
        return EINVAL;

    /* Read full sectors */
    ASSERT(Context->SectorNumber < MAXULONG);
    Lba = (ULONG)(/*Device*/Context->SectorOffset + Context->SectorNumber);
    if (FullSectors > 0)
    {
        Status = Device->ReadBlocks(Device->Context,
                                    Lba, // === SectorOffset,
                                    FullSectors,
                                    Buffer);
        if (Status != ESUCCESS)
            return Status;

        Buffer = (PUCHAR)Buffer + FullSectors * Device->SectorSize;
        N -= FullSectors * Device->SectorSize;
        *Count += FullSectors * Device->SectorSize;
        Context->SectorNumber += FullSectors;
        Lba += FullSectors;
    }

    /* Read incomplete last sector */
    if (N > 0)
    {
        PUCHAR Sector = ExAllocatePool(PagedPool, Device->SectorSize);
        if (!Sector)
            return ENOMEM;

        Status = Device->ReadBlocks(Device->Context,
                                    Lba, // === SectorOffset,
                                    1,
                                    Sector);
        if (Status != ESUCCESS)
        {
            ExFreePool(Sector);
            return Status; // EIO;
        }

        RtlCopyMemory(Buffer, Sector, N);
        *Count += N;
        /* Context->SectorNumber remains untouched (incomplete sector read) */
        ExFreePool(Sector);
    }

    return ESUCCESS;
}

static ARC_STATUS
DiskSeek(
    _In_ ULONG FileId,
    _In_ LARGE_INTEGER* Position,
    _In_ SEEKMODE SeekMode)
{
    PDISKCONTEXT Context = FsGetDeviceSpecific(FileId);
    PDISKDEVICE Device = Context->Device;
    LARGE_INTEGER NewPosition = *Position;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += (Context->SectorNumber * Device->SectorSize);
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.QuadPart & (Device->SectorSize - 1))
        return EINVAL;

    /* Convert in number of sectors */
    NewPosition.QuadPart /= Device->SectorSize;
    if (/*Device*/Context->SectorCount != 0 && /* HACK: CDROMs may have a SectorCount of 0 */
        NewPosition.QuadPart >= /*Device*/Context->SectorCount)
        return EINVAL;

    Context->SectorNumber = NewPosition.QuadPart;
    return ESUCCESS;
}

static const DEVVTBL DiskVtbl =
{
    DiskClose,
    DiskGetFileInformation,
    DiskOpen,
    DiskRead,
    DiskSeek,
};

ARC_STATUS
BlockIoCreate(
    _In_ PCSTR DeviceName,
/** Interface */
    _In_ CONFIGURATION_TYPE DeviceType, // FIXME: Necessary or not?
    _In_ PVOID DeviceData,
    _In_ PBLOCK_INIT DevInit,
    _In_ PBLOCK_UNINIT DevUninit,
    _In_ PBLOCK_READ ReadBlocks,
    _In_opt_ PBLOCK_WRITE WriteBlocks,
/** !Interface */
    _Out_opt_ PULONG NumPartitions)
{
    NTSTATUS NtStatus;
    PDISKDEVICE BlockDev; // "Device"
    /// PPARTDEVICE PartDev;
    ULONG i;
#if 1 // Old code
    PARTITION_TABLE_ENTRY PartitionTableEntry;
#else
    ARC_STATUS Status;
    ULONG FileId, Count;
    struct _DRIVE_LAYOUT_INFORMATION *PartitionBuffer;
#endif
    CHAR ArcName[MAX_PATH];

    /* Initialize the partition count */
    if (NumPartitions)
        *NumPartitions = 0;

    BlockDev = FrLdrTempAlloc(sizeof(*BlockDev), TAG_DISK_DEVICE);
    if (!BlockDev)
        return ENOMEM;
    BlockDev->DeviceType = DeviceType; // Or determine it from the DeviceName?
    BlockDev->Context = DeviceData;
    RtlFillMemory(&BlockDev->Geometry, sizeof(BlockDev->Geometry), 0xFF);
#if 0 // Re-enable if we don't want to do delay-initialization in DiskOpen().
    Status = BlockDev->DevInit(BlockDev->Context, DeviceName, &BlockDev->Geometry);
    if (Status != ESUCCESS)
        return Status;
    BlockDev->SectorSize = BlockDev->Geometry.BytesPerSector;
    BlockDev->SectorOffset = 0;
    BlockDev->SectorCount = BlockDev->Geometry.Sectors;
#endif
    BlockDev->DevInit = DevInit;
    BlockDev->DevUninit = DevUninit;
    BlockDev->ReadBlocks = ReadBlocks;
    BlockDev->WriteBlocks = WriteBlocks;

    /* Fill out the ARC disk block */
    // AddReactOSArcDiskInfo(DeviceName, Signature, Checksum, ValidPartitionTable);
    if (!FsRegisterDevice(DeviceName, &DiskVtbl, BlockDev))
        return ENOMEM;

#if 1 // TODO: Or should this be done by the partition() abstraction module?
    /* Register device with partition(0) suffix */
    NtStatus = RtlStringCbPrintfA(ArcName, sizeof(ArcName), "%spartition(0)", DeviceName);
    if (!NT_SUCCESS(NtStatus))
        return ENAMETOOLONG;
    if (!FsRegisterDevice(ArcName, &DiskVtbl, BlockDev))
        return ENOMEM;
#endif

    /* Don't search for partitions in non-"rigid" disk peripherals (floppies, CD-ROMs) */
    if (DeviceType != DiskPeripheral)
        return ESUCCESS;

    /* Detect disk partition type */
    BlockDev->PartitionStyle =
        DiskDetectPartitionStyle(BlockDev->Context, BlockDev->ReadBlocks);

    /* Add partitions */ // TODO: Done by the abstraction module.
#if 1 // Old code
    i = 1; // ARC partition indices start at 1
    while (DiskGetPartitionEntry(BlockDev->Context, BlockDev->ReadBlocks,
                                 BlockDev->PartitionStyle, i, &PartitionTableEntry))
    {
        if (PartitionTableEntry.SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            RtlStringCbPrintfA(ArcName, sizeof(ArcName), "%spartition(%lu)", DeviceName, i);
#if 0 ///
            PartDev = FrLdrTempAlloc(sizeof(*PartDev), TAG_PART_DEVICE);
            if (!BlockDev)
                continue; // return ENOMEM;
            PartDev->Device = BlockDev;
            PartDev->SectorCount = PartitionTableEntry.PartitionSectorCount;
            PartDev->SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
#endif ///
            FsRegisterDevice(ArcName, &DiskVtbl, /*PartDev*/BlockDev);
        }
        i++;
    }
    if (NumPartitions)
        *NumPartitions = --i; // Return the actual number of partitions.

#else

    /* Read device partition table */
    Status = ArcOpen(ArcName, OpenReadOnly, &FileId);
    if (Status != ESUCCESS)
        return Status;

    Count = 0;
    NtStatus = HALDISPATCH->HalIoReadPartitionTable((PDEVICE_OBJECT)(ULONG_PTR)FileId,
                                                    512 /*BlockDev->SectorSize*/, FALSE, &PartitionBuffer);
    if (NT_SUCCESS(NtStatus))
    {
        for (i = 0; i < PartitionBuffer->PartitionCount; i++)
        {
            if (PartitionBuffer->PartitionEntry[i].PartitionType != PARTITION_ENTRY_UNUSED)
            {
                ++Count;
                RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                                   "%spartition(%lu)",
                                   DeviceName,
                                   PartitionBuffer->PartitionEntry[i].PartitionNumber);
#if 0 ///
                PartDev = FrLdrTempAlloc(sizeof(*PartDev), TAG_PART_DEVICE);
                if (!BlockDev)
                    continue; // return ENOMEM;
                PartDev->Device = BlockDev;
                PartDev->SectorCount = PartitionTableEntry.PartitionSectorCount;
                PartDev->SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
#endif ///
                FsRegisterDevice(ArcName, &DiskVtbl, /*PartDev*/BlockDev);
            }
        }
        ExFreePool(PartitionBuffer);
    }
    ArcClose(FileId);
    if (NumPartitions)
        *NumPartitions = Count; // Return the actual number of partitions.
#endif

    return ESUCCESS;
}
