/*
 * PROJECT:     ReactOS Kernel Debugger
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     TEE Transport Extension DLL
 * COPYRIGHT:   Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *****************************************************************/

#define NOEXTAPI
#include <ntifs.h>
#include <halfuncs.h>
#include <stdio.h>
#include <arc/arc.h>
#include <windbgkd.h>
#include <kddll.h>

#define NDEBUG
#include <debug.h>

#undef DPRINT
#undef DPRINT1
#define DPRINT(...)  ((void)0)
#define DPRINT1(...) ((void)0)

/* GLOBALS *******************************************************************/

typedef NTSTATUS
(NTAPI *PKD_D03_TRANSITION)(VOID);

typedef NTSTATUS
(NTAPI *PKD_DEBUGGER_INITIALIZE)(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

typedef KDSTATUS
(NTAPI *PKD_RECEIVE_PACKET)(
    _In_ ULONG PacketType,
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength,
    _Inout_ PKD_CONTEXT Context);

typedef NTSTATUS
(NTAPI *PKD_SAVE_RESTORE)(
    _In_ BOOLEAN SleepTransition);

typedef VOID
(NTAPI *PKD_SEND_PACKET)(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context);

PCSTR KdDllExportsNames[] =
{
    "KdD0Transition",
    "KdD3Transition",
    "KdDebuggerInitialize0",
    "KdDebuggerInitialize1",
    "KdReceivePacket",
    "KdRestore",
    "KdSave",
    "KdSendPacket",
};

typedef union _KDDLL_EXPORTS_FUNCS
{
    struct
    {
        PKD_D03_TRANSITION pKdD0Transition;
        PKD_D03_TRANSITION pKdD3Transition;
        PKD_DEBUGGER_INITIALIZE pKdDebuggerInitialize0;
        PKD_DEBUGGER_INITIALIZE pKdDebuggerInitialize1;
        PKD_RECEIVE_PACKET pKdReceivePacket;
        PKD_SAVE_RESTORE pKdRestore;
        PKD_SAVE_RESTORE pKdSave;
        PKD_SEND_PACKET  pKdSendPacket;
    } s;
    PVOID Table[RTL_NUMBER_OF(KdDllExportsNames)];
} KDDLL_EXPORTS_FUNCS, *PKDDLL_EXPORTS_FUNCS;

KDDLL_EXPORTS_FUNCS KdDllExportsFuncs[2]; // For the two tee'd DLLs.


/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief   Returns the address of a named export from a given loaded module.
 * @note
 * Adapted from nt!RtlFindExportedRoutineByName. Sadly, only Windows 10+ offers
 * the luxury of the RtlFindExportedRoutineByName() export, so in the meantime
 * we re-implement it there.
 **/
static PVOID
KdpFindExportedRoutineByName(
    _In_ PVOID DllBase,
    _In_ PCSTR ExportName)
{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PULONG NameTable;
    PUSHORT OrdinalTable;
    ULONG ExportSize;
    LONG Low = 0, Mid = 0, High;
    LONG Ret;
    USHORT Ordinal;
    PULONG ExportTable;
    PVOID Function;

    /* Get the export directory */
    ExportDirectory = RtlImageDirectoryEntryToData(DllBase,
                                                   TRUE,
                                                   IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                   &ExportSize);
    if (!ExportDirectory)
        return NULL;

    /* Setup name tables */
    NameTable = (PULONG)((ULONG_PTR)DllBase +
                         ExportDirectory->AddressOfNames);
    OrdinalTable = (PUSHORT)((ULONG_PTR)DllBase +
                             ExportDirectory->AddressOfNameOrdinals);

    /* Get the ordinal */

    /* Fail if no names */
    if (!ExportDirectory->NumberOfNames)
        return NULL;

    /* Do a binary search */
    High = ExportDirectory->NumberOfNames - 1;
    while (High >= Low)
    {
        /* Get new middle value */
        Mid = (Low + High) >> 1;

        /* Compare name */
        Ret = strcmp(ExportName, (PCHAR)((ULONG_PTR)DllBase + NameTable[Mid]));
        if (Ret < 0)
        {
            /* Update high */
            High = Mid - 1;
        }
        else if (Ret > 0)
        {
            /* Update low */
            Low = Mid + 1;
        }
        else
        {
            /* We got it */
            break;
        }
    }

    /* Check if we couldn't find it */
    if (High < Low)
        return NULL;

    /* Otherwise, this is the ordinal */
    Ordinal = OrdinalTable[Mid];

    /* Validate the ordinal */
    if (Ordinal >= ExportDirectory->NumberOfFunctions)
        return NULL;

    /* Resolve the address and write it */
    ExportTable = (PULONG)((ULONG_PTR)DllBase +
                           ExportDirectory->AddressOfFunctions);
    Function = (PVOID)((ULONG_PTR)DllBase + ExportTable[Ordinal]);

    /* Check if the function is actually a forwarder */
    if (((ULONG_PTR)Function > (ULONG_PTR)ExportDirectory) &&
        ((ULONG_PTR)Function < ((ULONG_PTR)ExportDirectory + ExportSize)))
    {
        /* It is, fail */
        return NULL;
    }

    /* We found it! */
    return Function;
}

NTSTATUS
DelayLoadKdDll(
    _In_ PVOID KdDllBase,
    _Inout_ PKDDLL_EXPORTS_FUNCS FuncTable)
{
    ULONG i;
    PVOID ProcAddress;

    /* Loop all exports */
    for (i = 0; i < RTL_NUMBER_OF(KdDllExportsNames); ++i)
    {
        /* Get the address of the routine being looked up */
        ProcAddress = KdpFindExportedRoutineByName(KdDllBase,
                                                   KdDllExportsNames[i]);
        DPRINT1("KDTEE: Found %s at 0x%p\n",
                KdDllExportsNames[i], ProcAddress);
        if (!ProcAddress) break;
        FuncTable->Table[i] = ProcAddress;
    }

    /* Return success if we find them all, otherwise fail */
    if (i == RTL_NUMBER_OF(KdDllExportsNames))
        return STATUS_SUCCESS;

    return STATUS_UNSUCCESSFUL;
}


/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
KdDebuggerInitialize0(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    UNICODE_STRING BaseDll1 = RTL_CONSTANT_STRING(L"kdport1.dll");
    UNICODE_STRING BaseDll2 = RTL_CONSTANT_STRING(L"kdport2.dll");
    PLDR_DATA_TABLE_ENTRY EntryDlls[2] = {NULL, NULL};

    /* If we don't have a loader block, we cannot do much,
     * so just return success to not fail debugger attach */
    if (!LoaderBlock)
        return STATUS_SUCCESS;

    /*
     * Try finding the two kernel transport DLLs in the list of loaded modules.
     * They are loaded under the name "kdport1.dll" and "kdport2.dll" by the
     * FreeLdr NT-compatible loader.
     */

#if 0
//
// NOTE: PsLoadedModuleList is not an export, but can be obtained via
// the KdDebuggerDataBlock. See kdgdb for an example.
// NOTE 2: KeLoaderBlock is also an export of NTOS, and is in the KdDebuggerDataBlock.
//
    /* Check which list we should use */
    ListHead = KeLoaderBlock ? &KeLoaderBlock->LoadOrderListHead :
                               &PsLoadedModuleList;
#endif

    /* Loop the loaded module list */
    for (NextEntry = LoaderBlock->LoadOrderListHead.Flink;
         NextEntry != &LoaderBlock->LoadOrderListHead;
         NextEntry = NextEntry->Flink)
    {
        /* Get the loader entry */
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (RtlEqualUnicodeString(BaseDll1, &LdrEntry->BaseDllName, TRUE))
        {
            /* Port 1 DLL found */
            EntryDlls[0] = LdrEntry;
        }
        else if (RtlEqualUnicodeString(BaseDll2, &LdrEntry->BaseDllName, TRUE))
        {
            /* Port 2 DLL found */
            EntryDlls[1] = LdrEntry;
        }

        /* Break out if we have found both DLLs */
        if (EntryDll1 && EntryDll2)
            break;
    }

    /* Load the exports of each KD DLL */
    Status = DelayLoadKdDll(EntryDlls[0]->DllBase, &KdDllExportsFuncs[0]);
    Status = DelayLoadKdDll(EntryDlls[1]->DllBase, &KdDllExportsFuncs[1]);

// TODO: Check the returned status and fail if needed!

    /* Now call each DLL initialization routine */
    Status = KdDllExportsFuncs[0].pKdDebuggerInitialize0(LoaderBlock);
    Status = KdDllExportsFuncs[1].pKdDebuggerInitialize0(LoaderBlock);

// TODO: Check the returned status and fail if needed!

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdDebuggerInitialize1(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;

    /* Call each DLL initialization routine */
    Status = KdDllExportsFuncs[0].pKdDebuggerInitialize1(LoaderBlock);
    Status = KdDllExportsFuncs[1].pKdDebuggerInitialize1(LoaderBlock);

// TODO: Check the returned status and fail if needed!

    return STATUS_SUCCESS;
}

/**
 * @brief   Called when transitioning to the D0 working power state.
 **/
NTSTATUS
NTAPI
KdD0Transition(VOID)
{
    NTSTATUS Status;

    /* Call each DLL initialization routine in the reverse order
     * than D3 (so, first the slave device, then the master device). */
    Status = KdDllExportsFuncs[1].pKdD0Transition(LoaderBlock);
    Status = KdDllExportsFuncs[0].pKdD0Transition(LoaderBlock);

// TODO: Check the returned status and fail if needed!

    return STATUS_SUCCESS;
}

/**
 * @brief   Called when transitioning to the D3 low-power state.
 **/
NTSTATUS
NTAPI
KdD3Transition(VOID)
{
    NTSTATUS Status;

    /* Call each DLL initialization routine: first
     * the master device, then the slave device. */
    Status = KdDllExportsFuncs[0].pKdD0Transition(LoaderBlock);
    Status = KdDllExportsFuncs[1].pKdD0Transition(LoaderBlock);

// TODO: Check the returned status and fail if needed!

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdSave(
    _In_ BOOLEAN SleepTransition)
{
    /* Nothing to do on COM ports */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdRestore(
    _In_ BOOLEAN SleepTransition)
{
    /* Nothing to do on COM ports */
    return STATUS_SUCCESS;
}

VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
{
    /* Call each DLL initialization routine */
    KdDllExportsFuncs[0].pKdSendPacket(PacketType,
                                       MessageHeader,
                                       MessageData,
                                       Context);

    KdDllExportsFuncs[1].pKdSendPacket(PacketType,
                                       MessageHeader,
                                       MessageData,
                                       Context);

// TODO: Check the returned status and fail if needed!

    return;
}

KDSTATUS
NTAPI
KdReceivePacket(
    _In_ ULONG PacketType,
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength,
    _Inout_ PKD_CONTEXT Context)
{
    KDSTATUS Status;

    /* Call each DLL initialization routine */
    Status = KdDllExportsFuncs[0].pKdReceivePacket(PacketType,
                                                   MessageHeader,
                                                   MessageData,
                                                   DataLength,
                                                   Context);

    Status = KdDllExportsFuncs[1].pKdReceivePacket(PacketType,
                                                   MessageHeader,
                                                   MessageData,
                                                   DataLength,
                                                   Context);

// TODO: Check the returned status and fail if needed!

    return 0;
}

/* EOF */
