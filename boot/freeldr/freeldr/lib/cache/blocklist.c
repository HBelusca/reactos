/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Cache blocks list
 * COPYRIGHT:   Copyright 2002 Brian Palmer <brianp@sginet.com>
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(CACHE);

/* INTERNAL DATA *************************************************************/

extern ULONG  CacheBlockCount;
extern SIZE_T CacheSizeLimit;
extern SIZE_T CacheSizeCurrent;

/* FUNCTIONS *****************************************************************/

/**
 * @brief   Dumps the list of cached blocks to the debug output port.
 **/
static VOID
CacheInternalDumpBlockList(
    _In_ PCACHE_DRIVE CacheDrive)
{
    PLIST_ENTRY Entry;

#ifdef CACHE_FOR_FILESYSTEM
    TRACE("Dumping block list for device ID 0x%x:\n", CacheDrive->DeviceId);
#else
    TRACE("Dumping block list for device 0x%p:\n", CacheDrive->Device);
#endif
    TRACE("BytesPerSector: %lu\n", CacheDrive->BytesPerSector);
    TRACE("BlockSize: %lu\n", CacheDrive->BlockSize);
    TRACE("CacheBlockCount:  %lu\n", CacheBlockCount);
    TRACE("CacheSizeLimit:   %Iu\n", CacheSizeLimit);
    TRACE("CacheSizeCurrent: %Iu\n", CacheSizeCurrent);

    for (Entry = CacheDrive->CacheBlockHead.Flink;
         Entry != &CacheDrive->CacheBlockHead;
         Entry = Entry->Flink)
    {
        PCACHE_BLOCK CacheBlock = CONTAINING_RECORD(Entry, CACHE_BLOCK, ListEntry);

        TRACE("Cache Block: CacheBlock: 0x%p\n", CacheBlock);
        TRACE("Cache Block: Block Number: %lu\n", CacheBlock->BlockNumber);
        TRACE("Cache Block: Access Count: %lu\n", CacheBlock->AccessCount);
        TRACE("Cache Block: Block Data: 0x%p\n", CacheBlock->BlockData);
        TRACE("Cache Block: Locked in cache: %s\n", CacheBlock->LockedInCache ? "TRUE" : "FALSE");

        if (CacheBlock->BlockData == NULL)
            BugCheck("CacheBlock->BlockData == NULL\n");
    }
}

/**
 * @brief   Moves the specified block to the head of the list.
 **/
static VOID
CacheInternalOptimizeBlockList(
    _In_ PCACHE_DRIVE CacheDrive,
    _In_ PCACHE_BLOCK CacheBlock)
{
    TRACE("CacheInternalOptimizeBlockList()\n");

    /* Don't do this if this block is already at the head of the list */
    if (&CacheBlock->ListEntry != CacheDrive->CacheBlockHead.Flink)
    {
        /* Remove this item from the block list */
        RemoveEntryList(&CacheBlock->ListEntry);

        /* Re-insert it at the head of the list */
        InsertHeadList(&CacheDrive->CacheBlockHead, &CacheBlock->ListEntry);
    }
}

/**
 * @brief   Searches the block list for a particular block.
 **/
static PCACHE_BLOCK
CacheInternalFindBlock(
    _In_ PCACHE_DRIVE CacheDrive,
    _In_ ULONG BlockNumber)
{
    PLIST_ENTRY Entry;

    TRACE("CacheInternalFindBlock(BlockNumber: %lu)\n", BlockNumber);

    /* Search in the block list the block matching the corresponding number */
    for (Entry = CacheDrive->CacheBlockHead.Flink;
         Entry != &CacheDrive->CacheBlockHead;
         Entry = Entry->Flink)
    {
        PCACHE_BLOCK CacheBlock = CONTAINING_RECORD(Entry, CACHE_BLOCK, ListEntry);

        if (CacheBlock->BlockNumber == BlockNumber)
        {
            /* We found the block, so return it. Increment its access count. */
            CacheBlock->AccessCount++;
            return CacheBlock;
        }
    }

    return NULL;
}

BOOLEAN
CacheInternalFreeBlock(
    _In_ PCACHE_DRIVE CacheDrive);

/**
 * @brief
 * Checks the cache size limits to see if we can add a new block;
 * if not, calls CacheInternalFreeBlock().
 **/
static VOID
CacheInternalCheckCacheSizeLimits(
    _In_ PCACHE_DRIVE CacheDrive)
{
    SIZE_T NewCacheSize;

    TRACE("CacheInternalCheckCacheSizeLimits()\n");

    /* Calculate the size of the cache if we added a block */
    NewCacheSize = (CacheBlockCount + 1) * (CacheDrive->BlockSize * CacheDrive->BytesPerSector);

    /* Check the new size against the cache size limit */
    if (NewCacheSize > CacheSizeLimit)
    {
        CacheInternalFreeBlock(CacheDrive);
        CacheInternalDumpBlockList(CacheDrive);
    }
}

/**
 * @brief   Adds a block to the cache's block list.
 **/
static PCACHE_BLOCK
CacheInternalAddBlockToCache(
    _In_ PCACHE_DRIVE CacheDrive,
    _In_ ULONG BlockNumber)
{
    PCACHE_BLOCK CacheBlock;
    ARC_STATUS Status;

    TRACE("CacheInternalAddBlockToCache(BlockNumber: %lu)\n", BlockNumber);

    /* Check the size of the cache so we don't exceed our limits */
    CacheInternalCheckCacheSizeLimits(CacheDrive);

    /* We will need to add the block to the drive's list of cached blocks.
     * So allocate the block memory. */
    CacheBlock = FrLdrTempAlloc(sizeof(*CacheBlock), TAG_CACHE_BLOCK);
    if (CacheBlock == NULL)
        return NULL;

    /* Now initialize the structure and allocate room for the block data */
    RtlZeroMemory(CacheBlock, sizeof(*CacheBlock));
    CacheBlock->BlockNumber = BlockNumber;
    CacheBlock->BlockData = FrLdrTempAlloc(CacheDrive->BlockSize * CacheDrive->BytesPerSector,
                                           TAG_CACHE_DATA);
    if (CacheBlock->BlockData == NULL)
    {
        FrLdrTempFree(CacheBlock, TAG_CACHE_BLOCK);
        return NULL;
    }

    /* Now try to read in the block */
#ifdef CACHE_FOR_FILESYSTEM
    {
    LARGE_INTEGER Position;
    ULONG BytesRead;

    Position.QuadPart = ((ULONGLONG)BlockNumber * CacheDrive->BlockSize) * CacheDrive->BytesPerSector;
    Status = ArcSeek(CacheDrive->DeviceId, &Position, SeekAbsolute);
    if (Status == ESUCCESS)
    {
        Status = ArcRead(CacheDrive->DeviceId,
                         CacheBlock->BlockData,
                         CacheDrive->BlockSize * CacheDrive->BytesPerSector,
                         &BytesRead);
        if (/*Status != ESUCCESS ||*/ BytesRead != CacheDrive->BlockSize * CacheDrive->BytesPerSector)
            Status = EIO;
    }
    }
#else
    Status = CacheDrive->Device->ReadBlocks(CacheDrive->Device->Context,
                                            (ULONGLONG)BlockNumber * CacheDrive->BlockSize,
                                            CacheDrive->BlockSize,
                                            CacheBlock->BlockData);
#endif
    if (Status != ESUCCESS)
    {
        FrLdrTempFree(CacheBlock->BlockData, TAG_CACHE_DATA);
        FrLdrTempFree(CacheBlock, TAG_CACHE_BLOCK);
        return NULL;
    }

    /* Add it to our list of blocks managed by the cache */
    InsertTailList(&CacheDrive->CacheBlockHead, &CacheBlock->ListEntry);

    /* Update the cache data */
    CacheBlockCount++;
    CacheSizeCurrent = CacheBlockCount * (CacheDrive->BlockSize * CacheDrive->BytesPerSector);

    CacheInternalDumpBlockList(CacheDrive);

    return CacheBlock;
}

/**
 * @brief
 * Returns a pointer to a CACHE_BLOCK structure, given a block number.
 * Adds the block to the cache manager block list in cache memory
 * if it isn't already there.
 **/
PCACHE_BLOCK
CacheInternalGetBlockPointer(
    _In_ PCACHE_DRIVE CacheDrive,
    _In_ ULONG BlockNumber)
{
    PCACHE_BLOCK CacheBlock = NULL;

    TRACE("CacheInternalGetBlockPointer(BlockNumber: %lu)\n", BlockNumber);

    CacheBlock = CacheInternalFindBlock(CacheDrive, BlockNumber);
    if (CacheBlock != NULL)
    {
        TRACE("Cache hit! BlockNumber: %lu CacheBlock->BlockNumber: %lu\n",
              BlockNumber, CacheBlock->BlockNumber);
        return CacheBlock;
    }

    TRACE("Cache miss! BlockNumber: %lu\n", BlockNumber);
    CacheBlock = CacheInternalAddBlockToCache(CacheDrive, BlockNumber);

    /* Optimize the block list so it has a LRU structure */
    CacheInternalOptimizeBlockList(CacheDrive, CacheBlock);

    return CacheBlock;
}

/**
 * @brief   Removes a block from the cache's block list & frees the memory.
 **/
BOOLEAN
CacheInternalFreeBlock(
    _In_ PCACHE_DRIVE CacheDrive)
{
    PLIST_ENTRY Entry;
    PCACHE_BLOCK CacheBlockToFree = NULL;

    TRACE("CacheInternalFreeBlock()\n");

    /* Get a pointer to the last item in the block list that isn't locked
     * (i.e. forced to be) in the cache and remove it from the list */
    for (Entry = CacheDrive->CacheBlockHead.Blink;
         Entry != &CacheDrive->CacheBlockHead;
         Entry = Entry->Blink)
    {
        // CacheBlock
        CacheBlockToFree = CONTAINING_RECORD(Entry, CACHE_BLOCK, ListEntry);
        if (!CacheBlockToFree->LockedInCache)
            break; /* Non-locked candidate block found */
    }

    /* No blocks left in cache that can be freed so just return */
    // if (IsListEmpty(&CacheDrive->CacheBlockHead))
    if (Entry == &CacheDrive->CacheBlockHead) // || (CacheBlockToFree == NULL)
        return FALSE;

    RemoveEntryList(&CacheBlockToFree->ListEntry);

    /* Free the block memory and the block structure */
    FrLdrTempFree(CacheBlockToFree->BlockData, TAG_CACHE_DATA);
    FrLdrTempFree(CacheBlockToFree, TAG_CACHE_BLOCK);

    /* Update the cache data */
    CacheBlockCount--;
    CacheSizeCurrent = CacheBlockCount * (CacheDrive->BlockSize * CacheDrive->BytesPerSector);

    return TRUE;
}
