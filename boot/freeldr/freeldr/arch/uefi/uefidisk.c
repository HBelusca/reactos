/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk Access Functions
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define FIRST_PARTITION 1

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
SIZE_T DiskReadBufferSize;
UCHAR PcBiosDiskCount; // Maximum 255 disks supported.

/* FIXME: These two are unused, but are here to fix linking */
UCHAR FrldrBootDrive;
ULONG FrldrBootPartition;

static const CHAR Hex[] = "0123456789abcdef";
static CHAR PcDiskIdentifier[32][20];

/* UEFI-specific */
static ULONG UefiBootRootIdentifier;
static ULONG OffsetToBoot;
static UCHAR PublicBootArcDisk; // Maximum 255 disks supported.
static INTERNAL_UEFI_DISK* InternalUefiDisk = NULL;
static EFI_GUID bioGuid = BLOCK_IO_PROTOCOL;
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
BOOLEAN
UefiDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO* bio;
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber].UefiRootNumber;
    Status = GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    if (EFI_ERROR(Status) || bio == NULL /* || bio->Media->BlockSize == 0 */)
        return FALSE;

    Geometry->Cylinders = 1; // Not relevant for the UEFI BIO protocol
    Geometry->Heads = 1;     // Not relevant for the UEFI BIO protocol
    Geometry->SectorsPerTrack = (bio->Media->LastBlock + 1);
    Geometry->BytesPerSector = bio->Media->BlockSize;
    Geometry->Sectors = (bio->Media->LastBlock + 1);
    return TRUE;
}

// ~ StartDevice
static
ARC_STATUS
UefiDiskInit(
    _In_ PVOID This, // The "DeviceData" given to BlockIoCreate()
    _In_ PCSTR Path,
    _Out_ PGEOMETRY Geometry)
{
    ULONG AdapterNumber, ControllerNumber, DriveNumber, DrivePartition;
    ULONG PathSyntax;

    // TRACE("UefiDiskInit: File ID: %lu, Path: %s\n", *FileId, Path);

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
    if (PathSyntax != 1) /* multi() format */
        return ENODEV;
    /* We only support drives on adapter/controller 0/0 */
    if (AdapterNumber != 0 || ControllerNumber != 0)
        return ENODEV;

    /* DriveNumber must match our context */
    if (DriveNumber != (UCHAR)PtrToUlong(This)) // FIXME: BIOS-specific, remove
        return ENODEV;
#if 0 // FIXME: Temporarily disabled while partition numbers get there...
    /* And this has to be partition 0 */
    if (DrivePartition != 0)
        return ENODEV;
#endif

    TRACE("Opening disk: DriveNumber: %u, DrivePartition: %u\n", DriveNumber, DrivePartition);

    if (!UefiDiskGetDriveGeometry(DriveNumber, Geometry))
        return EIO;
    return ESUCCESS;
}

// ~ Stop/RemoveDevice
static
ARC_STATUS
UefiDiskUninit(
    _In_ PVOID This) // The "DeviceData" given to BlockIoCreate()
{
    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskReadBlocks(
    _In_ PVOID This, // The "DeviceData" given to BlockIoCreate()
    _In_ ULONGLONG SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO* bio;
    ULONG UefiDriveNumber;
    /***/UCHAR DriveNumber = (UCHAR)PtrToUlong(This);/***/ // FIXME: BIOS-specific, remove

    UefiDriveNumber = InternalUefiDisk[DriveNumber].UefiRootNumber;
    TRACE("UefiDiskReadBlocks: DriveNumber: %u\n", UefiDriveNumber);
    Status = GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    if (EFI_ERROR(Status) || bio == NULL /* || bio->Media->BlockSize == 0 */)
        return EIO;

    /* Read the blocks */
    Status = bio->ReadBlocks(bio, bio->Media->MediaId, SectorNumber, SectorCount * bio->Media->BlockSize, Buffer);
    return (!EFI_ERROR(Status) ? ESUCCESS : EIO);
}


PCHAR
GetHarddiskIdentifier(UCHAR DriveNumber)
{
    TRACE("GetHarddiskIdentifier: DriveNumber: %u\n", DriveNumber);
    return PcDiskIdentifier[DriveNumber];
}

static
VOID
GetHarddiskInformation(UCHAR DriveNumber)
{
    PCHAR Identifier = PcDiskIdentifier[DriveNumber];
    ARC_STATUS Status;
    CHAR ArcName[64];

    RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                       "multi(0)disk(0)rdisk(%u)",
                       DriveNumber);

    DiskReportError(FALSE);
    Status = BlockIoCreate(ArcName,
                           DiskPeripheral,
                           UlongToPtr((ULONG)DriveNumber),
                           UefiDiskInit,
                           UefiDiskUninit,
                           UefiDiskReadBlocks,
                           NULL,
                           &InternalUefiDisk[DriveNumber].NumOfPartitions);
    DiskReportError(TRUE);

    if (Status != ESUCCESS)
    {
        /* The disk failed to be initialized, use a default identifier */
        sprintf(Identifier, "BIOSDISK%u", DriveNumber);
        return;
    }

//
// TODO: Since we are a "miniport" of the lib/disk.c driver,
// we should be able to have access to some of its data.
//
    /****/sprintf(Identifier, "BIOSDISK%u", DriveNumber);/****/
#if 0
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
#endif
}

static
VOID
UefiSetupBlockDevices(VOID)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO* bio;
    ULONG BlockDeviceIndex;
    ULONG SystemHandleCount;
    ULONG i;
    UINTN handle_size = 0;

    PcBiosDiskCount = 0;
    UefiBootRootIdentifier = 0;

    /* 1) Setup a list of boot handles by using the LocateHandle protocol */
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    if (Status != EFI_BUFFER_TOO_SMALL || handle_size == 0)
    {
        ERR("Couldn't obtain UEFI BLOCK_IO_PROTOCOL for drive handles table\n");
        return;
    }
    handles = MmAllocateMemoryWithType(handle_size, LoaderFirmwareTemporary);
    if (!handles)
    {
        ERR("Couldn't allocate UEFI drive handles table\n");
        return;
    }
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    if (Status != EFI_SUCCESS)
    {
        ERR("Couldn't retrieve UEFI drive handles table\n");
        MmFreeMemory(handles);
        handles = NULL;
        return;
    }

    SystemHandleCount = handle_size / sizeof(EFI_HANDLE);
    InternalUefiDisk = MmAllocateMemoryWithType(sizeof(INTERNAL_UEFI_DISK) * SystemHandleCount, LoaderFirmwareTemporary);
    if (!InternalUefiDisk)
    {
        ERR("Couldn't allocate InternalUefiDisk\n");
        MmFreeMemory(handles);
        handles = NULL;
        return;
    }

    BlockDeviceIndex = 0;
    /* 2) Parse the handle list */
    for (i = 0; i < SystemHandleCount; ++i)
    {
        if (PcBiosDiskCount >= 0xFF)
            break;

        Status = GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);

        if (handles[i] == PublicBootHandle)
            OffsetToBoot = i; /* Drive offset in the handles list */

        if (EFI_ERROR(Status) || bio == NULL ||
            bio->Media->BlockSize == 0 || bio->Media->BlockSize > 4096)
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
                        TRACE("Found boot drive\n");
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
UefiSetBootPath(
    _Out_ PCONFIGURATION_TYPE DeviceType)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO* bio;

    TRACE("UefiSetBootPath: Setting up boot path\n");
    Status = GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
    if (EFI_ERROR(Status) || bio == NULL || bio->Media->BlockSize == 0)
        return FALSE;

    *DeviceType = 0;

    if (bio->Media->RemovableMedia == TRUE && bio->Media->BlockSize == 2048)
    {
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)cdrom(%u)", PublicBootArcDisk);
        *DeviceType = CdromController;
    }
    else
    {
        ULONG BootPartition;

        /* This is a hard disk, find the boot partition */
        if (!UefiGetBootPartitionEntry(PublicBootArcDisk, NULL, &BootPartition))
        {
            ERR("Failed to get boot partition entry\n");
            return FALSE;
        }

        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)rdisk(%u)partition(%lu)",
                           PublicBootArcDisk, BootPartition);
        *DeviceType = DiskPeripheral;
    }

    return TRUE;
}

BOOLEAN
UefiInitializeBootDevices(VOID)
{
    CONFIGURATION_TYPE DriveType;

    DiskReadBufferSize = EFI_PAGE_SIZE;
    DiskReadBuffer = MmAllocateMemoryWithType(DiskReadBufferSize, LoaderFirmwareTemporary);
    if (!DiskReadBuffer)
    {
        ERR("Couldn't allocate DiskReadBuffer\n");
        return FALSE;
    }

    UefiSetupBlockDevices();

    /* Initialize FrLdrBootPath, the path FreeLoader starts from */
    UefiSetBootPath(&DriveType);

    /* Add it, if it's a CD-ROM */
    if (DriveType == CdromController)
    {
        DiskReportError(FALSE);
        (void)BlockIoCreate(FrLdrBootPath,
                            DriveType,
                            UlongToPtr((ULONG)PublicBootArcDisk),
                            UefiDiskInit,
                            UefiDiskUninit,
                            UefiDiskReadBlocks,
                            NULL,
                            &InternalUefiDisk[PublicBootArcDisk].NumOfPartitions);
        DiskReportError(TRUE);

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

ULONG
UefiDiskGetCacheableBlockCount(UCHAR DriveNumber)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO* bio;
    ULONG UefiDriveNumber = InternalUefiDisk[DriveNumber].UefiRootNumber;

    TRACE("UefiDiskGetCacheableBlockCount: DriveNumber: %u\n", UefiDriveNumber);

    Status = GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    if (EFI_ERROR(Status) || bio == NULL || bio->Media->BlockSize == 0)
        return 0;
    return (bio->Media->LastBlock + 1);
}
