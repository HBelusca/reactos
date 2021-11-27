/*
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Virtual DOS Machine
 * FILE:            subsystems/mvdm/ntvdm/ntvdm.c
 * PURPOSE:         Virtual DOS Machine
 * PROGRAMMERS:     Aleksandar Andrejevic <theflash AT sdf DOT lonestar DOT org>
 */

/* INCLUDES *******************************************************************/

#include "ntvdm.h"

#define NDEBUG
#include <debug.h>

#include "emulator.h"

#include "bios/bios.h"
#include "cpu/cpu.h"

#ifdef _USE_DOS_
#include "dos/dem.h"
#endif

/* Extra PSDK/NDK Headers */
#include <ndk/psfuncs.h>

/* VARIABLES ******************************************************************/

NTVDM_SETTINGS GlobalSettings;

BOOL bStandalone = FALSE; // TRUE if Standalone mode; FALSE if not (default).

// Command line of NTVDM
INT     NtVdmArgc;
WCHAR** NtVdmArgv;

/* Full directory where NTVDM resides, or the SystemRoot\System32 path */
WCHAR NtVdmPath[MAX_PATH];
ULONG NtVdmPathSize; // Length without NULL terminator.

/* PRIVATE FUNCTIONS **********************************************************/

static NTSTATUS
NTAPI
NtVdmConfigureBios(IN PWSTR ValueName,
                   IN ULONG ValueType,
                   IN PVOID ValueData,
                   IN ULONG ValueLength,
                   IN PVOID Context,
                   IN PVOID EntryContext)
{
    PNTVDM_SETTINGS Settings = (PNTVDM_SETTINGS)Context;
    UNICODE_STRING ValueString;

    /* Check for the type of the value */
    if (ValueType != REG_SZ)
    {
        RtlInitEmptyAnsiString(&Settings->BiosFileName, NULL, 0);
        return STATUS_SUCCESS;
    }

    /* Convert the UNICODE string to ANSI and store it */
    RtlInitEmptyUnicodeString(&ValueString, (PWCHAR)ValueData, ValueLength);
    ValueString.Length = ValueString.MaximumLength;
    RtlUnicodeStringToAnsiString(&Settings->BiosFileName, &ValueString, TRUE);

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
NtVdmConfigureRom(IN PWSTR ValueName,
                  IN ULONG ValueType,
                  IN PVOID ValueData,
                  IN ULONG ValueLength,
                  IN PVOID Context,
                  IN PVOID EntryContext)
{
    PNTVDM_SETTINGS Settings = (PNTVDM_SETTINGS)Context;
    UNICODE_STRING ValueString;

    /* Check for the type of the value */
    if (ValueType != REG_MULTI_SZ)
    {
        RtlInitEmptyAnsiString(&Settings->RomFiles, NULL, 0);
        return STATUS_SUCCESS;
    }

    /* Convert the UNICODE string to ANSI and store it */
    RtlInitEmptyUnicodeString(&ValueString, (PWCHAR)ValueData, ValueLength);
    ValueString.Length = ValueString.MaximumLength;
    RtlUnicodeStringToAnsiString(&Settings->RomFiles, &ValueString, TRUE);

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
NtVdmConfigureFloppy(IN PWSTR ValueName,
                     IN ULONG ValueType,
                     IN PVOID ValueData,
                     IN ULONG ValueLength,
                     IN PVOID Context,
                     IN PVOID EntryContext)
{
    BOOLEAN Success;
    PNTVDM_SETTINGS Settings = (PNTVDM_SETTINGS)Context;
    ULONG DiskNumber = PtrToUlong(EntryContext);

    ASSERT(DiskNumber < ARRAYSIZE(Settings->FloppyDisks));

    /* Check whether the Hard Disk entry was not already configured */
    if (Settings->FloppyDisks[DiskNumber].Buffer != NULL)
    {
        DPRINT1("Floppy Disk %d -- '%wZ' already configured\n", DiskNumber, &Settings->FloppyDisks[DiskNumber]);
        return STATUS_SUCCESS;
    }

    /* Check for the type of the value */
    if (ValueType != REG_SZ)
    {
        RtlInitEmptyUnicodeString(&Settings->FloppyDisks[DiskNumber], NULL, 0);
        return STATUS_SUCCESS;
    }

    /* Initialize the string */
    Success = RtlCreateUnicodeString(&Settings->FloppyDisks[DiskNumber], (PCWSTR)ValueData);
    ASSERT(Success);

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
NtVdmConfigureHDD(IN PWSTR ValueName,
                  IN ULONG ValueType,
                  IN PVOID ValueData,
                  IN ULONG ValueLength,
                  IN PVOID Context,
                  IN PVOID EntryContext)
{
    BOOLEAN Success;
    PNTVDM_SETTINGS Settings = (PNTVDM_SETTINGS)Context;
    ULONG DiskNumber = PtrToUlong(EntryContext);

    ASSERT(DiskNumber < ARRAYSIZE(Settings->HardDisks));

    /* Check whether the Hard Disk entry was not already configured */
    if (Settings->HardDisks[DiskNumber].Buffer != NULL)
    {
        DPRINT1("Hard Disk %d -- '%wZ' already configured\n", DiskNumber, &Settings->HardDisks[DiskNumber]);
        return STATUS_SUCCESS;
    }

    /* Check for the type of the value */
    if (ValueType != REG_SZ)
    {
        RtlInitEmptyUnicodeString(&Settings->HardDisks[DiskNumber], NULL, 0);
        return STATUS_SUCCESS;
    }

    /* Initialize the string */
    Success = RtlCreateUnicodeString(&Settings->HardDisks[DiskNumber], (PCWSTR)ValueData);
    ASSERT(Success);

    return STATUS_SUCCESS;
}

static RTL_QUERY_REGISTRY_TABLE
NtVdmConfigurationTable[] =
{
    {
        NtVdmConfigureBios,
        0,
        L"BiosFile",
        NULL,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureRom,
        RTL_QUERY_REGISTRY_NOEXPAND,
        L"RomFiles",
        NULL,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureFloppy,
        0,
        L"FloppyDisk0",
        (PVOID)0,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureFloppy,
        0,
        L"FloppyDisk1",
        (PVOID)1,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureHDD,
        0,
        L"HardDisk0",
        (PVOID)0,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureHDD,
        0,
        L"HardDisk1",
        (PVOID)1,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureHDD,
        0,
        L"HardDisk2",
        (PVOID)2,
        REG_NONE,
        NULL,
        0
    },

    {
        NtVdmConfigureHDD,
        0,
        L"HardDisk3",
        (PVOID)3,
        REG_NONE,
        NULL,
        0
    },

    /* End of table */
    {0}
};

static BOOL
LoadGlobalSettings(IN PNTVDM_SETTINGS Settings)
{
    NTSTATUS Status;

    ASSERT(Settings);

    /*
     * Now we can do:
     * - CPU core choice
     * - Video choice
     * - Sound choice
     * - Mem?
     * - ...
     * - Debug settings
     */
    Status = RtlQueryRegistryValues(RTL_REGISTRY_CONTROL,
                                    L"NTVDM",
                                    NtVdmConfigurationTable,
                                    Settings,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NTVDM registry settings cannot be fully initialized, using default ones. Status = 0x%08lx\n", Status);
    }

    return NT_SUCCESS(Status);
}

static VOID
FreeGlobalSettings(IN PNTVDM_SETTINGS Settings)
{
    USHORT i;

    ASSERT(Settings);

    if (Settings->BiosFileName.Buffer)
        RtlFreeAnsiString(&Settings->BiosFileName);

    if (Settings->RomFiles.Buffer)
        RtlFreeAnsiString(&Settings->RomFiles);

    for (i = 0; i < ARRAYSIZE(Settings->FloppyDisks); ++i)
    {
        if (Settings->FloppyDisks[i].Buffer)
            RtlFreeUnicodeString(&Settings->FloppyDisks[i]);
    }

    for (i = 0; i < ARRAYSIZE(Settings->HardDisks); ++i)
    {
        if (Settings->HardDisks[i].Buffer)
            RtlFreeUnicodeString(&Settings->HardDisks[i]);
    }
}

static VOID
ConsoleCleanup(VOID);

/** HACK!! **/
#include "./console/console.c"
/** HACK!! **/

/*static*/ VOID
VdmShutdown(BOOLEAN Immediate)
{
    /*
     * Immediate = TRUE:  Immediate shutdown;
     *             FALSE: Delayed shutdown.
     */
    static BOOLEAN MustShutdown = FALSE;

    /* If a shutdown is ongoing, just return */
    if (MustShutdown)
    {
        DPRINT1("Shutdown is ongoing...\n");
        Sleep(INFINITE);
        return;
    }

#ifdef _USE_DOS_
    /* First notify DOS to see whether we can shut down now */
    MustShutdown = DosShutdown(Immediate);
#endif
    /*
     * In case we perform an immediate shutdown, or the DOS says
     * we can shut down, do it now.
     */
    MustShutdown = MustShutdown || Immediate;

    if (MustShutdown)
    {
        EmulatorTerminate();

        BiosCleanup();
        EmulatorCleanup();
        ConsoleCleanup();

        FreeGlobalSettings(&GlobalSettings);

        DPRINT1("\n\n\nNTVDM - Exiting...\n\n\n");
        /* Some VDDs rely on the fact that NTVDM calls ExitProcess on Windows */
        ExitProcess(0);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
DisplayMessage(IN LPCWSTR Format, ...)
{
#ifndef WIN2K_COMPLIANT
    WCHAR  StaticBuffer[256];
    LPWSTR Buffer = StaticBuffer; // Use the static buffer by default.
#else
    WCHAR  Buffer[2048]; // Large enough. If not, increase it by hand.
#endif
    size_t MsgLen;
    va_list args;

    va_start(args, Format);

#ifndef WIN2K_COMPLIANT
    /*
     * Retrieve the message length and if it is too long, allocate
     * an auxiliary buffer; otherwise use the static buffer.
     * The string is built to be NULL-terminated.
     */
    MsgLen = _vscwprintf(Format, args);
    if (MsgLen >= ARRAYSIZE(StaticBuffer))
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, (MsgLen + 1) * sizeof(WCHAR));
        if (Buffer == NULL)
        {
            /* Allocation failed, use the static buffer and display a suitable error message */
            Buffer = StaticBuffer;
            Format = L"DisplayMessage()\nOriginal message is too long and allocating an auxiliary buffer failed.";
            MsgLen = wcslen(Format);
        }
    }
#else
    MsgLen = ARRAYSIZE(Buffer) - 1;
#endif

    RtlZeroMemory(Buffer, (MsgLen + 1) * sizeof(WCHAR));
    _vsnwprintf(Buffer, MsgLen, Format, args);

    va_end(args);

    /* Display the message */
    DPRINT1("\n\nNTVDM Subsystem\n%S\n\n", Buffer);
    MessageBoxW(hConsoleWnd, Buffer, L"NTVDM Subsystem", MB_OK);

#ifndef WIN2K_COMPLIANT
    /* Free the buffer if needed */
    if (Buffer != StaticBuffer) RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
#endif
}

/*
 * This function, derived from DisplayMessage, is used by the BIOS and
 * the DOS to display messages to an output device. A printer function
 * is given for printing the characters.
 */
VOID
PrintMessageAnsi(IN CHAR_PRINT CharPrint,
                 IN LPCSTR Format, ...)
{
    static CHAR CurChar = 0;
    LPSTR str;

#ifndef WIN2K_COMPLIANT
    CHAR  StaticBuffer[256];
    LPSTR Buffer = StaticBuffer; // Use the static buffer by default.
#else
    CHAR  Buffer[2048]; // Large enough. If not, increase it by hand.
#endif
    size_t MsgLen;
    va_list args;

    va_start(args, Format);

#ifndef WIN2K_COMPLIANT
    /*
     * Retrieve the message length and if it is too long, allocate
     * an auxiliary buffer; otherwise use the static buffer.
     * The string is built to be NULL-terminated.
     */
    MsgLen = _vscprintf(Format, args);
    if (MsgLen >= ARRAYSIZE(StaticBuffer))
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, (MsgLen + 1) * sizeof(CHAR));
        if (Buffer == NULL)
        {
            /* Allocation failed, use the static buffer and display a suitable error message */
            Buffer = StaticBuffer;
            Format = "DisplayMessageAnsi()\nOriginal message is too long and allocating an auxiliary buffer failed.";
            MsgLen = strlen(Format);
        }
    }
#else
    MsgLen = ARRAYSIZE(Buffer) - 1;
#endif

    RtlZeroMemory(Buffer, (MsgLen + 1) * sizeof(CHAR));
    _vsnprintf(Buffer, MsgLen, Format, args);

    va_end(args);

    /* Display the message */
    // DPRINT1("\n\nNTVDM DOS32\n%s\n\n", Buffer);

    MsgLen = strlen(Buffer);
    str = Buffer;
    while (MsgLen--)
    {
        if (*str == '\n' && CurChar != '\r')
            CharPrint('\r');

        CurChar = *str++;
        CharPrint(CurChar);
    }

#ifndef WIN2K_COMPLIANT
    /* Free the buffer if needed */
    if (Buffer != StaticBuffer) RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
#endif
}

static VOID
Usage(VOID)
{
    wprintf(L"\n"
            L"ReactOS Virtual DOS Machine\n"
            L"\n"
            L"Usage:\n"
            L"NTVDM -h\n"
            L"NTVDM -r <executable> [<parameters>]\n"
            L"NTVDM [-i<SessionId>] [-w[s]]\n"
            L"\n"
            L"Options:\n"
            L"    -?, -h  Displays this help message.\n"
            L"    -r      Runs in Standalone mode the specified executable.\n"
            L"            Without this option, NTVDM runs in OS-integrated mode.\n"
            L"\n"
            L"Options for OS-integrated mode:\n"
            L"    -i<SessionId>   Specifies a DOS/WOW16 VDM session ID in hexadecimal format.\n"
            L"    -w              Starts a shared WOW16 VDM.\n"
            L"    -ws             Starts a separate WOW16 VDM.\n");
}

INT
wmain(INT argc, WCHAR *argv[])
{
    BOOL Success;

    /*
     * Check the first argument only for -h (Help screen) or -r (Standalone mode).
     */
    if (argc >= 2)
    {
        INT i = 1;
        if (argv[i][0] == L'-' || argv[i][0] == L'/')
        {
            /* Help */
            if ((argv[i][1] == L'?' || towlower(argv[i][1]) == L'h') && (argv[i][2] == 0))
            {
                Usage();
                return 0;
            }
            else
            /* "Run" - Standalone mode */
            if ((towlower(argv[i][1]) == L'r') && (argv[i][2] == 0))
            {
                bStandalone = TRUE;
            }
        }

        /* If Standalone mode, we must have more arguments following */
        if (bStandalone && (argc <= 2))
        {
            Usage();
            return 0;
        }
    }

    if (!bStandalone)
    {
        /* If not Standalone mode, we must be started as a VDM */
        NTSTATUS Status;
        ULONG VdmPower = 0;
        Status = NtQueryInformationProcess(NtCurrentProcess(),
                                           ProcessWx86Information,
                                           &VdmPower,
                                           sizeof(VdmPower),
                                           NULL);
        if (!NT_SUCCESS(Status) || (VdmPower == 0))
        {
            /* Not a VDM, bail out */
            return 0;
        }
    }

    NtVdmArgc = argc;
    NtVdmArgv = argv;

#ifdef ADVANCED_DEBUGGING
    {
    INT i = 20;

    printf("Waiting for debugger (10 secs)..");
    while (i--)
    {
        printf(".");
        if (IsDebuggerPresent())
        {
            DbgBreakPoint();
            break;
        }
        Sleep(500);
    }
    printf("Continue\n");
    }
#endif

    DPRINT1("\n\n\n"
            "NTVDM - Starting...\n"
            "Command Line: '%s'\n"
            "\n\n",
            GetCommandLineA());

    /*
     * Retrieve the full directory of the current running NTVDM instance.
     * In case of failure, use the default SystemRoot\System32 path.
     */
    NtVdmPathSize = GetModuleFileNameW(NULL, NtVdmPath, _countof(NtVdmPath));
    NtVdmPath[_countof(NtVdmPath) - 1] = UNICODE_NULL; // Ensure NULL-termination (see WinXP bug)

    Success = ((NtVdmPathSize != 0) && (NtVdmPathSize < _countof(NtVdmPath)) &&
               (GetLastError() != ERROR_INSUFFICIENT_BUFFER));
    if (Success)
    {
        /* Find the last path separator, remove it as well as the file name */
        PWCHAR pch = wcsrchr(NtVdmPath, L'\\');
        if (pch)
            *pch = UNICODE_NULL;
    }
    else
    {
        /* We failed, use the default SystemRoot\System32 path */
        NtVdmPathSize = GetSystemDirectoryW(NtVdmPath, _countof(NtVdmPath));
        Success = ((NtVdmPathSize != 0) && (NtVdmPathSize < _countof(NtVdmPath)));
        if (!Success)
        {
            /* We failed again, try to do it ourselves */
            NtVdmPathSize = (ULONG)wcslen(SharedUserData->NtSystemRoot) + _countof("\\System32") - 1;
            Success = (NtVdmPathSize < _countof(NtVdmPath));
            if (Success)
            {
                Success = NT_SUCCESS(RtlStringCchPrintfW(NtVdmPath,
                                                         _countof(NtVdmPath),
                                                         L"%s\\System32",
                                                         SharedUserData->NtSystemRoot));
            }
            if (!Success)
            {
                wprintf(L"FATAL: Could not retrieve NTVDM path.\n");
                goto Cleanup;
            }
        }
    }
    NtVdmPathSize = (ULONG)wcslen(NtVdmPath);

    /* Load the global VDM settings */
    LoadGlobalSettings(&GlobalSettings);

    /* Initialize the console */
    if (!ConsoleInit())
    {
        wprintf(L"FATAL: A problem occurred when trying to initialize the console.\n");
        goto Cleanup;
    }

    /* Initialize the emulator */
    if (!EmulatorInitialize(ConsoleInput, ConsoleOutput))
    {
        wprintf(L"FATAL: Failed to initialize the emulator.\n");
        goto Cleanup;
    }

    /* Initialize the system BIOS and option ROMs */
    if (!BiosInitialize(GlobalSettings.BiosFileName.Buffer,
                        GlobalSettings.RomFiles.Buffer))
    {
        wprintf(L"FATAL: Failed to initialize the VDM BIOS.\n");
        goto Cleanup;
    }

    /* Let's go! Start simulation */
    CpuSimulate();

    /* Quit the VDM */
Cleanup:
    VdmShutdown(TRUE);
    return 0;
}

/* EOF */
