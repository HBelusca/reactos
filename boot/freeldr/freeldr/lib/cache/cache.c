/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Block I/O cache
 * COPYRIGHT:   Copyright 2002 Brian Palmer <brianp@sginet.com>
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(CACHE);

/* INTERNAL DATA *************************************************************/

static CACHE_DRIVE CacheManagerDrive;
static BOOLEAN CacheManagerInitialized = FALSE;
static BOOLEAN CacheManagerDataInvalid = FALSE;
ULONG  CacheBlockCount = 0;
SIZE_T CacheSizeLimit = 0;
SIZE_T CacheSizeCurrent = 0;

/* FUNCTIONS *****************************************************************/

/* In blocklist.c */
extern PCACHE_BLOCK
CacheInternalGetBlockPointer(
    _In_ PCACHE_DRIVE CacheDrive,
    _In_ ULONG BlockNumber);

/* In blocklist.c */
extern BOOLEAN
CacheInternalFreeBlock(
    _In_ PCACHE_DRIVE CacheDrive);

BOOLEAN
CacheInitializeDrive(
#ifdef CACHE_FOR_FILESYSTEM
    _In_ ULONG DeviceId
#else
    _In_ PDISKDEVICE Device
#endif
)
{
    /* If we already have a cache for this drive then by all means lets keep it,
     * unless it is a removable drive, in which case we'll invalidate the cache */
    if (CacheManagerInitialized &&
#if 0
// TODO: We should retrieve the "Removable" bit in the
// configuration tree for this device.
        (DriveNumber == CacheManagerDrive.DriveNumber) &&
        (DriveNumber >= 0x80) &&
#endif
        !CacheManagerDataInvalid)
    {
        return TRUE;
    }

    CacheManagerDataInvalid = FALSE;

    /* If we have already been initialized then free the old data */
    if (CacheManagerInitialized)
    {
        CacheManagerInitialized = FALSE;

        TRACE("CacheBlockCount:  %lu\n", CacheBlockCount);
        TRACE("CacheSizeLimit:   %Iu\n", CacheSizeLimit);
        TRACE("CacheSizeCurrent: %Iu\n", CacheSizeCurrent);

        /* Loop through and free the cache blocks */
        while (!IsListEmpty(&CacheManagerDrive.CacheBlockHead))
        {
            PLIST_ENTRY Entry = RemoveHeadList(&CacheManagerDrive.CacheBlockHead);
            PCACHE_BLOCK NextCacheBlock = CONTAINING_RECORD(Entry, CACHE_BLOCK, ListEntry);

            FrLdrTempFree(NextCacheBlock->BlockData, TAG_CACHE_DATA);
            FrLdrTempFree(NextCacheBlock, TAG_CACHE_BLOCK);
        }
    }

    /* Initialize the structure */
    RtlZeroMemory(&CacheManagerDrive, sizeof(CacheManagerDrive));
    InitializeListHead(&CacheManagerDrive.CacheBlockHead);
#ifdef CACHE_FOR_FILESYSTEM
    CacheManagerDrive.DeviceId = DeviceId;
    {
    FILEINFORMATION Information;
    ARC_STATUS Status = ArcGetFileInformation(DeviceId, &Information);
    if (Information.Type == CdromController)
        CacheManagerDrive.BytesPerSector = 2048;
    else
        CacheManagerDrive.BytesPerSector = 512;
    }
#else
    CacheManagerDrive.Device = Device;
    CacheManagerDrive.BytesPerSector = Device->SectorSize;
#endif

    /* Get the number of sectors in each cache block */
    CacheManagerDrive.BlockSize = MachDiskGetCacheableBlockCount(DriveNumber);

    CacheBlockCount = 0;
    CacheSizeCurrent = 0;
    CacheSizeLimit = TotalPagesInLookupTable / 8 * MM_PAGE_SIZE;
    CacheSizeLimit = min(CacheSizeLimit, TEMP_HEAP_SIZE - (128 * 1024));

    CacheManagerInitialized = TRUE;

#ifdef CACHE_FOR_FILESYSTEM
    TRACE("Initializing device ID 0x%x:\n", CacheManagerDrive.DeviceId);
#else
    TRACE("Initializing device 0x%p:\n", CacheManagerDrive.Device);
#endif
    TRACE("BytesPerSector: %lu\n", CacheManagerDrive.BytesPerSector);
    TRACE("BlockSize: %lu\n", CacheManagerDrive.BlockSize);
    TRACE("CacheSizeLimit: %Iu\n", CacheSizeLimit);

    return TRUE;
}

VOID CacheInvalidateCacheData(VOID)
{
    CacheManagerDataInvalid = TRUE;
}

BOOLEAN
CacheReadDiskSectors(
#ifdef CACHE_FOR_FILESYSTEM
    _In_ ULONG DeviceId,
#else
    _In_ PDISKDEVICE Device,
#endif
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer)
{
    PCACHE_BLOCK CacheBlock;
    ULONG StartBlock;
    ULONG SectorOffsetInStartBlock;
    ULONG CopyLengthInStartBlock;
    ULONG EndBlock;
    ULONG SectorOffsetInEndBlock;
    ULONG BlockCount;
    ULONG Idx;

#ifdef CACHE_FOR_FILESYSTEM
    TRACE("CacheReadDiskSectors(DeviceId: 0x%x) StartSector: %I64u SectorCount: %lu Buffer: 0x%p\n",
          DeviceId, StartSector, SectorCount, Buffer);
#else
    TRACE("CacheReadDiskSectors(Device: 0x%p) StartSector: %I64u SectorCount: %lu Buffer: 0x%p\n",
          Device, StartSector, SectorCount, Buffer);
#endif

    /* If we aren't initialized yet then we cannot do this */
    if (CacheManagerInitialized == FALSE)
        return FALSE;

    /* Calculate which blocks we must cache */
    StartBlock = (ULONG)(StartSector / CacheManagerDrive.BlockSize);
    SectorOffsetInStartBlock = (ULONG)(StartSector % CacheManagerDrive.BlockSize);
    CopyLengthInStartBlock = (ULONG)((SectorCount > (CacheManagerDrive.BlockSize - SectorOffsetInStartBlock)) ? (CacheManagerDrive.BlockSize - SectorOffsetInStartBlock) : SectorCount);
    EndBlock = (ULONG)((StartSector + (SectorCount - 1)) / CacheManagerDrive.BlockSize);
    SectorOffsetInEndBlock = (ULONG)(1 + (StartSector + (SectorCount - 1)) % CacheManagerDrive.BlockSize);
    BlockCount = (EndBlock - StartBlock) + 1;
    TRACE("StartBlock: %lu SectorOffsetInStartBlock: %lu CopyLengthInStartBlock: %lu EndBlock: %lu SectorOffsetInEndBlock: %lu BlockCount: %lu\n",
          StartBlock, SectorOffsetInStartBlock, CopyLengthInStartBlock, EndBlock, SectorOffsetInEndBlock, BlockCount);

    /* Read the first block into the buffer */
    if (BlockCount > 0)
    {
        /* Get cache block pointer (this forces the disk sectors into the cache memory) */
        CacheBlock = CacheInternalGetBlockPointer(&CacheManagerDrive, StartBlock);
        if (CacheBlock == NULL)
            return FALSE;

        /* Copy the portion requested into the buffer */
        RtlCopyMemory(Buffer,
                      (PVOID)((ULONG_PTR)CacheBlock->BlockData + (SectorOffsetInStartBlock * CacheManagerDrive.BytesPerSector)),
                      CopyLengthInStartBlock * CacheManagerDrive.BytesPerSector);
        TRACE("1 - RtlCopyMemory(0x%p, 0x%p, %lu)\n",
              Buffer, ((ULONG_PTR)CacheBlock->BlockData + (SectorOffsetInStartBlock * CacheManagerDrive.BytesPerSector)),
              CopyLengthInStartBlock * CacheManagerDrive.BytesPerSector);

        /* Update the buffer address */
        Buffer = (PVOID)((ULONG_PTR)Buffer + (CopyLengthInStartBlock * CacheManagerDrive.BytesPerSector));

        /* Update the block count */
        BlockCount--;
    }

    /* Loop through the middle blocks and read them into the buffer */
    for (Idx = StartBlock + 1; BlockCount > 1; ++Idx)
    {
        /* Get cache block pointer (this forces the disk sectors into the cache memory) */
        CacheBlock = CacheInternalGetBlockPointer(&CacheManagerDrive, Idx);
        if (CacheBlock == NULL)
            return FALSE;

        /* Copy the portion requested into the buffer */
        RtlCopyMemory(Buffer,
                      CacheBlock->BlockData,
                      CacheManagerDrive.BlockSize * CacheManagerDrive.BytesPerSector);
        TRACE("2 - RtlCopyMemory(0x%p, 0x%p, %lu)\n",
              Buffer, CacheBlock->BlockData,
              CacheManagerDrive.BlockSize * CacheManagerDrive.BytesPerSector);

        /* Update the buffer address */
        Buffer = (PVOID)((ULONG_PTR)Buffer + (CacheManagerDrive.BlockSize * CacheManagerDrive.BytesPerSector));

        /* Update the block count */
        BlockCount--;
    }

    /* Read the last block into the buffer */
    if (BlockCount > 0)
    {
        /* Get cache block pointer (this forces the disk sectors into the cache memory) */
        CacheBlock = CacheInternalGetBlockPointer(&CacheManagerDrive, EndBlock);
        if (CacheBlock == NULL)
            return FALSE;

        /* Copy the portion requested into the buffer */
        RtlCopyMemory(Buffer,
                      CacheBlock->BlockData,
                      SectorOffsetInEndBlock * CacheManagerDrive.BytesPerSector);
        TRACE("3 - RtlCopyMemory(0x%p, 0x%p, %lu)\n",
              Buffer, CacheBlock->BlockData,
              SectorOffsetInEndBlock * CacheManagerDrive.BytesPerSector);

        /* Update the buffer address */
        Buffer = (PVOID)((ULONG_PTR)Buffer + (SectorOffsetInEndBlock * CacheManagerDrive.BytesPerSector));

        /* Update the block count */
        BlockCount--;
    }

    return TRUE;
}

#if 0
BOOLEAN
CacheForceDiskSectorsIntoCache(
#ifdef CACHE_FOR_FILESYSTEM
    _In_ ULONG DeviceId,
#else
    _In_ PDISKDEVICE Device,
#endif
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount)
{
    PCACHE_BLOCK CacheBlock;
    ULONG StartBlock, EndBlock, BlockCount;
    ULONG Idx;

#ifdef CACHE_FOR_FILESYSTEM
    TRACE("CacheForceDiskSectorsIntoCache(DeviceId: 0x%x) StartSector: %I64u SectorCount: %lu\n",
          DeviceId, StartSector, SectorCount);
#else
    TRACE("CacheForceDiskSectorsIntoCache(Device: 0x%p) StartSector: %I64u SectorCount: %lu\n",
          Device, StartSector, SectorCount);
#endif

    /* If we aren't initialized yet then we cannot do this */
    if (CacheManagerInitialized == FALSE)
        return FALSE;

    /* Calculate which blocks we must cache */
    StartBlock = StartSector / CacheManagerDrive.BlockSize;
    EndBlock = (StartSector + SectorCount) / CacheManagerDrive.BlockSize;
    BlockCount = (EndBlock - StartBlock) + 1;

    /* Loop through and cache them */
    for (Idx = StartBlock; Idx < (StartBlock + BlockCount); ++Idx)
    {
        /* Get cache block pointer (this forces the disk sectors into the cache memory) */
        CacheBlock = CacheInternalGetBlockPointer(&CacheManagerDrive, Idx);
        if (CacheBlock == NULL)
            return FALSE;

        /* Lock the sectors into the cache */
        CacheBlock->LockedInCache = TRUE;
    }

    return TRUE;
}
#endif

BOOLEAN
CacheReleaseMemory(
    _In_ ULONG MinimumAmountToRelease)
{
    ULONG AmountReleased;

    TRACE("CacheReleaseMemory(MinimumAmountToRelease: %lu)\n", MinimumAmountToRelease);

    /* If we aren't initialized yet then we cannot do this */
    if (CacheManagerInitialized == FALSE)
        return FALSE;

    /* Loop through and try to free the requested amount of memory */
    for (AmountReleased = 0; AmountReleased < MinimumAmountToRelease; )
    {
        /* Try to free a block; if this fails then break out of the loop */
        if (!CacheInternalFreeBlock(&CacheManagerDrive))
            break;

        /* It succeeded so increment the amount of memory we have freed */
        AmountReleased += CacheManagerDrive.BlockSize * CacheManagerDrive.BytesPerSector;
    }

    /* Return status */
    return (AmountReleased >= MinimumAmountToRelease);
}
