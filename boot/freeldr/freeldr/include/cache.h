/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Block I/O cache
 * COPYRIGHT:   Copyright 2002 Brian Palmer <brianp@sginet.com>
 */

#pragma once

/*
 * Defines how the cache code is to be used.
 * If CACHE_FOR_FILESYSTEM is defined, the cache code is used for file systems.
 * If it isn't defined however, the cache code is used by the disk driver.
 */
// #define CACHE_FOR_FILESYSTEM
// #define CACHE_DISKDRIVE

#ifndef CACHE_FOR_FILESYSTEM
///typedef struct _DISKDEVICE *PDISKDEVICE;
#include "disk.h"
/* Driver-specific disk device object extension */
typedef struct _DDISKDEVICE
{
    CONFIGURATION_TYPE DeviceType;
    ULONG DiskSignature;
    GEOMETRY Geometry;
    ULONG SectorSize;       // == Geometry.BytesPerSector
    ULONGLONG SectorCount;  // == Geometry.Sectors
    ULONGLONG SectorOffset; // Always 0 (whole disk) -- FIXME: Remove?
    PVOID Context; // For "miniports"
    PBLOCK_INIT DevInit;
    PBLOCK_UNINIT DevUninit;
    PBLOCK_READ ReadBlocks;
    PBLOCK_WRITE WriteBlocks;
/** Partition support */
    PARTITION_STYLE PartitionStyle;
} DISKDEVICE, *PDISKDEVICE; // BLOCK_IO_MEDIA

#endif


#define TAG_CACHE_DATA  'DcaC'
#define TAG_CACHE_BLOCK 'BcaC'

/**
 * @brief
 * This structure describes a cached block element. The disk is divided up into
 * cache blocks. For disks which LBA is not supported each block is the size of
 * one track. This will force the cache manager to make track sized reads, and
 * therefore maximizes throughput. For disks which support LBA the block size
 * is 64k because they have no cylinder, head, or sector boundaries.
 **/
typedef struct _CACHE_BLOCK
{
    LIST_ENTRY ListEntry;   ///< Doubly linked list synchronization member

    ULONG BlockNumber;      ///< Track index for CHS, 64k block index for LBA
    BOOLEAN LockedInCache;  ///< Indicates that this block is locked in cache memory
    ULONG AccessCount;      ///< Access count for this block

    PVOID BlockData;        ///< Pointer to block data
} CACHE_BLOCK, *PCACHE_BLOCK;

/**
 * @brief
 * This structure describes a cached drive. It contains the drive device ID
 * and indicates whether or not LBA is supported. If LBA is not supported then
 * the drive's geometry is described here.
 **/
typedef struct _CACHE_DRIVE
{
#ifdef CACHE_FOR_FILESYSTEM
    ULONG DeviceId;     ///< ID of the underlying disk device.
#else
    PDISKDEVICE Device; ///< Disk device.
#endif
    ULONG BytesPerSector;

    ULONG BlockSize;            ///< Block size (in sectors)
    LIST_ENTRY CacheBlockHead;  ///< Contains CACHE_BLOCK structures
} CACHE_DRIVE, *PCACHE_DRIVE;


/* FUNCTIONS *****************************************************************/

BOOLEAN
CacheInitializeDrive(
#ifdef CACHE_FOR_FILESYSTEM
    _In_ ULONG DeviceId
#else
    _In_ PDISKDEVICE Device
#endif
);

VOID CacheInvalidateCacheData(VOID);

BOOLEAN
CacheReadDiskSectors(
#ifdef CACHE_FOR_FILESYSTEM
    _In_ ULONG DeviceId,
#else
    _In_ PDISKDEVICE Device,
#endif
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer);

#if 0
BOOLEAN
CacheForceDiskSectorsIntoCache(
#ifdef CACHE_FOR_FILESYSTEM
    _In_ ULONG DeviceId,
#else
    _In_ PDISKDEVICE Device,
#endif
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount);
#endif

BOOLEAN
CacheReleaseMemory(
    _In_ ULONG MinimumAmountToRelease);
