/*
 *  FreeLoader
 *  Copyright (C) 2006-2008     Aleksey Bragin  <aleksey@reactos.org>
 *  Copyright (C) 2006-2009     Hervé Poussineau  <hpoussin@reactos.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(MEMORY);


/**
 * Adaptation of meminit.c
 **/

PVOID PageLookupTableAddress = NULL;
PFN_NUMBER TotalPagesInLookupTable = 0; // Used in lib/cache/cache.c
PFN_NUMBER FreePagesInLookupTable = 0;
PFN_NUMBER MmLowestPhysicalPage = 0xFFFFFFFF;
PFN_NUMBER MmHighestPhysicalPage = 0;

PFREELDR_MEMORY_DESCRIPTOR BiosMemoryMap;
ULONG BiosMemoryMapEntryCount;
SIZE_T FrLdrImageSize;

#if DBG
typedef struct
{
    TYPE_OF_MEMORY Type;
    PCSTR TypeString;
} FREELDR_MEMORY_TYPE, *PFREELDR_MEMORY_TYPE;

FREELDR_MEMORY_TYPE MemoryTypeArray[] =
{
    { LoaderMaximum, "Unknown memory" },
    { LoaderFree, "Free memory" },
    { LoaderBad, "Bad memory" },
    { LoaderLoadedProgram, "LoadedProgram" },
    { LoaderFirmwareTemporary, "FirmwareTemporary" },
    { LoaderFirmwarePermanent, "FirmwarePermanent" },
    { LoaderOsloaderHeap, "OsloaderHeap" },
    { LoaderOsloaderStack, "OsloaderStack" },
    { LoaderSystemCode, "SystemCode" },
    { LoaderHalCode, "HalCode" },
    { LoaderBootDriver, "BootDriver" },
    { LoaderRegistryData, "RegistryData" },
    { LoaderMemoryData, "MemoryData" },
    { LoaderNlsData, "NlsData" },
    { LoaderSpecialMemory, "SpecialMemory" },
    { LoaderReserve, "Reserve" },
};
ULONG MemoryTypeCount = sizeof(MemoryTypeArray) / sizeof(MemoryTypeArray[0]);

PCSTR
MmGetSystemMemoryMapTypeString(
    TYPE_OF_MEMORY Type)
{
    ULONG Index;

    for (Index = 1; Index < MemoryTypeCount; Index++)
    {
        if (MemoryTypeArray[Index].Type == Type)
        {
            return MemoryTypeArray[Index].TypeString;
        }
    }

    return MemoryTypeArray[0].TypeString;
}

VOID
DbgDumpMemoryMap(
    PFREELDR_MEMORY_DESCRIPTOR List)
{
    ULONG i;

    DbgPrint("Dumping Memory map:\n");
    for (i = 0; List[i].PageCount != 0; i++)
    {
        DbgPrint("%02d %08x - %08x: %s\n",
                 i,
                 List[i].BasePage * PAGE_SIZE,
                 (List[i].BasePage + List[i].PageCount) * PAGE_SIZE,
                 MmGetSystemMemoryMapTypeString(List[i].MemoryType));
    }
    DbgPrint("\n");
}
#endif

ULONG
AddMemoryDescriptor(
    IN OUT PFREELDR_MEMORY_DESCRIPTOR List,
    IN ULONG MaxCount,
    IN PFN_NUMBER BasePage,
    IN PFN_NUMBER PageCount,
    IN TYPE_OF_MEMORY MemoryType)
{
    ULONG Index, DescriptCount;
    PFN_NUMBER EndPage;
    TRACE("AddMemoryDescriptor(0x%Ix, 0x%Ix, %u)\n",
          BasePage, PageCount, MemoryType);

    EndPage = BasePage + PageCount;

    /* Skip over all descriptor below the new range */
    Index = 0;
    while ((List[Index].PageCount != 0) &&
           ((List[Index].BasePage + List[Index].PageCount) <= BasePage))
    {
        Index++;
    }

    /* Count the descriptors */
    DescriptCount = Index;
    while (List[DescriptCount].PageCount != 0)
    {
        DescriptCount++;
    }

    /* Check if the existing range conflicts with the new range */
    while ((List[Index].PageCount != 0) &&
           (List[Index].BasePage < EndPage))
    {
        TRACE("AddMemoryDescriptor conflict @%lu: new=[%lx:%lx], existing=[%lx,%lx]\n",
              Index, BasePage, PageCount, List[Index].BasePage, List[Index].PageCount);

        /*
         * We have 4 overlapping cases:
         *
         * Case              (a)       (b)       (c)       (d)
         * Existing range  |---|     |-----|    |---|      |---|
         * New range         |---|    |---|    |-----|   |---|
         *
         */

        /* Check if the existing range starts before the new range (a)/(b) */
        if (List[Index].BasePage < BasePage)
        {
            /* Check if the existing range extends beyond the new range (b) */
            if (List[Index].BasePage + List[Index].PageCount > EndPage)
            {
                /* Split the descriptor */
                RtlMoveMemory(&List[Index + 1],
                              &List[Index],
                              (DescriptCount - Index) * sizeof(List[0]));
                List[Index + 1].BasePage = EndPage;
                List[Index + 1].PageCount = List[Index].BasePage +
                                            List[Index].PageCount -
                                            List[Index + 1].BasePage;
                List[Index].PageCount = BasePage - List[Index].BasePage;
                Index++;
                DescriptCount++;
                break;
            }
            else
            {
                /* Crop the existing range and continue with the next range */
                List[Index].PageCount = BasePage - List[Index].BasePage;
                Index++;
            }
        }
        /* Check if the existing range is fully covered by the new range (c) */
        else if ((List[Index].BasePage + List[Index].PageCount) <=
                 EndPage)
        {
            /* Delete this descriptor */
            RtlMoveMemory(&List[Index],
                          &List[Index + 1],
                          (DescriptCount - Index) * sizeof(List[0]));
            DescriptCount--;
        }
        /* Otherwise the existing range ends after the new range (d) */
        else
        {
            /* Crop the existing range at the start and bail out */
            List[Index].PageCount -= EndPage - List[Index].BasePage;
            List[Index].BasePage = EndPage;
            break;
        }
    }

    /* Make sure we can still add a new descriptor */
    if (DescriptCount >= MaxCount)
    {
        FrLdrBugCheckWithMessage(
            MEMORY_INIT_FAILURE,
            __FILE__,
            __LINE__,
            "Ran out of static memory descriptors!");
    }

    /* Insert the new descriptor */
    if (Index < DescriptCount)
    {
        RtlMoveMemory(&List[Index + 1],
                      &List[Index],
                      (DescriptCount - Index) * sizeof(List[0]));
    }

    List[Index].BasePage = BasePage;
    List[Index].PageCount = PageCount;
    List[Index].MemoryType = MemoryType;
    DescriptCount++;

#if 0 // only enable on demand!
    DbgDumpMemoryMap(List);
#endif
    return DescriptCount;
}

const FREELDR_MEMORY_DESCRIPTOR*
ArcGetMemoryDescriptor(const FREELDR_MEMORY_DESCRIPTOR* Current)
{
    if (Current == NULL)
    {
        return BiosMemoryMap;
    }
    else
    {
        Current++;
        if (Current->PageCount == 0) return NULL;
        return Current;
    }
}

static
VOID
MmCheckFreeldrImageFile(VOID)
{
#ifndef UEFIBOOT
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_FILE_HEADER FileHeader;
    PIMAGE_OPTIONAL_HEADER OptionalHeader;

    /* Get the NT headers */
    NtHeaders = RtlImageNtHeader(&__ImageBase);
    if (!NtHeaders)
    {
        ERR("Could not get NtHeaders!\n");
        FrLdrBugCheckWithMessage(
            FREELDR_IMAGE_CORRUPTION,
            __FILE__,
            __LINE__,
            "Could not get NtHeaders!\n");
    }

    /* Check the file header */
    FileHeader = &NtHeaders->FileHeader;
    if ((FileHeader->Machine != IMAGE_FILE_MACHINE_NATIVE) ||
#ifndef MY_WIN32
        (FileHeader->NumberOfSections != FREELDR_SECTION_COUNT) ||
#endif /* MY_WIN32 */
        (FileHeader->PointerToSymbolTable != 0) ||  // Symbols stripped
        (FileHeader->NumberOfSymbols != 0) ||       //    ""      ""
        (FileHeader->SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER)))
    {
        ERR("FreeLdr FileHeader is invalid.\n");
        FrLdrBugCheckWithMessage(
            FREELDR_IMAGE_CORRUPTION,
            __FILE__,
            __LINE__,
            "FreeLdr FileHeader is invalid.\n"
            "Machine == 0x%lx, expected 0x%lx\n"
            "NumberOfSections == 0x%lx, expected 0x%lx\n"
            "PointerToSymbolTable == 0x%lx, expected 0\n"
            "NumberOfSymbols == 0x%lx, expected 0\n"
            "SizeOfOptionalHeader == 0x%lx, expected 0x%lx\n",
            FileHeader->Machine, IMAGE_FILE_MACHINE_NATIVE,
            FileHeader->NumberOfSections, FREELDR_SECTION_COUNT,
            FileHeader->PointerToSymbolTable,
            FileHeader->NumberOfSymbols,
            FileHeader->SizeOfOptionalHeader, sizeof(IMAGE_OPTIONAL_HEADER));
    }

    /* Check the optional header */
    OptionalHeader = &NtHeaders->OptionalHeader;
#ifndef MY_WIN32
    if ((OptionalHeader->Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) ||
        (OptionalHeader->Subsystem != IMAGE_SUBSYSTEM_NATIVE) ||
        (OptionalHeader->ImageBase != FREELDR_PE_BASE) ||
        (OptionalHeader->SizeOfImage > MAX_FREELDR_PE_SIZE) ||
        (OptionalHeader->SectionAlignment != OptionalHeader->FileAlignment))
    {
        ERR("FreeLdr OptionalHeader is invalid.\n");
        FrLdrBugCheckWithMessage(
            FREELDR_IMAGE_CORRUPTION,
            __FILE__,
            __LINE__,
            "FreeLdr OptionalHeader is invalid.\n"
            "Magic == 0x%lx, expected 0x%lx\n"
            "Subsystem == 0x%lx, expected 1 (native)\n"
            "ImageBase == 0x%lx, expected 0x%lx\n"
            "SizeOfImage == 0x%lx, maximum 0x%lx\n"
            "SectionAlignment 0x%lx doesn't match FileAlignment 0x%lx\n",
            OptionalHeader->Magic, IMAGE_NT_OPTIONAL_HDR_MAGIC,
            OptionalHeader->Subsystem,
            OptionalHeader->ImageBase, FREELDR_PE_BASE,
            OptionalHeader->SizeOfImage, MAX_FREELDR_PE_SIZE,
            OptionalHeader->SectionAlignment, OptionalHeader->FileAlignment);
    }
#endif /* MY_WIN32 */

    /* Calculate the full image size */
    FrLdrImageSize = (ULONG_PTR)&__ImageBase + OptionalHeader->SizeOfImage - FREELDR_BASE;
#endif
}

BOOLEAN MmInitializeMemoryManager(VOID)
{
#if DBG
    const FREELDR_MEMORY_DESCRIPTOR* MemoryDescriptor = NULL;
#endif

    TRACE("Initializing Memory Manager.\n");

    /* Check the freeldr binary */
    MmCheckFreeldrImageFile();

    BiosMemoryMap = MachVtbl.GetMemoryMap(&BiosMemoryMapEntryCount);

#if DBG
    // Dump the system memory map
    TRACE("System Memory Map (Base Address, Length, Type):\n");
    while ((MemoryDescriptor = ArcGetMemoryDescriptor(MemoryDescriptor)) != NULL)
    {
        TRACE("%x\t %x\t %s\n",
            MemoryDescriptor->BasePage * MM_PAGE_SIZE,
            MemoryDescriptor->PageCount * MM_PAGE_SIZE,
            MmGetSystemMemoryMapTypeString(MemoryDescriptor->MemoryType));
    }
#endif

#if 0
    // Find address for the page lookup table
    TotalPagesInLookupTable = MmGetAddressablePageCountIncludingHoles();
    PageLookupTableAddress = MmFindLocationForPageLookupTable(TotalPagesInLookupTable);
    LastFreePageHint = MmHighestPhysicalPage;

    if (PageLookupTableAddress == 0)
    {
        // If we get here then we probably couldn't
        // find a contiguous chunk of memory big
        // enough to hold the page lookup table
        printf("Error initializing memory manager!\n");
        return FALSE;
    }

    // Initialize the page lookup table
    MmInitPageLookupTable(PageLookupTableAddress, TotalPagesInLookupTable);

    MmUpdateLastFreePageHint(PageLookupTableAddress, TotalPagesInLookupTable);

    FreePagesInLookupTable = MmCountFreePagesInLookupTable(PageLookupTableAddress,
                                                        TotalPagesInLookupTable);
#endif

    MmInitializeHeap(PageLookupTableAddress);

    TRACE("Memory Manager initialized. 0x%x pages available.\n", FreePagesInLookupTable);

    return TRUE;
}



/**
 * Adaptation of mm.c
 **/


#if DBG
VOID    DumpMemoryAllocMap(VOID);
#endif // DBG

PFN_NUMBER LoaderPagesSpanned = 0;

PVOID MmAllocateMemoryWithType(SIZE_T MemorySize, TYPE_OF_MEMORY MemoryType)
{
    PFN_NUMBER PagesNeeded;
    // PFN_NUMBER FirstFreePageFromEnd;
    PVOID MemPointer;

    if (MemorySize == 0)
    {
        WARN("MmAllocateMemory() called for 0 bytes. Returning NULL.\n");
        UiMessageBoxCritical("Memory allocation failed: MmAllocateMemory() called for 0 bytes.");
        return NULL;
    }

    MemorySize = ROUND_UP(MemorySize, 4);

    // Find out how many blocks it will take to
    // satisfy this allocation
    PagesNeeded = ROUND_UP(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE;

    MemPointer = VirtualAlloc(NULL, MemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE /*PAGE_EXECUTE_READWRITE*/);
    if (!MemPointer)
    {
        ERR("Memory allocation failed in MmAllocateMemory(). Not enough free memory to allocate %d bytes.\n", MemorySize);
        UiMessageBoxCritical("Memory allocation failed: out of memory.");
        return NULL;
    }

    // FreePagesInLookupTable -= PagesNeeded;

    TRACE("Allocated %d bytes (%d pages) of memory (type %ld)\n" /* "starting at page 0x%lx.\n" */,
          MemorySize, PagesNeeded, MemoryType /*, FirstFreePageFromEnd*/);
    TRACE("Memory allocation pointer: 0x%x\n", MemPointer);

    // Update LoaderPagesSpanned count
    if ((((ULONG_PTR)MemPointer + MemorySize + PAGE_SIZE - 1) >> PAGE_SHIFT) > LoaderPagesSpanned)
        LoaderPagesSpanned = (((ULONG_PTR)MemPointer + MemorySize + PAGE_SIZE - 1) >> PAGE_SHIFT);

    // Now return the pointer
    return MemPointer;
}

PVOID MmAllocateMemoryAtAddress(SIZE_T MemorySize, PVOID DesiredAddress, TYPE_OF_MEMORY MemoryType)
{
    PFN_NUMBER PagesNeeded;
    PFN_NUMBER StartPageNumber;
    PVOID MemPointer;

    if (MemorySize == 0)
    {
        WARN("MmAllocateMemoryAtAddress() called for 0 bytes. Returning NULL.\n");
        UiMessageBoxCritical("Memory allocation failed: MmAllocateMemoryAtAddress() called for 0 bytes.");
        return NULL;
    }

    // Find out how many blocks it will take to
    // satisfy this allocation
    PagesNeeded = ROUND_UP(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE;

    // Get the starting page number
    // StartPageNumber = MmGetPageNumberFromAddress(DesiredAddress);
    StartPageNumber = (ULONG_PTR)DesiredAddress / MM_PAGE_SIZE;

    MemPointer = VirtualAlloc(DesiredAddress, MemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE /*PAGE_EXECUTE_READWRITE*/);
    if (!MemPointer)
    {
        ERR("Memory allocation failed in MmAllocateMemory(). Not enough free memory to allocate %d bytes.\n", MemorySize);
        UiMessageBoxCritical("Memory allocation failed: out of memory.");
        return NULL;
    }

    // FreePagesInLookupTable -= PagesNeeded;

    TRACE("Allocated %d bytes (%d pages) of memory starting at page %d.\n", MemorySize, PagesNeeded, StartPageNumber);
    TRACE("Memory allocation pointer: 0x%x\n", MemPointer);

    // Update LoaderPagesSpanned count
    if ((((ULONG_PTR)MemPointer + MemorySize + PAGE_SIZE - 1) >> PAGE_SHIFT) > LoaderPagesSpanned)
        LoaderPagesSpanned = (((ULONG_PTR)MemPointer + MemorySize + PAGE_SIZE - 1) >> PAGE_SHIFT);

    // Now return the pointer
    return MemPointer;
}

VOID MmSetMemoryType(PVOID MemoryAddress, SIZE_T MemorySize, TYPE_OF_MEMORY NewType)
{
#if 0
    PFN_NUMBER PagesNeeded;
    PFN_NUMBER StartPageNumber;

    // Find out how many blocks it will take to
    // satisfy this allocation
    PagesNeeded = ROUND_UP(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE;

    // Get the starting page number
    StartPageNumber = MmGetPageNumberFromAddress(MemoryAddress);

    // Set new type for these pages
    MmAllocatePagesInLookupTable(PageLookupTableAddress, StartPageNumber, PagesNeeded, NewType);
#endif
}

PVOID MmAllocateHighestMemoryBelowAddress(SIZE_T MemorySize, PVOID DesiredAddress, TYPE_OF_MEMORY MemoryType)
{
#if 0
    PFN_NUMBER PagesNeeded;
    PFN_NUMBER FirstFreePageFromEnd;
    PFN_NUMBER DesiredAddressPageNumber;
    PVOID MemPointer;

    if (MemorySize == 0)
    {
        WARN("MmAllocateHighestMemoryBelowAddress() called for 0 bytes. Returning NULL.\n");
        UiMessageBoxCritical("Memory allocation failed: MmAllocateHighestMemoryBelowAddress() called for 0 bytes.");
        return NULL;
    }

    // Find out how many blocks it will take to
    // satisfy this allocation
    PagesNeeded = ROUND_UP(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE;

    // Get the page number for their desired address
    DesiredAddressPageNumber = (ULONG_PTR)DesiredAddress / MM_PAGE_SIZE;

    // If we don't have enough available mem
    // then return NULL
    if (FreePagesInLookupTable < PagesNeeded)
    {
        ERR("Memory allocation failed in MmAllocateHighestMemoryBelowAddress(). Not enough free memory to allocate %d bytes.\n", MemorySize);
        UiMessageBoxCritical("Memory allocation failed: out of memory.");
        return NULL;
    }

    FirstFreePageFromEnd = MmFindAvailablePagesBeforePage(PageLookupTableAddress, TotalPagesInLookupTable, PagesNeeded, DesiredAddressPageNumber);

    if (FirstFreePageFromEnd == 0)
    {
        ERR("Memory allocation failed in MmAllocateHighestMemoryBelowAddress(). Not enough free memory to allocate %d bytes.\n", MemorySize);
        UiMessageBoxCritical("Memory allocation failed: out of memory.");
        return NULL;
    }

    MmAllocatePagesInLookupTable(PageLookupTableAddress, FirstFreePageFromEnd, PagesNeeded, MemoryType);

    FreePagesInLookupTable -= PagesNeeded;
    MemPointer = (PVOID)((ULONG_PTR)FirstFreePageFromEnd * MM_PAGE_SIZE);

    TRACE("Allocated %d bytes (%d pages) of memory starting at page %d.\n", MemorySize, PagesNeeded, FirstFreePageFromEnd);
    TRACE("Memory allocation pointer: 0x%x\n", MemPointer);

    // Update LoaderPagesSpanned count
    if ((((ULONG_PTR)MemPointer + MemorySize) >> PAGE_SHIFT) > LoaderPagesSpanned)
        LoaderPagesSpanned = (((ULONG_PTR)MemPointer + MemorySize) >> PAGE_SHIFT);

    // Now return the pointer
    return MemPointer;
#else
    ERR("MmAllocateHighestMemoryBelowAddress(MemorySize 0x%x, DesiredAddress 0x%p, MemoryType %lu) is UNIMPLEMENTED\n",
        MemorySize, DesiredAddress, MemoryType);
    return NULL;
#endif
}

VOID MmFreeMemory(PVOID MemoryPointer)
{
    BOOL Success = VirtualFree(MemoryPointer, 0, MEM_RELEASE);
    ASSERT(Success);
}

#if DBG
VOID DumpMemoryAllocMap(VOID)
{
    ERR("WIN32: MM: Dumping memory map is NOT SUPPORTED\n");
}
#endif // DBG

PPAGE_LOOKUP_TABLE_ITEM MmGetMemoryMap(PFN_NUMBER *NoEntries)
{
    PPAGE_LOOKUP_TABLE_ITEM RealPageLookupTable = (PPAGE_LOOKUP_TABLE_ITEM)PageLookupTableAddress;

    *NoEntries = TotalPagesInLookupTable;

    return RealPageLookupTable;
}

