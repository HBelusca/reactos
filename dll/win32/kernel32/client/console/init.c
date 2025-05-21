/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/console/init.c
 * PURPOSE:         Console API Client Initialization
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Aleksey Bragin (aleksey@reactos.org)
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include <k32.h>

// For Control Panel Applet
#include <cpl.h>

#define NDEBUG
#include <debug.h>


/* GLOBALS ********************************************************************/

RTL_CRITICAL_SECTION ConsoleLock;
BOOLEAN ConsoleInitialized = FALSE;
extern HANDLE InputWaitHandle;

static const PWSTR DefaultConsoleTitle = L"ReactOS Console";

/* FUNCTIONS ******************************************************************/

DWORD
WINAPI
PropDialogHandler(IN LPVOID lpThreadParameter)
{
    // NOTE: lpThreadParameter corresponds to the client shared section handle.

    NTSTATUS Status = STATUS_SUCCESS;
    HMODULE hConsoleApplet = NULL;
    APPLET_PROC CPlApplet;
    static BOOL AlreadyDisplayingProps = FALSE;
    WCHAR szBuffer[MAX_PATH];

    /*
     * Do not launch more than once the console property dialog applet,
     * or (albeit less probable), if we are not initialized.
     */
    if (!ConsoleInitialized || AlreadyDisplayingProps)
    {
        /* Close the associated client shared section handle if needed */
        if (lpThreadParameter)
            CloseHandle((HANDLE)lpThreadParameter);

        return STATUS_UNSUCCESSFUL;
    }

    AlreadyDisplayingProps = TRUE;

    /* Load the control applet */
    GetSystemDirectoryW(szBuffer, MAX_PATH);
    wcscat(szBuffer, L"\\console.dll");
    hConsoleApplet = LoadLibraryW(szBuffer);
    if (hConsoleApplet == NULL)
    {
        DPRINT1("Failed to load console.dll\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Quit;
    }

    /* Load its main function */
    CPlApplet = (APPLET_PROC)GetProcAddress(hConsoleApplet, "CPlApplet");
    if (CPlApplet == NULL)
    {
        DPRINT1("Error: console.dll misses CPlApplet export\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Quit;
    }

    /* Initialize the applet */
    if (CPlApplet(NULL, CPL_INIT, 0, 0) == FALSE)
    {
        DPRINT1("Error: failed to initialize console.dll\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Quit;
    }

    /* Check the count */
    if (CPlApplet(NULL, CPL_GETCOUNT, 0, 0) != 1)
    {
        DPRINT1("Error: console.dll returned unexpected CPL count\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Quit;
    }

    /*
     * Start the applet. For Windows compatibility purposes we need
     * to pass the client shared section handle (lpThreadParameter)
     * via the hWnd parameter of the CPlApplet function.
     */
    CPlApplet((HWND)lpThreadParameter, CPL_DBLCLK, 0, 0);

    /* We have finished */
    CPlApplet(NULL, CPL_EXIT, 0, 0);

Quit:
    if (hConsoleApplet) FreeLibrary(hConsoleApplet);
    AlreadyDisplayingProps = FALSE;
    return Status;
}


/**
 * @brief
 * Parses a legacy ProgMan shell information string, that is found in either
 * the STARTUPINFO::lpReserved or the RTL_USER_PROCESS_PARAMETERS::ShellInfo.Buffer
 * UNICODE string pointers.
 *
 * The string has the following format:
 *   "dde.#,hotkey.#,ntvdm.#"
 * (the `ntvdm.` part is optional and is only specified if a DOS or Win16/WoW
 * process is started for NTVDM).
 * Each field is comma-separated, where the hash-signs `#` are decimal values.
 *
 * @see https://www.catch22.net/tuts/system/undocumented-createprocess/
 * @note Contrary to what is mentioned in the article, the numerical values
 *       are *NOT* in hexadecimal format, but are decimal values.
 **/
static INT
ParseShellInfo(
    _In_ PCWSTR pszShellInfo,
    _In_ PCWSTR pszKeyword)
{
#define IS_FIELD_SEPARATOR(ch) ((ch) == L',' || (ch) == L' ' || (ch) == L'\t')
    INT Value = 0;

    // if (!pszShellInfo) return 0;
    // if (!*pszShellInfo) return 0;
    // if (!*pszKeyword) return 0;
    // if (pszKeyword[wcslen(pszKeyword)-1] != L'.') return 0;

    _SEH2_TRY
    {
        PCWSTR pch = pszShellInfo;
        // wcschr(pszShellInfo, L',');
        while ((pch = wcsstr(pch, pszKeyword)))
        {
            if (pch == pszShellInfo ||
                /* Check whether the keyword was preceded by a separator:
                 * the comma, but also allow for natural whitespace characters */
                (pch > pszShellInfo && IS_FIELD_SEPARATOR(*(pch-1))))
            {
                /* Keyword indeed either starts the string
                 * or follows a separator */
                PWCHAR endptr;

                /* Advance and check for a numerical value.
                 * Accept whatever value we retrieve, but warn
                 * in case something weird happened */
                pch += wcslen(pszKeyword);
                Value = wcstol(pch, &endptr, 0);
                if (endptr && *endptr != UNICODE_NULL && !IS_FIELD_SEPARATOR(*endptr))
                    DPRINT1("Potentially invalid shell-info string '%S'\n", pszShellInfo);
                break;
            }

            /* Keyword was a "substring" but invalid, as it was
             * part of something else; advance and retry again */
            pch += wcslen(pszKeyword);
            // ++pch;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    return Value;
#undef IS_FIELD_SEPARATOR
}


/*
 * NOTE:
 * The "LPDWORD Length" parameters point on input to the maximum size of
 * the buffers that can hold data (if != 0), and on output they hold the
 * real size of the data. If "Length" are == 0 on input, then on output
 * they receive the full size of the data.
 * The "LPWSTR* lpTitle" parameter has a double meaning:
 * - when "CaptureTitle" is TRUE, data is copied to the buffer pointed
 *   by the pointer (*lpTitle).
 * - when "CaptureTitle" is FALSE, "*lpTitle" is set to the address of
 *   the source data.
 */
VOID
SetUpConsoleInfo(IN BOOLEAN CaptureTitle,
                 IN OUT LPDWORD pTitleLength,
                 IN OUT LPWSTR* lpTitle OPTIONAL,
                 IN OUT LPDWORD pDesktopLength,
                 IN OUT LPWSTR* lpDesktop OPTIONAL,
                 IN OUT PCONSOLE_START_INFO ConsoleStartInfo)
{
    PRTL_USER_PROCESS_PARAMETERS Parameters = NtCurrentPeb()->ProcessParameters;
    DWORD Length;

    /* Initialize the fields */

    ConsoleStartInfo->IconIndex = 0;
    ConsoleStartInfo->hIcon   = NULL;
    ConsoleStartInfo->hIconSm = NULL;
    ConsoleStartInfo->dwStartupFlags = Parameters->WindowFlags;
    ConsoleStartInfo->nFont = 0;
    ConsoleStartInfo->nInputBufferSize = 0;
    ConsoleStartInfo->uCodePage = GetOEMCP();

    if (lpTitle)
    {
        LPWSTR Title;

        /* If we don't have any title, use the default one */
        if (Parameters->WindowTitle.Buffer == NULL)
        {
            Title  = DefaultConsoleTitle;
            Length = lstrlenW(DefaultConsoleTitle) * sizeof(WCHAR); // sizeof(DefaultConsoleTitle);
        }
        else
        {
            Title  = Parameters->WindowTitle.Buffer;
            Length = Parameters->WindowTitle.Length;
        }

        /* Retrieve the needed buffer size */
        Length += sizeof(WCHAR);
        if (*pTitleLength > 0) Length = min(Length, *pTitleLength);
        *pTitleLength = Length;

        /* Capture the data if needed, or, return a pointer to it */
        if (CaptureTitle)
        {
            /*
             * Length is always >= sizeof(WCHAR). Copy everything but the
             * possible trailing NULL character, and then NULL-terminate.
             */
            Length -= sizeof(WCHAR);
            RtlCopyMemory(*lpTitle, Title, Length);
            (*lpTitle)[Length / sizeof(WCHAR)] = UNICODE_NULL;
        }
        else
        {
            *lpTitle = Title;
        }
    }
    else
    {
        *pTitleLength = 0;
    }

    if (lpDesktop && Parameters->DesktopInfo.Buffer && *Parameters->DesktopInfo.Buffer)
    {
        /* Retrieve the needed buffer size */
        Length = Parameters->DesktopInfo.Length + sizeof(WCHAR);
        if (*pDesktopLength > 0) Length = min(Length, *pDesktopLength);
        *pDesktopLength = Length;

        /* Return a pointer to the data */
        *lpDesktop = Parameters->DesktopInfo.Buffer;
    }
    else
    {
        *pDesktopLength = 0;
        if (lpDesktop) *lpDesktop = NULL;
    }

    if (Parameters->WindowFlags & STARTF_USEFILLATTRIBUTE)
    {
        ConsoleStartInfo->wFillAttribute = (WORD)Parameters->FillAttribute;
    }
    if (Parameters->WindowFlags & STARTF_USECOUNTCHARS)
    {
        ConsoleStartInfo->dwScreenBufferSize.X = (SHORT)Parameters->CountCharsX;
        ConsoleStartInfo->dwScreenBufferSize.Y = (SHORT)Parameters->CountCharsY;
    }
    if (Parameters->WindowFlags & STARTF_USESHOWWINDOW)
    {
        ConsoleStartInfo->wShowWindow = (WORD)Parameters->ShowWindowFlags;
    }
    if (Parameters->WindowFlags & STARTF_USEPOSITION)
    {
        ConsoleStartInfo->dwWindowOrigin.X = (SHORT)Parameters->StartingX;
        ConsoleStartInfo->dwWindowOrigin.Y = (SHORT)Parameters->StartingY;
    }
    if (Parameters->WindowFlags & STARTF_USESIZE)
    {
        ConsoleStartInfo->dwWindowSize.X = (SHORT)Parameters->CountX;
        ConsoleStartInfo->dwWindowSize.Y = (SHORT)Parameters->CountY;
    }

    /*
     * Retrieve private shell-specific information: icon handle/index
     * and window activation hotkey.
     * See: https://www.catch22.net/tuts/system/undocumented-createprocess/
     *
     * For each parameter, check first whether the new-shell (WinNT4+)
     * information has been set in the STARTUPINFO structure; if not,
     * check for their presence in the legacy (WinNT3.x) ProgMan
     * string buffer (NOTE: ShellInfo.Buffer is NULL-terminated).
     *
     * Note that Windows does it in a broken manner: this information
     * is retrieved *ONLY* if the legacy ShellInfo string is specified.
     * If so, it looks for a ProgMan icon index, without looking at the
     * newer field. Then, it looks for a hotkey, but this time, checks
     * first the newer field, before looking if nothing has been specified,
     * at the legacy ShellInfo string.
     * In ReactOS we don't do this, but instead use a more sane logic.
     */
    /* NOTE: StandardOutput (and hIcon) may actually specify a monitor handle
     * where to open the console (Win2000+ feature); this ambiguity will be
     * resolved in CONSRV side. */
__debugbreak();
    if (Parameters->WindowFlags & STARTF_SHELLPRIVATE)
        ConsoleStartInfo->hIcon = (HICON)Parameters->StandardOutput;
    else if (Parameters->ShellInfo.Buffer)
        ConsoleStartInfo->IconIndex = ParseShellInfo(Parameters->ShellInfo.Buffer, L"dde.");

    if (Parameters->WindowFlags & STARTF_USEHOTKEY)
        ConsoleStartInfo->dwHotKey = HandleToUlong(Parameters->StandardInput);
    else if (Parameters->ShellInfo.Buffer)
        ConsoleStartInfo->dwHotKey = ParseShellInfo(Parameters->ShellInfo.Buffer, L"hotkey.");
}


VOID
SetUpHandles(IN PCONSOLE_START_INFO ConsoleStartInfo)
{
    PRTL_USER_PROCESS_PARAMETERS Parameters = NtCurrentPeb()->ProcessParameters;

    if (ConsoleStartInfo->dwStartupFlags & STARTF_USEHOTKEY)
        Parameters->WindowFlags &= ~STARTF_USEHOTKEY;
    if (ConsoleStartInfo->dwStartupFlags & STARTF_SHELLPRIVATE)
        Parameters->WindowFlags &= ~STARTF_SHELLPRIVATE;

    /* We got the handles, let's set them */
    Parameters->ConsoleHandle = ConsoleStartInfo->ConsoleHandle;

    if ((ConsoleStartInfo->dwStartupFlags & STARTF_USESTDHANDLES) == 0)
    {
        Parameters->StandardInput  = ConsoleStartInfo->InputHandle;
        Parameters->StandardOutput = ConsoleStartInfo->OutputHandle;
        Parameters->StandardError  = ConsoleStartInfo->ErrorHandle;
    }
}


static BOOLEAN
IsConsoleApp(VOID)
{
    PIMAGE_NT_HEADERS ImageNtHeader = RtlImageNtHeader(NtCurrentPeb()->ImageBaseAddress);
    return (ImageNtHeader && (ImageNtHeader->OptionalHeader.Subsystem ==
                              IMAGE_SUBSYSTEM_WINDOWS_CUI));
}


static BOOLEAN
ConnectConsole(IN PWSTR SessionDir,
               IN PCONSRV_API_CONNECTINFO ConnectInfo,
               OUT PBOOLEAN InServerProcess)
{
    NTSTATUS Status;
    ULONG ConnectInfoSize = sizeof(*ConnectInfo);

    ASSERT(SessionDir);

    /* Connect to the Console Server */
    DPRINT("Connecting to the Console Server...\n");
    Status = CsrClientConnectToServer(SessionDir,
                                      CONSRV_SERVERDLL_INDEX,
                                      ConnectInfo,
                                      &ConnectInfoSize,
                                      InServerProcess);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to connect to the Console Server (Status %lx)\n", Status);
        return FALSE;
    }

    /* Nothing to do for server-to-server */
    if (*InServerProcess) return TRUE;

    /* Nothing to do if this is not a console app */
    if (!ConnectInfo->IsConsoleApp) return TRUE;

    /* Wait for the connection to finish */
    // Is ConnectInfo->ConsoleStartInfo.InitEvents aligned on handle boundary ????
    Status = NtWaitForMultipleObjects(MAX_INIT_EVENTS,
                                      ConnectInfo->ConsoleStartInfo.InitEvents,
                                      WaitAny, FALSE, NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    NtClose(ConnectInfo->ConsoleStartInfo.InitEvents[INIT_SUCCESS]);
    NtClose(ConnectInfo->ConsoleStartInfo.InitEvents[INIT_FAILURE]);
    if (Status != INIT_SUCCESS)
    {
        NtCurrentPeb()->ProcessParameters->ConsoleHandle = NULL;
        return FALSE;
    }

    return TRUE;
}


BOOLEAN
WINAPI
ConDllInitialize(IN ULONG Reason,
                 IN PWSTR SessionDir)
{
    NTSTATUS Status;
    PRTL_USER_PROCESS_PARAMETERS Parameters = NtCurrentPeb()->ProcessParameters;
    BOOLEAN InServerProcess = FALSE;
    CONSRV_API_CONNECTINFO ConnectInfo;

    if (Reason != DLL_PROCESS_ATTACH)
    {
        if ((Reason == DLL_THREAD_ATTACH) && IsConsoleApp())
        {
            /* Sync the new thread's LangId with the console's one */
            SetTEBLangID();
        }
        else if (Reason == DLL_PROCESS_DETACH)
        {
            /* Free our resources */
            if (ConsoleInitialized != FALSE)
            {
                ConsoleInitialized = FALSE;
                RtlDeleteCriticalSection(&ConsoleLock);
            }
        }

        return TRUE;
    }

    DPRINT("ConDllInitialize for: %wZ\n"
           "Our current console handles are: 0x%p, 0x%p, 0x%p 0x%p\n",
           &Parameters->ImagePathName,
           Parameters->ConsoleHandle,
           Parameters->StandardInput,
           Parameters->StandardOutput,
           Parameters->StandardError);

    /* Initialize our global console DLL lock */
    Status = RtlInitializeCriticalSection(&ConsoleLock);
    if (!NT_SUCCESS(Status)) return FALSE;
    ConsoleInitialized = TRUE;

    /* Show by default the console window when applicable */
    ConnectInfo.IsWindowVisible = TRUE;
    /* If this is a console app, a console will be created/opened */
    ConnectInfo.IsConsoleApp = IsConsoleApp();

    /* Do nothing if this is not a console app... */
    if (!ConnectInfo.IsConsoleApp)
    {
        DPRINT("Image is not a console application\n");
    }

    /*
     * Handle the special flags given to us by BasePushProcessParameters.
     */
    if (Parameters->ConsoleHandle == HANDLE_DETACHED_PROCESS)
    {
        /* No console to create */
        DPRINT("No console to create\n");
        /*
         * The new process does not inherit its parent's console and cannot
         * attach to the console of its parent. The new process can call the
         * AllocConsole function at a later time to create a console.
         */
        Parameters->ConsoleHandle = NULL;  // Do not inherit the parent's console.
        ConnectInfo.IsConsoleApp  = FALSE; // Do not create any console.
    }
    else if (Parameters->ConsoleHandle == HANDLE_CREATE_NEW_CONSOLE)
    {
        /* We'll get the real one soon */
        DPRINT("Creating a new separate console\n");
        /*
         * The new process has a new console, instead of inheriting
         * its parent's console.
         */
        Parameters->ConsoleHandle = NULL; // Do not inherit the parent's console.
    }
    else if (Parameters->ConsoleHandle == HANDLE_CREATE_NO_WINDOW)
    {
        /* We'll get the real one soon */
        DPRINT("Creating a new invisible console\n");
        /*
         * The process is a console application that is being run
         * without a console window. Therefore, the console handle
         * for the application is not set.
         */
        Parameters->ConsoleHandle   = NULL;  // Do not inherit the parent's console.
        ConnectInfo.IsWindowVisible = FALSE; // A console is created but is not shown to the user.
    }
    else
    {
        DPRINT("Using existing console: 0x%p\n", Parameters->ConsoleHandle);
    }

    /* Do nothing if this is not a console app... */
    if (!ConnectInfo.IsConsoleApp)
    {
        /* Do not inherit the parent's console if we are not a console app */
        Parameters->ConsoleHandle = NULL;
    }

    /* Now use the proper console handle */
    ConnectInfo.ConsoleStartInfo.ConsoleHandle = Parameters->ConsoleHandle;

    /* Initialize the console dispatchers */
    ConnectInfo.CtrlRoutine = ConsoleControlDispatcher;
    ConnectInfo.PropRoutine = PropDialogHandler;
    // ConnectInfo.ImeRoutine  = ImeRoutine;

    /* Set up the console properties */
    if (ConnectInfo.IsConsoleApp && Parameters->ConsoleHandle == NULL)
    {
        /*
         * We can set up the console properties only if we create a new one
         * (we do not inherit it from our parent).
         */

        LPWSTR ConsoleTitle = ConnectInfo.ConsoleTitle;

        ConnectInfo.TitleLength   = sizeof(ConnectInfo.ConsoleTitle);
        ConnectInfo.DesktopLength = 0; // SetUpConsoleInfo will give us the real length.

        SetUpConsoleInfo(TRUE,
                         &ConnectInfo.TitleLength,
                         &ConsoleTitle,
                         &ConnectInfo.DesktopLength,
                         &ConnectInfo.Desktop,
                         &ConnectInfo.ConsoleStartInfo);
        DPRINT("ConsoleTitle = '%S' - Desktop = '%S'\n",
               ConsoleTitle, ConnectInfo.Desktop);
    }
    else
    {
        ConnectInfo.TitleLength   = 0;
        ConnectInfo.DesktopLength = 0;
    }

    /* Initialize the Input EXE name */
    if (ConnectInfo.IsConsoleApp)
    {
        LPWSTR CurDir  = ConnectInfo.CurDir;
        LPWSTR AppName = ConnectInfo.AppName;

        InitExeName();

        ConnectInfo.CurDirLength  = sizeof(ConnectInfo.CurDir);
        ConnectInfo.AppNameLength = sizeof(ConnectInfo.AppName);

        SetUpAppName(TRUE,
                     &ConnectInfo.CurDirLength,
                     &CurDir,
                     &ConnectInfo.AppNameLength,
                     &AppName);
        DPRINT("CurDir = '%S' - AppName = '%S'\n",
               CurDir, AppName);
    }
    else
    {
        ConnectInfo.CurDirLength  = 0;
        ConnectInfo.AppNameLength = 0;
    }

    /*
     * Initialize Console Ctrl Handling, that needs to be supported by
     * all applications, especially because it is used at shutdown.
     */
    InitializeCtrlHandling();

    /* Connect to the Console Server */
    if (!ConnectConsole(SessionDir,
                        &ConnectInfo,
                        &InServerProcess))
    {
        // DPRINT1("Failed to connect to the Console Server (Status %lx)\n", Status);
        return FALSE;
    }

    /* If we are not doing server-to-server init and if this is a console app... */
    if (!InServerProcess && ConnectInfo.IsConsoleApp)
    {
        /* ... set the handles that we got */
        if (Parameters->ConsoleHandle == NULL)
            SetUpHandles(&ConnectInfo.ConsoleStartInfo);

        InputWaitHandle = ConnectInfo.ConsoleStartInfo.InputWaitHandle;

        /* Sync the current thread's LangId with the console's one */
        SetTEBLangID();
    }

    DPRINT("Console setup: 0x%p, 0x%p, 0x%p, 0x%p\n",
           Parameters->ConsoleHandle,
           Parameters->StandardInput,
           Parameters->StandardOutput,
           Parameters->StandardError);

    return TRUE;
}

/* EOF */
