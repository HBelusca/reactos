/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARC path dissector - For BIOS drives only
 * COPYRIGHT:   Copyright 2001 Eric Kohl <eric.kohl@reactos.org>
 *              Copyright 2010 Hervé Poussineau  <hpoussin@reactos.org>
 */

#include <freeldr.h>

// HACK: Temp HACK, should be instead added to the CMake script!
#include "pathmap.c"

/**
 * @brief
 * Split a given ARC path and return a corresponding BIOS drive and
 * partition number. Optionally, return the sub-path part as well.
 *
 * @param[in]   ArcPath
 * The ARC path to split into its components.
 *
 * @param[out]  Path
 * Optional pointer to a variable that receives the sub-path part of
 * the given ARC path.
 *
 * @param[out]  DriveNumber
 * The BIOS drive number.
 *
 * @param[out]  PartitionNumber
 * The BIOS partition number for the drive.
 *
 * @return
 * TRUE if the splitting was successful, FALSE if not.
 **/
//
// ArcPathToHwDriveNumbers
//
BOOLEAN
DissectArcPath(
    _In_ PCSTR ArcPath,
    _Out_opt_ PCSTR* Path,
    _Out_ PUCHAR DriveNumber,
    _Out_ PULONG PartitionNumber)
{
    PCCH p;

    /* Detect ramdisk path */
    if (_strnicmp(ArcPath, "ramdisk(0)", 10) == 0)
    {
        /* Magic value for ramdisks */
        *DriveNumber = 0x49;
        *PartitionNumber = 1;

        /* Get the path (optional) */
        if (Path)
            *Path = ArcPath + 10;

        return TRUE;
    }

    /* NOTE: We are currently limited when handling multi()disk() paths!! */
    if (_strnicmp(ArcPath, "multi(0)disk(0)", 15) != 0)
        return FALSE;

    p = ArcPath + 15;
    if (_strnicmp(p, "fdisk(", 6) == 0)
    {
        /*
         * Floppy disk path:
         * multi(0)disk(0)fdisk(x)[\path]
         */
        p = p + 6;
        *DriveNumber = atoi(p);
        p = strchr(p, ')');
        if (p == NULL)
            return FALSE;
        ++p;
        *PartitionNumber = 0;
    }
    else if (_strnicmp(p, "cdrom(", 6) == 0)
    {
        /*
         * CD-ROM disk path:
         * multi(0)disk(0)cdrom(x)[\path]
         */
        p = p + 6;
        *DriveNumber = atoi(p) + 0x80;
        p = strchr(p, ')');
        if (p == NULL)
            return FALSE;
        ++p;
        *PartitionNumber = 0xff;
    }
    else if (_strnicmp(p, "rdisk(", 6) == 0)
    {
        /*
         * Hard disk path:
         * multi(0)disk(0)rdisk(x)[partition(y)][\path]
         */
        p = p + 6;
        *DriveNumber = atoi(p) + 0x80;
        p = strchr(p, ')');
        if (p == NULL)
            return FALSE;
        ++p;
        /* The partition is optional */
        if (_strnicmp(p, "partition(", 10) == 0)
        {
            p = p + 10;
            *PartitionNumber = atoi(p);
            p = strchr(p, ')');
            if (p == NULL)
                return FALSE;
            ++p;
        }
        else
        {
            *PartitionNumber = 0;
        }
    }
    else
    {
        return FALSE;
    }

    /* Get the path (optional) */
    if (Path)
        *Path = p;

    return TRUE;
}

/* PathSyntax: scsi() = 0, multi() = 1, ramdisk() = 2 */
BOOLEAN
DissectArcPath2(
    _In_ PCSTR ArcPath,
    _Out_ PULONG x,
    _Out_ PULONG y,
    _Out_ PULONG z,
    _Out_ PULONG Partition,
    _Out_ PULONG PathSyntax)
{
    /* Detect ramdisk() */
    if (_strnicmp(ArcPath, "ramdisk(0)", 10) == 0)
    {
        *x = *y = *z = 0;
        *Partition = 1;
        *PathSyntax = 2;
        return TRUE;
    }
    /* Detect scsi()disk()rdisk()partition() */
    else if (sscanf(ArcPath, "scsi(%lu)disk(%lu)rdisk(%lu)partition(%lu)", x, y, z, Partition) == 4)
    {
        *PathSyntax = 0;
        return TRUE;
    }
    /* Detect scsi()cdrom()fdisk() */
    else if (sscanf(ArcPath, "scsi(%lu)cdrom(%lu)fdisk(%lu)", x, y, z) == 3)
    {
        *Partition = 0;
        *PathSyntax = 0;
        return TRUE;
    }
    /* Detect multi()disk()rdisk()partition() */
    else if (sscanf(ArcPath, "multi(%lu)disk(%lu)rdisk(%lu)partition(%lu)", x, y, z, Partition) == 4)
    {
        *PathSyntax = 1;
        return TRUE;
    }
    /* Detect multi()disk()cdrom() */
    else if (sscanf(ArcPath, "multi(%lu)disk(%lu)cdrom(%lu)", x, y, z) == 3)
    {
        *Partition = 1;
        *PathSyntax = 1;
        return TRUE;
    }
    /* Detect multi()disk()fdisk() */
    else if (sscanf(ArcPath, "multi(%lu)disk(%lu)fdisk(%lu)", x, y, z) == 3)
    {
        *Partition = 1;
        *PathSyntax = 1;
        return TRUE;
    }

    /* Unknown syntax */
    return FALSE;
}

/* EOF */
