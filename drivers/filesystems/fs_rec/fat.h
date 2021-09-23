/*
 * PROJECT:     ReactOS File System Recognizer
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     FAT Recognizer Header File
 * COPYRIGHT:   Copyright 2002 Eric Kohl
 *              Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 */

/* Conversion types and macros taken from internal ntifs headers */
typedef union _UCHAR1
{
    UCHAR Uchar[1];
    UCHAR ForceAlignment;
} UCHAR1, *PUCHAR1;

typedef union _UCHAR2
{
    UCHAR Uchar[2];
    USHORT ForceAlignment;
} UCHAR2, *PUCHAR2;

typedef union _UCHAR4
{
    UCHAR Uchar[4];
    ULONG ForceAlignment;
} UCHAR4, *PUCHAR4;

#define CopyUchar1(Dst,Src) {                           \
    *((UCHAR1 *)(Dst)) = *((UNALIGNED UCHAR1 *)(Src));  \
}

#define CopyUchar2(Dst,Src) {                           \
    *((UCHAR2 *)(Dst)) = *((UNALIGNED UCHAR2 *)(Src));  \
}

#define CopyUchar4(Dst,Src) {                           \
    *((UCHAR4 *)(Dst)) = *((UNALIGNED UCHAR4 *)(Src));  \
}

#define FatUnpackBios(Bios,Pbios) {                                         \
    CopyUchar2(&(Bios)->BytesPerSector,    &(Pbios)->BytesPerSector[0]   ); \
    CopyUchar1(&(Bios)->SectorsPerCluster, &(Pbios)->SectorsPerCluster[0]); \
    CopyUchar2(&(Bios)->ReservedSectors,   &(Pbios)->ReservedSectors[0]  ); \
    CopyUchar1(&(Bios)->Fats,              &(Pbios)->Fats[0]             ); \
    CopyUchar2(&(Bios)->RootEntries,       &(Pbios)->RootEntries[0]      ); \
    CopyUchar2(&(Bios)->Sectors,           &(Pbios)->Sectors[0]          ); \
    CopyUchar1(&(Bios)->Media,             &(Pbios)->Media[0]            ); \
    CopyUchar2(&(Bios)->SectorsPerFat,     &(Pbios)->SectorsPerFat[0]    ); \
    CopyUchar2(&(Bios)->SectorsPerTrack,   &(Pbios)->SectorsPerTrack[0]  ); \
    CopyUchar2(&(Bios)->Heads,             &(Pbios)->Heads[0]            ); \
    CopyUchar4(&(Bios)->HiddenSectors,     &(Pbios)->HiddenSectors[0]    ); \
    CopyUchar4(&(Bios)->LargeSectors,      &(Pbios)->LargeSectors[0]     ); \
}


/* Unpacked version of the BPB */
typedef struct BIOS_PARAMETER_BLOCK
{
    USHORT BytesPerSector;
    UCHAR SectorsPerCluster;
    USHORT ReservedSectors;
    UCHAR Fats;
    USHORT RootEntries;
    USHORT Sectors;
    UCHAR Media;
    USHORT SectorsPerFat;
    USHORT SectorsPerTrack;
    USHORT Heads;
    ULONG32 HiddenSectors;
    ULONG32 LargeSectors;
    ULONG32 LargeSectorsPerFat;
    union
    {
        USHORT ExtendedFlags;
        struct
        {
            ULONG ActiveFat:4;
            ULONG Reserved0:3;
            ULONG MirrorDisabled:1;
            ULONG Reserved1:8;
        };
    };
    USHORT FsVersion;
    ULONG32 RootDirFirstCluster;
    USHORT FsInfoSector;
    USHORT BackupBootSector;
} BIOS_PARAMETER_BLOCK, *PBIOS_PARAMETER_BLOCK;

/* EOF */
