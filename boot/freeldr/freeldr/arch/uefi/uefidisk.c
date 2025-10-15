/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk Access Functions
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define TAG_HW_RESOURCE_LIST    'lRwH'
#define TAG_HW_DISK_CONTEXT     'cDwH'

#define FIRST_PARTITION 1 // TODO: Remove

typedef struct tagDISKCONTEXT
{
    UCHAR DriveNumber;
    ULONG SectorSize;
    ULONGLONG SectorOffset;
    ULONGLONG SectorCount;
    ULONGLONG SectorNumber;
} DISKCONTEXT;

typedef struct _INTERNAL_UEFI_DISK
{
    UCHAR ArcDriveNumber;
    UCHAR UefiRootNumber;
    BOOLEAN IsThisTheBootDrive;
    ULONG NumOfPartitions; // FIXME: Unused
} INTERNAL_UEFI_DISK, *PINTERNAL_UEFI_DISK;

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern EFI_HANDLE PublicBootHandle; /* Freeldr itself */

/* Made to match BIOS */
PVOID DiskReadBuffer;
UCHAR PcBiosDiskCount; // Maximum 255 disks supported.

SIZE_T DiskReadBufferSize;
PVOID Buffer;

static const CHAR Hex[] = "0123456789abcdef";
static CHAR PcDiskIdentifier[32][20];

/* UEFI-specific */
static ULONG UefiBootRootIdentifier;
static ULONG OffsetToBoot;
static UCHAR PublicBootArcDisk; // Maximum 255 disks supported.
static INTERNAL_UEFI_DISK* InternalUefiDisk = NULL;
static EFI_GUID bioGuid = BLOCK_IO_PROTOCOL;
static EFI_BLOCK_IO* bio;
static EFI_HANDLE* handles = NULL;

/* FUNCTIONS *****************************************************************/

static LONG lReportError = 0; // >= 0: display errors; < 0: hide errors.

LONG
DiskReportError(BOOLEAN bShowError)
{
    /* Set the reference count */
    if (bShowError) ++lReportError;
    else            --lReportError;
    return lReportError;
}

static
BOOLEAN
UefiGetBootPartitionEntry(
    _In_ UCHAR DriveNumber,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry,
    _Out_ PULONG BootPartition)
{
    ULONG PartitionNum;

    UNREFERENCED_PARAMETER(PartitionEntry);

    TRACE("UefiGetBootPartitionEntry: DriveNumber: %u\n", DriveNumber);
    /* UefiBootRoot is the offset into the array of handles where the raw disk of the boot drive is.
     * Partitions start with 1 in ARC, but UEFI root drive identifier is also first partition. */
    PartitionNum = (OffsetToBoot - UefiBootRootIdentifier);
    if (PartitionNum == 0)
    {
        TRACE("Boot PartitionNumber is 0\n");
        /* The OffsetToBoot is equal to the RootIdentifier */
        PartitionNum = FIRST_PARTITION;
    }

    *BootPartition = PartitionNum;
    TRACE("UefiGetBootPartitionEntry: Boot Partition is: %u\n", PartitionNum);
    return TRUE;
}

static
ARC_STATUS
UefiDiskClose(ULONG FileId)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    FrLdrTempFree(Context, TAG_HW_DISK_CONTEXT);
    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskGetFileInformation(ULONG FileId, FILEINFORMATION *Information)
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

    Information->Type = DiskPeripheral; /* No floppy for you for now... */

    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskOpen(CHAR *Path, OPENMODE OpenMode, ULONG *FileId)
{
    DISKCONTEXT* Context;
    ULONG AdapterNumber, ControllerNumber, DriveNumber, DrivePartition;
    ULONG PathSyntax;
    ULONG SectorSize;
    GEOMETRY Geometry;
    ULONGLONG SectorOffset = 0;
    ULONGLONG SectorCount = 0;

    TRACE("UefiDiskOpen: File ID: %lu, Path: %s\n", *FileId, Path);

    if (DiskReadBufferSize == 0)
    {
        ERR("DiskOpen(): DiskReadBufferSize is 0, something is wrong.\n");
        ASSERT(FALSE);
        return ENOMEM;
    }

    /* Parse ARC path */
    if (!DissectArcPath2(Path, &AdapterNumber, &ControllerNumber,
                         &DriveNumber, &DrivePartition, &PathSyntax))
    {
        return ENODEV;
    }
    if (PathSyntax != 1) /* multi() format */
        return ENODEV;
    /* We only support drives on adapter/controller 0/0 */
    if (AdapterNumber != 0 || ControllerNumber != 0)
        return ENODEV;

    TRACE("Opening disk: DriveNumber: %u, DrivePartition: %u\n", DriveNumber, DrivePartition);

    if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
        return EIO;
    SectorSize = Geometry.BytesPerSector;

    if (DrivePartition != 0xff && DrivePartition != 0)
    {
        /*
         * NOTE: *FileId is the candidate for the multi()disk()rdisk()partition()
         * being opened, and already has a FuncTable set. We could open the
         * underlying disk by building its path name, without partition(),
         * opening it and doing the job; or, we can temporarily reopen the
         * disk by using a temporary DISKCONTEXT and using the *FileId slot
         * just for this purpose. Afterwards, we release it so that it can be
         * used for its initial intended purpose.
         *
         * Ideally we would like to find the parent disk...
         */
        DISKCONTEXT TempContext;

        TempContext.DriveNumber = DriveNumber;
        TempContext.SectorSize = SectorSize;
        TempContext.SectorOffset = 0; // RAW disk, i.e. partition 0
        TempContext.SectorCount = Geometry.Sectors;
        TempContext.SectorNumber = 0;

        FsSetDeviceSpecific(*FileId, &TempContext);
        BOOLEAN Success = DiskGetPartitionSize(NULL, *FileId, SectorSize,
                                               DrivePartition, &SectorOffset, &SectorCount);
        FsSetDeviceSpecific(*FileId, NULL);

        if (!Success)
            return EIO;

        SectorOffset /= SectorSize;
        SectorCount /= SectorSize;
    }
    else
    {
        SectorOffset = 0;
        SectorCount = Geometry.Sectors;
    }

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
UefiDiskRead(ULONG FileId, VOID *Buffer, ULONG N, ULONG *Count)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    UCHAR* Ptr = (UCHAR*)Buffer;
    ULONG Length, TotalSectors, MaxSectors, ReadSectors;
    ULONGLONG SectorOffset;
    BOOLEAN ret;

    ASSERT(DiskReadBufferSize > 0);

    TotalSectors = (N + Context->SectorSize - 1) / Context->SectorSize;
    MaxSectors   = DiskReadBufferSize / Context->SectorSize;
    SectorOffset = Context->SectorOffset + Context->SectorNumber;

    // If MaxSectors is 0, this will lead to infinite loop.
    // In release builds assertions are disabled, however we also have sanity checks in DiskOpen()
    ASSERT(MaxSectors > 0);

    ret = TRUE;

    while (TotalSectors)
    {
        ReadSectors = min(TotalSectors, MaxSectors);

        ret = MachDiskReadLogicalSectors(Context->DriveNumber,
                                         SectorOffset,
                                         ReadSectors,
                                         DiskReadBuffer);
        if (!ret)
            break;

        Length = ReadSectors * Context->SectorSize;
        Length = min(Length, N);

        RtlCopyMemory(Ptr, DiskReadBuffer, Length);

        Ptr += Length;
        N -= Length;
        SectorOffset += ReadSectors;
        TotalSectors -= ReadSectors;
    }

    *Count = (ULONG)((ULONG_PTR)Ptr - (ULONG_PTR)Buffer);
    Context->SectorNumber = SectorOffset - Context->SectorOffset;

    return (ret ? ESUCCESS : EIO);
}

static
ARC_STATUS
UefiDiskSeek(ULONG FileId, LARGE_INTEGER *Position, SEEKMODE SeekMode)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += (Context->SectorNumber * Context->SectorSize);
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.QuadPart & (Context->SectorSize - 1))
        return EINVAL;

    /* Convert in number of sectors */
    NewPosition.QuadPart /= Context->SectorSize;

    /* HACK: CDROMs may have a SectorCount of 0 */
    if (Context->SectorCount != 0 && NewPosition.QuadPart >= Context->SectorCount)
        return EINVAL;

    Context->SectorNumber = NewPosition.QuadPart;
    return ESUCCESS;
}

static const DEVVTBL UefiDiskVtbl =
{
    UefiDiskClose,
    UefiDiskGetFileInformation,
    UefiDiskOpen,
    UefiDiskRead,
    UefiDiskSeek,
};


#if 0 // FIXME: Not yet used. For functionality similar to pchw.c!DetectBiosDisks().
PCHAR
GetHarddiskIdentifier(UCHAR DriveNumber)
{
    TRACE("GetHarddiskIdentifier: DriveNumber: %u\n", DriveNumber);
    return PcDiskIdentifier[DriveNumber /* - FIRST_BIOS_DISK*/];
}
#endif

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

static
VOID
GetHarddiskInformation(UCHAR DriveNumber)
{
    PMASTER_BOOT_RECORD Mbr;
    ULONG Signature, Checksum;
    BOOLEAN ValidPartitionTable;
    PCHAR Identifier = PcDiskIdentifier[DriveNumber];
    CHAR ArcName[MAX_PATH];

    /* Read the MBR */
    if (!MachDiskReadLogicalSectors(DriveNumber, 0ULL, 1, DiskReadBuffer))
    {
        ERR("Reading MBR failed\n");
        /* We failed, use a default identifier */
        sprintf(Identifier, "BIOSDISK%u", DriveNumber);
        return;
    }
    Mbr = (PMASTER_BOOT_RECORD)DiskReadBuffer;
    Signature = Mbr->Signature;
    TRACE("Signature: %x\n", Signature);

    /* Calculate the MBR checksum */
    Checksum = GenSectorChecksum(DiskReadBuffer, 512);
    TRACE("Checksum: %x\n", Checksum);

    ValidPartitionTable = (Mbr->MasterBootRecordMagic == 0xAA55);

    /* Fill out the ARC disk block */
    RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                       "multi(0)disk(0)rdisk(%u)",
                       DriveNumber);
    AddReactOSArcDiskInfo(ArcName, Signature, Checksum, ValidPartitionTable);
    // FsRegisterDevice(ArcName, &DiskVtbl);

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
    Identifier[19] = 0;
    TRACE("Identifier: %s\n", Identifier);

    /* Enumerate and add the ARC device partitions */
    DiskReportError(FALSE);
    (void)PartRegisterDevicePartitions(ArcName, &UefiDiskVtbl, 512 /*FIXME*/, &InternalUefiDisk[DriveNumber].NumOfPartitions);
    DiskReportError(TRUE);
}

static
VOID
UefiSetupBlockDevices(VOID)
{
    EFI_STATUS Status;
    ULONG BlockDeviceIndex;
    ULONG SystemHandleCount;
    ULONG i;

    UINTN handle_size = 0;
    PcBiosDiskCount = 0;
    UefiBootRootIdentifier = 0;

    /* 1) Setup a list of boot handles by using the LocateHandle protocol */
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    handles = MmAllocateMemoryWithType(handle_size, LoaderFirmwareTemporary);
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    SystemHandleCount = handle_size / sizeof(EFI_HANDLE);
    InternalUefiDisk = MmAllocateMemoryWithType(sizeof(INTERNAL_UEFI_DISK) * SystemHandleCount, LoaderFirmwareTemporary);

    BlockDeviceIndex = 0;
    /* 2) Parse the handle list */
    for (i = 0; i < SystemHandleCount; ++i)
    {
        if (PcBiosDiskCount >= 0xFF)
            break;

        Status = GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);
        if (handles[i] == PublicBootHandle)
        {
            OffsetToBoot = i; /* Drive offset in the handles list */
        }

        if (EFI_ERROR(Status) || 
            bio == NULL ||
            bio->Media->BlockSize == 0 ||
            bio->Media->BlockSize > 4096)
        {
            TRACE("UefiSetupBlockDevices: UEFI has found a block device that failed, skipping\n");
            continue;
        }
        if (bio->Media->LogicalPartition == FALSE)
        {
            TRACE("Found root of a HDD\n");
            PcBiosDiskCount++;
            InternalUefiDisk[BlockDeviceIndex].ArcDriveNumber = BlockDeviceIndex;
            InternalUefiDisk[BlockDeviceIndex].UefiRootNumber = i;
            GetHarddiskInformation(BlockDeviceIndex);
            BlockDeviceIndex++;
        }
        else if (handles[i] == PublicBootHandle)
        {
            GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);
            if (bio->Media->LogicalPartition == FALSE)
            {
                ULONG j;

                TRACE("Found root at index %u\n", i);
                UefiBootRootIdentifier = i;

                for (j = 0; j < PcBiosDiskCount; ++j)
                {
                    /* Now only of the root drive number is equal to this drive we found above */
                    if (InternalUefiDisk[j].UefiRootNumber == UefiBootRootIdentifier)
                    {
                        InternalUefiDisk[j].IsThisTheBootDrive = TRUE;
                        PublicBootArcDisk = j;
                        TRACE("Found Boot drive\n");
                    }
                }
            }
        }
    }

    TRACE("UEFI reports %u harddisk%s\n",
          PcBiosDiskCount, (PcBiosDiskCount == 1) ? "" : "s");
}

static
BOOLEAN
UefiSetBootpath(VOID)
{
    TRACE("UefiSetBootpath: Setting up boot path\n");
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
    if (bio->Media->RemovableMedia == TRUE && bio->Media->BlockSize == 2048)
    {
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)cdrom(%u)", PublicBootArcDisk);
    }
    else
    {
        ULONG BootPartition;

        /* This is a hard disk. Find the boot partition. */
        if (!UefiGetBootPartitionEntry(PublicBootArcDisk, NULL, &BootPartition))
        {
            ERR("Failed to get boot partition entry\n");
            return FALSE;
        }

        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)rdisk(%u)partition(%lu)",
                           PublicBootArcDisk, BootPartition);
    }

    return TRUE;
}

BOOLEAN
UefiInitializeBootDevices(VOID)
{
    DiskReadBufferSize = EFI_PAGE_SIZE;
    DiskReadBuffer = MmAllocateMemoryWithType(DiskReadBufferSize, LoaderFirmwareTemporary);
    UefiSetupBlockDevices();
    UefiSetBootpath();

    /* Add it, if it's a cdrom */
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
    if (bio->Media->RemovableMedia == TRUE && bio->Media->BlockSize == 2048)
    {
        PMASTER_BOOT_RECORD Mbr;
        ULONG Signature, Checksum;

        /* Read the MBR */
        if (!MachDiskReadLogicalSectors(PublicBootArcDisk, 16ULL, 1, DiskReadBuffer))
        {
            ERR("Reading MBR failed\n");
            return FALSE;
        }
        Mbr = (PMASTER_BOOT_RECORD)DiskReadBuffer;
        Signature = Mbr->Signature;
        TRACE("Signature: %x\n", Signature);

        /* Calculate the MBR checksum */
        Checksum = GenSectorChecksum(DiskReadBuffer, 2048);
        TRACE("Checksum: %x\n", Checksum);

        /* Fill out the ARC disk block */
        AddReactOSArcDiskInfo(FrLdrBootPath, Signature, Checksum, TRUE);

        FsRegisterDevice(FrLdrBootPath, &UefiDiskVtbl);
        PcBiosDiskCount++; // This is not accounted for in the number of pre-enumerated BIOS drives!
        TRACE("Additional boot drive detected: 0x%02X\n", PublicBootArcDisk);
    }
    return TRUE;
}

UCHAR
UefiGetFloppyCount(VOID)
{
    /* No floppy for you for now... */
    return 0;
}


//
// FIXME: These are old-style functions that should be replaced in the future.
// They are used only in this file and in cache/blocklist.c and cache.c
// (that latter module should be rewritten).
// Their implementation could be used within this file (instead of the Mach* ones).
//

BOOLEAN
UefiDiskReadLogicalSectors(
    IN UCHAR DriveNumber,
    IN ULONGLONG SectorNumber,
    IN ULONG SectorCount,
    OUT PVOID Buffer)
{
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber].UefiRootNumber;
    TRACE("UefiDiskReadLogicalSectors: DriveNumber: %u\n", UefiDriveNumber);
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);

    /* Devices setup */
    bio->ReadBlocks(bio, bio->Media->MediaId, SectorNumber, SectorCount * bio->Media->BlockSize, Buffer);
    return TRUE;
}

BOOLEAN
UefiDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber].UefiRootNumber;
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    Geometry->Cylinders = 1; // Not relevant for the UEFI BIO protocol
    Geometry->Heads = 1;     // Not relevant for the UEFI BIO protocol
    Geometry->SectorsPerTrack = (bio->Media->LastBlock + 1);
    Geometry->BytesPerSector = bio->Media->BlockSize;
    Geometry->Sectors = (bio->Media->LastBlock + 1);

    return TRUE;
}

ULONG
UefiDiskGetCacheableBlockCount(UCHAR DriveNumber)
{
    ULONG UefiDriveNumber = InternalUefiDisk[DriveNumber].UefiRootNumber;
    TRACE("UefiDiskGetCacheableBlockCount: DriveNumber: %u\n", UefiDriveNumber);

    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    return (bio->Media->LastBlock + 1);
}
