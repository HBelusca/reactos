/*
 * PROJECT:         ReactOS VHD File Library
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            sdk/lib/vhdlib/vhdlib.h
 * PURPOSE:         Virtual Hard Disk Format.
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 *
 * DOCUMENTATION:
 *
 *  http://citrixblogger.org/2008/12/01/dynamic-vhd-walkthrough/
 *  http://www.microsoft.com/en-us/download/details.aspx?id=23850
 *  https://projects.honeynet.org/svn/sebek/virtualization/qebek/trunk/block/vpc.c
 *  https://raw.githubusercontent.com/qemu/qemu/master/block/vpc.c
 *  https://raw.githubusercontent.com/mirror/vbox/master/src/VBox/Storage/VHD.cpp
 *  https://raw.githubusercontent.com/mirror/vbox/master/src/VBox/Storage/VD.cpp
 *  https://gitweb.gentoo.org/proj/qemu-kvm.git/tree/block/vpc.c?h=qemu-kvm-0.12.4-gentoo&id=827dccd6740639c64732418539bf17e6e4c99d77
 *
 */

#ifndef __VHDLIB_H__
#define __VHDLIB_H__

#pragma once

/* PSDK/NDK Headers */
// #define WIN32_NO_STATUS
// #include <windef.h>
// #include <winbase.h>
// #include <winnt.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
// #include <ndk/rtltypes.h>

// FIXME! (see wdm.h)
// typedef GUID UUID; // see ntddk.h
typedef GUID *PGUID;

// FIXME everywhere: do NOT use EXPLICIT casts to ULONG!
#ifndef ROUND_DOWN
// #define ROUND_DOWN(n, align) (((ULONG)n) & ~((align) - 1l))
#define ROUND_DOWN(n, align) ((n) & ~((align) - 1))
#endif

#ifndef ROUND_UP
// #define ROUND_UP(n, align) ROUND_DOWN(((ULONG)n) + (align) - 1, (align))
#define ROUND_UP(n, align) ROUND_DOWN((n) + (align) - 1, (align))
#endif

// FIXME in ntoskrnl: use ULONG_PTR instead of ULONG64!
#define IS_ALIGNED(n, align) (((n) & ((align) - 1)) == 0)



// #include <endian.h>
// #include <byteswap.h>

// #ifdef LITTLE_ENDIAN

#define BE16_TO_HOST(x) RtlUshortByteSwap(x)    // be16toh
#define BE32_TO_HOST(x) RtlUlongByteSwap(x)     // be32toh
#define BE64_TO_HOST(x) RtlUlonglongByteSwap(x) // be64toh

#define HOST_TO_BE16(x) RtlUshortByteSwap(x)    // htobe16
#define HOST_TO_BE32(x) RtlUlongByteSwap(x)     // htobe32
#define HOST_TO_BE64(x) RtlUlonglongByteSwap(x) // htobe64

// #else

// #define BE16_TO_HOST(x) (x)
// #define BE32_TO_HOST(x) (x)
// #define BE64_TO_HOST(x) (x)

// #define HOST_TO_BE16(x) (x)
// #define HOST_TO_BE32(x) (x)
// #define HOST_TO_BE64(x) (x)

// #endif


typedef enum _VHD_TYPE
{
    RESERVED_0          = 0, // "None" disk; how this is usable?? (does it mean: uninitialized?)
    RESERVED_1          = 1,
    VHD_FIXED           = 2,
    VHD_DYNAMIC         = 3,
    VHD_DIFFERENCING    = 4, // Undo disks are also differencing disks
    RESERVED_2,
    RESERVED_3,      // = 6, // Used for images linked to physical drives
    VHD_MAX_TYPE
} VHD_TYPE;

typedef struct _VHD_PARENT_LOCATOR_INFO
{
    UINT32  Platform;
    UINT32  DataLength;
    PVOID   Data;
} VHD_PARENT_LOCATOR_INFO, *PVHD_PARENT_LOCATOR_INFO;

/* Seconds from Jan 1, 1970 0:00:00 (UTC) to Jan 1, 2000 0:00:00 (UTC) */
#define VHD_TIMESTAMP_BASE 946684800

#define VHD_SECTOR_SHIFT    9
#define VHD_SECTOR_SIZE    (1 << VHD_SECTOR_SHIFT) // 512

/* Make the code cleaner with some definitions for size multiples */
#define _1KB (1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024 * _1MB)

/* Default block size (but not mandatory) */
#define VHD_BLOCK_SIZE  (2 * _1MB)

/* Maximum disk CHS geometry */
#define VHD_CHS_MAX_C   65535LL
#define VHD_CHS_MAX_H   16
#define VHD_CHS_MAX_S   255

#define VHD_MAX_SECTORS     0xFF000000    /* 2040 GiB max image size */
/* ATA (AT Attachment, i.e. IDE) disk limitation of 127 GB */
#define VHD_MAX_GEOMETRY    (VHD_CHS_MAX_C * VHD_CHS_MAX_H * VHD_CHS_MAX_S)


#include <pshpack1.h>

/*
 * This is the VHD footer and header -- two copies for redundancy purposes.
 * The information stored in it is *ALWAYS* in BIG-endian format!
 */
typedef struct _VHD_FOOTER
{
    CHAR        creator[8]; // "conectix"
    UINT32      features;
    UINT32      version;

    // Offset of next header structure, 0xFFFFFFFF if none
    UINT64      data_offset;

    // Seconds since Jan 1, 2000 0:00:00 (UTC)
    UINT32      timestamp;

    CHAR        creator_app[4]; // "vpc "; "win"
    UINT32      creator_version;
    CHAR        creator_os[4];  // "Wi2k" // HostOS

    UINT64      orig_size;
    UINT64      current_size;

    UINT16      cyls;
    UINT8       heads;
    UINT8       secs_per_cyl;

    UINT32      type;   // VHD_TYPE

    // Checksum of the Hard Disk Footer ("one's complement of the sum
    // of all the bytes in the footer without the checksum field")
    UINT32      checksum;

    // UUID used to identify a parent hard disk (backing file)
    UINT8       uuid[16];

    UINT8       in_saved_state;

    UINT8       Padding[0x200-0x55];

} VHD_FOOTER, *PVHD_FOOTER;
C_ASSERT(sizeof(VHD_FOOTER) == 0x200); // VHD_SECTOR_SIZE

typedef struct _VHD_DYNAMIC_HEADER
{
    CHAR        magic[8]; // "cxsparse"

    // Offset of next header structure, 0xFFFFFFFF if none
    UINT64      data_offset;

    // Offset of the Block Allocation Table (BAT)
    UINT64      table_offset;

    UINT32      version;
    UINT32      max_table_entries; // 32bit/entry

    // 2 MB by default, must be a power of two
    UINT32      block_size;

    UINT32      checksum;
    UINT8       parent_uuid[16];
    UINT32      parent_timestamp;
    UINT32      reserved;

    // Backing file name (in UTF-16)
    UINT16      parent_name[256];

    struct
    {
        UINT32      platform;
        UINT32      data_space;
        UINT32      data_length;
        UINT32      reserved;
        UINT64      data_offset;
    } parent_locator[8];

    UINT8       Padding[0x400-0x300];

} VHD_DYNAMIC_HEADER, *PVHD_DYNAMIC_HEADER;
C_ASSERT(sizeof(VHD_DYNAMIC_HEADER) == 0x400);

/*
 * For VHD images linked to physical drives, this is the header used
 * in lieu of a VHD_FOOTER header. The VHD image footer is however a
 * standard VHD_FOOTER with 'type' == 0x06.
 */
typedef struct _VHD_PHYSICAL_HEADER
{
    CHAR        magic[8]; // "\0\0\0cxraw"

    // Offset of next header structure, 0xFFFFFFFF if none
    UINT64      data_offset;

    UINT32      version;
    UINT32      checksum;

    CHAR        creator_os[4]; // "Wi2k"
    UINT32      unknown;

    UINT64      orig_size;
    UINT32      version2;    // ???
    UINT32      reserved;

    UINT8       Padding[0x200-0x30];

    // Physical drive name (in ANSI)
    UINT8       drive_name[512]; // parent_name
} VHD_PHYSICAL_HEADER, *PVHD_PHYSICAL_HEADER;
C_ASSERT(sizeof(VHD_PHYSICAL_HEADER) == 0x400);

/*
 * For images linked to physical drives, the head block is:
 *
Offset(h) 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F

00000000  00 00 00 63 78 72 61 77 FF FF FF FF FF FF FF FF  ...cxrawÿÿÿÿÿÿÿÿ
00000010  00 01 00 00 FF FF EE 35 57 69 32 6B 00 00 01 04  ....ÿÿî5Wi2k....
00000020  00 00 00 3A 38 8B 02 00 00 01 00 00 00 00 00 00  ...:8‹..........
00000030  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000060  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000070  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000080  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000090  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000E0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000F0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000100  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000110  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000120  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000130  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000140  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000150  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000160  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000170  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000180  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000190  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001E0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001F0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000200  5C 5C 2E 5C 50 48 59 53 49 43 41 4C 44 52 49 56  \\.\PHYSICALDRIV
00000210  45 31 00 00 00 00 00 00 00 00 00 00 00 00 00 00  E1..............
00000220  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000230  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000240  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000250  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000260  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000270  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000280  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000290  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000002A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000002B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000002C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000002D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000002E0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000002F0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000300  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000310  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000320  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000330  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000340  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000350  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000360  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000370  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000380  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000390  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000003A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000003B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000003C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000003D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000003E0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000003F0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

... (only NULLs)

00010200  63 6F 6E 65 63 74 69 78 00 00 00 02 00 01 00 00  conectix........
00010210  00 00 00 00 00 00 00 00 20 6C 69 EF 76 70 63 20  ........ liïvpc 
00010220  00 05 00 03 57 69 32 6B 00 00 00 3A 38 8B 02 00  ....Wi2k...:8‹..
00010230  00 00 00 3A 38 8B 02 00 FF FF 10 FF 00 00 00 06  ...:8‹..ÿÿ.ÿ....
00010240  FF FF EB E5 A4 6C 34 DB 13 44 11 E7 82 A0 E9 56  ÿÿëå¤l4Û.D.ç‚ éV
00010250  17 38 2A AF 00 00 00 00 00 00 00 00 00 00 00 00  .8*¯............
00010260  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010270  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010280  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010290  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000102A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000102B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000102C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000102D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000102E0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000102F0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010300  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010310  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010320  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010330  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010340  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010350  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010360  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010370  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010380  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00010390  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000103A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000103B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000103C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000103D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000103E0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000103F0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

 */

#include <poppack.h>

struct _VHDFILE;

typedef PVOID
(NTAPI *PVHD_ALLOCATE_ROUTINE)(
    IN SIZE_T Size,
    IN ULONG Flags,
    IN ULONG Tag
);

typedef VOID
(NTAPI *PVHD_FREE_ROUTINE)(
    IN PVOID Ptr,
    IN ULONG Flags,
    IN ULONG Tag
);

typedef NTSTATUS
(NTAPI *PVHD_FILE_READ_ROUTINE)(
    IN  struct _VHDFILE* LogFile,
    IN  PLARGE_INTEGER FileOffset,
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL
);

typedef NTSTATUS
(NTAPI *PVHD_FILE_WRITE_ROUTINE)(
    IN  struct _VHDFILE* LogFile,
    IN  PLARGE_INTEGER FileOffset,
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL
);

typedef NTSTATUS
(NTAPI *PVHD_FILE_SET_SIZE_ROUTINE)(
    IN struct _VHDFILE* LogFile,
    IN ULONG FileSize,
    IN ULONG OldFileSize
);

typedef NTSTATUS
(NTAPI *PVHD_FILE_FLUSH_ROUTINE)(
    IN struct _VHDFILE* LogFile,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length
);

// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363972(v=vs.85).aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363976(v=vs.85).aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363969(v=vs.85).aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa365231(v=vs.85).aspx

typedef struct _DISK_INFO
{
    UINT16 Cylinders;       // DWORD
    UINT8  Heads;           // DWORD
    UINT8  Sectors;         // QWORD    // SectorsPerCylinder
    // SectorPerTrack; ???  // DWORD
    UINT16 SectorSize;
} DISK_INFO, *PDISK_INFO;

typedef struct _VHDFILE
{
    PVHD_ALLOCATE_ROUTINE   Allocate;
    PVHD_FREE_ROUTINE       Free;
    PVHD_FILE_SET_SIZE_ROUTINE FileSetSize;
    PVHD_FILE_WRITE_ROUTINE FileWrite;
    PVHD_FILE_READ_ROUTINE  FileRead;
    PVHD_FILE_FLUSH_ROUTINE FileFlush;

    // UNICODE_STRING FileName;

    VHD_TYPE Type;
    BOOLEAN  ReadOnly;
    DISK_INFO DiskInfo;
    UINT64 TotalSize;   // Total size of the virtualized disk, as reported to the host OS
    LARGE_INTEGER EndOfData; // Physical end of data, before the VHD footer (EndOfData + sizeof(VHD_FOOTER) == current VHD file size)

    /* Copy of the VHD footer */
    VHD_FOOTER Footer;


/** Only valid for dynamic & differencing disks ******************************/

    /* Cached Block Allocation Table (BAT) */
    UINT64 BlockAllocationTableOffset;
    ULONG  BlockAllocationTableSize;    // Size of the BAT in number of sectors
    ULONG  BlockAllocationTableEntries;
    PULONG BlockAllocationTable;

    /* Generic VHD data block size in bytes; must be a power of 2 */
    ULONG BlockSize;
    /* Corresponding number of VHD sectors in a block == BlockSize / VHD_SECTOR_SIZE */
    ULONG BlockSectors; // Number of sectors in a data block; also equal
                        // to the number of bits in the block bitmap.

    /* Cache for data block bitmaps, used for each operation on block being used */
    ULONG  CurrentBlockNumber;  // Current cached data block index
    ULONG  BlockBitmapSize;     // Size of the bitmap in number of sectors
    PULONG BlockBitmapBuffer;
    RTL_BITMAP BlockBitmap;
} VHDFILE, *PVHDFILE;


// Disk Geometry stuff
#if 0

LARGE_INTEGER DiskLength;
LONG DiskOffset;

/* Data we get from the disk */
ULONG BytesPerSector;
ULONG SectorsPerTrack;
ULONG NumberOfHeads;
ULONG Cylinders;
ULONG HiddenSectors;

typedef struct _PARTITION_INFORMATION {
    LARGE_INTEGER  StartingOffset;
    LARGE_INTEGER  PartitionLength;
    ULONG  HiddenSectors;
    ULONG  PartitionNumber;
    UCHAR  PartitionType;
    BOOLEAN  BootIndicator;
    BOOLEAN  RecognizedPartition;
    BOOLEAN  RewritePartition;
} PARTITION_INFORMATION, *PPARTITION_INFORMATION;

IOCTL_DISK_GET_DRIVE_LAYOUT -->
typedef struct _DRIVE_LAYOUT_INFORMATION {
  DWORD                 PartitionCount;
  DWORD                 Signature;
  PARTITION_INFORMATION PartitionEntry[1];
} DRIVE_LAYOUT_INFORMATION, *PDRIVE_LAYOUT_INFORMATION;

 typedef struct _DISK_GEOMETRY {
    LARGE_INTEGER  Cylinders;
    MEDIA_TYPE  MediaType;
    ULONG  TracksPerCylinder;
    ULONG  SectorsPerTrack;
    ULONG  BytesPerSector;
} DISK_GEOMETRY, *PDISK_GEOMETRY;
typedef struct _DISK_GEOMETRY_EX {
    DISK_GEOMETRY  Geometry;
    LARGE_INTEGER  DiskSize;
    UCHAR  Data[1];
} DISK_GEOMETRY_EX, *PDISK_GEOMETRY_EX;

PIO_STACK_LOCATION IoStackLocation->Parameters.Read/Write :
struct {
    ULONG Length;
    ULONG POINTER_ALIGNMENT Key;
    LARGE_INTEGER ByteOffset;
} Read;
struct {
    ULONG Length;
    ULONG POINTER_ALIGNMENT Key;
    LARGE_INTEGER ByteOffset;
} Write;

#endif


extern GUID toto;

NTSTATUS
NTAPI
VhdCreateDisk(
    IN OUT PVHDFILE VhdFile,
    IN VHD_TYPE Type,
    IN BOOLEAN  WipeOut,
    IN UINT64   TotalSize, // ULONGLONG MaximumSize
    IN ULONG    BlockSizeInBytes,  // UINT32
    IN ULONG    SectorSizeInBytes, // UINT32 // I think it's useless, because for VHDs it can only be == 512 bytes == VHD_SECTOR_SIZE
    IN PGUID    UniqueId OPTIONAL,
    IN ULONG    TimeStamp,
    IN PGUID ParentIdentifier OPTIONAL,
    IN ULONG ParentTimeStamp OPTIONAL,
    IN PCWSTR ParentPath OPTIONAL,
    IN PVHD_PARENT_LOCATOR_INFO ParentLocatorInfoArray OPTIONAL,
    IN ULONG NumberOfParentLocators OPTIONAL,
    IN CHAR  CreatorApp[4],
    IN ULONG CreatorVersion,
    IN CHAR  CreatorHostOS[4],
    IN PVHD_ALLOCATE_ROUTINE   Allocate,
    IN PVHD_FREE_ROUTINE       Free,
    IN PVHD_FILE_SET_SIZE_ROUTINE FileSetSize,
    IN PVHD_FILE_WRITE_ROUTINE FileWrite,
    IN PVHD_FILE_READ_ROUTINE  FileRead,
    IN PVHD_FILE_FLUSH_ROUTINE FileFlush);

NTSTATUS
NTAPI
VhdOpenDisk(
    IN OUT PVHDFILE VhdFile,
    IN UINT64   FileSize,
    // IN BOOLEAN  CreateNew,
    IN BOOLEAN  ReadOnly,
    IN PVHD_ALLOCATE_ROUTINE   Allocate,
    IN PVHD_FREE_ROUTINE       Free,
    IN PVHD_FILE_SET_SIZE_ROUTINE FileSetSize,
    IN PVHD_FILE_WRITE_ROUTINE FileWrite,
    IN PVHD_FILE_READ_ROUTINE  FileRead,
    IN PVHD_FILE_FLUSH_ROUTINE FileFlush);

NTSTATUS
NTAPI
VhdFlushDisk(
    IN PVHDFILE VhdFile);

VOID    // or NTSTATUS, to return whether or not the disk is still used??
NTAPI
VhdCloseDisk(
    IN PVHDFILE VhdFile);

/* Constant alignment of 1 sector (512 bytes) */
#define VhdGetAlignmentBytes()  VHD_SECTOR_SIZE

NTSTATUS
NTAPI
VhdReadDiskAligned(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL);

NTSTATUS
NTAPI
VhdWriteDiskAligned(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL);

UINT64
NTAPI
VhdGetVirtTotalSize(
    IN PVHDFILE VhdFile);

UINT64
NTAPI
VhdGetFileSize(
    IN PVHDFILE VhdFile);

NTSTATUS
NTAPI
VhdCompactDisk(
    IN PVHDFILE VhdFile);

NTSTATUS
NTAPI
VhdExpandDisk(
    IN PVHDFILE VhdFile);

NTSTATUS
NTAPI
VhdRepairDisk(
    IN PVHDFILE VhdFile);

#endif /* __VHDLIB_H__ */
