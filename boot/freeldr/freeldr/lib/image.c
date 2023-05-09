/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     External FreeLdr boot application / driver PE image support.
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(PELOADER);

/* GLOBALS ********************************************************************/

LIST_ENTRY FrLdrModuleList;
PLDR_DATA_TABLE_ENTRY FreeldrDTE;

/* FUNCTIONS ******************************************************************/

static BOOLEAN
FrLdrInitImageSupport(VOID)
{
    BOOLEAN Success;

    if (FreeldrDTE)
    {
        /* Already initialized, bail out */
        return TRUE;
    }

    /* Initialize the loaded module list */
    InitializeListHead(&FrLdrModuleList);

    /*
     * Add freeldr.sys to the list of loaded executables, as it
     * contains exports that may be imported by the loaded image.
     * For example, ScsiPort* exports, imported by ntbootdd.sys.
     */
    Success = PeLdrAllocateDataTableEntry(&FrLdrModuleList,
                                          "freeldr.sys",
                                          "FREELDR.SYS",
                                          &__ImageBase,
                                          &FreeldrDTE);
    if (!Success)
    {
        /* Cleanup and bail out */
        ERR("PeLdrAllocateDataTableEntry('%s') failed\n", "FREELDR.SYS");
        return FALSE; // ENOMEM;
    }

    /* Now unlink the DTEs, they won't be valid later */
    // RemoveEntryList(&FreeldrDTE->InLoadOrderLinks);

    return Success;
}

#ifdef UEFIBOOT

// Add handling for EFI PE images.

#endif

/**
 * @brief
 * External FreeLdr PE image loader.
 **/
ARC_STATUS
FldrpLoadImage(
    _In_ PCSTR ImageFilePath,
    _In_opt_ PCSTR ImportName,
    _In_ TYPE_OF_MEMORY MemoryType,
    _Out_ PLDR_DATA_TABLE_ENTRY* ImageEntry,
    _Out_opt_ PVOID* ImageBasePA)
{
    ARC_STATUS Status;
    BOOLEAN Success;
    PVOID ImageBase = NULL;
    PLDR_DATA_TABLE_ENTRY ImageDTE;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_IMPORT_DESCRIPTOR ImportTable;
    ULONG ImportTableSize;

    /* Initialize image loading support */
    // if (!FreeldrDTE)
    if (!FrLdrInitImageSupport())
    {
        ERR("Cannot initialize Image Support\n");
        return ENOEXEC;
    }

    /* Load the image */
    Status = PeLdrLoadImage(ImageFilePath, MemoryType, &ImageBase);
    if (Status != ESUCCESS)
    {
        ERR("PeLdrLoadImage('%s') failed\n", ImageFilePath);
        return Status;
    }

    if (!ImportName)
    {
        /* Get the file name from the path */
        ImportName = strrchr(ImageFilePath, '\\');
        if (ImportName)
        {
            /* Name is past the path separator */
            ImportName++;
        }
        else
        {
            /* No directory, just use the given path */
            ImportName = ImageFilePath;
        }
    }

    /* Allocate a DTE for it */
    Success = PeLdrAllocateDataTableEntry(&FrLdrModuleList,
                                          ImportName,
                                          ImageFilePath,
                                          ImageBase,
                                          &ImageDTE);
    if (!Success)
    {
        /* Cleanup and bail out */
        ERR("PeLdrAllocateDataTableEntry('%s') failed\n", ImageFilePath);
        MmFreeMemory(ImageBase);
        return ENOMEM;
    }

    /* Reset ImageBase */
    ASSERT(VaToPa(ImageDTE->DllBase) == ImageBase);
    // ImageBase = VaToPa(ImageDTE->DllBase);

    /* Load any other referenced DLLs for the loaded image */
    Success = PeLdrScanImportDescriptorTable(&FrLdrModuleList, ""/*DirPath*/, ImageDTE);
    if (!Success)
    {
        /* Cleanup and bail out */
        ERR("PeLdrScanImportDescriptorTable('%s') failed\n", ImageFilePath);
        Status = EIO;
        goto Failure;
    }

    // /* Now unlink the DTEs, they won't be valid later */
    // RemoveEntryList(&FreeldrDTE->InLoadOrderLinks);
    // RemoveEntryList(&ImageDTE->InLoadOrderLinks);

    /* Change imports to PA */
    ImportTable =
        (PIMAGE_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(ImageBase,
                                                               TRUE,
                                                               IMAGE_DIRECTORY_ENTRY_IMPORT,
                                                               &ImportTableSize);
    for (; (ImportTable->Name != 0) && (ImportTable->FirstThunk != 0); ImportTable++)
    {
        PIMAGE_THUNK_DATA ThunkData = (PIMAGE_THUNK_DATA)VaToPa(RVA(ImageDTE->DllBase, ImportTable->FirstThunk));

        while (((PIMAGE_THUNK_DATA)ThunkData)->u1.AddressOfData != 0)
        {
            ThunkData->u1.Function = (ULONG_PTR)VaToPa((PVOID)ThunkData->u1.Function);
            ThunkData++;
        }
    }

    NtHeaders = RtlImageNtHeader(ImageBase);
    ASSERT(NtHeaders); // PeLdrLoadImage succeeded, so the image was valid and had a header...
    // ASSERT(NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR_MAGIC);

    /* Relocate image to PA */
    Success = (BOOLEAN)LdrRelocateImageWithBias(ImageBase,
                                                NtHeaders->OptionalHeader.ImageBase - (ULONG_PTR)ImageDTE->DllBase,
                                                "FreeLdr",
                                                TRUE,
                                                TRUE, /* In case of conflict still return success */
                                                FALSE);
    if (!Success)
    {
        Status = EIO;
        goto Failure;
    }

    *ImageEntry = ImageDTE;
    if (ImageBasePA)
        *ImageBasePA = ImageBase;

    return ESUCCESS;

Failure:
    /* We failed, cleanup */
    FldrpUnloadImage(ImageDTE);
    return Status;
}

/**
 * @brief
 * Unload a loaded external FreeLdr PE image.
 **/
BOOLEAN
FldrpUnloadImage(
    _Inout_ PLDR_DATA_TABLE_ENTRY ImageEntry)
{
    PVOID ImageBase = VaToPa(ImageEntry->DllBase);
    PeLdrFreeDataTableEntry(ImageEntry);
    MmFreeMemory(ImageBase);
    // ERR("FldrpUnloadImage failed, possible memory leak\n");
    return TRUE;
}

/**
 * @brief
 * Execute a loaded external FreeLdr boot application or driver PE image.
 **/
ARC_STATUS
FldrpStartImageEx(
    _In_ PLDR_DATA_TABLE_ENTRY ImageEntry,
    _In_opt_ PCSTR CommandLine,
    _In_opt_ PCHAR Envp[])
{
    ARC_STATUS Status;
    PIMAGE_NT_HEADERS NtHeaders;
    USHORT Subsystem;

    /* The supported entrypoint signatures */
    union
    {
        PVOID Ptr;
        NTSTATUS (NTAPI *DriverEntry)(PVOID /*PDRIVER_OBJECT*/, PVOID /*PUNICODE_STRING*/);
        VOID (NTAPI *AppEntry)(PBOOT_CONTEXT ParamBlock);
    } EntryPoint;

    EntryPoint.Ptr = VaToPa(ImageEntry->EntryPoint);

    NtHeaders = RtlImageNtHeader(VaToPa(ImageEntry->DllBase));
    ASSERT(NtHeaders); // If ImageEntry valid, the image was valid and had a header...
    Subsystem = NtHeaders->OptionalHeader.Subsystem;

    Status = ESUCCESS;

    /* Determine the entrypoint format from PE subsystem and call it accordingly */
    if (Subsystem == IMAGE_SUBSYSTEM_NATIVE)
    {
        /* FreeLdr-specific application or driver */

        if (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_DLL)
        {
            /* Driver */
            NTSTATUS NtStatus = EntryPoint.DriverEntry(NULL, NULL);
            if (!NT_SUCCESS(NtStatus))
            {
                /* Driver loading failed, unload it */
                FldrpUnloadImage(ImageEntry);
                Status = (ARC_STATUS)NtStatus;
            }
        }
        else
        {
            /* Boot application */
            // KDESCRIPTOR Gdt, Idt;
            PVOID BootData;
            PVOID NewStack /*, NewGdt, NewIdt*/;
            PBOOT_CONTEXT BootContext;
            ULONG_PTR BootSizeNeeded;

            /* Allocate space for the IDT, GDT, boot context and 24 pages of stack */
            // TODO: IDT, GDT
            BootSizeNeeded = (ULONG_PTR)PAGE_ALIGN(25 * PAGE_SIZE +
                                                   /*Idt.Limit + Gdt.Limit + 1 +*/
                                                   sizeof(BOOT_CONTEXT));
            BootData = MmAllocateMemoryWithType(BootSizeNeeded, LoaderLoadedProgram);
            if (!BootData)
            {
                ERR("Failed to allocate 0x%lx bytes\n", BootSizeNeeded);
                return ENOMEM;
            }
            RtlZeroMemory(BootData, BootSizeNeeded);

            /* Set the new stack, GDT and IDT */
            // For x86/x64... stack goes from top down to BootData.
            NewStack = (PVOID)((ULONG_PTR)BootData + (24 * PAGE_SIZE) - 8);
            BootContext = (PBOOT_CONTEXT)((ULONG_PTR)BootData + (24 * PAGE_SIZE));
            // NewGdt = (PVOID)((ULONG_PTR)BootData + (24 * PAGE_SIZE) + sizeof(BOOT_CONTEXT));
            // NewIdt = (PVOID)((ULONG_PTR)BootData + (24 * PAGE_SIZE) + sizeof(BOOT_CONTEXT) + Gdt.Limit + 1);

            /* Prepare the boot context */
            BootContext->Signature = BOOT_CONTEXT_SIGNATURE;
            BootContext->Size = sizeof(BOOT_CONTEXT);
            BootContext->MemoryTranslation = BOOT_MEMORY_PHYSICAL;
            BootContext->ExitStatus = ESUCCESS;
            RtlInitAnsiString(&BootContext->CommandLine, CommandLine);
            BootContext->Envp = Envp;
            BootContext->MachVtbl = &MachVtbl;
            BootContext->UiTable = &UiVtbl;

            // TODO: Switch to new stack.
            DBG_UNREFERENCED_PARAMETER(NewStack);

            /* Make it so! */
            EntryPoint.AppEntry(BootContext);

            // TODO: Switch back to current stack.

            Status = BootContext->ExitStatus;
            TRACE("Boot app returned 0x%08lx\n", Status);

            /* Cleanup and bail out */
            MmFreeMemory(BootData);
        }
    }
#if 0
    else if (Subsystem == IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
    {
        // Windows Boot Environment Application, see TheDarkFire
    }
#endif

    return Status;
}

ARC_STATUS
FldrpStartImage(
    _In_ PLDR_DATA_TABLE_ENTRY ImageEntry)
{
    return FldrpStartImageEx(ImageEntry, NULL, NULL);
}

ARC_STATUS
NativeExecuteImage(
    _In_ PCSTR ImageFilePath,
    _In_ PCSTR CommandLine,
    _In_ PCHAR Envp[])
{
    ARC_STATUS Status;
    PLDR_DATA_TABLE_ENTRY ImageDTE;

    // TODO: Handle CommandLine and Envp.

    /* Load the image */
    Status = FldrpLoadImage(ImageFilePath,
                            NULL,
                            LoaderLoadedProgram,
                            &ImageDTE,
                            NULL);
    if (Status != ESUCCESS)
        return Status;

    /* Execute it */
    TRACE("Executing '%s'\n", ImageFilePath);
    // Status = FldrpStartImage(ImageDTE);
    Status = FldrpStartImageEx(ImageDTE, CommandLine, Envp);
    TRACE("'%s' terminated\n", ImageFilePath);

    /* And unload it */
    FldrpUnloadImage(ImageDTE);

    return Status;
}

ARC_STATUS
LoadAndExecuteImage(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ PCHAR Envp[])
{
    ARC_STATUS Status;
    PCSTR ArgValue;
    PCSTR ImageFilePath;
    PSTR CommandLine;

    PCSTR BootPath;
    CHAR ArcPath[MAX_PATH];

    /* Retrieve the (mandatory) boot type */
    ArgValue = GetArgumentValue(Argc, Argv, "BootType");
    if (!ArgValue || !*ArgValue)
        return EINVAL;
    if (_stricmp(ArgValue, "Image") != 0)
        return EINVAL;

    /* Get the image file path */
    ImageFilePath = GetArgumentValue(Argc, Argv, "ImageFilePath");
    if (!ImageFilePath || !*ImageFilePath)
    {
        UiMessageBox("Image file path not specified!");
        return ENOEXEC;
    }

    /* Get the load options command line (optional) */
    CommandLine = GetArgumentValue(Argc, Argv, "Options");

    /* Fall back to using the system partition as default path */
    BootPath = GetArgumentValue(Argc, Argv, "SystemPartition");

    /* Concatenate paths */
    RtlStringCbCopyA(ArcPath, sizeof(ArcPath), BootPath);
    if (ArcPath[strlen(ArcPath)-1] != '\\')
        RtlStringCbCatA(ArcPath, sizeof(ArcPath), "\\");
    RtlStringCbCatA(ArcPath, sizeof(ArcPath), ImageFilePath);

#if 0
//#ifdef UEFIBOOT

    // TODO: Prefer doing it the UEFI way
    return ESUCCESS;

//#elif defined(ARC)

    // TODO: Split the command line into Argv

    /* Execute the image */
    return ArcExecute(ArcPath, 0 /*Argc*/, NULL /*Argv*/, Envp);

//#else
#endif

    /* BIOS-type loader: do manual PE loading */
    Status = NativeExecuteImage(ArcPath, CommandLine, Envp);
    if (Status != ESUCCESS)
        UiMessageBox("Could not load %s", ArcPath);
    return Status;

//#endif
}

/* EOF */
