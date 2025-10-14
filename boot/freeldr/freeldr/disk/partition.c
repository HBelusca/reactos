/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Block Device Partition Management. Implements the partition()
 *              protocol for MBR, GPT, and XBOX partitioned block devices.
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2010 Hervé Poussineau <hpoussin@reactos.org>
 *              Copyright 2016 Wim Hueskes
 *              Copyright 2019 Stanislav Motylkov <x86corez@gmail.com>
 *              Copyright 2019-2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#ifndef _M_ARM
#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);
#undef TRACE
#undef WARN
#define TRACE ERR
#define WARN  ERR

/* BRFR signature at disk offset 0x600 (== 3 * 0x200) */
#define XBOX_SIGNATURE_SECTOR 3
#define XBOX_SIGNATURE        ('B' | ('R' << 8) | ('F' << 16) | ('R' << 24))

/* Default hardcoded partition number to boot from Xbox disk */
#define FATX_DATA_PARTITION 1

static struct
{
    ULONG SectorStart;
    ULONG SectorCount;
    CHAR PartitionType;
} XboxPartitions[] =
{
    /* This is in the \Device\Harddisk0\Partition.. order used by the Xbox kernel */
    { 0x0055F400, 0x0098F800, PARTITION_FAT32  }, /* Store , E: */
    { 0x00465400, 0x000FA000, PARTITION_FAT_16 }, /* System, C: */
    { 0x00000400, 0x00177000, PARTITION_FAT_16 }, /* Cache1, X: */
    { 0x00177400, 0x00177000, PARTITION_FAT_16 }, /* Cache2, Y: */
    { 0x002EE400, 0x00177000, PARTITION_FAT_16 }  /* Cache3, Z: */
};


/* FUNCTIONS *****************************************************************/

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

static BOOLEAN
DiskReadBootRecord(
    _In_ ULONG DeviceId,
    _In_ ULONGLONG LogicalSectorNumber,
    _Out_ PMASTER_BOOT_RECORD BootRecord)
{
#if DBG
    ULONG Index;
#endif

    /* Read one sector (master boot record) from a given position */
    if (DiskReadSector(DeviceId, LogicalSectorNumber,
                       sizeof(MASTER_BOOT_RECORD), BootRecord) != ESUCCESS)
    {
        return FALSE;
    }

#if DBG
    TRACE("Dumping partition table for drive 0x%x:\n", DeviceId);
    TRACE("Boot record logical start sector = %llu\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < 4; Index++)
    {
        PPARTITION_TABLE_ENTRY PartitionTableEntry = &BootRecord->PartitionTable[Index];

        TRACE("-------------------------------------------\n");
        TRACE("Partition %u\n", (Index + 1));
        TRACE("BootIndicator: 0x%x\n", PartitionTableEntry->BootIndicator);
        TRACE("StartHead: 0x%x\n", PartitionTableEntry->StartHead);
        TRACE("StartSector (Plus 2 cylinder bits): 0x%x\n", PartitionTableEntry->StartSector);
        TRACE("StartCylinder: 0x%x\n", PartitionTableEntry->StartCylinder);
        TRACE("SystemIndicator: 0x%x\n", PartitionTableEntry->SystemIndicator);
        TRACE("EndHead: 0x%x\n", PartitionTableEntry->EndHead);
        TRACE("EndSector (Plus 2 cylinder bits): 0x%x\n", PartitionTableEntry->EndSector);
        TRACE("EndCylinder: 0x%x\n", PartitionTableEntry->EndCylinder);
        TRACE("SectorCountBeforePartition: 0x%x\n", PartitionTableEntry->SectorCountBeforePartition);
        TRACE("PartitionSectorCount: 0x%x\n", PartitionTableEntry->PartitionSectorCount);
    }
#endif

    /* Check the partition table magic value */
    return (BootRecord->MasterBootRecordMagic == 0xaa55);
}

static BOOLEAN
DiskGetFirstPartitionEntry(
    _In_ PMASTER_BOOT_RECORD MasterBootRecord,
    _Out_ PPARTITION_TABLE_ENTRY* pPartitionTableEntry)
{
    ULONG Index;

    for (Index = 0; Index < 4; Index++)
    {
        /* Check the system indicator. If it's not an extended or unused partition then we're done. */
        if ((MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_ENTRY_UNUSED) &&
            (MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_EXTENDED) &&
            (MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_XINT13_EXTENDED))
        {
            *pPartitionTableEntry = &MasterBootRecord->PartitionTable[Index];
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
DiskGetFirstExtendedPartitionEntry(
    _In_ PMASTER_BOOT_RECORD MasterBootRecord,
    _Out_ PPARTITION_TABLE_ENTRY* pPartitionTableEntry)
{
    ULONG Index;

    for (Index = 0; Index < 4; Index++)
    {
        /* Check the system indicator. If it an extended partition then we're done. */
        if ((MasterBootRecord->PartitionTable[Index].SystemIndicator == PARTITION_EXTENDED) ||
            (MasterBootRecord->PartitionTable[Index].SystemIndicator == PARTITION_XINT13_EXTENDED))
        {
            *pPartitionTableEntry = &MasterBootRecord->PartitionTable[Index];
            return TRUE;
        }
    }

    return FALSE;
}

static VOID
DiskMbrPartitionTableEntryToInformation(
    _Out_ PPARTITION_INFORMATION PartitionEntry,
    _In_ PPARTITION_TABLE_ENTRY PartitionTableEntry,
    _In_ ULONG PartitionNumber,
    _In_ ULONG SectorSize)
{
    PartitionEntry->StartingOffset.QuadPart  = (ULONGLONG)PartitionTableEntry->SectorCountBeforePartition * SectorSize;
    PartitionEntry->PartitionLength.QuadPart = (ULONGLONG)PartitionTableEntry->PartitionSectorCount * SectorSize;
    PartitionEntry->HiddenSectors = 0;
    PartitionEntry->PartitionNumber = PartitionNumber;
    PartitionEntry->PartitionType = PartitionTableEntry->SystemIndicator;
    PartitionEntry->BootIndicator = (PartitionTableEntry->BootIndicator == 0x80); // or "& 0x80"
    PartitionEntry->RecognizedPartition = TRUE;
    PartitionEntry->RewritePartition = FALSE;
}

static BOOLEAN
DiskGetActivePartitionEntry(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry,
    _Out_ PULONG ActivePartition)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    ULONG BootablePartitionCount = 0;
    ULONG CurrentPartitionNumber;
    ULONG Index;

    ASSERT(SectorSize >= 512);

    *ActivePartition = 0;

    /* Read master boot record */
    if (!DiskReadBootRecord(DeviceId, 0, &MasterBootRecord))
        return FALSE;

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < 4; Index++)
    {
        PPARTITION_TABLE_ENTRY PartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (PartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            PartitionTableEntry->SystemIndicator != PARTITION_EXTENDED &&
            PartitionTableEntry->SystemIndicator != PARTITION_XINT13_EXTENDED)
        {
            CurrentPartitionNumber++;

            /* Test if this is the bootable partition */
            if (PartitionTableEntry->BootIndicator == 0x80)
            {
                BootablePartitionCount++;
                *ActivePartition = CurrentPartitionNumber;

                /* Copy the partition table entry */
                if (PartitionEntry)
                    DiskMbrPartitionTableEntryToInformation(PartitionEntry, PartitionTableEntry, CurrentPartitionNumber, SectorSize);
            }
        }
    }

    /* Make sure there was only one bootable partition */
    if (BootablePartitionCount == 0)
    {
        ERR("No bootable (active) partitions found.\n");
        return FALSE;
    }
    else if (BootablePartitionCount != 1)
    {
        ERR("Too many bootable (active) partitions found.\n");
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
DiskGetMbrPartitionEntry(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_ PPARTITION_INFORMATION PartitionEntry)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    PPARTITION_TABLE_ENTRY PartitionTableEntry;
    PARTITION_TABLE_ENTRY ExtendedPartitionTableEntry;
    ULONG ExtendedPartitionNumber;
    ULONG ExtendedPartitionOffset;
    ULONG Index;
    ULONG CurrentPartitionNumber;

    ASSERT(SectorSize >= 512);

    if (PartitionNumber == 0)
        return FALSE;

    /* Read master boot record */
    if (!DiskReadBootRecord(DeviceId, 0, &MasterBootRecord))
        return FALSE;

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < 4; Index++)
    {
        PartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (PartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            PartitionTableEntry->SystemIndicator != PARTITION_EXTENDED &&
            PartitionTableEntry->SystemIndicator != PARTITION_XINT13_EXTENDED)
        {
            CurrentPartitionNumber++;
        }

        if (PartitionNumber == CurrentPartitionNumber)
        {
            DiskMbrPartitionTableEntryToInformation(PartitionEntry, PartitionTableEntry, PartitionNumber, SectorSize);
            return TRUE;
        }
    }

    /*
     * They want an extended partition entry so we will need
     * to loop through all the extended partitions on the disk
     * and return the one they want.
     */
    ExtendedPartitionNumber = PartitionNumber - CurrentPartitionNumber - 1;

    /*
     * Set the initial relative starting sector to 0.
     * This is because extended partition starting
     * sectors a numbered relative to their parent.
     */
    ExtendedPartitionOffset = 0;

    for (Index = 0; Index <= ExtendedPartitionNumber; Index++)
    {
        /* Get the extended partition table entry */
        if (!DiskGetFirstExtendedPartitionEntry(&MasterBootRecord, &ExtendedPartitionTableEntry))
            return FALSE;

        /* Adjust the relative starting sector of the partition */
        ExtendedPartitionTableEntry.SectorCountBeforePartition += ExtendedPartitionOffset;
        if (ExtendedPartitionOffset == 0)
        {
            /* Set the start of the parent extended partition */
            ExtendedPartitionOffset = ExtendedPartitionTableEntry.SectorCountBeforePartition;
        }
        /* Read the partition boot record */
        if (!DiskReadBootRecord(DeviceId, ExtendedPartitionTableEntry.SectorCountBeforePartition, &MasterBootRecord))
            return FALSE;

        /* Get the first real partition table entry */
        if (!DiskGetFirstPartitionEntry(&MasterBootRecord, &PartitionTableEntry))
            return FALSE;

        /* Now correct the start sector of the partition */
        PartitionTableEntry->SectorCountBeforePartition += ExtendedPartitionTableEntry.SectorCountBeforePartition;
    }

    /*
     * When we get here we should have the correct entry already
     * stored in PartitionTableEntry, so just return TRUE.
     */
    DiskMbrPartitionTableEntryToInformation(PartitionEntry, PartitionTableEntry, PartitionNumber, SectorSize);
    return TRUE;
}

static BOOLEAN
DiskGetBrfrPartitionEntry(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry)
{
    UCHAR Buffer[512];

    ASSERT(SectorSize >= 512);

    /*
     * Get partition entry of an Xbox-standard BRFR partitioned disk.
     */
    if (!(1 <= PartitionNumber && PartitionNumber <= RTL_NUMBER_OF(XboxPartitions)) ||
        DiskReadSector(DeviceId, XBOX_SIGNATURE_SECTOR, sizeof(Buffer), Buffer) != ESUCCESS)
    {
        /* Partition does not exist */
        return FALSE;
    }

    if (*((PULONG)Buffer) != XBOX_SIGNATURE)
    {
        /* No magic Xbox partitions */
        return FALSE;
    }

    if (PartitionEntry)
    {
        // RtlZeroMemory(PartitionEntry, sizeof(*PartitionEntry));
        PartitionEntry->StartingOffset.QuadPart  = (ULONGLONG)XboxPartitions[PartitionNumber - 1].SectorStart * SectorSize;
        PartitionEntry->PartitionLength.QuadPart = (ULONGLONG)XboxPartitions[PartitionNumber - 1].SectorCount * SectorSize;
        PartitionEntry->HiddenSectors = 0;
        PartitionEntry->PartitionNumber = PartitionNumber;
        PartitionEntry->PartitionType = XboxPartitions[PartitionNumber - 1].PartitionType;
        PartitionEntry->BootIndicator = (PartitionNumber == FATX_DATA_PARTITION);
        PartitionEntry->RecognizedPartition = TRUE;
        PartitionEntry->RewritePartition = FALSE;
    }
    return TRUE;
}


/**
 * @brief
 * Detects the partitioning scheme used on the specified device.
 **/
static PARTITION_STYLE
DiskDetectPartitionStyle(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize)
{
    MASTER_BOOT_RECORD MasterBootRecord;

    /* Probe for Master Boot Record */
    if (DiskReadBootRecord(DeviceId, 0, &MasterBootRecord))
    {
        PARTITION_STYLE PartitionStyle = PARTITION_STYLE_MBR;
        ULONG Index;
        ULONG PartitionCount = 0;
        BOOLEAN GPTProtect = FALSE;

        /* Check for GUID Partition Table */
        for (Index = 0; Index < 4; Index++)
        {
            PPARTITION_TABLE_ENTRY PartitionTableEntry = &MasterBootRecord.PartitionTable[Index];
            if (PartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED)
            {
                PartitionCount++;

                if (Index == 0 && PartitionTableEntry->SystemIndicator == PARTITION_GPT)
                    GPTProtect = TRUE;
            }
        }

        if (PartitionCount == 1 && GPTProtect)
            PartitionStyle = PARTITION_STYLE_GPT;

        TRACE("Device 0x%lx partition style %s\n", DeviceId, PartitionStyle == PARTITION_STYLE_MBR ? "MBR" : "GPT");
        return PartitionStyle;
    }

    /* Probe for Xbox-BRFR partitioning */
    if (DiskGetBrfrPartitionEntry(DeviceId, SectorSize, FATX_DATA_PARTITION, NULL))
    {
        TRACE("Device 0x%lx partition style Xbox-BRFR\n", DeviceId);
        return PARTITION_STYLE_BRFR;
    }

    /* Failed to detect partitions, assume unpartitioned disk */
    TRACE("Device 0x%lx partition style unknown\n", DeviceId);
    return PARTITION_STYLE_RAW;
}

// TODO: Move this function into hwdisk.c
/**
 * @brief
 * Find the "boot" partition of the given disk.
 * - For MBR this is the single active partition.
 * - For XBOX disks this is always the "first" partition.
 *
 * NOT YET SUPPORTED:
 * - GPT disks aren't supported yet. The "boot" partition may be
 *   an EFI system partition, or any other one.
 * - On ARC systems, the case is similar to EFI systems (not supported).
 **/
BOOLEAN
DiskGetBootPartitionEntry(
    _In_opt_ PCSTR DiskDeviceName,
    _In_opt_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    // _In_ PARTITION_STYLE PartitionStyle,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry,
    _Out_ PULONG BootPartition)
{
    BOOLEAN Success = FALSE;
    PARTITION_STYLE PartitionStyle;

    if (SectorSize < 512)
        return FALSE;

    if ((DiskDeviceName && DeviceId != INVALID_FILE_ID) ||
        (!DiskDeviceName && DeviceId == INVALID_FILE_ID))
    {
        ERR("Invalid parameters\n");
        return FALSE;
    }

    if (DiskDeviceName)
    {
        ARC_STATUS Status;
        Status = ArcOpen((PSTR)DiskDeviceName, OpenReadOnly, &DeviceId);
        if (Status != ESUCCESS)
        {
            ERR("Couldn't open %s, Status %lu\n", DiskDeviceName, Status);
            return FALSE;
        }
    }

    /* Determine the partitioning scheme */
    PartitionStyle = DiskDetectPartitionStyle(DeviceId, SectorSize);

    switch (PartitionStyle)
    {
        case PARTITION_STYLE_MBR:
        {
            Success = DiskGetActivePartitionEntry(DeviceId, SectorSize, PartitionEntry, BootPartition);
            break;
        }
        case PARTITION_STYLE_GPT:
        {
            FIXME("DiskGetBootPartitionEntry() unimplemented for GPT\n");
            break; // Success = FALSE;
        }
        case PARTITION_STYLE_RAW:
        {
            FIXME("DiskGetBootPartitionEntry() unimplemented for RAW\n");
            break; // Success = FALSE;
        }
        case PARTITION_STYLE_BRFR:
        {
            Success = DiskGetBrfrPartitionEntry(DeviceId, SectorSize, FATX_DATA_PARTITION, PartitionEntry);
            if (Success)
                *BootPartition = FATX_DATA_PARTITION;
            break;
        }
        default:
        {
            ERR("Device 0x%lx partition style = %u, should not happen!\n", DeviceId, PartitionStyle);
            ASSERT(FALSE);
        }
    }

    if (DiskDeviceName)
        ArcClose(DeviceId);

    return Success;
}

// FIXME: Use a partitioning-style-independent partition entry buffer.
BOOLEAN
DiskGetPartitionEntry(
    _In_opt_ PCSTR DiskDeviceName,
    _In_opt_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    // _In_ PARTITION_STYLE PartitionStyle,
    _In_ ULONG PartitionNumber,
    _Out_ PPARTITION_INFORMATION PartitionEntry)
{
    BOOLEAN Success = FALSE;
    PARTITION_STYLE PartitionStyle;

    if (SectorSize < 512)
        return FALSE;

    if ((DiskDeviceName && DeviceId != INVALID_FILE_ID) ||
        (!DiskDeviceName && DeviceId == INVALID_FILE_ID))
    {
        ERR("Invalid parameters\n");
        return FALSE;
    }

    if (DiskDeviceName)
    {
        ARC_STATUS Status;
        Status = ArcOpen((PSTR)DiskDeviceName, OpenReadOnly, &DeviceId);
        if (Status != ESUCCESS)
        {
            ERR("Couldn't open %s, Status %lu\n", DiskDeviceName, Status);
            return FALSE;
        }
    }

    /* Determine the partitioning scheme */
    PartitionStyle = DiskDetectPartitionStyle(DeviceId, SectorSize);

    switch (PartitionStyle)
    {
        case PARTITION_STYLE_MBR:
        {
            Success = DiskGetMbrPartitionEntry(DeviceId, SectorSize, PartitionNumber, PartitionEntry);
            break;
        }
        case PARTITION_STYLE_GPT:
        {
            FIXME("DiskGetPartitionEntry() unimplemented for GPT\n");
            break; // Success = FALSE;
        }
        case PARTITION_STYLE_RAW:
        {
            FIXME("DiskGetPartitionEntry() unimplemented for RAW\n");
            break; // Success = FALSE;
        }
        case PARTITION_STYLE_BRFR:
        {
            Success = DiskGetBrfrPartitionEntry(DeviceId, SectorSize, PartitionNumber, PartitionEntry);
            break;
        }
        default:
        {
            ERR("Device 0x%lx partition style = %u, should not happen!\n", DeviceId, PartitionStyle);
            ASSERT(FALSE);
        }
    }

    if (DiskDeviceName)
        ArcClose(DeviceId);

    return Success;
}

BOOLEAN
DiskGetPartitionSize(
    _In_opt_ PCSTR DiskDeviceName,
    _In_opt_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    // _In_ PARTITION_STYLE PartitionStyle,
    _In_ ULONG PartitionNumber,
    _Out_ PULONGLONG StartingOffset,
    _Out_ PULONGLONG Length)
{
    PARTITION_INFORMATION PartitionEntry;
    BOOLEAN Success;

    *StartingOffset = 0ULL;
    *Length = 0ULL;

    Success = DiskGetPartitionEntry(DiskDeviceName, DeviceId, SectorSize, PartitionNumber, &PartitionEntry);
    if (Success)
    {
        *StartingOffset = PartitionEntry.StartingOffset.QuadPart;
        *Length = PartitionEntry.PartitionLength.QuadPart;
    }
    return Success;
}

ARC_STATUS
PartRegisterDevicePartitions(
    _In_ PCSTR DeviceName,
    _In_ const DEVVTBL* FuncTable,
    _In_ ULONG SectorSize,
    _Out_opt_ PULONG NumPartitions)
{
    ARC_STATUS Status;
    NTSTATUS NtStatus;
    ULONG DeviceId;
#if 0
    NTSTATUS ret;
    PDRIVE_LAYOUT_INFORMATION PartitionBuffer;
#else
    // PARTITION_STYLE PartitionStyle;
    PARTITION_INFORMATION PartitionEntry;
#endif
    ULONG i;
    CHAR PartitionName[MAX_PATH];

    if (NumPartitions)
        *NumPartitions = 0;

    if (SectorSize < 512)
        return EINVAL;

    /* Register the device with partition(0) suffix ("Disk FDO") */
    NtStatus = RtlStringCbPrintfA(PartitionName, sizeof(PartitionName), "%spartition(0)", DeviceName);
    if (!NT_SUCCESS(NtStatus))
        return ENAMETOOLONG;
    FsRegisterDevice(PartitionName, FuncTable);

    /* Read the device partition table */
    Status = ArcOpen((PSTR)/*DeviceName*/PartitionName, OpenReadOnly, &DeviceId);
    if (Status != ESUCCESS)
    {
        ERR("Couldn't open %s, Status %lu\n", /*DeviceName*/PartitionName, Status);
        return Status;
    }

#if 0
// Method using HAL/NTOS functions, not yet GPT-compatible...
    ret = HALDISPATCH->HalIoReadPartitionTable((PDEVICE_OBJECT)(ULONG_PTR)DeviceId,
                                               SectorSize, FALSE, &PartitionBuffer);
    if (NT_SUCCESS(ret))
    {
        for (i = 0; i < PartitionBuffer->PartitionCount; ++i)
        {
            if (PartitionBuffer->PartitionEntry[i].PartitionType == PARTITION_ENTRY_UNUSED)
                continue;

            NtStatus = RtlStringCbPrintfA(PartitionName, sizeof(PartitionName),
                                          "%spartition(%lu)",
                                          DeviceName,
                                          PartitionBuffer->PartitionEntry[i].PartitionNumber);
            if (!NT_SUCCESS(NtStatus))
            {
                Status = ENAMETOOLONG;
                break;
            }
            FsRegisterDevice(PartitionName, FuncTable); // Or use partition-specific FuncTable
        }
        ExFreePool(PartitionBuffer);
    }
#else
    /* Determine the partitioning scheme */
    // PartitionStyle = DiskDetectPartitionStyle(DeviceId, SectorSize);

    /* Add partitions */
    i = 1;
    while (DiskGetPartitionEntry(NULL, DeviceId, SectorSize, /*PartitionStyle,*/ i, &PartitionEntry))
    {
        if (PartitionEntry.PartitionType != PARTITION_ENTRY_UNUSED)
        {
            NtStatus = RtlStringCbPrintfA(PartitionName, sizeof(PartitionName),
                                          "%spartition(%lu)",
                                          DeviceName, i);
            if (!NT_SUCCESS(NtStatus))
            {
                Status = ENAMETOOLONG;
                break;
            }
            FsRegisterDevice(PartitionName, FuncTable); // Or use partition-specific FuncTable
        }
        ++i;
    }
    --i; // Adjust for return value.
#endif

    ArcClose(DeviceId);

    if ((Status == ESUCCESS) && NumPartitions)
        *NumPartitions = i;
    return Status;
}

#endif
