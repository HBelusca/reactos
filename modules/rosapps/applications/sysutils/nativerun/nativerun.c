/*
 * PROJECT:     NativeRun Command
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NT Native Applications startup program.
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *
 * NOTE: Loosely based on the "nativerun" tool by Pavel Yosifovich,
 * https://github.com/zodiacon/NativeApps/blob/master/nativerun/nativerun.cpp
 * https://github.com/zodiacon/winnativeapibooksamples/blob/main/Chapter03/nativerun/nativerun.cpp
 *
 * See also the (less portable) "runnt" tool at:
 * https://csclub.uwaterloo.ca/~pbarfuss/nt_utils/runnt.c
 */

/* INCLUDES ******************************************************************/

#include "ntcrtio.h" // <stdio.h>
#include <stdlib.h>

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
// #include <windows.h>
#include <windef.h>
#include <winbase.h>
#include <intsafe.h>

// #include <ntndk.h>
#define NTOS_MODE_USER
#include <ndk/lpctypes.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>

/* CSRSS Headers (for basic Win32 app support) */
#include <csr/csr.h>
#include <win/basemsg.h>


/* Copyright information */
#define PROGNAME        "nativerun"
#define VERSION         "0.6b"
#define COPYRIGHT_YEARS "2025"


/* GLOBALS *******************************************************************/

PIMAGE_NT_HEADERS g_NtHeader = NULL;
PRTL_USER_PROCESS_PARAMETERS g_ProcessParameters = NULL;
BOOLEAN g_IsNative = FALSE;


/* FUNCTIONS *****************************************************************/

/*
 * VOID
 * __cdecl
 * PrintWarning(
 *     _In_ PCSTR fmt,
 *     ...);
 */
#define PrintWarning(fmt, ...) \
    (void)fprintf(stdout, PROGNAME ": " fmt, ##__VA_ARGS__)

/*
 * VOID
 * __cdecl
 * PrintError(
 *     _In_ PCSTR fmt,
 *     ...);
 */
#define PrintError(fmt, ...) \
    (void)fprintf(stderr, PROGNAME ": " fmt, ##__VA_ARGS__)

static void
Banner(void)
{
    puts("NativeRun Command\n"
         "Version " VERSION "\n"
         "Copyright " COPYRIGHT_YEARS " Herm\xE8s B\xE9lusca-Ma\xEFto <hermes.belusca-maito@reactos.org>\n"
         // L"Under GPL-2.0+ license (https://spdx.org/licenses/GPL-2.0+)\n"
         "Under MIT license (https://spdx.org/licenses/MIT)\n"
         );//"\n");
}

static void
Usage(void)
{
    Banner();
    puts("Startup program for NT Native Applications, with bare support\n"
         "for Win32 applications as well.\n"
         "\n"
         "Usage:\n"
         "  " PROGNAME " [-p] [-e] [-d] [-s] <executable> [arguments...]\n"
         "  " PROGNAME " -s <pid>\n"
         "  " PROGNAME " -r <pid>\n"
         "  " PROGNAME " -k <pid>\n"
         "  " PROGNAME " -?\n"
         "\n"
         "Options:\n"
      // "    -n, --nologo    Remove the banner.\n"
         "    -p, --default-dll-path  Use a default DLL path:\n"
         "                              `SystemRoot;SystemRoot\\system32`\n"
         "                            rather than the current one.\n"
         "    -e, --default-environ   Use a default environment\n"
         "                            rather than the current one.\n"
      // "    -d, --debug     Start the specified executable in Debug mode.\n"
         "    -d, --debug     Start the specified executable and suspend the process\n"
         "                    to allow the user to attach a debugger to it.\n"
         "    -s, --suspend   Start the specified executable in Suspended mode.\n"
         "    -s <pid>        Suspend the process specified by <pid>.\n"
         "    -r, --resume    Resume the suspended process specified by <pid>.\n"
         "    -k, --kill      Kill the process specified by <pid>.\n"
         "    -?, --help      Display this help message.\n");
}

NTSTATUS Error(_In_ NTSTATUS Status)
{
    PrintError("Error: Status = 0x%08lX\n", Status);
    return Status;
}


/**
 * @brief
 * Registers the created (and suspended) process with the Win32 subsystem.
 *
 *         !!   ONLY THE BARE INFORMATION IS REGISTERED,  !!
 *         !! WITHOUT SUPPORT FOR SxS ACTIVATION CONTEXTS !!
 *
 * The current function is also limited to Windows XP/2003.
 * Windows Vista+ is somewhat able to support starting Win32 processes without
 * pre-registration with the Win32 subsystem/CSR. The reason is that NT threads
 * can be lazy-registered by CSRSS when necessary (when a CSR call is made).
 *
 * In addition, the size of the BASE_API_MESSAGE structure sent to CSR is
 * version-specific; currently no attempt to make the code working on Vista+
 * has been made.
 *
 * @param[in]   ImageInformation
 * Pointer to the process' image information structure.
 *
 * @param[in]   RemotePeb
 * Pointer to the process' environment block structure.
 *
 * @param[in]   CreationFlags
 * The creation flags used for starting the process.
 * Should match those provided to NativeCreateProcess_U().
 *
 * @param[in]   ProcessInformation
 * Pointer to a structure that specifies identification information
 * about the new process. (The one returned by NativeCreateProcess_U().)
 *
 * @return
 * STATUS_SUCCESS if the process was successfully registered,
 * or an error NTSTATUS code otherwise.
 **/
static NTSTATUS
RegisterWithWin32(
    _In_ PSECTION_IMAGE_INFORMATION ImageInformation,
    _In_ PPEB RemotePeb,
    _In_ ULONG CreationFlags,
    _In_ PRTL_USER_PROCESS_INFORMATION ProcessInformation // LPPROCESS_INFORMATION pProcessInformation
    )
{
#define AddToHandle(x,y)       ((x) = (HANDLE)((ULONG_PTR)(x) | (y)))
#define RemoveFromHandle(x,y)  ((x) = (HANDLE)((ULONG_PTR)(x) & ~(y)))

    BASE_API_MESSAGE ApiMessage = {0};
    PBASE_CREATE_PROCESS CreateProcessMsg = &ApiMessage.Data.CreateProcessRequest;

    /* FIXME: For the time being: Ignore registration if we are on Vista+ */
    //if (SharedUserData->NtMajorVersion >= 6)
    //    return STATUS_UNSUCCESSFUL; // STATUS_REQUEST_ABORTED; // STATUS_ALREADY_REGISTERED;

#if 0 // Disabled, let's allow any image to be registered with the Win32 subsystem!
    /* Check if this isn't a Win32 image */
    if ((ImageInformation->SubSystemType != IMAGE_SUBSYSTEM_WINDOWS_GUI) &&
        (ImageInformation->SubSystemType != IMAGE_SUBSYSTEM_WINDOWS_CUI))
    {
        /* We don't support anything else than Win32 images */
        return STATUS_INVALID_IMAGE_FORMAT;
    }
#endif

    /* Begin filling out the CSRSS message, first with our IDs and handles */
    CreateProcessMsg->ProcessHandle = ProcessInformation->ProcessHandle;
    CreateProcessMsg->ThreadHandle = ProcessInformation->ThreadHandle;
    CreateProcessMsg->ClientId = ProcessInformation->ClientId;

    /* Write the remote PEB address */
    CreateProcessMsg->PebAddressNative = RemotePeb;
#ifdef _WIN64
    PrintWarning("TODO: WOW64 is not supported yet\n");
    CreateProcessMsg->PebAddressWow64 = 0;
#else
    CreateProcessMsg->PebAddressWow64 = (ULONG)RemotePeb;
#endif

    /* Now check what kind of architecture this image was made for */
    switch (ImageInformation->Machine)
    {
        /* IA32, IA64 and AMD64 are supported in Server 2003 */
        case IMAGE_FILE_MACHINE_I386:
            CreateProcessMsg->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
            break;
        case IMAGE_FILE_MACHINE_IA64:
            CreateProcessMsg->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_IA64;
            break;
        case IMAGE_FILE_MACHINE_AMD64:
            CreateProcessMsg->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
            break;

        /* Anything else results in image unknown -- but no failure */
        default:
            PrintWarning("No mapping for ImageInformation->Machine == %04x\n",
                         ImageInformation->Machine);
            CreateProcessMsg->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
            break;
    }

    /* Write the input creation flags except any debugger-related flags */
    CreateProcessMsg->CreationFlags = CreationFlags &
                                      ~(DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);

    /* CSRSS needs to know if this is a GUI app or not */
    if (ImageInformation->SubSystemType == IMAGE_SUBSYSTEM_WINDOWS_GUI)
    {
        /*
         * For GUI apps we turn on the 2nd bit. This allow CSRSS server dlls
         * (basesrv in particular) to know whether or not this is a GUI or a
         * TUI application.
         */
        AddToHandle(CreateProcessMsg->ProcessHandle, 2);

        /* Also check if the parent (we the launcher) is also a GUI process */
        if (g_NtHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
        {
            /* Let it know that it should display the hourglass mouse cursor */
            AddToHandle(CreateProcessMsg->ProcessHandle, 1);
        }
    }

#if 0
    /* For all apps, if this flag is on, the hourglass mouse cursor is shown.
     * Likewise, the opposite holds as well, and no-feedback has precedence. */
    if (StartupInfo.dwFlags & STARTF_FORCEONFEEDBACK)
        AddToHandle(CreateProcessMsg->ProcessHandle, 1);
    if (StartupInfo.dwFlags & STARTF_FORCEOFFFEEDBACK)
        RemoveFromHandle(CreateProcessMsg->ProcessHandle, 1);
#endif

    /* We are finally ready to call CSRSS to tell it about our new process! */
    //
    // FIXME: The Joke: at least on Windows 7 x64 (and maybe others?),
    // there seems to be a bug in the WoW64 ntdll.dll:
    //
    // - On input, the ApiMessage is correctly understood as being the x86
    //   version of the structure, using the x86 version of PORT_MESSAGE.
    //
    // - On output, however, the PORT_MESSAGE is considered to be the x64 one!
    //   Hence, the Status return value, that should be stored in
    //   BASE_API_MESSAGE::Status (i.e. CSR_API_MESSAGE::Status), offset 0x20,
    //   gets stored in BASE_API_MESSAGE.Data.CreateProcessRequest.ClientId.UniqueThread,
    //   offset 0x34 !!
    //
    // See https://www.geoffchappell.com/studies/windows/win32/csrsrv/api/apireqst/api_msg.htm
    // for the x86 and x64 layouts.
    //
    // TODO: See this post: http://www.rohitab.com/discuss/topic/30203-executing-user-processes-from-kernel-mode/?p=10038369
    //
    NTSTATUS Status =
    CsrClientCallServer((PCSR_API_MESSAGE)&ApiMessage,
                        NULL,
                        CSR_CREATE_API_NUMBER(BASESRV_SERVERDLL_INDEX,
                                              BasepCreateProcess),
                        sizeof(*CreateProcessMsg));
    return /*ApiMessage.*/Status;

#undef RemoveFromHandle
#undef AddToHandle
}

/**
 * @brief
 * The magic routine!
 * Starts the specified process using NTDLL's Rtl* functions only.
 * This routine supports Native processes, and barely Win32 ones.
 *
 * @param[in]   ProcessName
 * Optional pointer to the executable image name to start. If none
 * is specified, the function uses the provided @p CommandLine.
 *
 * @param[in]   CommandLine
 * Optional pointer to the command line to be used for the process.
 *
 * @param[in]   CreationFlags
 * @param[in]   debug, suspend, defaultDllPath, defaultEnvironment
 * Flags specifying how to create the process. TBD.
 *
 * @param[out]  ProcessInformation
 * A pointer to a structure that receives identification information
 * about the new process.
 * The handles in this structure (@p ProcessHandle and @p ThreadHandle)
 * must be closed with @p NtClose() once they are no longer needed.
 *
 * @return
 * STATUS_SUCCESS if the process was successfully created,
 * or an error NTSTATUS code otherwise.
 **/
static NTSTATUS
NativeCreateProcess_U(
    _In_opt_ PCUNICODE_STRING ProcessName,
    _In_opt_ PCUNICODE_STRING CommandLine,
    _In_ ULONG CreationFlags,
    _In_ BOOLEAN debug,
    _In_ BOOLEAN suspend,
    _In_ BOOLEAN defaultDllPath,
    _In_ BOOLEAN defaultEnvironment,
    _Out_ PRTL_USER_PROCESS_INFORMATION ProcessInformation // LPPROCESS_INFORMATION pProcessInformation
    )
{
    NTSTATUS Status;
    PPEB RemotePeb = NULL;

    RtlZeroMemory(ProcessInformation, sizeof(*ProcessInformation));

    /* Initialize a default DLL path, if requested */
    UNICODE_STRING DllPath = {0};
    WCHAR DllPathBuffer[MAX_PATH + sizeof(";") + MAX_PATH + sizeof("\\system32")];
    if (defaultDllPath)
    {
        // UNICODE_STRING SystemRoot;
        // RtlInitUnicodeString(&SystemRoot, SharedUserData->NtSystemRoot);

        /* Allocate an empty string for the path */
        // ULONG Length = SystemRoot.MaximumLength + sizeof(L";") +
        //                SystemRoot.MaximumLength + sizeof(L"\\system32");
        RtlInitEmptyUnicodeString(&DllPath,
                                  DllPathBuffer, // RtlAllocateHeap(RtlGetProcessHeap(), 0, Length),
                                  sizeof(DllPathBuffer)); // (USHORT)Length);
        if (DllPath.Buffer)
        {
            /* Append SystemRoot and SystemRoot\system32,
             * using the SystemRoot from the shared user data string */
            if (
                // !NT_SUCCESS(Status = RtlAppendUnicodeStringToString(&DllPath, &SystemRoot)) ||
                !NT_SUCCESS(Status = RtlAppendUnicodeToString(&DllPath, SharedUserData->NtSystemRoot)) ||
                !NT_SUCCESS(Status = RtlAppendUnicodeToString(&DllPath, L";")) ||
                // !NT_SUCCESS(Status = RtlAppendUnicodeStringToString(&DllPath, &SystemRoot)) ||
                !NT_SUCCESS(Status = RtlAppendUnicodeToString(&DllPath, SharedUserData->NtSystemRoot)) ||
                !NT_SUCCESS(Status = RtlAppendUnicodeToString(&DllPath, L"\\system32"))
                )
            {
                /* Fail if there was a problem */
                PrintError("Unable to initialize default DLL path (Status: 0x%08lx)\n", Status);
                return Status;
            }
        }
    }

    /* Initialize a default environment, if requested */
    PWCHAR DefaultEnvironment;
    if (defaultEnvironment)
    {
        Status = RtlCreateEnvironment(TRUE, &DefaultEnvironment);
        if (!NT_SUCCESS(Status))
        {
            /* Fail if there was a problem */
            PrintError("Unable to create default environment (Status: 0x%08lx)\n", Status);
            // if (defaultDllPath)
            //     RtlFreeUnicodeString(&DllPath);
            return Status;
        }
    }

    /* Create parameters for the target process.
     *
     * - Parameters automatically inherited from the caller, if set to NULL:
     *   DllPath, CurrentDirectory, Environment
     *
     * - Parameters not automatically inherited: all the others
     *   (especially the handles). They need to be copied manually.

     */
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    Status = RtlCreateProcessParameters(&ProcessParameters,
                                        (PUNICODE_STRING)ProcessName,
                                        // DllPath (NULL: Inherit from caller)
                                        defaultDllPath ? &DllPath : NULL,
                                        // CurrentDirectory (Inherit from caller)
                                        NULL,
                                        (PUNICODE_STRING)CommandLine,
                                        // Environment (NULL: Inherit from caller)
                                        defaultEnvironment ? DefaultEnvironment : NULL,
                                        // WindowTitle (Use process path)
                                        (PUNICODE_STRING)ProcessName,
                                        // DesktopInfo (Use caller's)
                                        &g_ProcessParameters->DesktopInfo,
                                        NULL,   // ShellInfo    (None)
                                        NULL);  // RuntimeInfo  (None)
    if (!NT_SUCCESS(Status))
    {
        // if (defaultDllPath)
        //     RtlFreeUnicodeString(&DllPath);
        if (defaultEnvironment)
            RtlDestroyEnvironment(DefaultEnvironment);
        return Status;
    }
// TODO dev: Verify that ProcessParameters contains the default values for DllPath, etc.
    RtlNormalizeProcessParams(ProcessParameters);
    RtlDeNormalizeProcessParams(ProcessParameters);

    // /* Inherit the debug flag that was passed to ourselves */
    // ProcessParameters->DebugFlags = SmpDebug;

#if 0
    /* Write new parameters */
    ProcessParameters->StartingX = StartupInfo->dwX;
    ProcessParameters->StartingY = StartupInfo->dwY;
    ProcessParameters->CountX = StartupInfo->dwXSize;
    ProcessParameters->CountY = StartupInfo->dwYSize;
    ProcessParameters->CountCharsX = StartupInfo->dwXCountChars;
    ProcessParameters->CountCharsY = StartupInfo->dwYCountChars;
    ProcessParameters->FillAttribute = StartupInfo->dwFillAttribute;
    ProcessParameters->WindowFlags = StartupInfo->dwFlags;
    ProcessParameters->ShowWindowFlags = StartupInfo->wShowWindow;
#endif

#if 0
    /* Inherit console handles */
    ProcessParameters->ConsoleHandle  = g_ProcessParameters->ConsoleHandle;
    ProcessParameters->StandardInput  = g_ProcessParameters->StandardInput;
    ProcessParameters->StandardOutput = g_ProcessParameters->StandardOutput;
    ProcessParameters->StandardError  = g_ProcessParameters->StandardError;
    // TODO: Needs NtDuplicateObject; do this for IMAGE_SUBSYSTEM_WINDOWS_CUI
    // (see kernel32!proc.c:StuffStdHandle)

    // ProcessParameters->ConsoleFlags = 1; // Only if creating a new process group.
#endif

    /* Now create the process in suspended state */
    ProcessInformation->Size = sizeof(*ProcessInformation); // Set the size field as required.
    Status = RtlCreateUserProcess((PUNICODE_STRING)ProcessName,
                                  OBJ_CASE_INSENSITIVE,
                                  ProcessParameters,
                                  NULL,
                                  NULL,
                                  NULL, // ParentProcess  (Same as NtCurrentProcess())
                                  TRUE, // InheritHandles (Inherit from caller)
                                  NULL, // DebugPort
                                  NULL, // ExceptionPort
                                  ProcessInformation);

    RtlDestroyProcessParameters(ProcessParameters);
    // if (defaultDllPath)
    //     RtlFreeUnicodeString(&DllPath);
    if (defaultEnvironment)
        RtlDestroyEnvironment(DefaultEnvironment);

    if (!NT_SUCCESS(Status))
        return Status;

    PSECTION_IMAGE_INFORMATION ImageInformation = &ProcessInformation->ImageInformation;
#if 0
    /* Check if this isn't a Native or Windows image */
    if ((ImageInformation->SubSystemType != IMAGE_SUBSYSTEM_NATIVE)      &&
        (ImageInformation->SubSystemType != IMAGE_SUBSYSTEM_WINDOWS_GUI) &&
        (ImageInformation->SubSystemType != IMAGE_SUBSYSTEM_WINDOWS_CUI))
    {
        /* We don't support anything else than Native or Windows */
        Status = STATUS_INVALID_IMAGE_FORMAT;
        NtTerminateProcess(ProcessInformation->ProcessHandle, Status);
        (void)NtClose(ProcessInformation->ThreadHandle);
        (void)NtClose(ProcessInformation->ProcessHandle);
        return Status;
    }
#endif

    if (!RemotePeb)
    {
        /* Retrieve the process' PEB via its information */
        PROCESS_BASIC_INFORMATION ProcessBasicInfo;
        Status = NtQueryInformationProcess(ProcessInformation->ProcessHandle,
                                           ProcessBasicInformation,
                                           &ProcessBasicInfo,
                                           sizeof(ProcessBasicInfo),
                                           NULL);
        if (!NT_SUCCESS(Status))
            PrintWarning("Could not query Process Info for PEB (Status: 0x%08lx)\n", Status);
        else
            RemotePeb = ProcessBasicInfo.PebBaseAddress;
    }

    // TODO: Consider "forcing" registration with Win32 only if requested?
    /*
     * For Windows <= 2003 ONLY:
     * If we launch a Win32 (GUI or CUI) process, we need to inform CSRSS about
     * the process being started, so that it can later connect successfully to
     * CSRSS (kernel32!DllMain(DLL_PROCESS_ATTACH) invokes CsrClientConnectToServer()).
     * Otherwise, the connection will be refused.
     *
     * **NOTE:** This also implies that *WE* must be a Win32 process connected
     * to CSRSS, in order to be able to do that!
     */
    if ((ImageInformation->SubSystemType == IMAGE_SUBSYSTEM_WINDOWS_GUI) ||
        (ImageInformation->SubSystemType == IMAGE_SUBSYSTEM_WINDOWS_CUI))
    {
        BOOLEAN IsGuiProcess = (ImageInformation->SubSystemType == IMAGE_SUBSYSTEM_WINDOWS_GUI);
        printf("Win32 %s process detected, registering with CSRSS\n",
               IsGuiProcess ? "GUI" : "TUI");

        /* Register with Win32/CSR and check if it failed to accept ownership of the new Win32 process */
        Status = RegisterWithWin32(ImageInformation, RemotePeb, CreationFlags, ProcessInformation);
        if (!NT_SUCCESS(Status))
        {
            /* Instead of terminating the process, ignore the failure and hope for the best... */
            PrintError("Failed to tell CSRSS about new process (Status: 0x%08lx); ignoring...\n", Status);
        }
    }

#if 0 // If we use LPPROCESS_INFORMATION instead
    pProcessInformation->hProcess = ProcessInformation.ProcessHandle;
    pProcessInformation->hThread = ProcessInformation.ThreadHandle;
    pProcessInformation->dwProcessId = HandleToUlong(ProcessInformation.ClientId.UniqueProcess);
    pProcessInformation->dwThreadId = HandleToUlong(ProcessInformation.ClientId.UniqueThread);
#endif
    Status = STATUS_SUCCESS;

    return Status;
}

/**
 * @brief
 * This function has the same purpose as @p NativeCreateProcess_U(),
 * but uses NULL-terminated strings instead.
 *
 * @see NativeCreateProcess_U()
 **/
/*static*/ NTSTATUS
NativeCreateProcess(
    _In_ PCWSTR ProcessName,
    _In_ PCWSTR CommandLine,
    _In_ ULONG CreationFlags,
    _In_ BOOLEAN debug,
    _In_ BOOLEAN suspend,
    _In_ BOOLEAN defaultDllPath,
    _In_ BOOLEAN defaultEnvironment,
    _Out_ PRTL_USER_PROCESS_INFORMATION ProcessInformation // LPPROCESS_INFORMATION
    )
{
    UNICODE_STRING ProcessNameU, CommandLineU;

    RtlInitUnicodeString(&ProcessNameU, ProcessName);
    RtlInitUnicodeString(&CommandLineU, CommandLine);

    return NativeCreateProcess_U(&ProcessNameU,
                                 &CommandLineU,
                                 CreationFlags,
                                 debug,
                                 suspend,
                                 defaultDllPath,
                                 defaultEnvironment,
                                 ProcessInformation);
}

/**
 * @brief
 * Suspends or resume the specified process.
 *
 * @param[in]   ProcessId
 * The PID of the process to suspend or resume.
 *
 * @param[in]   DoSuspend
 * TRUE to suspend the process, or FALSE to resume it.
 *
 * @return
 * STATUS_SUCCESS if the process was successfully suspended or resumed,
 * or an error NTSTATUS code otherwise.
 **/
static NTSTATUS
NativeSuspendResumeProcess(
    _In_ ULONG ProcessId,
    _In_ BOOLEAN DoSuspend)
{
    NTSTATUS Status;
    HANDLE ProcessHandle;
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_INIT_OBJECT_ATTRIBUTES(NULL, 0);
    CLIENT_ID ClientId;

    /* Open the process by PID */
    ClientId.UniqueProcess = UlongToHandle(ProcessId);
    ClientId.UniqueThread = 0;
    Status = NtOpenProcess(&ProcessHandle,
                           PROCESS_SUSPEND_RESUME,
                           &ObjectAttributes,
                           &ClientId);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Suspend or resume the process */
    // printf("%s the process with PID %lu...\n",
    //        DoSuspend ? "Suspending" : "Resuming", ProcessId);
    Status = (DoSuspend ? NtSuspendProcess : NtResumeProcess)(ProcessHandle);

    NtClose(ProcessHandle);
    return Status;
}

/**
 * @brief
 * Terminates (kills) the specified process.
 *
 * @param[in]   ProcessId
 * The PID of the process to terminate.
 *
 * @return
 * STATUS_SUCCESS if the process was successfully terminated,
 * or an error NTSTATUS code otherwise.
 **/
static NTSTATUS
NativeTerminateProcess(
    _In_ ULONG ProcessId)
{
    NTSTATUS Status;
    HANDLE ProcessHandle;
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_INIT_OBJECT_ATTRIBUTES(NULL, 0);
    CLIENT_ID ClientId;

    /* Open the process by PID */
    ClientId.UniqueProcess = UlongToHandle(ProcessId);
    ClientId.UniqueThread = 0;
    Status = NtOpenProcess(&ProcessHandle,
                           PROCESS_TERMINATE,
                           &ObjectAttributes,
                           &ClientId);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Terminate the process and all of its threads */
    // printf("Killing the process with PID %lu...\n", ProcessId);
    Status = NtTerminateProcess(ProcessHandle, STATUS_UNSUCCESSFUL);

    NtClose(ProcessHandle);
    return Status;
}


/* Get the start and end of the next possibly-quoted command-line argument */
static BOOL
GetArg(
    _Out_ PCWCH* pStart,
    _Inout_ PCWCH* pEnd)
{
    BOOL bInQuotes = FALSE;
    PCWCH p = *pEnd;
    // p += wcsspn(p, L" \t");
    while (*p && iswspace(*p))
        ++p;
    if (!*p)
        return FALSE;
    *pStart = p;
    do
    {
        if (!bInQuotes && /*(*p == L' ' || *p == L'\t')*/ iswspace(*p))
            break;
        bInQuotes ^= (*p++ == L'"');
    } while (*p);
    *pEnd = p;
    return TRUE;
}

static NTSTATUS
ntmain(
    _In_ PUNICODE_STRING pImageFileName,
    _In_ PUNICODE_STRING pCommandLine)
{
    NTSTATUS Status;

    union
    {
        struct
        {
            BOOLEAN Debug   :1;
            BOOLEAN Suspend :1;
            BOOLEAN Resume  :1;
            BOOLEAN Kill    :1;
            BOOLEAN DefaultDllPath      :1;
            BOOLEAN DefaultEnvironment  :1;
        };
        BOOLEAN /*LOGICAL*/ AsBool;
    } Options = {0};
    ULONG CreationFlags = 0;
    ULONG ProcessId;

    UNREFERENCED_PARAMETER(pImageFileName);

    /* Parse the command line */

#define WCSICMP_SE(start, end, cmpstr)  _wcsnicmp((start), (cmpstr), (end)-(start))
#define  WCSCMP_SE(start, end, cmpstr)    wcsncmp((start), (cmpstr), (end)-(start))
#define CHECK_OPTION(shortName, longName, isLongOpt, start, end) \
    ( (!(isLongOpt) && (WCSICMP_SE(&(start)[1], (end), (shortName)) == 0)) || \
      ( (isLongOpt) &&  (WCSCMP_SE(&(start)[2], (end),  (longName)) == 0)) )

    UINT argc = 0;
    PCWSTR pArg = pCommandLine->Buffer;
    PCWCH pEnd; ///< End of current argument.

    /* Jump over the first argument, if any (should be our own
     * image name), i.e. it doesn't start with a whitespace */
    if (*pArg && !iswspace(*pArg))
        argc += (GetArg(&pArg, &pArg) ? 1 : 0);

    for (; *pArg; pArg = pEnd)
    {
        /* Skip left whitespace */
        while (*pArg && iswspace(*pArg))
            ++pArg;
        if (!*pArg)
            break;

        /* We've got an argument */
        ++argc;

        /* Check for short form ("-x") or long form ("--xxx") option.
         * A long option must always start with a dash '-'. */
#if 0
        BOOLEAN bLongOpt = (pArg[0] == '-' && pArg[1] == '-');
        if (bLongOpt || (pArg[0] == '-' || pArg[0] == '/'))
        {
            /* We have an option */
        }
#else
        BOOLEAN bLongOpt;
        /* Short option form ("-x") */
        if ( (pArg[0] == '-' || pArg[0] == '/') &&
            !(pArg[1] == '-' || pArg[1] == '/') )
        {
            bLongOpt = FALSE;
        }
        /* Long option form ("--xxx") */
        else if (pArg[0] == '-' && pArg[1] == '-')
        {
            bLongOpt = TRUE;
        }
#endif
        else
        {
            /* We are out of options (they come first before
             * anything else, and cannot come after). */
            break;
        }
        /* Support *nix-like "double-dash" end-of-options
         * delimiter to disable further option processing. */
        if (bLongOpt && (!pArg[2] || iswspace(pArg[2])))
            break;

        /* Find the end of this current option,
         * at the first encountered whitespace */
        pEnd = pArg;
        while (*pEnd && !iswspace(*pEnd))
            ++pEnd;

        /* Help */
        if (CHECK_OPTION(L"?", L"help", bLongOpt, pArg, pEnd))
        {
            /* Set argc to special value case */
            argc = 1;
            break;
        }
        else
        /* Use default DLL path */
        if (CHECK_OPTION(L"p", L"default-dll-path", bLongOpt, pArg, pEnd))
        {
            Options.DefaultDllPath = TRUE;
        }
        else
        /* Use default environment */
        if (CHECK_OPTION(L"e", L"default-environ", bLongOpt, pArg, pEnd))
        {
            Options.DefaultEnvironment = TRUE;
        }
        else
        /* Pause for allowing the user to debug the process */
        if (CHECK_OPTION(L"d", L"debug", bLongOpt, pArg, pEnd))
        {
            Options.Debug = TRUE;
        }
        else
        /* Start the process in suspended mode, or suspend an existing process */
        if (CHECK_OPTION(L"s", L"suspend", bLongOpt, pArg, pEnd))
        {
            Options.Suspend = TRUE;
        }
        else
        /* Resume an existing process */
        if (CHECK_OPTION(L"r", L"resume", bLongOpt, pArg, pEnd))
        {
            Options.Resume = TRUE;
        }
        else
        /* Terminate (kill) an existing process */
        if (CHECK_OPTION(L"k", L"kill", bLongOpt, pArg, pEnd))
        {
            Options.Kill = TRUE;
        }
        else
        /* Unknown option */
        {
            PrintError("Unknown option: '%.*ws'\n"
                       "Type \"" PROGNAME " -?\" for usage.\n",
                       pEnd-pArg, pArg);
            return STATUS_UNSUCCESSFUL;
        }
    }
    /* Check for no arguments or for help */
    if (argc <= 1)
    {
        Usage();
        return STATUS_SUCCESS;
    }

    /* Check for incompatible parameters */
    if ((/* Options.Suspend || */ Options.Resume || Options.Kill) &&
        (Options.Debug || Options.Suspend || Options.DefaultDllPath || Options.DefaultEnvironment))
    {
        PrintError("Incompatible parameters specified.\n"
                   "Type \"" PROGNAME " -?\" for usage.\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Skip any remaining left whitespace */
    while (*pArg && iswspace(*pArg))
        ++pArg;


    /* Check whether we should start an executable,
     * or whether these are command options */

    if ( (Options.Suspend || Options.Resume || Options.Kill) &&
        !(Options.Debug || Options.DefaultDllPath || Options.DefaultEnvironment))
    {
        /* We might be running a command taking a process ID, try to get it */
        PWCHAR pszNext = NULL;
        ProcessId = wcstoul(pArg, &pszNext, 0);
        if (*pszNext && !iswspace(*pszNext))
        {
            /* We haven't got a valid process ID; set it
             * to an invalid value to distinguish the error */
            ProcessId = ULONG_MAX;
        }

        /* If resume or kill, the argument *MUST* be a valid PID.
         * If suspend, we don't know yet. */
        if (!Options.Suspend)
        {
            if (/*(Options.Resume || Options.Kill) &&*/ (ProcessId == ULONG_MAX))
            {
                PrintError("Invalid parameter: '%ws'\n"
                           "Type \"" PROGNAME " -?\" for usage.\n",
                           pArg);
                return STATUS_UNSUCCESSFUL;
            }
        }
        else if (ProcessId == ULONG_MAX)
        {
            /* We are actually trying to start an executable in suspended mode */
            goto doRunImage;
        }

        /* Should we suspend the specified process? */
        PCSTR Action = "n/a";
        if (Options.Suspend)
        {
            Action = "suspend";
            Status = NativeSuspendResumeProcess(ProcessId, TRUE);
        }
        /* Or resume it? */
        else if (Options.Resume)
        {
            Action = "resume";
            Status = NativeSuspendResumeProcess(ProcessId, FALSE);
        }
        /* Or kill it perhaps? */
        else if (Options.Kill)
        {
            Action = "terminate";
            Status = NativeTerminateProcess(ProcessId);
        }

        /* Check for errors */
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_INVALID_CID)
            {
                /* Process with given PID doesn't exist */
                PrintError("Invalid PID 0x%lX (%lu)\n", ProcessId, ProcessId);
            }
            else if (Status == STATUS_ACCESS_DENIED)
            {
                /* Requested access rights cannot be granted */
                PrintError("Could not grant %s access rights for process with PID 0x%lX (%lu)\n",
                           Action, ProcessId, ProcessId);
            }
            else
            {
                /* Any other error */
                PrintError("Could not %s the process with PID 0x%lX (%lu)\n",
                           Action, ProcessId, ProcessId);
            }
            return Error(Status);
        }

        if (Options.Suspend)
        {
            printf("Process 0x%lX (%lu) has been suspended.\n"
                   "You can resume it later by running:\n"
                   "  " PROGNAME " -r %lu\n",
                   ProcessId, ProcessId, ProcessId);
        }
        printf("Process 0x%lX (%lu) %s%s successfully.\n",
               ProcessId, ProcessId, Action, Options.Suspend ? "ed" : "d");
        return STATUS_SUCCESS;
    }

    /* We should then start the specified executable */
doRunImage:
    ; // Shut up GCC "error: a label can only be part of a statement and a declaration is not a statement"

    /* Because we don't use argc/argv but the direct command line string,
     * we can directly use that one for the new process! Excepting, that
     * we also need to isolate the executable image name too. */

    UNICODE_STRING CommandLine;
    RtlInitUnicodeString(&CommandLine, pArg);

    /* Retrieve the name of the (possibly-quoted) executable we want to start */
    pEnd = pArg;
    GetArg(&pArg, &pEnd);

    /* Unquote it if necessary */
    // FIXME: In principle we should collapse the quotes, etc.
    if ((*pArg == L'"') && (*(pEnd-1) == L'"'))
        ++pArg, --pEnd;
    /* Since RtlDosPathNameToNtPathName_U doesn't take in input a counted
     * UNICODE_STRING, but only a C string, we need to patch it with a NUL
     * to effectively "remove" the end-space or quote */
    WCHAR chr = *pEnd;
    *(PWCHAR)pEnd = UNICODE_NULL;

    UNICODE_STRING NtFileName;
    BOOLEAN Success = RtlDosPathNameToNtPathName_U(pArg, &NtFileName, NULL, NULL);

    /* Restore the character */
    *(PWCHAR)pEnd = chr;

    if (!Success)
    {
        /* Path was invalid */
        return Error(STATUS_OBJECT_PATH_SYNTAX_BAD);
    }

    /* And now, we are ready to create the process proper! */
    RTL_USER_PROCESS_INFORMATION ProcessInformation; // PROCESS_INFORMATION
    Status = NativeCreateProcess_U(&NtFileName,
                                   &CommandLine,
                                   CreationFlags,
                                   Options.Debug,
                                   Options.Suspend,
                                   Options.DefaultDllPath,
                                   Options.DefaultEnvironment,
                                   &ProcessInformation);
    if (!NT_SUCCESS(Status))
    {
        //_CRT_DISABLE_GCC_PRINTF_WARNINGS
        PrintError("Could not start process\n  '%wZ'\n"
                   "with command-line:\n  '%wZ'\n",
                   &NtFileName, &CommandLine);
        //_CRT_RESTORE_GCC_PRINTF_WARNINGS
        return Error(Status);
    }

    ProcessId = HandleToULong(ProcessInformation.ClientId.UniqueProcess);

    if (Options.Debug)
    {
        /* As we start the process for debugging, suspend it also.
         * We will then ask the user to resume the process, once a debugger
         * has been attached to it. */
        Options.Suspend = TRUE;
        printf("Process 0x%lX (%lu) has been started for debugging.\n"
               "Attach a debugger to it, then resume the process by running:\n"
               "  " PROGNAME " -r %lu\n",
               ProcessId, ProcessId, ProcessId);
    }
    if (Options.Suspend)
    {
        printf("Process 0x%lX (%lu) has been suspended.\n"
               "You can resume it later by running:\n"
               "  " PROGNAME " -r %lu\n",
               ProcessId, ProcessId, ProcessId);
    }
    else
    {
        Status = NtResumeThread(ProcessInformation.ThreadHandle, NULL);
        if (!NT_SUCCESS(Status))
        {
            PrintError("Could not resume process 0x%lX (%lu) (Status: 0x%08lx)\n",
                       ProcessId, ProcessId, Status);
        }
    }
    printf("Process 0x%lX (%lu) created successfully%s.\n",
           ProcessId, ProcessId, Options.Suspend ? " (Suspended)" : "");

    (void)NtClose(ProcessInformation.ThreadHandle);
    (void)NtClose(ProcessInformation.ProcessHandle);

    return STATUS_SUCCESS;
}


/**
 * @brief
 * Executable entry-point.
 *
 * @note
 * This is actually how the executable is started by the NTDLL loader,
 * whether or not it has been originally started from a Native or a Win32
 * (using kernel32!CreateProcess) process.
 * The usual Win32 (w)mainCRTStartup(void) entry-points are back-compatible
 * with NtProcessStartup(PPEB).
 *
 * However, as mentioned in comments:
 * https://stackoverflow.com/q/61684051#comment109119810_61685837
 * https://stackoverflow.com/q/61684051#comment109119837_61685837
 * (by Erik Sun),
 * only on Vista+ does the Win32 subsystem pass a non-NULL PEB parameter
 * to the function, while on Win2003 and below, no PEB parameter is given.
 * On the other hand the NTDLL loader always passes a PEB.
 **/
// int __cdecl (w)mainCRTStartup(void)
VOID NTAPI
NtProcessStartup(
    _In_ PPEB Peb)
{
    NTSTATUS Status;
    PUNICODE_STRING pImageFileName, pCommandLine;

    DbgPrint("%s(0x%p)\n", __FUNCTION__, Peb);
    //if (!Peb)
    Peb = NtCurrentPeb();

    /* Retrieve and normalize the process parameters */
    g_ProcessParameters = RtlNormalizeProcessParams(Peb->ProcessParameters);
    if (!g_ProcessParameters)
    {
        Status = STATUS_UNSUCCESSFUL;
        DbgPrint("Error: Status = 0x%08lX\n", Status);
        goto Quit;
    }

    /* Check whether we are marked as a Native image ourselves */
    g_NtHeader = RtlImageNtHeader(Peb->ImageBaseAddress);
    if (!g_NtHeader)
    {
        /* The image isn't valid */
        // DbgPrint("Invalid image\n");
        Status = STATUS_INVALID_IMAGE_FORMAT;
        DbgPrint("Error: Status = 0x%08lX\n", Status);
        goto Quit;
    }
    g_IsNative = (g_NtHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_NATIVE);

    /* Initialize the minimal NT CRT IO functions support */
    InitNtCrtIO();

    /* Save the image name and command line */
    pImageFileName = &g_ProcessParameters->ImagePathName;
    pCommandLine = &g_ProcessParameters->CommandLine;
#if 0
    /* If we don't have a command line, use the image path instead */
    if (!pCommandLine->Buffer || !pCommandLine->Length)
        pCommandLine = pImageFileName;
#endif

    /* Call our ntmain() function */
    Status = ntmain(pImageFileName, pCommandLine);

    FiniNtCrtIO();
Quit:
    /* We're done here */
    (void)NtTerminateProcess(NtCurrentProcess(), Status);
}
