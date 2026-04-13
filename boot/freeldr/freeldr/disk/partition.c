/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Block Device partition management
 * COPYRIGHT:   Copyright 2002-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2019 Stanislav Motylkov <x86corez@gmail.com>
 *              Copyright 2025-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#ifndef _M_ARM
#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define MaxDriveNumber 0xFF
static PARTITION_STYLE DiskPartitionType[MaxDriveNumber + 1];

#include "part_mbr.h" // FIXME: For MASTER_BOOT_RECORD

BOOLEAN
DiskReadBootRecord(
    IN UCHAR DriveNumber,
    IN ULONGLONG LogicalSectorNumber,
    OUT PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    /* Read master boot record */
    if (!MachDiskReadLogicalSectors(DriveNumber, LogicalSectorNumber, 1, DiskReadBuffer))
    {
        return FALSE;
    }
    RtlCopyMemory(BootRecord, DiskReadBuffer, sizeof(MASTER_BOOT_RECORD));

    TRACE("Dumping partition table for drive 0x%x:\n", DriveNumber);
    TRACE("Boot record logical start sector = %d\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x.\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < 4; Index++)
    {
        TRACE("-------------------------------------------\n");
        TRACE("Partition %d\n", (Index + 1));
        TRACE("BootIndicator: 0x%x\n", BootRecord->PartitionTable[Index].BootIndicator);
        TRACE("StartHead: 0x%x\n", BootRecord->PartitionTable[Index].StartHead);
        TRACE("StartSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].StartSector);
        TRACE("StartCylinder: 0x%x\n", BootRecord->PartitionTable[Index].StartCylinder);
        TRACE("SystemIndicator: 0x%x\n", BootRecord->PartitionTable[Index].SystemIndicator);
        TRACE("EndHead: 0x%x\n", BootRecord->PartitionTable[Index].EndHead);
        TRACE("EndSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].EndSector);
        TRACE("EndCylinder: 0x%x\n", BootRecord->PartitionTable[Index].EndCylinder);
        TRACE("SectorCountBeforePartition: 0x%x\n", BootRecord->PartitionTable[Index].SectorCountBeforePartition);
        TRACE("PartitionSectorCount: 0x%x\n", BootRecord->PartitionTable[Index].PartitionSectorCount);
    }

    /* Check the partition table magic value */
    return (BootRecord->MasterBootRecordMagic == 0xaa55);
}

#include "part_mbr.c"
#include "part_gpt.c"
#include "part_brfr.c"

typedef struct _BOOT_SECTOR_INFO
{
    UCHAR JumpByte[1];
    UCHAR Ignore1[2];
    UCHAR OemData[8];
    UCHAR BytesPerSector[2];
    UCHAR Ignore2[6];
    UCHAR NumberOfSectors[2];
    UCHAR MediaByte[1];
    UCHAR Ignore3[2];
    UCHAR SectorsPerTrack[2];
    UCHAR NumberOfHeads[2];
} BOOT_SECTOR_INFO, *PBOOT_SECTOR_INFO;

/**
 * @brief
 * Detects whether a device without a partition table is a "super-floppy",
 * i.e. is non-partitioned, has an 0xaa55 signature on it and has a recognized
 * BIOS Parameter Block (BPB).
 **/
static
BOOLEAN
DiskIsSuperFloppy(
    _In_ PMASTER_BOOT_RECORD BootRecord)
{
    BOOLEAN MbrFound = (BootRecord->MasterBootRecordMagic == 0xaa55);
    BOOLEAN IsSuperFloppy = FALSE;

    if (//(DeviceType == RemovableMedia) &&
        /*(j == 0) &&*/ MbrFound /*&& IsEmpty*/)
    {
        PBOOT_SECTOR_INFO BootSectorInfo = (PBOOT_SECTOR_INFO)Buffer;

        /* Read the jump bytes to detect super-floppy */
        if ((BootSectorInfo->JumpByte[0] == 0xeb) ||
            (BootSectorInfo->JumpByte[0] == 0xe9))
        {
            /* Super floppies don't have typical MBRs, so skip them */
            ERR("Jump byte %#x found along with empty partition table - disk is a super floppy and has no valid MBR\n",
                BootSectorInfo->JumpByte);
            IsSuperFloppy = TRUE;
        }
    }

    if (IsSuperFloppy)
    {
        /* Print out debugging information */
        DPRINT1("Drive %#p has no valid MBR. Make it into a super-floppy\n",
                BootRecord /*DriveNumber*/);
        DPRINT1("Drive has %I64d sectors and is %#016I64x bytes large\n",
                EndSector, DiskGeometryEx.DiskSize);
    }
    return IsSuperFloppy;
}

VOID
DiskDetectPartitionType(
    _In_ UCHAR DriveNumber)
{
    MASTER_BOOT_RECORD MasterBootRecord;

    /* Probe for Master Boot Record */
    if (DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
#if 0
        ULONG Index, PartitionCount = 0;
        BOOLEAN GPTProtect = FALSE;
#else
        GPT_TABLE_HEADER GptHeader;
#endif

        DiskPartitionType[DriveNumber] = PARTITION_STYLE_MBR;

#if 0
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
#else
        if (DiskReadGptHeader(DriveNumber, &GptHeader))
#endif
        {
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_GPT;
        }
        TRACE("Drive 0x%X partition type %s\n", DriveNumber, DiskPartitionType[DriveNumber] == PARTITION_STYLE_MBR ? "MBR" : "GPT");
        return;
    }

    /* Probe for Xbox-BRFR partitioning */
    if (DiskIsBrfr(DriveNumber))
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_BRFR;
        TRACE("Drive 0x%X partition type Xbox-BRFR\n", DriveNumber);
        return;
    }

    /* Failed to detect partitions, assume non-partitioned disk */
    DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
    TRACE("Drive 0x%X partition type unknown\n", DriveNumber);
}

// FIXME: This function is specific to BIOS-based PC platform.
BOOLEAN
DiskGetBootPartitionEntry(
    _In_ UCHAR DriveNumber,
    _Out_opt_ PPARTITION_INFORMATION PartitionEntry,
    _Out_ PULONG BootPartition)
{
#if 0
    GEOMETRY Geometry;
    if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
        return FALSE;
#endif

    switch (DiskPartitionType[DriveNumber])
    {
        case PARTITION_STYLE_MBR:
        {
            return DiskGetActivePartitionEntry(DriveNumber,
            /* MBR partition table always uses 512-byte sectors per specification */
                                               512, // Geometry.BytesPerSector
                                               PartitionEntry, BootPartition);
        }
        case PARTITION_STYLE_GPT:
        {
            FIXME("DiskGetBootPartitionEntry() unimplemented for GPT\n");
            return FALSE;
        }
        case PARTITION_STYLE_RAW:
        {
            PARTITION_INFORMATION TempPartitionEntry;
            if (!PartitionEntry)
                PartitionEntry = &TempPartitionEntry;

            /* Consider the whole disk to be bootable */
            if (DiskGetPartitionEntry(DriveNumber,
                                      512, // Geometry.BytesPerSector
                                      0,
                                      PartitionEntry))
            {
                *BootPartition = 0;
                return TRUE;
            }
            return FALSE;
        }
        case PARTITION_STYLE_BRFR:
        {
            PARTITION_INFORMATION TempPartitionEntry;
            if (!PartitionEntry)
                PartitionEntry = &TempPartitionEntry;
            if (DiskGetBrfrPartitionEntry(DriveNumber,
                                          512, // Geometry.BytesPerSector
                                          FATX_DATA_PARTITION,
                                          PartitionEntry))
            {
                *BootPartition = FATX_DATA_PARTITION;
                return TRUE;
            }
            return FALSE;
        }
        default:
        {
            ERR("Drive 0x%X partition type = %d, should not happen!\n", DriveNumber, DiskPartitionType[DriveNumber]);
            ASSERT(FALSE);
        }
    }
    return FALSE;
}

BOOLEAN
DiskGetPartitionEntry(
    _In_ UCHAR DriveNumber,
    _In_opt_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _Out_ PPARTITION_INFORMATION PartitionEntry)
{
    if (SectorSize == 0)
    {
        GEOMETRY Geometry;
        if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
            return FALSE;
        SectorSize = Geometry.BytesPerSector;
    }
    if (SectorSize < 512)
    {
        ERR("Drive 0x%X: Invalid sector size %lu\n", DriveNumber, SectorSize);
        return FALSE;
    }

    switch (DiskPartitionType[DriveNumber])
    {
        case PARTITION_STYLE_MBR:
        {
            return DiskGetMbrPartitionEntry(DriveNumber,
            /* MBR partition table always uses 512-byte sectors per specification */
                                            512, // SectorSize
                                            PartitionNumber,
                                            PartitionEntry);
        }
        case PARTITION_STYLE_GPT:
        {
            return DiskGetGptPartitionEntry(DriveNumber,
                                            SectorSize,
                                            PartitionNumber,
                                            PartitionEntry);
        }
        case PARTITION_STYLE_RAW:
        {
            GEOMETRY Geometry;

            if (PartitionNumber != 0)
                return FALSE;

            ERR("DiskGetPartitionEntry() returning whole-disk partition\n");
            if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
                return FALSE;

            /* Represent the whole disk */
            PartitionEntry->StartingOffset.QuadPart  = 0ULL;
            PartitionEntry->PartitionLength.QuadPart = (Geometry.Sectors * Geometry.BytesPerSector);
            PartitionEntry->HiddenSectors = 0;
            PartitionEntry->PartitionNumber = PartitionNumber;
            PartitionEntry->PartitionType = PARTITION_FAT_16;
            PartitionEntry->BootIndicator = TRUE;
            PartitionEntry->RecognizedPartition = TRUE;
            PartitionEntry->RewritePartition = FALSE;
            return TRUE;
        }
        case PARTITION_STYLE_BRFR:
        {
            return DiskGetBrfrPartitionEntry(DriveNumber,
                                             512, // SectorSize
                                             PartitionNumber,
                                             PartitionEntry);
        }
        default:
        {
            ERR("Drive 0x%X partition type = %d, should not happen!\n", DriveNumber, DiskPartitionType[DriveNumber]);
            ASSERT(FALSE);
        }
    }
    return FALSE;
}

#endif
