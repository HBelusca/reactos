/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Block I/O cache
 * COPYRIGHT:   Copyright 2002 Brian Palmer <brianp@sginet.com>
 */

#pragma once

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
 * This structure describes a cached drive. It contains the BIOS drive number
 * and indicates whether or not LBA is supported. If LBA is not supported then
 * the drive's geometry is described here.
 **/
typedef struct _CACHE_DRIVE
{
    UCHAR DriveNumber;
    ULONG BytesPerSector;

    ULONG BlockSize;            ///< Block size (in sectors)
    LIST_ENTRY CacheBlockHead;  ///< Contains CACHE_BLOCK structures
} CACHE_DRIVE, *PCACHE_DRIVE;


/* FUNCTIONS *****************************************************************/

BOOLEAN
CacheInitializeDrive(
    _In_ UCHAR DriveNumber);

VOID CacheInvalidateCacheData(VOID);

BOOLEAN
CacheReadDiskSectors(
    _In_ UCHAR DriveNumber,
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer);

#if 0
BOOLEAN
CacheForceDiskSectorsIntoCache(
    _In_ UCHAR DriveNumber,
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount);
#endif

BOOLEAN
CacheReleaseMemory(
    _In_ ULONG MinimumAmountToRelease);
