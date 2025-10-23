/*
 *  FreeLoader
 *
 *  Copyright (C) 2003, 2004  Eric Kohl
 *  Copyright (C) 2009  Hervé Poussineau
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
DBG_DEFAULT_CHANNEL(DISK); // HWDETECT

/*
 * This is the common code for harddisk for both the PC and the XBOX.
 */

#define FIRST_BIOS_DISK 0x80

/* Data cache for BIOS disks pre-enumeration */
UCHAR PcBiosDiskCount = 0;
static CHAR PcDiskIdentifier[32][20];

PVOID DiskReadBuffer;
SIZE_T DiskReadBufferSize;


/* FUNCTIONS *****************************************************************/

// ~ StartDevice
static
ARC_STATUS
BiosDiskInit(
    _In_ PVOID This, // The "DeviceData" given to BlockIoCreate()
    _In_ PCSTR Path,
    _Out_ PGEOMETRY Geometry)
{
    ULONG AdapterNumber, ControllerNumber, DriveNumber, DrivePartition;
    ULONG PathSyntax;
    // CONFIGURATION_TYPE DriveType;

ERR("BiosDiskInit(%s)\n", Path);

    if (DiskReadBufferSize == 0)
    {
        ERR("BiosDiskInit(): DiskReadBufferSize is 0, something is wrong.\n");
        ASSERT(FALSE);
        return ENOMEM;
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
    if (PathSyntax != 1) /* multi() format */
        return ENODEV;
    /* We only support drives on adapter/controller 0/0 */
    if (AdapterNumber != 0 || ControllerNumber != 0)
        return ENODEV;

    /* Convert the DriveNumber to a BIOS number */
    if (strstr(Path, ")rdisk(") || strstr(Path, ")cdrom("))
        DriveNumber += FIRST_BIOS_DISK;
ERR("  --> BIOS drive number %lu\n", DriveNumber);

    /* DriveNumber must match our context */
    if (DriveNumber != (UCHAR)PtrToUlong(This)) // FIXME: BIOS-specific, remove
        return ENODEV;
#if 0 // FIXME: Temporarily disabled while partition numbers get there...
    /* And this has to be partition 0 */
    if (DrivePartition != 0)
        return ENODEV;
#endif

    // DriveType = DiskGetConfigType(DriveNumber);
    if (!MachDiskGetDriveGeometry(DriveNumber, Geometry))
        return EIO;
    return ESUCCESS;
}

// ~ Stop/RemoveDevice
static
ARC_STATUS
BiosDiskUninit(
    _In_ PVOID This) // The "DeviceData" given to BlockIoCreate()
{
    return ESUCCESS;
}

static
ARC_STATUS
BiosDiskReadBlocks(
    _In_ PVOID This, // The "DeviceData" given to BlockIoCreate()
    _In_ ULONGLONG SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer)
{
    /* Because this function is designed for BIOS-based systems, using 16-bit
     * INTn BIOS calls, the data buffer where disk data is being read into must
     * be under the 1MB line. We use DiskReadBuffer as the temporary buffer
     * then transfer it back to the caller. */

    UCHAR* Ptr = (UCHAR*)Buffer;
    GEOMETRY Geometry; // FIXME: Temporary HACK!
    ULONG Length, MaxSectors, ReadSectors;
    BOOLEAN ret;

    ASSERT(DiskReadBufferSize > 0);

    // FIXME: Temporary HACK!
    if (!MachDiskGetDriveGeometry((UCHAR)PtrToUlong(This), &Geometry))
        return EIO;

    MaxSectors = DiskReadBufferSize / Geometry.BytesPerSector;

    // If MaxSectors is 0, this will lead to infinite loop.
    // In release builds assertions are disabled, however we also have sanity checks in DiskOpen()
    ASSERT(MaxSectors > 0);

    ret = TRUE;

    while (SectorCount)
    {
        ReadSectors = min(SectorCount, MaxSectors);

        ret = MachDiskReadLogicalSectors((UCHAR)PtrToUlong(This),
                                         SectorNumber,
                                         ReadSectors,
                                         DiskReadBuffer);
        if (!ret)
            break;

        Length = min(ReadSectors, SectorCount);
        Length *= Geometry.BytesPerSector;

        RtlCopyMemory(Ptr, DiskReadBuffer, Length);

        Ptr += Length;
        SectorNumber += ReadSectors;
        SectorCount -= ReadSectors;
    }

    return (ret ? ESUCCESS : EIO);
}


PCHAR
GetHarddiskIdentifier(UCHAR DriveNumber)
{
    return PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK];
}

extern VOID
GetHarddiskInformation(
    _In_ PCSTR DeviceName,
    _In_ UCHAR DriveNumber,
    _Out_writes_z_(20) PSTR Identifier);

static UCHAR
EnumerateHarddisks(OUT PBOOLEAN BootDriveReported)
{
    UCHAR DiskCount, DriveNumber;
    CHAR ArcName[64];

    *BootDriveReported = FALSE;

    /* Count the number of visible harddisk drives */
    DiskCount = 0;
    DriveNumber = FIRST_BIOS_DISK;

    ASSERT(DiskReadBufferSize > 0);

    /*
     * There are some really broken BIOSes out there. There are even BIOSes
     * that happily report success when you ask them to read from non-existent
     * harddisks. So, we set the buffer to known contents first, then try to
     * read. If the BIOS reports success but the buffer contents haven't
     * changed then we fail anyway.
     */
    DiskReportError(FALSE);
    memset(DiskReadBuffer, 0xcd, DiskReadBufferSize);
    while (MachDiskReadLogicalSectors(DriveNumber, 0ULL, 1, DiskReadBuffer))
    {
        ARC_STATUS ArcStatus;
        BOOLEAN Changed = FALSE;
        ULONG i;
        for (i = 0; !Changed && i < DiskReadBufferSize; i++)
        {
            Changed = ((PUCHAR)DiskReadBuffer)[i] != 0xcd;
        }
        if (!Changed)
        {
            TRACE("BIOS reports success for disk %u (0x%02X) but data didn't change\n",
                  DiskCount, DriveNumber);
            break;
        }

        /* Register the disk */
        RtlStringCbPrintfA(ArcName, sizeof(ArcName),
                           "multi(0)disk(0)rdisk(%u)",
                           DriveNumber - FIRST_BIOS_DISK);
        DiskReportError(FALSE);
        ArcStatus = BlockIoCreate(ArcName,
                                  DiskPeripheral, // DiskGetConfigType(DriveNumber),
                                  UlongToPtr((ULONG)DriveNumber),
                                  BiosDiskInit,
                                  BiosDiskUninit,
                                  BiosDiskReadBlocks,
                                  NULL,
                                  NULL);
        DiskReportError(TRUE);

        /* Cache the hard disk information for later use */
        if (ArcStatus == ESUCCESS)
        {
            GetHarddiskInformation(ArcName, DriveNumber - FIRST_BIOS_DISK,
                                   PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK]);
        }
        else
        {
            /* The disk failed to be initialized, use a default identifier */
            sprintf(PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK], "BIOSDISK%u", DriveNumber - FIRST_BIOS_DISK + 1);
        }

        /* Check if we have seen the boot drive */
        if (FrldrBootDrive == DriveNumber)
            *BootDriveReported = TRUE;

        DiskCount++;
        if (DriveNumber >= 0xFF)
            break;
        DriveNumber++;
        memset(DiskReadBuffer, 0xcd, DiskReadBufferSize);
    }
    DiskReportError(TRUE);

    PcBiosDiskCount = DiskCount;
    TRACE("BIOS reports %u harddisk%s\n",
          DiskCount, (DiskCount == 1) ? "" : "s");

    return DiskCount;
}

static BOOLEAN
DiskGetBootPath(
    _In_ BOOLEAN IsPxe,
    _Out_ PCONFIGURATION_TYPE DeviceType)
{
    if (*FrLdrBootPath)
        return TRUE;

    *DeviceType = 0;

    // FIXME: Do this in some drive recognition procedure!
    if (IsPxe)
    {
        RtlStringCbCopyA(FrLdrBootPath, sizeof(FrLdrBootPath), "net(0)");
        *DeviceType = NetworkPeripheral;
    }
    else
    /* 0x49 is our magic ramdisk drive, so try to detect it first */
    if (FrldrBootDrive == 0x49)
    {
        /* This is the ramdisk. See ArmInitializeBootDevices() too... */
        // RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath), "ramdisk(%u)", 0);
        RtlStringCbCopyA(FrLdrBootPath, sizeof(FrLdrBootPath), "ramdisk(0)");
        *DeviceType = DiskPeripheral;
    }
    else if (FrldrBootDrive < FIRST_BIOS_DISK) // (DiskGetConfigType(FrldrBootDrive) == FloppyDiskPeripheral)
    {
        /* This is a floppy */
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)fdisk(%u)", FrldrBootDrive);
        *DeviceType = FloppyDiskPeripheral;
    }
    else if (FrldrBootPartition == 0xFF) // (DiskGetConfigType(FrldrBootDrive) == CdromController)
    {
        /* Boot Partition 0xFF is the magic value that indicates booting from CD-ROM (see isoboot.S) */
        /* TODO: Check if it's really a CD-ROM drive */
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)cdrom(%u)", FrldrBootDrive - FIRST_BIOS_DISK);
        *DeviceType = CdromController;
    }
    else // (DiskGetConfigType(FrldrBootDrive) == DiskPeripheral)
    {
        ULONG BootPartition;

        /* This is a hard disk, find the boot partition */
        if (!DiskGetBootPartitionEntry(UlongToPtr((ULONG)FrldrBootDrive),
                                       BiosDiskReadBlocks,
                                       0xFF, NULL, &BootPartition))
        {
            ERR("Failed to get boot partition entry\n");
            return FALSE;
        }
        FrldrBootPartition = BootPartition;

        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)rdisk(%u)partition(%lu)",
                           FrldrBootDrive - FIRST_BIOS_DISK, FrldrBootPartition);
        *DeviceType = DiskPeripheral;
    }

    return TRUE;
}

BOOLEAN
PcInitializeBootDevices(VOID)
{
    UCHAR DiskCount;
    BOOLEAN BootDriveReported = FALSE;
    CONFIGURATION_TYPE DriveType;

    DiskCount = EnumerateHarddisks(&BootDriveReported);

    /* Initialize FrLdrBootPath, the path FreeLoader starts from */
    DiskGetBootPath(PxeInit(), &DriveType);

    /* Add it, if it's a floppy or CD-ROM */
    if ((FrldrBootDrive >= FIRST_BIOS_DISK && !BootDriveReported) ||
        (DriveType == FloppyDiskPeripheral || DriveType == CdromController))
    {
        ARC_STATUS Status;
        DiskReportError(FALSE);
        Status = BlockIoCreate(FrLdrBootPath,
                               DriveType,
                               UlongToPtr((ULONG)FrldrBootDrive),
                               BiosDiskInit,
                               BiosDiskUninit,
                               BiosDiskReadBlocks,
                               NULL,
                               NULL);
        DiskReportError(TRUE);

        if (Status == ESUCCESS)
        {
            DiskCount++; // This is not accounted for in the number of pre-enumerated BIOS drives!
            TRACE("Additional boot drive detected: 0x%02X\n", FrldrBootDrive);
        }
        else
            ERR("Additional boot drive 0x%02X failed\n", FrldrBootDrive);
    }

    return (DiskCount != 0);
}
