/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     MBR partitioning scheme support
 * COPYRIGHT:   Copyright 2002-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2025-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#ifndef _M_ARM
// #include <freeldr.h>
#include "part_mbr.h"

// #include <debug.h>
// DBG_DEFAULT_CHANNEL(DISK);

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


#if DBG
/*static*/ VOID
DumpMbrPartitionTable(
    _In_ UCHAR DriveNumber,
    _In_ ULONGLONG LogicalSectorNumber,
    _Out_ PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    TRACE("Dumping MBR partition table for drive 0x%x:\n", DriveNumber);
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
#endif

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

// NOTE: On MBR the "active" partition can only be a primary one.
BOOLEAN // DiskGetMbrActivePartitionEntry
DiskGetActivePartitionEntry(
    _In_ UCHAR DriveNumber,
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
    if (!DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
        return FALSE;

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < MBR_NUM_PARTITION_TABLE_ENTRIES; Index++)
    {
        PPARTITION_TABLE_ENTRY PartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (PartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            !IsContainerPartition(PartitionTableEntry->SystemIndicator))
        {
            CurrentPartitionNumber++;

            /* Test if this is the bootable partition */
            if (PartitionTableEntry->BootIndicator == 0x80)
            {
                BootablePartitionCount++;
                *ActivePartition = CurrentPartitionNumber;

                /* Copy the partition table entry */
                if (PartitionEntry)
                {
                    DiskMbrPartitionTableEntryToInformation(PartitionEntry, PartitionTableEntry,
                                                            *ActivePartition, SectorSize);
                }
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

/*static*/ BOOLEAN
IsValidPartitionEntry(
    _In_ PPARTITION_TABLE_ENTRY Entry,
    _In_ ULONGLONG MaxOffset,
    _In_ ULONGLONG MaxSector)
{
    ULONGLONG EndingSector;

    /* Unused partitions are considered valid */
    if (Entry->SystemIndicator == PARTITION_ENTRY_UNUSED)
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

BOOLEAN
DiskGetMbrPartitionEntry(
    _In_ UCHAR DriveNumber,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_ PPARTITION_INFORMATION PartitionEntry)
{
    MASTER_BOOT_RECORD BootRecord;
    PPARTITION_TABLE_ENTRY PartitionDescriptor;
    ULONG Index;
    BOOLEAN IsValid /*, IsEmpty = TRUE*/;

    ASSERT(SectorSize >= 512);

    /* Validate partition number */
    if (PartitionNumber < 1) //if (PartitionNumber == 0)
        return FALSE;

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
        if (!DiskReadBootRecord(DriveNumber, StartSector, &BootRecord))
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

#endif
