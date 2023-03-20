/*
 * PROJECT:     ReactOS KDBG Kernel Debugger
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Manage loading/unloading and getting information
 *              about debug symbols.
 * COPYRIGHT:   Copyright 2002 David Welch <welch@cwcom.net>
 *              Copyright 2004 Gregor Anich <blight@blight.eu.org>
 *              Copyright 2005 Gé van Geldorp <gvg@reactos.com>
 *              Copyright 2007-2009 Aleksey Bragin <aleksey@reactos.org>
 *              Copyright 2009 Colin Finck <colin@reactos.org>
 *              Copyright 2021 Jérôme Gardou <jerome.gardou@reactos.org>
 *              Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include "kdb.h"

#define NDEBUG
#include "debug.h"

/* GLOBALS ******************************************************************/

/* Whether or not to load symbols */
static BOOLEAN LoadSymbols = FALSE;

/**
 * @brief
 * List of boot modules entries from KeLoaderBlock->LoadOrderListHead, whose
 * symbols are to be loaded once the Mm subsystem and the PsLoadedModuleList
 * are initialized, and those entries copied into PsLoadedModuleList.
 **/
static LIST_ENTRY BootSymbolsToLoad;

/* List of deferred symbols to load from image file,
 * once the IO subsystem is initialized. */
static LIST_ENTRY SymbolsToLoad;
static KSPIN_LOCK SymbolsToLoadLock;
static KEVENT SymbolsToLoadEvent;

/* List of modules whose symbols have been loaded */
static LIST_ENTRY LoadedSymbols;
static KSPIN_LOCK LoadedSymbolsLock;

/* Initialization status of the Mm and IO subsystems */
extern ULONG KdbInitPhase;
static BOOLEAN MmInitialized = FALSE;
static BOOLEAN IoInitialized = FALSE;


/* FUNCTIONS ****************************************************************/

static inline
BOOLEAN
IsMmInitialized(VOID)
{
    /* Check whether the Mm subsystem has been initialized, by looking at whether
     * PsLoadedModuleList is initialized (done by MiInitializeLoadedModuleList). */
    if (!MmInitialized)
        MmInitialized = (PsLoadedModuleList.Flink != NULL);
    return MmInitialized;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
IsIoInitialized(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING NtSystemRoot;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE DirHandle;

    if (IoInitialized)
        return IoInitialized;

    if (!MmInitialized)
        return FALSE;

    _IRQL_limited_to_(PASSIVE_LEVEL);

    /* Check whether SystemRoot can be opened */
    RtlInitUnicodeString(&NtSystemRoot, SharedUserData->NtSystemRoot);
    InitializeObjectAttributes(&ObjectAttributes,
                               &NtSystemRoot,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenFile(&DirHandle,
                        FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE);
    ZwClose(DirHandle);

    IoInitialized = NT_SUCCESS(Status);
    return IoInitialized;
}

static inline
BOOLEAN
CheckLoadSymbols(VOID)
{
    if (LoadSymbols && MmInitialized)
    {
        /* Do not load symbols if we have less than 96MB of RAM */
        if (MmNumberOfPhysicalPages < (96 * 1024 * 1024 / PAGE_SIZE))
            LoadSymbols = FALSE;
    }

    return LoadSymbols;
}


/**
 * @brief
 * Find a module, either given by its load address, or by its load order index,
 * in a user-provided module list.
 *
 * @param[in]   ModuleListHead
 * A user-provided list of modules, linking @p LDR_DATA_TABLE_ENTRY structures
 * via their @p InLoadOrderLinks field. It is typically @p PsLoadedModuleList
 * or @p KeLoaderBlock->LoadOrderListHead.
 *
 * @param[in,out]   Count
 * Counter at which to restart the enumeration.
 *
 * @param[in]   Address
 * If @p Address is not NULL the module containing @p Address is searched.
 *
 * @param[in]   Index
 * If @p Index is >= 0 the Index'th module will be returned.
 *
 * @param[out]  pLdrEntry
 * Pointer to a PLDR_DATA_TABLE_ENTRY which is filled.
 *
 * @return
 * TRUE if the module was found, @p pLdrEntry is filled.
 * FALSE if no module was found.
 **/
static
BOOLEAN
KdbpSymSearchModuleList(
    _In_ PLIST_ENTRY ModuleListHead,
    _Inout_ PULONG Count,
    _In_ PVOID Address,
    _In_ INT Index,
    _Out_ PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    PLIST_ENTRY ListEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    for (ListEntry = ModuleListHead->Flink;
         ListEntry != ModuleListHead;
         ListEntry = ListEntry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(ListEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if ((Address && Address >= (PVOID)LdrEntry->DllBase &&
             Address < (PVOID)((ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage)) ||
            (Index >= 0 && (*Count)++ == Index))
        {
            /* Found the module, return its entry */
            *pLdrEntry = LdrEntry;
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief
 * Find a module, either given by its load address, or by its load order index.
 *
 * @param[in]   Address
 * If @p Address is not NULL the module containing @p Address is searched.
 *
 * @param[in]   Index
 * If @p Index is >= 0 the Index'th module will be returned.
 *
 * @param[out]  pLdrEntry
 * Pointer to a PLDR_DATA_TABLE_ENTRY which is filled.
 *
 * @return
 * TRUE if the module was found, @p pLdrEntry is filled.
 * FALSE if no module was found.
 *
 * @see KdbpSymSearchModuleList().
 **/
BOOLEAN
KdbpSymFindModule(
    _In_opt_ PVOID Address,
    _In_opt_ INT Index,
    _Out_ PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    PLIST_ENTRY ModuleList;
    PEPROCESS CurrentProcess;
    ULONG Count = 0;
    BOOLEAN Success;

    /*
     * First, try to look up the module in the kernel module list.
     */

    /* Check whether the Mm subsystem has been initialized, by looking at whether
     * PsLoadedModuleList is initialized (done by MiInitializeLoadedModuleList). */
    IsMmInitialized();

    /* Select which list we should use (note that LoadOrderListHead can only
     * be used temporarily, since the KeLoaderBlock gets freed later on). */
    ModuleList = MmInitialized ? &PsLoadedModuleList
                               : &KeLoaderBlock->LoadOrderListHead;

    if (MmInitialized)
        KeAcquireSpinLockAtDpcLevel(&PsLoadedModuleSpinLock);
    Success = KdbpSymSearchModuleList(ModuleList,
                                      &Count,
                                      Address,
                                      Index,
                                      pLdrEntry);
    if (MmInitialized)
        KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);

    if (Success)
        return TRUE;

    /* Do not continue further if the Mm subsystem is not initialized yet */
    if (!MmInitialized)
        return FALSE;

    /*
     * This didn't succeed, try the module list of the current process now.
     */
    // NOTE: Consider retrieving a process specified by Id from parameter,
    // especially when such is given when loading symbols.
    CurrentProcess = PsGetCurrentProcess();

    if (!CurrentProcess || !CurrentProcess->Peb || !CurrentProcess->Peb->Ldr)
        return FALSE;

    ModuleList = &CurrentProcess->Peb->Ldr->InLoadOrderModuleList;
    return KdbpSymSearchModuleList(ModuleList,
                                   &Count,
                                   Address,
                                   Index,
                                   pLdrEntry);
}

static
PCHAR
KdbpSymUnicodeToAnsi(
    _In_ PCUNICODE_STRING Unicode,
    _Out_ PCHAR Ansi,
    _In_ ULONG Length)
{
    PCHAR p;
    PWCHAR pw;
    ULONG i;

    /* Set length and normalize it */
    i = Unicode->Length / sizeof(WCHAR);
    i = min(i, Length - 1);

    /* Set source and destination, and copy */
    pw = Unicode->Buffer;
    p = Ansi;
    while (i--) *p++ = (CHAR)*pw++;

    /* Null terminate and return */
    *p = ANSI_NULL;
    return Ansi;
}

/*! \brief Print address...
 *
 * Tries to lookup line number, file name and function name for the given
 * address and prints it.
 * If no such information is found the address is printed in the format
 * <module: offset>, otherwise the format will be
 * <module: offset (filename:linenumber (functionname))>
 *
 * \retval TRUE  Module containing \a Address was found, \a Address was printed.
 * \retval FALSE  No module containing \a Address was found, nothing was printed.
 */
BOOLEAN
KdbSymPrintAddress(
    IN PVOID Address,
    IN PCONTEXT Context)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR RelativeAddress;
    BOOLEAN Printed = FALSE;
    CHAR ModuleNameAnsi[64];

    if (!KdbpSymFindModule(Address, -1, &LdrEntry))
        return FALSE;

    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry->DllBase;

    KdbpSymUnicodeToAnsi(&LdrEntry->BaseDllName,
                         ModuleNameAnsi,
                         sizeof(ModuleNameAnsi));

    if (LdrEntry->PatchInformation)
    {
        ULONG LineNumber;
        CHAR FileName[256];
        CHAR FunctionName[256];

        if (RosSymGetAddressInformation(LdrEntry->PatchInformation,
                                        RelativeAddress,
                                        &LineNumber,
                                        FileName,
                                        FunctionName))
        {
            KdbPrintf("<%s:%x (%s:%d (%s))>",
                      ModuleNameAnsi, RelativeAddress,
                      FileName, LineNumber, FunctionName);
            Printed = TRUE;
        }
    }

    if (!Printed)
    {
        /* Just print module & address */
        KdbPrintf("<%s:%x>", ModuleNameAnsi, RelativeAddress);
    }

    return TRUE;
}


/**
 * @brief
 * Finds the image file corresponding to the given loaded
 * module, and attempts to load symbols from it.
 *
 * @param[in]   LdrEntry
 * The module entry whose image file is to be found
 * and the symbols loaded from it.
 *
 * @return  TRUE if the symbols were loaded; FALSE if not.
 *
 * @see KdbpSymLoadSymbolsWorker().
 **/
static
_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
// KdbpSymLoadModuleSymbols
KdbpSymLoadSymbolsFromFile(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES Attrib;
    IO_STATUS_BLOCK Iosb;
    HANDLE FileHandle;
    BOOLEAN Success;

    _IRQL_limited_to_(PASSIVE_LEVEL);

    KdpDprintf("Loading symbols for %wZ...\n", &LdrEntry->FullDllName);

    // TODO: Restore the possibility of loading an external .sym file?

    InitializeObjectAttributes(&Attrib,
                               &LdrEntry->FullDllName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    KdpDprintf("    Trying %wZ\n", &LdrEntry->FullDllName);
    Status = ZwOpenFile(&FileHandle,
                        FILE_READ_ACCESS | SYNCHRONIZE,
                        &Attrib,
                        &Iosb,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        /* Try system paths */
        static const UNICODE_STRING System32Dir = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\");
        UNICODE_STRING ImagePath;
        WCHAR ImagePathBuffer[256];

        RtlInitEmptyUnicodeString(&ImagePath, ImagePathBuffer, sizeof(ImagePathBuffer));
        RtlCopyUnicodeString(&ImagePath, &System32Dir);
        RtlAppendUnicodeStringToString(&ImagePath, &LdrEntry->BaseDllName);
        InitializeObjectAttributes(&Attrib,
                                   &ImagePath,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);

        KdpDprintf("    Trying %wZ\n", &ImagePath);
        Status = ZwOpenFile(&FileHandle,
                            FILE_READ_ACCESS | SYNCHRONIZE,
                            &Attrib,
                            &Iosb,
                            FILE_SHARE_READ,
                            FILE_SYNCHRONOUS_IO_NONALERT);
        if (!NT_SUCCESS(Status))
        {
            static const UNICODE_STRING DriversDir = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\drivers\\");

            RtlInitEmptyUnicodeString(&ImagePath, ImagePathBuffer, sizeof(ImagePathBuffer));
            RtlCopyUnicodeString(&ImagePath, &DriversDir);
            RtlAppendUnicodeStringToString(&ImagePath, &LdrEntry->BaseDllName);
            InitializeObjectAttributes(&Attrib,
                                       &ImagePath,
                                       OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                       NULL, NULL);

            KdpDprintf("    Trying %wZ\n", &ImagePath);
            Status = ZwOpenFile(&FileHandle,
                                FILE_READ_ACCESS | SYNCHRONIZE,
                                &Attrib,
                                &Iosb,
                                FILE_SHARE_READ,
                                FILE_SYNCHRONOUS_IO_NONALERT);
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed opening file %wZ (%wZ) for reading symbols (0x%08lx)\n",
                Attrib.ObjectName, &LdrEntry->BaseDllName, Status);
        return FALSE;
    }

//     KdpDprintf("    Loading symbols from %wZ...\n", Attrib.ObjectName);

    /* Hand it to RosSym */
    Success = RosSymCreateFromFile(&FileHandle,
                                   (PROSSYM_INFO*)&LdrEntry->PatchInformation);
    if (!Success)
        LdrEntry->PatchInformation = NULL;

#if 0
    if (Success)
    {
        KdpDprintf("Loaded symbols: %wZ 0x%p\n",
                   Attrib.ObjectName, LdrEntry->PatchInformation);
    }
#endif

    /* We're done for this one */
    ZwClose(FileHandle);
    return Success;
}

/**
 * @brief
 * Loads symbols from image mapping. If this fails,
 * loads them from the image file itself.
 *
 * @param[in]   LdrEntry
 * The module entry to load symbols from.
 *
 * @return  TRUE if the symbols were loaded; FALSE if not.
 *
 * @see KdbpSymLoadSymbolsFromFile(), LoadSymbolsRoutine().
 **/
static NTSTATUS
KdbpSymLoadSymbolsWorker(
    _Inout_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    CHAR ModuleNameAnsi[64];

    KdbpSymUnicodeToAnsi(&LdrEntry->FullDllName,
                         ModuleNameAnsi,
                         sizeof(ModuleNameAnsi));

// KdpDprintf("KdbpSymLoadSymbolsWorker(%s) at IRQL %u\n", ModuleNameAnsi, KeGetCurrentIrql());

    /* Load symbol information from the mapped image in memory (if present) */
    _SEH2_TRY
    {
        // if (!RosSymCreateFromMem(LdrEntry->DllBase,
                                 // LdrEntry->SizeOfImage,
                                 // (PROSSYM_INFO*)&LdrEntry->PatchInformation))
        if (TRUE) // Testing the file route...
        {
            /*
             * The symbols could not be loaded from memory. Try to load them
             * from the file image, but only if we are at PASSIVE_LEVEL and
             * the IO subsystem is initialized (or we are in InitPhase >= 2).
             * Otherwise, tell the caller to retry later.
             */
            if ((KeGetCurrentIrql() <= PASSIVE_LEVEL) &&
                ((KdbInitPhase >= 2) || IsIoInitialized()))
            {
                if (!KdbpSymLoadSymbolsFromFile(LdrEntry))
                {
                    KdpDprintf("Could not load symbols for %s\n", ModuleNameAnsi);
                    _SEH2_YIELD(return STATUS_NOT_FOUND);
                }
            }
            else
            {
                /* We should retry later */
                //KdpDprintf("    Retrying later\n");
                KdpDprintf("Pending symbols loading for %s\n", ModuleNameAnsi);
                _SEH2_YIELD(return STATUS_RETRY);
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        NTSTATUS Status = _SEH2_GetExceptionCode();
        KdpDprintf("RosSym faulted with status 0x%08lx\n", Status);
        _SEH2_YIELD(return Status);
    }
    _SEH2_END;

    KdbpSymUnicodeToAnsi(&LdrEntry->BaseDllName,
                         ModuleNameAnsi,
                         sizeof(ModuleNameAnsi));

    KdpDprintf("Loaded symbols: " /* "%wZ" */ "%s @ %p-%p %p\n",
               /* &LdrEntry->BaseDllName */ ModuleNameAnsi,
               LdrEntry->DllBase,
               (PVOID)((ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage),
               LdrEntry->PatchInformation);

#if 0
// FIXME: For whatever reason, at some point we get a fastfail assertion on
// that LoadedSymbols list, i.e. which becomes corrupted for whatever reason.
    /* Add it into the loaded symbols list */
    ExInterlockedInsertTailList(&LoadedSymbols,
                                &LdrEntry->InInitializationOrderLinks,
                                &LoadedSymbolsLock);
#endif

    return STATUS_SUCCESS;
}

/**
 * @param[in]   InsertAtHead
 * Whether to queue the module entry at the head (TRUE) or at the tail (FALSE)
 * of the list of symbols.
 **/
static inline
VOID
KdbpSymQueueLoadSymbols(
    _Inout_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ BOOLEAN InsertAtHead)
{
    /* Add a reference until we really process it. It will be released
     * with a call to MmUnloadSystemImage() in the worker thread. */
    LdrEntry->LoadCount++;

    /* Tell the worker thread to reload it */
    if (InsertAtHead)
    {
        ExInterlockedInsertHeadList(&SymbolsToLoad,
                                    &LdrEntry->InInitializationOrderLinks,
                                    &SymbolsToLoadLock);
    }
    else
    {
        ExInterlockedInsertTailList(&SymbolsToLoad,
                                    &LdrEntry->InInitializationOrderLinks,
                                    &SymbolsToLoadLock);
    }
    KeSetEvent(&SymbolsToLoadEvent, IO_NO_INCREMENT, FALSE);
}

/**
 * @brief
 * Loads the boot module symbols awaiting loading, once the
 * Mm subsystem and the PsLoadedModuleList are initialized.
 *
 * @see BootSymbolsToLoad.
 **/
static VOID
KdbpSymLoadPendingBootSymbols(VOID)
{
    PLIST_ENTRY ListEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry1, LdrEntry2;
    NTSTATUS Status;
    KIRQL OldIrql;

    while (!IsListEmpty(&BootSymbolsToLoad))
    {
        ListEntry = RemoveHeadList(&BootSymbolsToLoad);
        LdrEntry1 = CONTAINING_RECORD(ListEntry,
                                      LDR_DATA_TABLE_ENTRY,
                                      InInitializationOrderLinks);

        /* We cannot use this entry since it points inside
         * KeLoaderBlock->LoadOrderListHead. Instead we need to
         * find the corresponding entry in PsLoadedModuleList. */

        /* Acquire the lock at the correct IRQL */
        if ((OldIrql = KeGetCurrentIrql()) >= DISPATCH_LEVEL)
            KeAcquireSpinLockAtDpcLevel(&PsLoadedModuleSpinLock);
        else
            KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);

        for (ListEntry = PsLoadedModuleList.Flink;
             ListEntry != &PsLoadedModuleList;
             ListEntry = ListEntry->Flink)
        {
            LdrEntry2 = CONTAINING_RECORD(ListEntry,
                                          LDR_DATA_TABLE_ENTRY,
                                          InLoadOrderLinks);

            if (LdrEntry1->DllBase == LdrEntry2->DllBase &&
                LdrEntry1->SizeOfImage == LdrEntry2->SizeOfImage)
            {
                /* Entry found */
                break;
            }
        }

        /* Release the lock at the correct IRQL */
        if (OldIrql >= DISPATCH_LEVEL)
            KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);
        else
            KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);

        if (ListEntry == &PsLoadedModuleList)
        {
            /* Entry not found, that's strange; just ignore it */
            continue;
        }

        Status = KdbpSymLoadSymbolsWorker(LdrEntry2);
        if (Status == STATUS_RETRY)
        {
            /* Queue the entry for deferred loading */
            KdbpSymQueueLoadSymbols(LdrEntry2, TRUE);
        }
        /* Otherwise, we either succeeded, or failed
         * for whatever other reason: just continue. */
    }
}

/**
 * @brief
 * The symbol loader thread routine. It opens the image file for reading
 * and loads the symbols section from there.
 *
 * @param   Context
 * Unused.
 *
 * @note
 * We must do this because KdbSymProcessSymbols is called at high IRQL
 * and we can't set the event from here.
 **/
static KSTART_ROUTINE LoadSymbolsRoutine;
_Use_decl_annotations_
static VOID
NTAPI
LoadSymbolsRoutine(
    _In_ PVOID Context)
{
    LIST_ENTRY DelayedLoading;

    UNREFERENCED_PARAMETER(Context);

    /*
     * Initialize the temporary list that contains the modules
     * we need to retry loading their symbols later. It will be
     * prepended back into the SymbolsToLoad list once the latter
     * has been enumerated.
     */
    InitializeListHead(&DelayedLoading);

// FIXME: Should we re-do this here too? (see also KdbSymProcessSymbols)
    /* Load any pending boot symbols */
    if (!IsListEmpty(&BootSymbolsToLoad))
        KdbpSymLoadPendingBootSymbols();

    while (TRUE)
    {
        PLIST_ENTRY ListEntry;
        PLDR_DATA_TABLE_ENTRY LdrEntry;
        NTSTATUS Status;
        KIRQL OldIrql;

        Status = KeWaitForSingleObject(&SymbolsToLoadEvent,
                                       WrKernel,
                                       KernelMode,
                                       FALSE, NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("KeWaitForSingleObject failed?! 0x%08lx\n", Status);
            LoadSymbols = FALSE;
            return;
        }

        /* Loop through the list of modules whose symbols to load */
        while ((ListEntry = ExInterlockedRemoveHeadList(&SymbolsToLoad, &SymbolsToLoadLock)))
        {
            LdrEntry = CONTAINING_RECORD(ListEntry,
                                         LDR_DATA_TABLE_ENTRY,
                                         InInitializationOrderLinks);

            /***/InitializeListHead(&LdrEntry->InInitializationOrderLinks);/***/

            Status = KdbpSymLoadSymbolsWorker(LdrEntry);
            if (Status == STATUS_RETRY)
            {
                /* Move it to the temporary list, we will retry later */
                InsertHeadList(&DelayedLoading,
                               &LdrEntry->InInitializationOrderLinks);
            }
            else
            {
                /* Release the reference we took previously */
                MmUnloadSystemImage(LdrEntry);
            }
        }

        /* Finally, move the modules we need to reload later on,
         * back in orderly fashion into the list of symbols to load. */
        KeAcquireSpinLock(&SymbolsToLoadLock, &OldIrql);
        while (!IsListEmpty(&DelayedLoading))
        {
            ListEntry = RemoveHeadList(&DelayedLoading);
            LdrEntry = CONTAINING_RECORD(ListEntry,
                                         LDR_DATA_TABLE_ENTRY,
                                         InInitializationOrderLinks);

            /***/InitializeListHead(&LdrEntry->InInitializationOrderLinks);/***/

            InsertHeadList(&SymbolsToLoad,
                           &LdrEntry->InInitializationOrderLinks);
        }
        KeReleaseSpinLock(&SymbolsToLoadLock, OldIrql);
    }
}


/**
 * @brief
 * Loads or unloads symbols either for a loaded module,
 * either from a mapped image or a file image.
 *
 * @param[in]   LdrEntry
 * The module entry to load or unload symbols from.
 *
 * @param[in]   Load
 * Whether to load (TRUE) or unload (FALSE) the symbols
 * of the corresponding module.
 */
VOID
KdbSymProcessSymbols(
    _Inout_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ BOOLEAN Load)
{
    NTSTATUS Status;
    CHAR ModuleNameAnsi[64];

    if (!LoadSymbols)
        return;

    KdbpSymUnicodeToAnsi(&LdrEntry->FullDllName,
                         ModuleNameAnsi,
                         sizeof(ModuleNameAnsi));

    // KdpDprintf("InitPhase %d - KdbSymProcessSymbols(%s, %s)\n",
               // KdbInitPhase, ModuleNameAnsi, Load ? "TRUE" : "FALSE");

    /* Check whether the module is unloaded */
    if (!Load)
    {
        /* Did we process it? */
        if (LdrEntry->PatchInformation)
        {
            KdpDprintf("Unloading symbols for %s\n", ModuleNameAnsi);
#if 0
            /* Remove it from the loaded symbols list */
            KeAcquireSpinLockAtDpcLevel(&LoadedSymbolsLock);
            if (!IsListEmpty(&LdrEntry->InInitializationOrderLinks))
                RemoveEntryList(&LdrEntry->InInitializationOrderLinks);
            KeReleaseSpinLockFromDpcLevel(&LoadedSymbolsLock);
#endif

            if (MmInitialized)
                RosSymDelete(LdrEntry->PatchInformation);
            LdrEntry->PatchInformation = NULL;
        }
        return;
    }

    /* Don't reload symbols if they already exist */
    if (LdrEntry->PatchInformation)
    {
        KdpDprintf("Symbols already loaded for %s\n", ModuleNameAnsi);
        return;
    }

    /* If the Mm subsystem is not initialized, queue the
     * entry for reloading it as soon as possible later. */
    if (!MmInitialized)
    {
        /* This entry currently resides in KeLoaderBlock->LoadOrderListHead.
         * Wait for it to be copied to PsLoadedModuleList by the Mm subsystem
         * before we can reliably load its symbols. */
        KdpDprintf("Pending symbols loading for %s\n", ModuleNameAnsi);
        InsertTailList(&BootSymbolsToLoad,
                       &LdrEntry->InInitializationOrderLinks);
        return;
    }

    /*
     * The Mm subsystem is now initialized, or we are in InitPhase >= 1:
     * try to load the symbols from the loaded image in memory (loading
     * them requires memory allocations by RosSym). If loading the symbols
     * from memory fails, defer-load the symbols from the file image:
     * we cannot load them now since we are at HIGH_IRQL.
     */

    /* Load any pending boot symbols */
    if (!IsListEmpty(&BootSymbolsToLoad))
        KdbpSymLoadPendingBootSymbols();

    Status = KdbpSymLoadSymbolsWorker(LdrEntry);
    if (Status == STATUS_RETRY)
    {
        /* Queue the entry for deferred loading */
        KdbpSymQueueLoadSymbols(LdrEntry, FALSE);
    }
    /* Otherwise, we either succeeded, or failed
     * for whatever other reason: just bail out. */
}

/**
 * @brief   Reloads symbols for all existing loaded modules.
 * @return  None.
 * @remark  Unused yet. Should be rewritten to not hold the spinlock
 *          while loading the symbols.
 **/
VOID
KdbSymReloadSymbols(VOID)
{
    PLIST_ENTRY ListEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    KIRQL OldIrql;

    if (!LoadSymbols)
        return;
    if (!MmInitialized)
        return;

    KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);

    for (ListEntry = PsLoadedModuleList.Flink;
         ListEntry != &PsLoadedModuleList;
         ListEntry = ListEntry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(ListEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);
        KdbSymProcessSymbols(LdrEntry, TRUE);
    }

    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);
}

/**
 * @brief   Initializes the KDB symbols implementation.
 *
 * @param[in]   BootPhase
 * Phase of initialization.
 *
 * @return
 * TRUE if symbols are to be loaded at this given BootPhase; FALSE if not.
 **/
BOOLEAN
KdbSymInit(
    _In_ ULONG BootPhase)
{
    DPRINT("KdbSymInit() BootPhase=%d\n", BootPhase);

    if (BootPhase == 0)
    {
        PSTR CommandLine;
        SHORT Found = FALSE;
        CHAR YesNo;

        /* By default, load symbols in DBG builds, but not in REL builds
           or anything other than x86, because they only work on x86
           and can cause the system to hang on x64. */
#if DBG && defined(_M_IX86)
        LoadSymbols = TRUE;
#else
        LoadSymbols = FALSE;
#endif

        /* Check the command line for LOADSYMBOLS, NOLOADSYMBOLS,
         * LOADSYMBOLS={YES|NO}, NOLOADSYMBOLS={YES|NO} */
        ASSERT(KeLoaderBlock);
        CommandLine = KeLoaderBlock->LoadOptions;
        while (*CommandLine)
        {
            /* Skip any whitespace */
            while (isspace(*CommandLine))
                ++CommandLine;

            Found = 0;
            if (_strnicmp(CommandLine, "LOADSYMBOLS", 11) == 0)
            {
                Found = +1;
                CommandLine += 11;
            }
            else if (_strnicmp(CommandLine, "NOLOADSYMBOLS", 13) == 0)
            {
                Found = -1;
                CommandLine += 13;
            }
            if (Found != 0)
            {
                if (*CommandLine == '=')
                {
                    ++CommandLine;
                    YesNo = toupper(*CommandLine);
                    if (YesNo == 'N' || YesNo == '0')
                    {
                        Found = -1 * Found;
                    }
                }
                LoadSymbols = (0 < Found);
            }

            /* Move on to the next option */
            while (*CommandLine && !isspace(*CommandLine))
                ++CommandLine;
        }

        /* Initialize symbols support */
        InitializeListHead(&BootSymbolsToLoad);
        InitializeListHead(&SymbolsToLoad);
        KeInitializeSpinLock(&SymbolsToLoadLock);
        KeInitializeEvent(&SymbolsToLoadEvent, SynchronizationEvent, FALSE);

        RosSymInitKernelMode();

        InitializeListHead(&LoadedSymbols);
        KeInitializeSpinLock(&LoadedSymbolsLock);
    }
    else if (BootPhase == 1)
    {
        HANDLE Thread;
        NTSTATUS Status;

        /* The Mm subsystem is initialized by now */
        MmInitialized = TRUE;

        /* Do not continue loading symbols if we have less than 96MB of RAM */
        if (MmNumberOfPhysicalPages < (96 * 1024 * 1024 / PAGE_SIZE))
            LoadSymbols = FALSE;

        /* Continue this phase only if we need to load symbols */
        if (!LoadSymbols)
            return LoadSymbols;

        /* Launch the delay-loading worker thread */
        Status = PsCreateSystemThread(&Thread,
                                      THREAD_ALL_ACCESS,
                                      NULL, NULL, NULL,
                                      LoadSymbolsRoutine,
                                      NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed starting symbols loader thread: 0x%08lx\n", Status);
            LoadSymbols = FALSE;
            return LoadSymbols;
        }
    }

    return LoadSymbols;
}

/* EOF */
