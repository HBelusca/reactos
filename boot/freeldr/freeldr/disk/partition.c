/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Block Device Partition Management. Implements the partition()
 *              protocol for MBR, GPT, and XBOX partitioned block devices.
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2010 Hervé Poussineau <hpoussin@reactos.org>
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
#define XBOX_SIGNATURE_SECTOR   3
#define XBOX_SIGNATURE          ('B' | ('R' << 8) | ('F' << 16) | ('R' << 24))

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

#if 0 // TODO: Investigate

/* NEC PC-98 IPL1 signature at disk offset 4 */
#define IPL1_SIGNATURE_OFFSET   4
#define IPL1_SIGNATURE          ('I' | ('P' << 8) | ('L' << 16) | ('1' << 24))
#define IPL1_NUM_PARTITION_TABLE_ENTRIES    8

#include <pshpack1.h>

typedef struct _IPL1_BOOT_RECORD
{
    UCHAR JumpBoot[4];          /* 0x000 */
    ULONG IPL1Signature;        /* 0x004 - "IPL1" */
    UCHAR CodeAndData[0xF6];    /* 0x008 */
    USHORT BootRecordMagic;     /* 0x0FE */
    // ULONG Signature;         /* 0x100 ??? */
    PARTITION_TABLE_ENTRY PartitionTable[16];  /* 0x1BE */
    USHORT BootRecordMagic;              /* 0x1FE */
} IPL1_BOOT_RECORD, *PIPL1_BOOT_RECORD;

/* PC-98 IPL1 partition table entry.
 * Taken from GNU Parted source code.
 * Maximum 16 entries. */
typedef struct _PC98RawPartition
{
    uint8_t     mid;        /* 0x00: unused, 0x20: MS-DOS(not bootable), 0xa1-0xaf: MS-DOS(bootable) */
    uint8_t     sid;        /* 0x00: unused, 0x81,0x91,0xa1,0xe1: MS-DOS(active), 0x01,0x11,0x21,0x61: MS-DOS(sleep) */
    uint8_t     dum1;       /* dummy for padding */
    uint8_t     dum2;       /* dummy for padding */
    uint8_t     ipl_sect;   /* IPL sector */
    uint8_t     ipl_head;   /* IPL head */
    uint16_t    ipl_cyl;    /* IPL cylinder */
    uint8_t     sector;     /* starting sector */
    uint8_t     head;       /* starting head */
    uint16_t    cyl;        /* starting cylinder */
    uint8_t     end_sector; /* end sector */
    uint8_t     end_head;   /* end head */
    uint16_t    end_cyl;    /* end cylinder */
    char        name[16];
} PC98RawPartition, *PPC98RawPartition;

#include <poppack.h>

#endif // IPL1


/* FUNCTIONS *****************************************************************/

static PCSTR
PartTypeToName(
    _In_ PARTITION_STYLE PartitionStyle)
{
    static CHAR Unknown[] = "Unknown (0x??)";
    switch (PartitionStyle)
    {
    case PARTITION_STYLE_MBR:
        return "MBR";
    case PARTITION_STYLE_GPT:
        return "GPT";
    case PARTITION_STYLE_RAW:
        return "RAW";
#ifdef __REACTOS__
    /* ReactOS custom partition handlers */
    case PARTITION_STYLE_IPL1:
        return "NEC PC-98 IPL1";
    case PARTITION_STYLE_BRFR:
        return "Xbox-BRFR";
#endif
    default:
        sprintf(Unknown, "Unknown (0x%02x)", (UCHAR)PartitionStyle);
        return Unknown;
    }
}

#if DBG
static VOID
DumpMbrPartitionTable(
    _In_ ULONG DeviceId,
    _In_ ULONGLONG LogicalSectorNumber,
    _Out_ PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    TRACE("Dumping MBR partition table for drive 0x%x:\n", DeviceId);
    TRACE("Boot record logical start sector = %llu\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < MBR_NUM_PARTITION_TABLE_ENTRIES; ++Index)
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
}

#if 0 // TODO: Investigate
static VOID
DumpIPL1PartitionTable(
    _In_ ULONG DeviceId,
    _In_ ULONGLONG LogicalSectorNumber,
    _Out_ PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    TRACE("Dumping IPL1 partition table for drive 0x%x:\n", DeviceId);
    TRACE("Boot record logical start sector = %llu\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < IPL1_NUM_PARTITION_TABLE_ENTRIES; ++Index)
    {
        PPARTITION_TABLE_ENTRY PartitionTableEntry = &BootRecord->PartitionTable[IPL1_NUM_PARTITION_TABLE_ENTRIES - Index];

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

        // TODO: Dump the PC-98-specific info found in the second sector.
    }
}
#endif
#endif

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

#define MBR_NUM_PARTITION_TABLE_ENTRIES 4

#ifndef IsContainerPartition
#define IsContainerPartition(PartitionType) \
  ( ((PartitionType) == PARTITION_EXTENDED) || \
    ((PartitionType) == PARTITION_XINT13_EXTENDED) )
#endif

#define GET_STARTING_SECTOR(p) \
    ((p)->SectorCountBeforePartition)

#define GET_PARTITION_LENGTH(p) \
    ((p)->PartitionSectorCount)

static BOOLEAN
DiskReadBootRecord(
    _In_ ULONG DeviceId,
    _In_ ULONGLONG LogicalSectorNumber,
    _Out_ PMASTER_BOOT_RECORD BootRecord)
{
    /* Read one sector (master boot record) from a given position */
    if (DiskReadSector(DeviceId, LogicalSectorNumber,
                       sizeof(MASTER_BOOT_RECORD), BootRecord) != ESUCCESS)
    {
        return FALSE;
    }

#if DBG
    DumpMbrPartitionTable();
#endif

    /* Check the partition table magic value */
    return (BootRecord->MasterBootRecordMagic == 0xaa55);
}

static VOID
DiskMbrPartitionTableEntryToInformation(
    _Out_ PPARTITION_INFORMATION PartitionEntry,
    _In_ PPARTITION_TABLE_ENTRY PartitionTableEntry,
    _In_ ULONG PartitionNumber,
    _In_ ULONG SectorSize)
{
    PartitionEntry->StartingOffset.QuadPart  = (ULONGLONG)GET_STARTING_SECTOR(PartitionTableEntry) * SectorSize;
    PartitionEntry->PartitionLength.QuadPart = (ULONGLONG)GET_PARTITION_LENGTH(PartitionTableEntry) * SectorSize;
    PartitionEntry->HiddenSectors = 0;
    PartitionEntry->PartitionNumber = PartitionNumber;
    PartitionEntry->PartitionType = PartitionTableEntry->SystemIndicator;
    PartitionEntry->BootIndicator = (PartitionTableEntry->BootIndicator == 0x80); // or "& 0x80"
    PartitionEntry->RecognizedPartition = TRUE;
    PartitionEntry->RewritePartition = FALSE;
}

// On MBR the "active" partition can only be a primary one.
static BOOLEAN
DiskGetMbrActivePartitionEntry(
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
    for (Index = 0; Index < MBR_NUM_PARTITION_TABLE_ENTRIES; Index++)
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
IsValidPartitionEntry(
    _In_ PPARTITION_DESCRIPTOR Entry,
    _In_ ULONGLONG MaxOffset,
    _In_ ULONGLONG MaxSector)
{
    ULONGLONG EndingSector;

    /* Unused partitions are considered valid */
    if (Entry->PartitionType == PARTITION_ENTRY_UNUSED)
        return TRUE;

    /* Get the last sector of the partition */
    EndingSector = GET_STARTING_SECTOR(Entry) + GET_PARTITION_LENGTH(Entry);

    /* Check if it's more then the maximum sector */
    if (EndingSector > MaxSector)
    {
        /* Invalid partition */
        ERR("Entry is invalid\n");
        ERR("\toffset %#08lx\n", GET_STARTING_SECTOR(Entry));
        ERR("\tlength %#08lx\n", GET_PARTITION_LENGTH(Entry));
        ERR("\tend %#I64x\n", EndingSector);
        ERR("\tmax %#I64x\n", MaxSector);
        return FALSE;
    }
    else if (GET_STARTING_SECTOR(Entry) > MaxOffset)
    {
        /* Invalid partition */
        ERR("Entry is invalid\n");
        ERR("\toffset %#08lx\n", GET_STARTING_SECTOR(Entry));
        ERR("\tlength %#08lx\n", GET_PARTITION_LENGTH(Entry));
        ERR("\tend %#I64x\n", EndingSector);
        ERR("\tmaxOffset %#I64x\n", MaxOffset);
        return FALSE;
    }

    /* It's fine, return success */
    return TRUE;
}

static BOOLEAN
DiskGetMbrPartitionEntry(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_ PPARTITION_INFORMATION PartitionEntry)
{
    MASTER_BOOT_RECORD BootRecord;
    PPARTITION_TABLE_ENTRY PartitionDescriptor;
    ULONG Index;
    BOOLEAN IsValid /*, IsEmpty = TRUE*/;

    ASSERT(SectorSize >= 512);
    ASSERT(PartitionNumber >= 1);

    /*
     * REMARKS about MBR partitions:
     *
     * - The MBR is the primary partition table. It contains 4 entries for
     *   primary partitions, where at most 1 is for an extended partition.
     *   The entry for an extended partition links to an extended partition
     *   record (EBR).
     *
     * - About EBRs and extended partitions:
     *   Extended partition records (EBRs) are link-listed with each other.
     *   It is commonly documented that in EBRs, the first partition entry
     *   points to the logical volume data, the second partition entry points
     *   to the next EBR (and has type 0x05 or 0x0F), while both third and
     *   fourth entries are unused.
     *   However, it can be observed by experiments that e.g. Windows NT
     *   supports the two valid entries to be in any of the four available
     *   EBR partition slots. For example, the entry pointing to the next EBR
     *   may be in the third slot, while the entry pointing to the logical
     *   volume data is on the fourth slot (or the second slot, or...).
     */
    ULONG CurrentPartitionNumber = 0;

    ULONGLONG StartSector = 0ULL;
    PPARTITION_TABLE_ENTRY ExtendedPartitionDescriptor = NULL; // The single extended partition we may encounter next.
    /*
     * Set the initial relative starting sector to 0.
     * This is because extended partition starting
     * sectors a numbered relative to their parent.
     */
    ULONG ExtendedPartitionOffset = 0; // TODO: Rename (VolumeOffset). Offset of where the extended "volume" starts (from which each subsequent EPBR is relative).

    // TODO: GetFullGeometry of the drive.

#if DBG
    ULONG BootRecordIndex = 0; // Partition table index
#endif
    do
    {
        /* Read the Master Boot Record (StartSector == 0) or the
         * next Extended Partition Boot Record (StartSector != 0) */
        if (!DiskReadBootRecord(DeviceId, StartSector, &BootRecord))
            return FALSE;

        /* Assume the partition table is valid */
        IsValid = TRUE;

        ExtendedPartitionDescriptor = NULL;
        ULONG CountExtendedPartitions = 0; // Must be <= 1

        TRACE("Partition Table %lu:\n", BootRecordIndex);
        for (Index = 0; Index < MBR_NUM_PARTITION_TABLE_ENTRIES; Index++)
        {
            PartitionDescriptor = &BootRecord.PartitionTable[Index];
            UCHAR PartitionType = PartitionDescriptor->SystemIndicator;

            TRACE("Partition Entry %lu,%lu: type %#x %s\n",
                  BootRecordIndex, (Index + 1),
                  PartitionType,
                  (PartitionDescriptor->BootIndicator/*ActiveFlag*/) ? "Active" : "");
            TRACE("\tOffset %#08lx for %#08lx Sectors\n",
                  GET_STARTING_SECTOR(PartitionDescriptor),
                  GET_PARTITION_LENGTH(PartitionDescriptor));

#if DBG // && ....
// Do extended diagnostics: If we are in an extended partition boot record,
// verify that 1st partition is "data", 2nd partition is extended or unused,
// and 3rd and 4th slots are unused. Otherwise warn that we have a disk that
// may not be supported by all OSes.

/* The extended partition table entry is anywhere in the 4 slots
 * in the initial MBR, otherwise it's the 2nd entry in an existing EBR;
 * if the EBR chain stops, the 2nd entry is zeroed out. */
#endif

#if 0
            /* Make sure that the partition entry is valid; fail only
             * if it resides on the primary partition table */
            if (!IsValidPartitionEntry(PartitionDescriptor,
                                       DiskGeometryEx.DiskSize.QuadPart,
                                       MaxSector) && (BootRecordIndex == 0))
            {
                /* It's invalid, so fail */
                IsValid = FALSE;
                break;
            }
#endif

            /* Verify the count of extended partitions listed in this boot record */
            if (IsContainerPartition(PartitionType))
            {
                CountExtendedPartitions++;
                if (CountExtendedPartitions > 1)
                {
                    /* More than one table is invalid */
                    ERR("Multiple container partitions found in "
                        "partition table %lu\n - table is invalid\n",
                        BootRecordIndex);
                    IsValid = FALSE;
                    break;
                }
                /* This is the next extended partition we will have to enumerate! */
                ExtendedPartitionDescriptor = PartitionDescriptor;
            }


            /* We only care about "recognized" partitions */
            if ((PartitionType == PARTITION_ENTRY_UNUSED) || IsContainerPartition(PartitionType))
                continue;

            ++CurrentPartitionNumber;

#if 0
            // For DATA partitions: actual starting sector is ==
            // start-sector from the beginning of disk + their relative start sector.
            StartSector + GET_STARTING_SECTOR(PartitionDescriptor);

            // For sub-EXTENDED partitions: actual starting sector is ==
            // offset of the first extended partition + their relative start sector.
            ExtendedPartitionOffset + GET_STARTING_SECTOR(PartitionDescriptor);
#endif

            if (PartitionNumber == CurrentPartitionNumber)
            {
                /* Correct the start sector of the partition */
                PartitionDescriptor->SectorCountBeforePartition += StartSector;
                DiskMbrPartitionTableEntryToInformation(PartitionEntry, PartitionDescriptor,
                                                        PartitionNumber, SectorSize);
                return TRUE;
            }
        }

        /* Finish debug log, and check for failure */
        TRACE("\n");
        // if (!NT_SUCCESS(Status)) break;

        /* Also check if we hit an invalid entry here */
        if (!IsValid)
        {
            // /* We did, so break out of the loop minus one entry */
            // BootRecordIndex--;
            break;
        }

        StartSector = 0ULL;

        /* Do we have an extended partition to look for next? If so, get where
         * we should restart reading the next boot sector, and loop again */
        if (ExtendedPartitionDescriptor)
        {
            /* Adjust the relative partition starting sector */
            StartSector = GET_STARTING_SECTOR(ExtendedPartitionDescriptor);
            StartSector += ExtendedPartitionOffset;

            /* If this is the very first extended partition,
             * parent of all the others, this is its start */
            if (ExtendedPartitionOffset == 0)
                ExtendedPartitionOffset = StartSector;

            //////// /* Also update the maximum sector */
            //////// MaxSector = GET_PARTITION_LENGTH(PartitionDescriptor);
            //////// TRACE("FSTUB: MaxSector now = %I64d\n", MaxSector);
        }

    } while (ExtendedPartitionDescriptor != NULL); // i.e. (StartSector != 0ULL)

    /* We haven't found the sought partition, bail out */
    return FALSE;
}

#if 0 // TODO: Investigate
static BOOLEAN
DiskGetIPL1PartitionEntry(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry)
{
    UCHAR Buffer[512];
    ULONG Index;

    ASSERT(SectorSize >= 512);
    ASSERT(PartitionNumber >= 1);

    /*
     * Get partition entry of a NEC PC-98 IPL1 partitioned disk.
     */
    if (DiskReadSector(DeviceId, 0, sizeof(Buffer), Buffer) != ESUCCESS)
        return FALSE;
    if (*(PULONG)(Buffer + IPL1_SIGNATURE_OFFSET) != IPL1_SIGNATURE)
        return FALSE; /* Not an IPL1 disk */
    // if (!((*(PUSHORT)(Buffer + 0xEE) == 0xaa55 || *(PUSHORT)(Buffer + 0xFE) == 0xaa55) && *(PUSHORT)(Buffer + 0x1FE) == 0xaa55))
    //     return FALSE; /* Missing end-of-sector signatures */

    /* Contrary to MBR disks, IPL1 disks enumerate their partitions from bottom up */
    for (Index = 0; Index < IPL1_NUM_PARTITION_TABLE_ENTRIES; ++Index)
    {
        IPL1_NUM_PARTITION_TABLE_ENTRIES
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
#endif // IPL1

static BOOLEAN
DiskGetBrfrPartitionEntry(
    _In_ ULONG DeviceId,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry)
{
    UCHAR Buffer[512];

    ASSERT(SectorSize >= 512);
    ASSERT(PartitionNumber >= 1);

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
        return FALSE; /* No magic Xbox partitions */

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
    PARTITION_STYLE PartitionStyle;

    ASSERT(SectorSize >= 512);

    /* Probe for Master Boot Record */
    if (DiskReadBootRecord(DeviceId, 0, &MasterBootRecord))
    {
        ULONG Index;
        ULONG PartitionCount = 0;
        BOOLEAN GPTProtect = FALSE;

        /* Determine whether the disk uses standard MBR, NEC PC-98 IPL1, or GPT scheme */

        if (*(PULONG)((PUCHAR)&MasterBootRecord + IPL1_SIGNATURE_OFFSET) == IPL1_SIGNATURE)
        {
            PartitionStyle = PARTITION_STYLE_IPL1;
            goto Quit;
        }

        PartitionStyle = PARTITION_STYLE_MBR;

        /* Check for GUID Partition Table */
        for (Index = 0; Index < MBR_NUM_PARTITION_TABLE_ENTRIES; Index++)
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
    }
    else /* Probe for Xbox-BRFR partitioning */
    if (DiskGetBrfrPartitionEntry(DeviceId, SectorSize, FATX_DATA_PARTITION, NULL))
    {
        PartitionStyle = PARTITION_STYLE_BRFR;
    }
    else
    {
        /* Failed to detect partitions, assume unpartitioned disk */
        PartitionStyle = PARTITION_STYLE_RAW;
    }

Quit:
    TRACE("Device 0x%lx partition style %s\n", DeviceId, PartTypeToName(PartitionStyle));
    return PartitionStyle;
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

    if ((DiskDeviceName && DeviceId != INVALID_FILE_ID) ||
        (!DiskDeviceName && DeviceId == INVALID_FILE_ID))
    {
        ERR("Invalid parameters\n");
        return FALSE;
    }

    if (SectorSize < 512)
        return FALSE;

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
            Success = DiskGetMbrActivePartitionEntry(DeviceId, SectorSize, PartitionEntry, BootPartition);
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
#if 0 // TODO: Investigate
        case PARTITION_STYLE_IPL1:
        {
            Success = DiskGetIPL1ActivePartitionEntry(DeviceId, SectorSize, PartitionEntry, BootPartition);
            break;
        }
#endif
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

    if ((DiskDeviceName && DeviceId != INVALID_FILE_ID) ||
        (!DiskDeviceName && DeviceId == INVALID_FILE_ID))
    {
        ERR("Invalid parameters\n");
        return FALSE;
    }

    if (SectorSize < 512)
        return FALSE;

    if (PartitionNumber == 0)
        return FALSE;

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
#if 0 // TODO: Investigate
        case PARTITION_STYLE_IPL1:
        {
            Success = DiskGetIPL1PartitionEntry(DeviceId, SectorSize, PartitionEntry, BootPartition);
            break;
        }
#endif
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
