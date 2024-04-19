/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#ifdef _M_IX86

BOOLEAN DriveMapInstalled = FALSE; // Tells if the drive map INT 13h handler code is already installed
ULONG OldInt13HandlerAddress = 0;  // Address of BIOS INT 13h handler
ULONG DriveMapHandlerAddress = 0;  // Linear address of the drive map handler
ULONG DriveMapHandlerSegOff  = 0;  // Segment:offset style address of the drive map handler

#endif // _M_IX86

#if defined(_M_IX86) || defined(_M_AMD64)

/**
 * @brief
 * Given a drive type and drive number, usually retrieved from a
 * previous call to DriveStrToNumber(), retrieve a corresponding
 * BIOS drive number. Optionally adjusts the partition number too.
 *
 * @param[in,out]   DriveType
 * Recognized drive type. If DriveType == 0, the function attempts
 * to recalculate the type, based on the drive number.
 *
 * @param[in]   DriveNumber
 * Drive number. The following limits apply for it to be mapped
 * to a BIOS drive number:
 * - Floppy disks:
 *   DriveType == 0 and 0 <= DriveNumber < 0x80, or
 *   DriveType == 'f' and 0 <= DriveNumber < 127;
 *
 * - Hard disks:
 *   DriveType == 0 and 0x80 <= DriveNumber, or
 *   DriveType == 'h' and 0 <= DriveNumber < 127;
 *
 * - CD-ROMs: DriveType == 'c' and 0 <= DriveNumber < 127.
 *
 * - Single ramdisk:
 *   DriveType == 0 and DriveNumber == 0x49, or
 *   DriveType == 'r' and DriveNumber == 0.
 *
 * @param[out]  BiosDriveNumber
 * Corresponding BIOS drive number.
 *
 * @param[in,out]   PartitionNumber
 * Optional drive partition number (or zero if none).
 *
 * @return  TRUE if successful, FALSE if not.
 * @see DriveStrToNumber().
 **/
BOOLEAN
DriveToBIOSNumber(
    _Inout_ PUCHAR DriveType,
    _In_ ULONG DriveNumber,
    _Out_ PUCHAR BiosDriveNumber,
    _Inout_opt_ PULONG PartitionNumber)
{
    /* Check the BIOS-specific limits */
    switch (*DriveType)
    {
    case 'f': // Floppy disk
    {
        if (DriveNumber > 0x7F)
            return FALSE;
        // /* Floppies always have one single partition */
        // if (PartitionNumber)
        //     *PartitionNumber = 1;
        // HACK: Exclude drive 0x49 (reserved for ramdisk, see below)
        if (DriveNumber == 0x49)
            return FALSE;
        break;
    }
    case 'h': // Hard disk
    {
        if (DriveNumber > 0x7F)
            return FALSE;
        /* It's a hard disk, set the high bit */
        DriveNumber |= 0x80;
        break;
    }
    case 'c': // CD-ROM
    {
        if (DriveNumber > 0x7F)
            return FALSE;
        /* CD-ROMs get a BIOS drive number > 0x80 */
        DriveNumber += 0x80;
        // FIXME: Find the actual corresponding CD-ROM drive
        // /* CD-ROMs always have one single partition (at least...) */
        // if (PartitionNumber)
        //     *PartitionNumber = 1;
        break;
    }
    case 'r': // Ramdisk
    {
        if (DriveNumber != 0)
            return FALSE;
        DriveNumber = 0x49; // Magic value for ramdisk
        break;
    }
    case 0: // Not yet determined
    {
        if (DriveNumber > 0xFF)
            return FALSE;
        if (DriveNumber == 0x49)
        {
            /* The ramdisk */
            *DriveType = 'r';
        }
        else if (DriveNumber <= 0x7F)
        {
            /* This is a floppy disk */
            *DriveType = 'f';
            /* Floppies always have one single partition */
            if (PartitionNumber)
                *PartitionNumber = 1;
        }
        else
        {
            /* Assume this is a hard disk */
            *DriveType = 'h';
        }
        break;
    }
    default:
        ASSERT(FALSE);
        return FALSE;
    }

    *BiosDriveNumber = (UCHAR)DriveNumber;
    return TRUE;
}

#endif /* _M_IX86 || _M_AMD64 */

#ifdef _M_IX86

/**
 * @brief
 * Returns a BIOS drive number for any given drive name (e.g. 0x80 for 'hd0')
 * NOTE: We can only map floppy or hard disks, so check only for such types.
 **/
static BOOLEAN
DriveMapGetBiosDriveNumber(
    _In_ PCSTR DriveString,
    _Out_ PUCHAR BiosDriveNumber)
{
    PCSTR Trailing = NULL;
    UCHAR DriveType;
    ULONG DriveNumber, HwDriveNumber;

    *BiosDriveNumber = 0;

    if (!DriveStrToNumber(DriveString,
                          &Trailing,
                          &DriveType,
                          &DriveNumber,
                          NULL,
                          &HwDriveNumber) /*||
        !DriveToBIOSNumber(&DriveType,
                           DriveNumber,
                           BiosDriveNumber,
                           NULL)*/ ||
        (Trailing && *Trailing))
    {
        return FALSE;
    }
    if ((DriveType != 'f') && (DriveType != 'h'))
        return FALSE;

    *BiosDriveNumber = (UCHAR)HwDriveNumber;
    return TRUE;
}

static VOID
DriveMapInstallInt13Handler(
    _In_ PDRIVE_MAP_LIST DriveMap);

static VOID
DriveMapRemoveInt13Handler(VOID);

VOID
DriveMapMapDrivesInSection(
    _In_ ULONG_PTR SectionId)
{
    DRIVE_MAP_LIST DriveMapList;
    ULONG SectionItemCount;
    ULONG Index;
    PCHAR Drive1, Drive2, sep;
    UCHAR BiosDriveNum1, BiosDriveNum2;
    CHAR SettingName[80];
    CHAR SettingValue[80];

    if (SectionId == 0)
        return;

    RtlZeroMemory(&DriveMapList, sizeof(DriveMapList));

    /* Get the number of items in this section */
    SectionItemCount = IniGetNumSectionItems(SectionId);

    /* Loop through each one and check if its a DriveMap= setting */
    for (Index = 0; Index < SectionItemCount; Index++)
    {
        /* Get the next setting from the .ini file section */
        if (IniReadSettingByNumber(SectionId, Index,
                                   SettingName, sizeof(SettingName),
                                   SettingValue, sizeof(SettingValue)))
        {
            if (_stricmp(SettingName, "DriveMap") == 0)
            {
                /* Make sure we haven't exceeded the drive map max count */
                if (DriveMapList.DriveMapCount >= 4)
                {
                    UiMessageBox("Max DriveMap count exceeded in section [%s]:\n\n%s=%s",
                                 ((PINI_SECTION)SectionId)->SectionName,
                                 SettingName, SettingValue);
                    continue;
                }

                /* Split the value of the form "hd0,hd1" into two strings
                 * "hd0" and "hd1" at the separator character (comma: ',') */
                Drive1 = SettingValue;
                Drive2 = sep = strchr(SettingValue, ',');
                if (Drive2) *Drive2++ = ANSI_NULL;

                /* Make sure we got good values before we add them to the map */
                if (!sep ||
                    !DriveMapGetBiosDriveNumber(Drive1, &BiosDriveNum1) ||
                    !DriveMapGetBiosDriveNumber(Drive2, &BiosDriveNum2))
                {
                    /* Restore the separator for showing the error */
                    if (sep) *sep = ',';

                    UiMessageBox("Error in DriveMap setting in section [%s]:\n\n%s=%s",
                                 ((PINI_SECTION)SectionId)->SectionName,
                                 SettingName, SettingValue);
                    continue;
                }

                /* Add them to the map */
                DriveMapList.DriveMap[(DriveMapList.DriveMapCount * 2)  ] = BiosDriveNum1;
                DriveMapList.DriveMap[(DriveMapList.DriveMapCount * 2)+1] = BiosDriveNum2;
                DriveMapList.DriveMapCount++;

                TRACE("Mapping BIOS drive 0x%x to drive 0x%x\n",
                      BiosDriveNum1, BiosDriveNum2);
            }
        }
    }

#ifndef MY_WIN32
    if (DriveMapList.DriveMapCount)
    {
        TRACE("Installing Int13 drive map for %d drives.\n", DriveMapList.DriveMapCount);
        DriveMapInstallInt13Handler(&DriveMapList);
    }
    else
    {
        TRACE("Removing any previously installed Int13 drive map.\n");
        DriveMapRemoveInt13Handler();
    }
#endif /* MY_WIN32 */
}

#ifndef MY_WIN32

/**
 * @brief   Installs the INT 13h handler for the drive mapper.
 **/
static VOID
DriveMapInstallInt13Handler(
    _In_ PDRIVE_MAP_LIST DriveMap)
{
    PULONG  RealModeIVT = (PULONG)UlongToPtr(0x00000000);
    PUSHORT BiosLowMemorySize = (PUSHORT)UlongToPtr(0x00000413);

#if defined(SARCH_PC98)
    /* FIXME */
    return;
#endif

    if (!DriveMapInstalled)
    {
        /* Get the old INT 13h handler address from the vector table */
        OldInt13HandlerAddress = RealModeIVT[0x13];

        /* Decrease the size of low memory */
        (*BiosLowMemorySize)--;

        /* Get linear address for drive map handler */
        DriveMapHandlerAddress = (ULONG)(*BiosLowMemorySize) << 10;

        /* Convert to segment:offset style address */
        DriveMapHandlerSegOff = (DriveMapHandlerAddress << 12) & 0xffff0000;
    }

    /* Copy the drive map structure to the proper place */
    RtlCopyMemory(&DriveMapInt13HandlerMapList, DriveMap, sizeof(*DriveMap));

    /* Set the address of the BIOS INT 13h handler */
    DriveMapOldInt13HandlerAddress = OldInt13HandlerAddress;

    /* Copy the code to the reserved area */
    RtlCopyMemory(UlongToPtr(DriveMapHandlerAddress),
                  &DriveMapInt13HandlerStart,
                  ((PUCHAR)&DriveMapInt13HandlerEnd - (PUCHAR)&DriveMapInt13HandlerStart));

    /* Update the IVT */
    RealModeIVT[0x13] = DriveMapHandlerSegOff;

    CacheInvalidateCacheData();
    DriveMapInstalled = TRUE;
}

/**
 * @brief   Removes a previously installed INT 13h drive map handler.
 **/
static VOID
DriveMapRemoveInt13Handler(VOID)
{
    PULONG  RealModeIVT = (PULONG)UlongToPtr(0x00000000);
    PUSHORT BiosLowMemorySize = (PUSHORT)UlongToPtr(0x00000413);

#if defined(SARCH_PC98)
    /* FIXME */
    return;
#endif

    if (DriveMapInstalled)
    {
        /* Get the old INT 13h handler address from the vector table */
        RealModeIVT[0x13] = OldInt13HandlerAddress;

        /* Increase the size of low memory */
        (*BiosLowMemorySize)++;

        CacheInvalidateCacheData();
        DriveMapInstalled = FALSE;
    }
}

#endif /* MY_WIN32 */

#endif // _M_IX86
