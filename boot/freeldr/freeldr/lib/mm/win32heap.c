/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Heap management wrappers
 * COPYRIGHT:   Copyright 2011 Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>

#if DBG
#include <winnls.h> // For MultiByteToWideChar()
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(HEAP);

/*
 * Structure for kernel32!HeapSummary() function.
 * Valid since at least Windows 2003 but only documented started Windows 10.
 * https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsummary
 * https://learn.microsoft.com/en-us/windows/win32/api/heapapi/ns-heapapi-heap_summary
 */
typedef struct _HEAP_SUMMARY
{
    DWORD cb;
    SIZE_T cbAllocated;
    SIZE_T cbCommitted;
    SIZE_T cbReserved;
    SIZE_T cbMaxReserve;
} HEAP_SUMMARY, *PHEAP_SUMMARY;

BOOL
WINAPI
HeapSummary(
    _In_ HANDLE hHeap,
    _In_ DWORD dwFlags,
    _Out_ PHEAP_SUMMARY lpSummary);


#define FREELDR_HEAP_VERIFIER

PVOID FrLdrDefaultHeap;
PVOID FrLdrTempHeap;

typedef struct _HEAP
{
    HANDLE hHeap;
    ULONG HeapTag;
} HEAP, *PHEAP;

PVOID
FrLdrHeapCreate(
    SIZE_T MaximumSize,
    TYPE_OF_MEMORY MemoryType)
{
    PHEAP Heap;
#if DBG
    WCHAR TagSubName[30];
#endif

    TRACE("HeapCreate(MemoryType=%ld)\n", MemoryType);

    /* Allocate heap meta-structure */
    Heap = HeapAlloc(GetProcessHeap(), 0, sizeof(*Heap));
    if (!Heap)
    {
        ERR("HEAP: Failed to allocate heap meta-structure\n");
        return NULL;
    }

    /* Allocate some memory for the heap */
    MaximumSize = ALIGN_UP_BY(MaximumSize, MM_PAGE_SIZE);
    // Heap->hHeap = MmAllocateMemoryWithType(MaximumSize, MemoryType);
    Heap->hHeap = HeapCreate(0, MM_PAGE_SIZE, MaximumSize);
    if (!Heap->hHeap)
    {
        ERR("HEAP: Failed to allocate heap of size 0x%lx, Type %lu\n",
            MaximumSize, MemoryType);
        HeapFree(GetProcessHeap(), 0, Heap);
        return NULL;
    }

#if DBG
    MultiByteToWideChar(CP_ACP, 0,
                        MmGetSystemMemoryMapTypeString(MemoryType), -1,
                        TagSubName, _countof(TagSubName));
    // HeapCreateTagsW() but the Win32 kernel32 export is removed in Vista+.
    Heap->HeapTag = RtlCreateTagHeap(Heap->hHeap, 0, L"FreeLdr!", TagSubName);
#endif

    return (PVOID)Heap;
}

VOID
FrLdrHeapDestroy(
    _In_ PVOID HeapHandle)
{
    PHEAP Heap = HeapHandle;

    HeapDestroy(Heap->hHeap);
    HeapFree(GetProcessHeap(), 0, Heap);
}

#ifdef FREELDR_HEAP_VERIFIER
VOID
FrLdrHeapVerify(
    _In_ PVOID HeapHandle)
{
    PHEAP Heap = HeapHandle;
    BOOL Success;

    Success = HeapValidate(Heap->hHeap, 0, NULL);
    ASSERT(Success);
}
#endif /* FREELDR_HEAP_VERIFIER */

VOID
FrLdrHeapRelease(
    _In_ PVOID HeapHandle)
{
    PHEAP Heap = HeapHandle;

    TRACE("HeapRelease(%p)\n", HeapHandle);

#ifdef FREELDR_HEAP_VERIFIER
    /* Verify the heap */
    FrLdrHeapVerify(HeapHandle);
#endif

    /* Don't just release, but destroy as well */
    HeapDestroy(Heap->hHeap);
    HeapFree(GetProcessHeap(), 0, Heap);

    TRACE("HeapRelease() done\n");
}

VOID
FrLdrHeapCleanupAll(VOID)
{
#if DBG
    PHEAP Heap;
    HEAP_SUMMARY Summary = {sizeof(HEAP_SUMMARY), 0};

    Heap = FrLdrDefaultHeap;
    if (HeapSummary(Heap->hHeap, 0, &Summary))
    {
        TRACE("Heap statistics for default heap:\n"
              "Allocated=0x%lx, Committed=0x%lx, Reserved=0x%lx, MaxReserve=0x%lx\n",
              Summary.cbAllocated, Summary.cbCommitted, Summary.cbReserved, Summary.cbMaxReserve);
    }
#endif

    /* Release free pages from the default heap */
    FrLdrHeapRelease(FrLdrDefaultHeap);

#if DBG
    Heap = FrLdrTempHeap;
    if (HeapSummary(Heap->hHeap, 0, &Summary))
    {
        TRACE("Heap statistics for temp heap:\n"
              "Allocated=0x%lx, Committed=0x%lx, Reserved=0x%lx, MaxReserve=0x%lx\n",
              Summary.cbAllocated, Summary.cbCommitted, Summary.cbReserved, Summary.cbMaxReserve);
    }
#endif

    /* Destroy the temp heap */
    FrLdrHeapDestroy(FrLdrTempHeap);
}

PVOID
FrLdrHeapAllocateEx(
    _In_ PVOID HeapHandle,
    _In_ SIZE_T ByteSize,
    _In_ ULONG Tag)
{
    PHEAP Heap = HeapHandle;
    PVOID Buffer;

#ifdef FREELDR_HEAP_VERIFIER
    /* Verify the heap */
    FrLdrHeapVerify(HeapHandle);
#endif

#if DBG
    Tag |= Heap->HeapTag;
#endif
    Buffer = HeapAlloc(Heap->hHeap, 0 | Tag, ByteSize);

    // /* HACK: zero out the allocation */
    // RtlZeroMemory(Buffer, ByteSize);

    TRACE("HeapAllocate(%p, %ld, %.4s) -> return %p\n",
          HeapHandle, ByteSize, &Tag, Buffer);

    /* Return pointer to the data */
    return Buffer;
}

VOID
FrLdrHeapFreeEx(
    _In_ PVOID HeapHandle,
    _In_ PVOID Pointer,
    _In_ ULONG Tag)
{
    PHEAP Heap = HeapHandle;
    BOOL Success;

    TRACE("HeapFree(%p, %p)\n", HeapHandle, Pointer);

#ifdef FREELDR_HEAP_VERIFIER
    /* Verify the heap */
    FrLdrHeapVerify(HeapHandle);
#endif

#if 0
    /* Check if the tag matches */
    if ((Tag && (Block->Tag != Tag)) || (Block->Tag == 0))
    {
        ERR("HEAP: Bad tag! Pointer=%p: block tag '%.4s', requested '%.4s', size=0x%lx\n",
            Pointer, &Block->Tag, &Tag, Block->Size);
        ASSERT(FALSE);
    }
#endif

#if DBG
    Tag |= Heap->HeapTag;
#endif
    Success = HeapFree(Heap->hHeap, 0 | Tag, Pointer);
    if (!Success)
    {
        ERR("HEAP: Bad buffer! Pointer=%p\n", Pointer);
        ASSERT(Success);
    }
}


/* Wrapper functions *********************************************************/

VOID
MmInitializeHeap(
    _In_ PVOID PageLookupTable)
{
    TRACE("MmInitializeHeap()\n");
    UNREFERENCED_PARAMETER(PageLookupTable);

    /* Create the default heap */
    FrLdrDefaultHeap = FrLdrHeapCreate(DEFAULT_HEAP_SIZE, LoaderOsloaderHeap);
    ASSERT(FrLdrDefaultHeap);

    /* Create a temporary heap */
    FrLdrTempHeap = FrLdrHeapCreate(TEMP_HEAP_SIZE, LoaderFirmwareTemporary);
    ASSERT(FrLdrTempHeap);

    TRACE("MmInitializeHeap() done, default heap %p, temp heap %p\n",
          FrLdrDefaultHeap, FrLdrTempHeap);
}

PVOID
NTAPI
ExAllocatePoolWithTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    return FrLdrHeapAllocateEx(FrLdrDefaultHeap, NumberOfBytes, Tag);
}

PVOID
NTAPI
ExAllocatePool(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes)
{
    return FrLdrHeapAllocateEx(FrLdrDefaultHeap, NumberOfBytes, 0);
}

VOID
NTAPI
ExFreePool(
    _In_ PVOID P)
{
    FrLdrHeapFreeEx(FrLdrDefaultHeap, P, 0);
}

VOID
NTAPI
ExFreePoolWithTag(
    _In_ PVOID P,
    _In_ ULONG Tag)
{
    FrLdrHeapFreeEx(FrLdrDefaultHeap, P, Tag);
}

PVOID
NTAPI
RtlAllocateHeap(
    _In_ PVOID HeapHandle,
    _In_ ULONG Flags,
    _In_ SIZE_T Size)
{
    PVOID ptr;

    ptr = FrLdrHeapAllocateEx(FrLdrDefaultHeap, Size, ' ltR');
    if (ptr && (Flags & HEAP_ZERO_MEMORY))
    {
        RtlZeroMemory(ptr, Size);
    }

    return ptr;
}

BOOLEAN
NTAPI
RtlFreeHeap(
    _In_ PVOID HeapHandle,
    _In_ ULONG Flags,
    _In_ PVOID HeapBase)
{
    FrLdrHeapFreeEx(FrLdrDefaultHeap, HeapBase, ' ltR');
    return TRUE;
}

/* EOF */
