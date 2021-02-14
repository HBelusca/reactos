/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Partition list functions
 * COPYRIGHT:   Copyright 2003-2019 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2018-2019 Hermes Belusca-Maito
 */

#pragma once

/* EXTRA HANDFUL MACROS *****************************************************/

// NOTE: They should be moved into some global header.

/* OEM MBR partition types recognized by NT (see [MS-DMRP] Appendix B) */
#define PARTITION_EISA          0x12    // EISA partition
#define PARTITION_HIBERNATION   0x84    // Hibernation partition for laptops
#define PARTITION_DIAGNOSTIC    0xA0    // Diagnostic partition on some Hewlett-Packard (HP) notebooks
#define PARTITION_DELL          0xDE    // Dell partition
#define PARTITION_IBM           0xFE    // IBM Initial Microprogram Load (IML) partition

#define IsOEMPartition(PartitionType) \
    ( ((PartitionType) == PARTITION_EISA)        || \
      ((PartitionType) == PARTITION_HIBERNATION) || \
      ((PartitionType) == PARTITION_DIAGNOSTIC)  || \
      ((PartitionType) == PARTITION_DELL)        || \
      ((PartitionType) == PARTITION_IBM) )

// This should go into diskguid.h

#ifdef __cplusplus
#define IsEqualPartitionType         IsEqualGUID
#else
#define IsEqualPartitionType(_a, _b) IsEqualGUID(&(_a), &(_b))
#endif

#define IsRecognizedGptPartition(_t) \
    ( IsEqualPartitionType((_t), PARTITION_BSP_GUID)           || \
      IsEqualPartitionType((_t), PARTITION_DPP_GUID)           || \
      IsEqualPartitionType((_t), PARTITION_BASIC_DATA_GUID)    || \
      IsEqualPartitionType((_t), PARTITION_MAIN_OS_GUID)       || \
      IsEqualPartitionType((_t), PARTITION_MSFT_RECOVERY_GUID) || \
      IsEqualPartitionType((_t), PARTITION_OS_DATA_GUID)       || \
      IsEqualPartitionType((_t), PARTITION_PRE_INSTALLED_GUID) || \
      IsEqualPartitionType((_t), PARTITION_WINDOWS_SYSTEM_GUID) )


/* PRIVATE MACROS ***********************************************************/

/*
 * WORK IN PROGRESS !!
 * Evaluate the best way how to store the per-partition type
 */

// #define TEST_NEW_PARTTYPE_WAY

#ifdef TEST_NEW_PARTTYPE_WAY

#define GET_PARTITION_TYPE(PartEntry)   \
    ( ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_MBR) ?  \
          (PartEntry)->PartInfo.Mbr.PartitionType :                 \
      ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_GPT) ?  \
          (PartEntry)->PartInfo.Gpt.PartitionType :                 \
      0 )

#else

#define GET_PARTITION_TYPE(PartEntry)   \
    ( ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_MBR) ?  \
          (PartEntry)->PartitionType.MbrType :                      \
      ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_GPT) ?  \
          (PartEntry)->PartitionType.GptType :                      \
      0 )

#endif


// #define IS_CONTAINER_PARTITION()
// Uses IsContainerPartition() if DiskStyle == MBR...
// otherwise use a list of known partition container GUIDs for GPT.


#ifdef TEST_NEW_PARTTYPE_WAY

#define IS_OEM_RESERVED_PARTITION(PartEntry) \
    ( ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_MBR) ?  \
          IsOEMPartition((PartEntry)->PartInfo.Mbr.PartitionType) : \
          !!((PartEntry)->PartInfo.Gpt.Attributes & GPT_ATTRIBUTE_PLATFORM_REQUIRED) ) // PARTITION_STYLE_GPT

#else

#define IS_OEM_RESERVED_PARTITION(PartEntry) \
    ( ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_MBR) ? \
          IsOEMPartition((PartEntry)->PartitionType.MbrType) :      \
          !!((PartEntry)->PartInfo.Gpt.Attributes & GPT_ATTRIBUTE_PLATFORM_REQUIRED) ) // PARTITION_STYLE_GPT

#endif


/*
 * IMPORTANT NOTE!
 * A container partition (for MBR: the partition is an extended container,
 * i.e. IsContainerPartition() returns TRUE, or for MBR/GPT, this could be
 * an LDM ... container) can _NEVER_ be also a recognized partition, i.e.
 * a partition that can hold a filesystem on which we can write files.
 */

#ifdef TEST_NEW_PARTTYPE_WAY

#define IS_RECOGNIZED_PARTITION_EX(PartStyle, PartType)  \
    ( ((PartStyle) == PARTITION_STYLE_GPT) ?             \
          IsRecognizedGptPartition((PartType).Gpt.PartitionType) : \
          IsRecognizedPartition((PartType).Mbr.PartitionType) ) // PARTITION_STYLE_MBR

#define IS_RECOGNIZED_PARTITION(PartEntry)                        \
    IS_RECOGNIZED_PARTITION_EX((PartEntry)->DiskEntry->DiskStyle, \
                               (PartEntry)->PartInfo)

#else

#define IS_RECOGNIZED_PARTITION_EX(PartStyle, PartType)  \
    ( ((PartStyle) == PARTITION_STYLE_GPT) ?             \
          IsRecognizedGptPartition((PartType).GptType) : \
          IsRecognizedPartition((PartType).MbrType) ) // PARTITION_STYLE_MBR

#define IS_RECOGNIZED_PARTITION(PartEntry)                        \
    IS_RECOGNIZED_PARTITION_EX((PartEntry)->DiskEntry->DiskStyle, \
                               (PartEntry)->PartitionType)

#endif


#ifdef TEST_NEW_PARTTYPE_WAY

#define IS_PARTITION_UNUSED(PartEntry)                             \
    ( ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_GPT) ? \
          IsEqualPartitionType((PartEntry)->PartInfo.Gpt.PartitionType, \
                               PARTITION_ENTRY_UNUSED_GUID) :      \
          ((PartEntry)->PartInfo.Mbr.PartitionType == PARTITION_ENTRY_UNUSED) ) // PARTITION_STYLE_MBR

#else

#if 0 // Old version

C_ASSERT(PARTITION_ENTRY_UNUSED_GUID == GUID_NULL);

#define IS_PARTITION_UNUSED(PartEntry)                             \
    ( ((PartEntry)->DiskEntry->DiskStyle == PARTITION_STYLE_GPT) ? \
          IsEqualPartitionType((PartEntry)->PartitionType.GptType, \
                               PARTITION_ENTRY_UNUSED_GUID) :      \
          ((PartEntry)->PartitionType.MbrType == PARTITION_ENTRY_UNUSED) ) // PARTITION_STYLE_MBR

#else // Faster version

/* This is possible since GptType is in union with MbrType, is
 * larger than the latter, and PARTITION_ENTRY_UNUSED == 0 as well. */
#define IS_PARTITION_UNUSED(PartEntry)               \
    IsEqualPartitionType((PartEntry)->PartitionType.GptType, \
                         PARTITION_ENTRY_UNUSED_GUID)

#endif

#endif


#define GET_PARTITION_LAYOUT_ENTRY(PartEntry)       \
    ( ASSERT((PartEntry)->DiskEntry->LayoutBuffer), \
      ASSERT((PartEntry)->PartitionIndex < (PartEntry)->DiskEntry->LayoutBuffer->PartitionCount), \
      &(PartEntry)->DiskEntry->LayoutBuffer->PartitionEntry[(PartEntry)->PartitionIndex] )


/* PARTITION UTILITY FUNCTIONS **********************************************/

/* Supplemental enum values for PARTITION_STYLE */
// #define PARTITION_STYLE_BRFR        0x80
#define PARTITION_STYLE_NEC98       0x81
#define PARTITION_STYLE_SUPERFLOPPY 0xFF

typedef enum _FORMATSTATE
{
    Unformatted,
    UnformattedOrDamaged,
    UnknownFormat,
    Preformatted,
    Formatted
} FORMATSTATE, *PFORMATSTATE;

typedef struct _PARTENTRY
{
    LIST_ENTRY ListEntry;

    /* The disk this partition belongs to */
    struct _DISKENTRY *DiskEntry;

    /* Partition geometry */
    ULARGE_INTEGER StartSector;
    ULARGE_INTEGER SectorCount;

    /* Partition state flags */
    union
    {
        UCHAR AsByte;
        struct
        {
            // NOTE: See comment for the PARTLIST::SystemPartition member.
            // MBR: BootIndicator == TRUE / GPT: PARTITION_SYSTEM_GUID.
            BOOLEAN IsSystemPartition : 1;

            BOOLEAN LogicalPartition  : 1; // MBR-specific

            /* Partition is partitioned disk space */
            BOOLEAN IsPartitioned     : 1;

            /* Partition is new, table does not exist on disk yet */
            BOOLEAN New               : 1;

            /* Partition was created automatically */
            BOOLEAN AutoCreate        : 1;
        };
    };

#ifdef TEST_NEW_PARTTYPE_WAY

    union
    {
        union
        {
            UCHAR PartType;
            PARTITION_INFORMATION_MBR PartInfo;
        } Mbr;
        union
        {
            GUID PartType;
            PARTITION_INFORMATION_GPT PartInfo;
        } Gpt;
    };

#else

    union
    {
        /* Cached partition type. This duplicates the information stored
         * in PartInfo.Mbr.PartitionType or PartInfo.Gpt.PartitionType. */
        union
        {
            UCHAR MbrType; // Mbr;
            GUID  GptType; // Gpt;
        } PartitionType;   // PartType;

        /* Cached type-specific partition information */
        union
        {
            PARTITION_INFORMATION_MBR Mbr;
            PARTITION_INFORMATION_GPT Gpt;
        } PartInfo;
    };
#endif
    //
    // NOTE: In order to get the NT partition layout corresponding to
    // this PARTENTRY, use the 'PartitionIndex' member that indexes the
    // LayoutBuffer->PartitionEntry[] cached array of the corresponding DiskEntry.
    // We cannot use direct pointers to LayoutBuffer->PartitionEntry[]
    // since the latter can be redimensioned at any time when new partitions
    // are being created.
    //

    ULONG OnDiskPartitionNumber; /* Enumerated partition number (primary partitions first, excluding the extended partition container, then the logical partitions) */
    ULONG PartitionNumber;       /* Current partition number, only valid for the currently running NTOS instance */
    ULONG PartitionIndex;        /* Index in the LayoutBuffer->PartitionEntry[] cached array of the corresponding DiskEntry */


    /* Volume information */
    WCHAR DriveLetter;
    WCHAR VolumeLabel[20];
    WCHAR FileSystem[MAX_PATH+1];
    FORMATSTATE FormatState;
    BOOLEAN NeedsCheck; /* Partition must be checked */

} PARTENTRY, *PPARTENTRY;

#if 0 // In arcname.h
/* See also UEFI specification - Media Device (UEFI Specs v2.8: 10.3.5.1 Hard Drive) */
typedef enum _DEVICE_SIGNATURE_TYPE
{
    SignatureNone = 0x00,
    SignatureLong = 0x01,
    SignatureGuid = 0x02
} DEVICE_SIGNATURE_TYPE, *PDEVICE_SIGNATURE_TYPE;

typedef struct _DEVICE_SIGNATURE
{
    DEVICE_SIGNATURE_TYPE Type;
    union
    {
        ULONG Long;
        GUID  Guid;
    }
} DEVICE_SIGNATURE, *PDEVICE_SIGNATURE;
#endif

typedef struct _DISKENTRY
{
    LIST_ENTRY ListEntry;

    /* The list of disks/partitions this disk belongs to */
    struct _PARTLIST *PartList;

    MEDIA_TYPE MediaType;   /* FixedMedia or RemovableMedia */

    /* Disk geometry */

    ULONGLONG Cylinders;
    ULONG TracksPerCylinder;
    ULONG SectorsPerTrack;
    ULONG BytesPerSector;

    ULARGE_INTEGER SectorCount;
    ULONG SectorAlignment;
    ULONG CylinderAlignment;

    /* BIOS Firmware parameters */
    BOOLEAN BiosFound;
    ULONG HwAdapterNumber;
    ULONG HwControllerNumber;
    ULONG HwDiskNumber;         /* Disk number currently assigned on the system */
    ULONG HwFixedDiskNumber;    /* Disk number on the system when *ALL* removable disks are not connected */

    /* SCSI parameters */
    ULONG DiskNumber;
//  SCSI_ADDRESS;
    USHORT Port;
    USHORT Bus;
    USHORT Id;

    UNICODE_STRING DriverName;

    /* Has the partition list been modified? */
    BOOLEAN Dirty;

    BOOLEAN NewDisk; /* If TRUE, the disk is uninitialized */
    PARTITION_STYLE DiskStyle;  /* MBR/GPT-partitioned disk, super-floppy, or uninitialized disk (RAW) */
    // DISK_SIGNATURE DiskSignature;

    PDRIVE_LAYOUT_INFORMATION_EX LayoutBuffer;

#define PRIMARY_PARTITIONS  0   /* List of primary partitions */
#define LOGICAL_PARTITIONS  1   /* List of logical partitions (Valid only for MBR-partitioned disks) */
    LIST_ENTRY PartList[2];

    /* Pointer to the unique extended partition on this disk (Valid only for MBR-partitioned disks) */
    PPARTENTRY ExtendedPartition;

} DISKENTRY, *PDISKENTRY;

/* INFO: Stuff around LEGACY_BIOS_DATA should be not needed in the partition
 * code, since this should be already handled by the storage stack. */
// #define LEGACY_BIOS_DATA

typedef struct _BIOSDISKENTRY
{
    LIST_ENTRY ListEntry;
    ULONG AdapterNumber;
    ULONG ControllerNumber;
    ULONG DiskNumber;
    ULONG Signature;        /* MBR: Disk signature; GPT: Signature stored in the GPT protective MBR */
    ULONG Checksum;         /* MBR: Disk MBR checksum; GPT: checksum of the GPT protective MBR */
    PDISKENTRY DiskEntry;   /* Corresponding recognized disk; is NULL if the disk is not recognized */
#ifdef LEGACY_BIOS_DATA
    CM_DISK_GEOMETRY_DEVICE_DATA DiskGeometry;
    CM_INT13_DRIVE_PARAMETER Int13DiskData;
#endif
} BIOSDISKENTRY, *PBIOSDISKENTRY;

typedef struct _PARTLIST
{
    /*
     * The system partition where the boot manager resides.
     * The corresponding system disk is obtained via:
     *    SystemPartition->DiskEntry.
     */
    // NOTE: It seems to appear that the specifications of ARC and (u)EFI
    // actually allow for multiple system partitions to exist on the system.
    // If so we should instead rely on the IsSystemPartition bit of the
    // PARTENTRY structure in order to find these.
    PPARTENTRY SystemPartition;

    LIST_ENTRY DiskListHead;
    LIST_ENTRY BiosDiskListHead;

} PARTLIST, *PPARTLIST;

//
// For MBR only
//
#define PARTITION_TBL_SIZE  4

#define PARTITION_MAGIC     0xAA55

/* Defines system type for MBR showing that a GPT is following */
#define EFI_PMBR_OSTYPE_EFI 0xEE

#include <pshpack1.h>

typedef struct _PARTITION
{
    unsigned char   BootFlags;        /* bootable?  0=no, 128=yes  */
    unsigned char   StartingHead;     /* beginning head number */
    unsigned char   StartingSector;   /* beginning sector number */
    unsigned char   StartingCylinder; /* 10 bit nmbr, with high 2 bits put in begsect */
    unsigned char   PartitionType;    /* Operating System type indicator code */
    unsigned char   EndingHead;       /* ending head number */
    unsigned char   EndingSector;     /* ending sector number */
    unsigned char   EndingCylinder;   /* also a 10 bit nmbr, with same high 2 bit trick */
    unsigned int  StartingBlock;      /* first sector relative to start of disk */
    unsigned int  SectorCount;        /* number of sectors in partition */
} PARTITION, *PPARTITION;

typedef struct _PARTITION_SECTOR
{
    UCHAR BootCode[440];                     /* 0x000 */
    ULONG Signature;                         /* 0x1B8 */
    UCHAR Reserved[2];                       /* 0x1BC */
    PARTITION Partition[PARTITION_TBL_SIZE]; /* 0x1BE */
    USHORT Magic;                            /* 0x1FE */
} PARTITION_SECTOR, *PPARTITION_SECTOR;

#include <poppack.h>


ULONGLONG
AlignDown(
    IN ULONGLONG Value,
    IN ULONG Alignment);

ULONGLONG
AlignUp(
    IN ULONGLONG Value,
    IN ULONG Alignment);

ULONGLONG
RoundingDivide(
   IN ULONGLONG Dividend,
   IN ULONGLONG Divisor);


BOOLEAN
IsSystemPartition(
    IN PPARTENTRY PartEntry);

PPARTLIST
CreatePartitionList(VOID);

VOID
DestroyPartitionList(
    IN PPARTLIST List);

PDISKENTRY
GetDiskByBiosNumber(
    IN PPARTLIST List,
    IN ULONG HwDiskNumber);

PDISKENTRY
GetDiskByNumber(
    IN PPARTLIST List,
    IN ULONG DiskNumber);

PDISKENTRY
GetDiskBySCSI(
    IN PPARTLIST List,
    IN USHORT Port,
    IN USHORT Bus,
    IN USHORT Id);

PDISKENTRY
GetDiskBySignature(
    IN PPARTLIST List,
    IN DEVICE_SIGNATURE Signature);

PPARTENTRY
GetPartition(
    // IN PPARTLIST List,
    IN PDISKENTRY DiskEntry,
    IN ULONG PartitionNumber);

BOOLEAN
GetDiskOrPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber OPTIONAL,
    OUT PDISKENTRY* pDiskEntry,
    OUT PPARTENTRY* pPartEntry OPTIONAL);

PPARTENTRY
SelectPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber);

PPARTENTRY
GetNextPartition(
    IN PPARTLIST List,
    IN PPARTENTRY CurrentPart OPTIONAL);

PPARTENTRY
GetPrevPartition(
    IN PPARTLIST List,
    IN PPARTENTRY CurrentPart OPTIONAL);

BOOLEAN
CreatePrimaryPartition(
    IN PPARTLIST List,
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount,
    IN BOOLEAN AutoCreate);

BOOLEAN
CreateExtendedPartition(
    IN PPARTLIST List,
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount);

BOOLEAN
CreateLogicalPartition(
    IN PPARTLIST List,
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount,
    IN BOOLEAN AutoCreate);

NTSTATUS
DismountVolume(
    IN PPARTENTRY PartEntry);

BOOLEAN
DeletePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    OUT PPARTENTRY* FreeRegion OPTIONAL);

PPARTENTRY
FindSupportedSystemPartition(
    IN PPARTLIST List,
    IN BOOLEAN ForceSelect,
    IN PDISKENTRY AlternativeDisk OPTIONAL,
    IN PPARTENTRY AlternativePart OPTIONAL);

BOOLEAN
SetActivePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    IN PPARTENTRY OldActivePart OPTIONAL);

NTSTATUS
WritePartitions(
    IN PDISKENTRY DiskEntry);

BOOLEAN
WritePartitionsToDisk(
    IN PPARTLIST List);

BOOLEAN
SetMountedDeviceValue(
    IN WCHAR Letter,
    IN ULONG Signature,
    IN LARGE_INTEGER StartingOffset);

BOOLEAN
SetMountedDeviceValues(
    IN PPARTLIST List);

VOID
SetMBRPartitionType(
    IN PPARTENTRY PartEntry,
    IN UCHAR PartitionType);

ERROR_NUMBER
PrimaryPartitionCreationChecks(
    IN PPARTENTRY PartEntry);

ERROR_NUMBER
ExtendedPartitionCreationChecks(
    IN PPARTENTRY PartEntry);

ERROR_NUMBER
LogicalPartitionCreationChecks(
    IN PPARTENTRY PartEntry);

BOOLEAN
GetNextUnformattedPartition(
    IN PPARTLIST List,
    OUT PDISKENTRY *pDiskEntry OPTIONAL,
    OUT PPARTENTRY *pPartEntry);

BOOLEAN
GetNextUncheckedPartition(
    IN PPARTLIST List,
    OUT PDISKENTRY *pDiskEntry OPTIONAL,
    OUT PPARTENTRY *pPartEntry);


//
// Bootsector routines
//

NTSTATUS
InstallMbrBootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE DiskHandle);

/* EOF */
