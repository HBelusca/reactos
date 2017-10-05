/*
 * PROJECT:         ReactOS System Deployment / Storage Device Image (SDI) File Library
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            sdk/lib/sdilib/sdilib.h
 * PURPOSE:         System Deployment / Storage Device Image File Format.
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 *
 * DOCUMENTATION:
 *
 *  https://msdn.microsoft.com/en-us/library/ms838543(v=winembedded.5).aspx
 *  https://msdn.microsoft.com/en-us/library/ms838543.aspx
 *  "SDI file format specification" reversed by Sergii Kolisnyk, http://skolk.livejournal.com/1320.html
 *      http://skolk.livejournal.com/1591.html
 *  http://www.zytor.com/pub/git/users/alekdu/syslinux.git/com32/modules/sdi.c
 *  http://dox.ipxe.org/sdi_8c_source.html
 *  http://certcollection.org/forum/topic/54800-windows-embedded-standard-2009-step-by-step-deployment/
 *  "How Booting into a Boot Image Works" (MSDN) https://technet.microsoft.com/en-us/library/cc771845(v=ws.10).aspx
 */

#ifndef __SDILIB_H__
#define __SDILIB_H__

#pragma once

/* PSDK/NDK Headers */
// #define WIN32_NO_STATUS
// #include <windef.h>
// #include <winbase.h>
// #include <winnt.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
// #include <ndk/rtltypes.h>

#ifndef ROUND_DOWN
#define ROUND_DOWN(n, align) (((ULONG)n) & ~((align) - 1l))
#endif

#ifndef ROUND_UP
#define ROUND_UP(n, align) ROUND_DOWN(((ULONG)n) + (align) - 1, (align))
#endif



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


typedef GUID UUID;

typedef enum _MDB
{
    MDB_Unknown = 0,
    MDB_RAM,
    MDB_ROM
} MDB, *PMDB;

/* Seconds since Jan 1, 2000 0:00:00 (UTC) */
#define SDI_TIMESTAMP_BASE 946684800

#define SDI_SECTOR_SHIFT    9
#define SDI_SECTOR_SIZE    (1 << SDI_SECTOR_SHIFT) // 512

// /* Default block size (but not mandatory) */
// #define SDI_BLOCK_SIZE  (2 * _1M)


/**************** HARD DRIVES -- SDI FIXED DISK FORMAT SUPPORT ****************/

#include <pshpack1.h>

/*
 * This is the SDI header.
 * The information stored in it is *ALWAYS* in little-endian 64-bit format.
 */
typedef struct _SDI_HEADER
{
    CHAR    Signature[4];   // "$SDI"
    CHAR    Version[4];     // "0001"
    UINT32  MDBType;
    UINT32  SDIReserved;

    /* Points to the BOOT blob */
    UINT64  BootCodeOffset; // UINT32 Low; UINT32 High;
    /* Size of the BOOT blob */
    UINT64  BootCodeSize;   // UINT32 Low; UINT32 High;

    UINT64  VendorID;       // 16-bit HEX value
    UINT64  DeviceID;       // 16-bit HEX value
    GUID    DeviceModel;
    UINT64  DeviceRole;     // Int32 value
    UINT64  Reserved1;
    GUID    RuntimeGUID;
    UINT64  RuntimeOEMRev;  // Int32 value
    UINT64  Reserved2;
    UINT64  PageAlignment;  // BLOB alignment value in number of pages  // PageAlignmentFactor
    UINT64  Reserved3[48];
    UINT64  Checksum;

    // Padding
    // Table of Contents, containing the blob entries
} SDI_HEADER, *PSDI_HEADER;
C_ASSERT(sizeof(SDI_HEADER) == 0x400); // SDI_SECTOR_SIZE

/* Blob types */
#define SDI_BLOB_BOOT   0x544F4F42  // Bootstrap ('BOOT')
#define SDI_BLOB_LOAD   0x44414F4C  // OS loader ('LOAD')
#define SDI_BLOB_DISK   0x4B534944  // Disk image ('DISK')
#define SDI_BLOB_PART   0x54524150  // Partition image ('PART')
/* Custom blob types */
#define SDI_BLOB_WIM    0x004D4957  // "Windows Imaging Format" image ('WIM\0')

typedef struct _SDI_BLOB
{
    CHAR    Signature[4];
    UINT32  Reserved;
    UINT64  Attribute;  // UInt32 value
    UINT64  Offset;
    UINT64  Size;
    UINT64  BaseAddress;
    // 0 for non-filesystem BLOBs, filesystem code for PART (as in MBR)
    // 7 for NTFS, 6 for BIGFAT, etc.
    UINT64  Reserved[3];
} SDI_BLOB, *PSDI_BLOB;

#include <poppack.h>

struct _SDIFILE;

typedef PVOID
(NTAPI *PSDI_ALLOCATE_ROUTINE)(
    IN SIZE_T Size,
    IN ULONG Flags,
    IN ULONG Tag
);

typedef VOID
(NTAPI *PSDI_FREE_ROUTINE)(
    IN PVOID Ptr,
    IN ULONG Flags,
    IN ULONG Tag
);

typedef NTSTATUS
(NTAPI *PSDI_FILE_READ_ROUTINE)(
    IN  struct _SDIFILE* LogFile,
    IN  PLARGE_INTEGER FileOffset,
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL
);

typedef NTSTATUS
(NTAPI *PSDI_FILE_WRITE_ROUTINE)(
    IN  struct _SDIFILE* LogFile,
    IN  PLARGE_INTEGER FileOffset,
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL
);

typedef NTSTATUS
(NTAPI *PSDI_FILE_SET_SIZE_ROUTINE)(
    IN struct _SDIFILE* LogFile,
    IN ULONG FileSize,
    IN ULONG OldFileSize
);

typedef NTSTATUS
(NTAPI *PSDI_FILE_FLUSH_ROUTINE)(
    IN struct _SDIFILE* LogFile,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length
);

typedef struct _SDIFILE
{
    PSDI_ALLOCATE_ROUTINE   Allocate;
    PSDI_FREE_ROUTINE       Free;
    PSDI_FILE_SET_SIZE_ROUTINE FileSetSize;
    PSDI_FILE_WRITE_ROUTINE FileWrite;
    PSDI_FILE_READ_ROUTINE  FileRead;
    PSDI_FILE_FLUSH_ROUTINE FileFlush;

    // UNICODE_STRING FileName;

    SDI_TYPE Type;
    BOOLEAN  ReadOnly;
    ULONG CurrentSize;  // Current SDI file size
    UINT64 TotalSize;   // Total size of the virtualized disk, as reported to the host OS
    LARGE_INTEGER EndOfData;    // Physical end of data, before the SDI header

    /* Copy of the SDI header */
    SDI_HEADER Header;

    // /* Cached Block Allocation Table (BAT) */
    // UINT64 BlockAllocationTableOffset;
    // PULONG BlockAllocationTable;
    // ULONG  BlockAllocationTableEntries;

    /* SDI block size in bytes; must be a power of 2 */
    ULONG BlockSize;
    /* Corresponding number of SDI sectors in a block == BlockSize / SDI_SECTOR_SIZE */
    ULONG BlockSectors; // Is also equal to the number of valid bits in the bitmap.

    /* Cache for block bitmaps, used for each operation on block being used */
    ULONG  BlockBitmapSize;  // In number of sectors
    PULONG BlockBitmapBuffer;
    RTL_BITMAP BlockBitmap;
} SDIFILE, *PSDIFILE;



NTSTATUS
NTAPI
SdiCreateDisk(
    IN PSDIFILE SdiFile,
    IN SDI_TYPE Type);

NTSTATUS
NTAPI
SdiOpenDisk(
    IN OUT PSDIFILE SdiFile,
    IN ULONG    FileSize,
    // IN BOOLEAN  CreateNew,
    IN BOOLEAN  ReadOnly,
    IN PSDI_ALLOCATE_ROUTINE   Allocate,
    IN PSDI_FREE_ROUTINE       Free,
    IN PSDI_FILE_SET_SIZE_ROUTINE FileSetSize,
    IN PSDI_FILE_WRITE_ROUTINE FileWrite,
    IN PSDI_FILE_READ_ROUTINE  FileRead,
    IN PSDI_FILE_FLUSH_ROUTINE FileFlush);

NTSTATUS
NTAPI
SdiFlushDisk(
    IN PSDIFILE SdiFile);

VOID    // or NTSTATUS, to return whether or not the disk is still used??
NTAPI
SdiCloseDisk(
    IN PSDIFILE SdiFile);

NTSTATUS
NTAPI
SdiReadDisk(
    IN  PSDIFILE SdiFile,
    OUT PVOID Buffer,
    IN  ULONG Length, // SIZE_T
    IN  PLARGE_INTEGER ByteOffset); // OPTIONAL

NTSTATUS
NTAPI
SdiWriteDisk(
    IN PSDIFILE SdiFile,
    IN PVOID Buffer,
    IN ULONG Length, // SIZE_T
    IN PLARGE_INTEGER ByteOffset); // OPTIONAL

NTSTATUS
NTAPI
SdiPackDisk(    // Consolidate
    IN PSDIFILE SdiFile,
    IN ULONG AlignmentFactor);

NTSTATUS
NTAPI
SdiAddBlob(
    IN PSDIFILE SdiFile,
    IN ULONG Type,
    IN ULONG Attribute,
    IN PVOID Buffer OPTIONAL,
    IN ULONG Length, // SIZE_T
    /*IN ULONG Size*/);

NTSTATUS
NTAPI
SdiDeleteBlob(
    IN PSDIFILE SdiFile,
    IN ULONG Type);

NTSTATUS
NTAPI
SdiReadBlob(
    IN  PSDIFILE SdiFile,
    OUT PVOID Buffer,
    IN  ULONG Length); // SIZE_T

#endif /* __SDILIB_H__ */
