/*
 * PROJECT:     ReactOS File System Recognizer
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     FAT / NTFS Common Header File
 * COPYRIGHT:   Copyright 2002 Eric Kohl
 *              Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 */

/* Packed versions of the BPB and Boot Sector */
typedef struct _PACKED_BIOS_PARAMETER_BLOCK
{
    UCHAR BytesPerSector[2];
    UCHAR SectorsPerCluster[1];
    UCHAR ReservedSectors[2];
    UCHAR Fats[1];
    UCHAR RootEntries[2];
    UCHAR Sectors[2];
    UCHAR Media[1];
    UCHAR SectorsPerFat[2];
    UCHAR SectorsPerTrack[2];
    UCHAR Heads[2];
    UCHAR HiddenSectors[4];
    UCHAR LargeSectors[4];
} PACKED_BIOS_PARAMETER_BLOCK, *PPACKED_BIOS_PARAMETER_BLOCK;

typedef struct _PACKED_BOOT_SECTOR
{
    UCHAR Jump[3];
    UCHAR Oem[8];
    PACKED_BIOS_PARAMETER_BLOCK PackedBpb;
    UCHAR PhysicalDriveNumber;
    UCHAR CurrentHead;
    UCHAR Signature;
    UCHAR Id[4];
    UCHAR VolumeLabel[11];
    UCHAR SystemId[8];
} PACKED_BOOT_SECTOR, *PPACKED_BOOT_SECTOR;

/* EOF */
