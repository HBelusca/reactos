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

// #define USE_CACHE // And disable CACHE_FOR_FILESYSTEM in cache.h

#ifndef USE_CACHE // See cache.h
/* Driver-specific disk device object extension */
typedef struct _DDISKDEVICE
{
    CONFIGURATION_TYPE DeviceType;
    ULONG DiskSignature;
    GEOMETRY Geometry;
    ULONG SectorSize;       // == Geometry.BytesPerSector
    ULONGLONG SectorCount;  // == Geometry.Sectors
    ULONGLONG SectorOffset; // Always 0 (whole disk) -- FIXME: Remove?
    PVOID SectorCache; // A SectorSize-sized buffer to cache whole sectors for reads/writes.
    ULONGLONG CurrentLBA;  // LBA of the current sector in the cache.
    PVOID Context; // For "miniports"
    PBLOCK_INIT DevInit;
    PBLOCK_UNINIT DevUninit;
    PBLOCK_READ ReadBlocks;
    PBLOCK_WRITE WriteBlocks;
/** Partition support */
    PARTITION_STYLE PartitionStyle;
} DISKDEVICE, *PDISKDEVICE; // BLOCK_IO_MEDIA
#endif

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
    if (Device->SectorCache)
        FrLdrTempFree(Device->SectorCache, TAG_DISK_CONTEXT);
    Device->SectorCache = NULL;
    Device->CurrentLBA = -1; // Invalidate the LBA
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
DiskReadBlocks(
    _In_ PVOID This, // This is a PDISKDEVICE, and NOT the "DeviceData" given to BlockIoCreate()!
    _In_ ULONGLONG SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer)
{
    PDISKDEVICE Device = This;
#ifdef USE_CACHE
    BOOLEAN Success;
    Success = CacheReadDiskSectors(Device,
                                   SectorNumber,
                                   SectorCount,
                                   Buffer);
    return (Success ? ESUCCESS : EIO);
#else
    return Device->ReadBlocks(Device->Context,
                              SectorNumber,
                              SectorCount,
                              Buffer);
#endif
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

        /* Reset the sector cache if it exists, since the media sector size
         * may have changed. The cache remains uninitialized; it'll be lazily
         * initialized by DiskRead() */
        if (Device->SectorCache)
            FrLdrTempFree(Device->SectorCache, TAG_DISK_CONTEXT);
        Device->SectorCache = NULL;
        Device->CurrentLBA = -1; // Invalidate the LBA

// #ifdef USE_CACHE
        // (void)CacheInitializeDrive(Device); // FIXME!!!!
// #endif
    }
#ifdef USE_CACHE
/*********/(void)CacheInitializeDrive(Device);/*********/
#endif

#if 1 // TODO: Temporarily handle partitions here....
    if (DrivePartition != 0)
    {
        PARTITION_TABLE_ENTRY PartitionTableEntry;

        if (!DiskGetPartitionEntry(Device/*->Context*/, DiskReadBlocks,
                                   Device->PartitionStyle, DrivePartition, &PartitionTableEntry))
        {
            return EINVAL;
        }

        SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
        SectorCount = PartitionTableEntry.PartitionSectorCount;

        /* Validate the offset and count */
        if (!(0 <= SectorOffset && SectorOffset < Device->SectorCount) ||
            !(SectorOffset <= SectorOffset + SectorCount) || // Check for wrapping.
            !(SectorOffset + SectorCount <= Device->SectorCount))
        {
            ERR("Invalid partition: Offset: %I64u - Count: %I64u",
                SectorOffset, SectorCount);
            return EIO;
        }

        // /* Clamp offset and count to what's allowable from the device */
        // SectorOffset = min(SectorOffset, Device->SectorCount - 1);
        // SectorCount = min(SectorCount, Device->SectorCount - SectorOffset);
    }
    else
#endif
    {
        SectorOffset = Device->SectorOffset;
        SectorCount = Device->SectorCount;
    }
    if (SectorCount == 0)
    {
        ERR("Invalid SectorCount: %I64u", SectorCount);
        return EIO;
    }

ERR("-- Drive %lu:\n"
    "   DeviceType:   %lu\n"
    "   SectorSize:   %lu\n"
    "   SectorCount:  %I64u\n"
    "   SectorOffset: %I64u\n"
    "   Context:      0x%p\n",
    DriveNumber,
    Device->DeviceType,
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

        Status = DiskReadBlocks(Device,
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
// TODO: Support reads where the beginning isn't sector-aligned.
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
    ULONGLONG Lba;

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
    Lba = /*Device*/Context->SectorOffset + Context->SectorNumber;

    /* Check whether this LBA is in cache; if so, read from it */
    if (FullSectors > 0 && Device->SectorCache && Device->CurrentLBA == Lba)
    {
        /* Copy the cache contents and invalidate its LBA */
        TRACE("Sector Cache hit! LBA: %I64u\n", Lba);
        RtlCopyMemory(Buffer, Device->SectorCache, Device->SectorSize);
        Device->CurrentLBA = -1;

        /* We've read only one sector */
        Buffer = (PUCHAR)Buffer + Device->SectorSize;
        N -= Device->SectorSize;
        *Count += Device->SectorSize;
        Context->SectorNumber++;
        Lba++;
        FullSectors--;
    }

    /* Read whole sectors directly */
    if (FullSectors > 0)
    {
        Status = DiskReadBlocks(Device,
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
        /* Lazy-initialize the sector cache */
        // ExAllocatePool(PagedPool, Device->SectorSize);
        if (!Device->SectorCache)
            Device->SectorCache = FrLdrTempAlloc(Device->SectorSize, TAG_DISK_CONTEXT);
        if (!Device->SectorCache)
        {
            ERR("Couldn't allocate SectorCache for device 0x%p\n", Device);
            return ENOMEM;
        }
        Device->CurrentLBA = -1; // Invalidate the LBA

        Status = DiskReadBlocks(Device,
                                Lba, // === SectorOffset,
                                1,
                                Device->SectorCache);
        if (Status != ESUCCESS)
            return Status;

        /* Read succeeded, update the cached LBA */
        Device->CurrentLBA = Lba;

        RtlCopyMemory(Buffer, Device->SectorCache, N);
        *Count += N;
        /* Context->SectorNumber remains untouched (incomplete sector read) */
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


static ULONG
GenSectorChecksum(
    _In_ const VOID* Sector,
    _In_ ULONG SectorSize)
{
    const ULONG* Buffer = (const ULONG*)Sector;
    ULONG Checksum, i;

    Checksum = 0;
    for (i = 0; i < SectorSize / sizeof(ULONG); ++i)
    {
        Checksum += Buffer[i];
    }
    Checksum = ~Checksum + 1;
    return Checksum;
}

// GetDiskSignatureAndChecksum
static ARC_STATUS
GetDiskSignature(
    _In_ PDISKDEVICE Device)
{
    ARC_STATUS Status;
    ULONGLONG SectorStart;
    ULONG SectorSize;
    PMASTER_BOOT_RECORD Mbr;
    ULONG Checksum;
    BOOLEAN IsCdRom;
    BOOLEAN ValidPartitionTable;

    IsCdRom = (Device->DeviceType == CdromController);
    if (IsCdRom)
    {
        SectorStart = 16ULL;
        SectorSize = 2048;
    }
    else // (DeviceType == FloppyDiskPeripheral || DiskPeripheral)
    {
        SectorStart = 0ULL;
        SectorSize = 512;
    }

    Mbr = FrLdrTempAlloc(SectorSize, TAG_DISK_DEVICE);
    if (!Mbr)
    {
        ERR("Couldn't allocate MBR buffer\n");
        return FALSE; /* No available resources anymore, stop enumeration */
    }

    /* Read the MBR */
    Status = Device->ReadBlocks(Device->Context, SectorStart, 1, Mbr);
    if (Status != ESUCCESS)
    {
        ERR("Reading MBR failed\n");
        FrLdrTempFree(Mbr, TAG_DISK_DEVICE);
        return Status;
    }
    Device->DiskSignature = Mbr->Signature;
    /*TRACE*/ERR("Signature: %x\n", Device->DiskSignature);

    /* Calculate the MBR checksum */
    Checksum = GenSectorChecksum(Mbr, SectorSize);
    /*TRACE*/ERR("Checksum: %x\n", Checksum);

    ValidPartitionTable = (IsCdRom || (Mbr->MasterBootRecordMagic == 0xAA55));
    /*TRACE*/ERR("IsPartitionValid: %s\n", ValidPartitionTable ? "TRUE" : "FALSE");

    FrLdrTempFree(Mbr, TAG_DISK_DEVICE);

    return ESUCCESS;
}

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
    BlockDev->SectorCache = NULL;
    BlockDev->CurrentLBA = -1; // Invalidate the LBA
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

    /* Register the block device */
    if (!FsRegisterDevice(DeviceName, &DiskVtbl, BlockDev))
        return ENOMEM;

    /* Retrieve the disk signature */
    (void)GetDiskSignature(BlockDev);
    ERR("** Block IO: '%s' has signature 0x%X\n", DeviceName, BlockDev->DiskSignature);

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


//
// Disk registration for NT loader
//

// TODO: Move ntoskrnl.c!IopReadBootRecord() here and use it?
/**
 * @brief
 * Reads a single sector of size @SectorSize into @Buffer.
 **/
static ARC_STATUS
DiskReadSector(
    _In_ ULONG DeviceId,
    _In_ ULONGLONG StartingSector,
    _In_ ULONG SectorSize,
    _Out_ PVOID Buffer)
{
    LARGE_INTEGER Position;
    ULONG BytesRead;
    ARC_STATUS Status;

    Position.QuadPart = StartingSector * SectorSize;
    Status = ArcSeek(DeviceId, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
        return Status;

    Status = ArcRead(DeviceId, Buffer, SectorSize, &BytesRead);
    if (Status != ESUCCESS || BytesRead != SectorSize)
    {
ERR("ArcRead(%lu) failed or read not completed (wanted %lu, got %lu), Status %lu\n",
    DeviceId, SectorSize, BytesRead, Status);
        return Status;
    }

    return ESUCCESS;
}

// ARC_STATUS GetDiskSignatureAndChecksum(ULONG FileId, ULONG SectorSize, PULONG Signature, PULONG Checksum) { ... }

static const CHAR Hex[] = "0123456789abcdef";

VOID
GetHarddiskInformation(
    _In_ PCSTR DeviceName,
    _In_ UCHAR DriveNumber,
    _Out_writes_z_(20) PSTR Identifier)
{
    ARC_STATUS Status;
    ULONG DeviceId;
    // FILEINFORMATION Information;
    ULONG Signature, Checksum;
    ULONGLONG SectorStart;
    ULONG SectorSize;
    PMASTER_BOOT_RECORD Mbr;
    BOOLEAN ValidPartitionTable;
    BOOLEAN IsCdRom;

    Status = ArcOpen((PSTR)DeviceName, OpenReadOnly, &DeviceId);
    if (Status != ESUCCESS)
    {
        ERR("Couldn't open %s, Status %lu\n", DeviceName, Status);
        return;
    }
#if 0
    Status = ArcGetFileInformation(DeviceId, &Information);
    IsCdRom = (Information.Type == CdromController);
    {
        SectorStart = 16ULL;
        SectorSize = 2048;
    }
    else // (Information.Type == FloppyDiskPeripheral || DiskPeripheral)
#endif
    IsCdRom = FALSE; // FIXME
    {
        SectorStart = 0ULL;
        SectorSize = 512;
    }

    /* Read the MBR */
    if (DiskReadSector(DeviceId, SectorStart, 1, DiskReadBuffer) != ESUCCESS)
    {
        ERR("Reading MBR failed\n");
        /* We failed, use a default identifier (NOTE: DriveNumber is 0-based) */
        RtlStringCbPrintfA(Identifier, 20, "BIOSDISK%u", DriveNumber + 1);
        return;
    }

    ArcClose(DeviceId);

    Mbr = (PMASTER_BOOT_RECORD)DiskReadBuffer;
    Signature = Mbr->Signature;
    TRACE("Signature: %x\n", Signature);

    /* Calculate the MBR checksum */
    Checksum = GenSectorChecksum(DiskReadBuffer, SectorSize);
    TRACE("Checksum: %x\n", Checksum);

    ValidPartitionTable = (IsCdRom || (Mbr->MasterBootRecordMagic == 0xAA55));
    TRACE("IsPartitionValid: %s\n", ValidPartitionTable ? "TRUE" : "FALSE");

    /* Convert checksum and signature to identifier string */
    Identifier[0] = Hex[(Checksum >> 28) & 0x0F];
    Identifier[1] = Hex[(Checksum >> 24) & 0x0F];
    Identifier[2] = Hex[(Checksum >> 20) & 0x0F];
    Identifier[3] = Hex[(Checksum >> 16) & 0x0F];
    Identifier[4] = Hex[(Checksum >> 12) & 0x0F];
    Identifier[5] = Hex[(Checksum >> 8) & 0x0F];
    Identifier[6] = Hex[(Checksum >> 4) & 0x0F];
    Identifier[7] = Hex[Checksum & 0x0F];
    Identifier[8] = '-';
    Identifier[9] = Hex[(Signature >> 28) & 0x0F];
    Identifier[10] = Hex[(Signature >> 24) & 0x0F];
    Identifier[11] = Hex[(Signature >> 20) & 0x0F];
    Identifier[12] = Hex[(Signature >> 16) & 0x0F];
    Identifier[13] = Hex[(Signature >> 12) & 0x0F];
    Identifier[14] = Hex[(Signature >> 8) & 0x0F];
    Identifier[15] = Hex[(Signature >> 4) & 0x0F];
    Identifier[16] = Hex[Signature & 0x0F];
    Identifier[17] = '-';
    Identifier[18] = (ValidPartitionTable ? 'A' : 'X');
    Identifier[19] = ANSI_NULL;
    TRACE("Identifier: %s\n", Identifier);
}
