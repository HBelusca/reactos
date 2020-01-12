/*
 * PROJECT:         ReactOS msgina.dll
 * FILE:            dll/win32/msgina/tui.c
 * PURPOSE:         ReactOS Logon GINA DLL
 * PROGRAMMER:      Hervé Poussineau (hpoussin@reactos.org)
 */

#include "msgina.h"

#include <wincon.h>

#include <ntstrsafe.h>
#include <reactos/buildno.h>

static BOOL
TUIInitialize(
    IN OUT PGINA_CONTEXT pgContext)
{
    TRACE("TUIInitialize(%p)\n", pgContext);

    /*
     * Winlogon initialized the console for us.
     * Since we are attached to it we can use its standard IO handles.
     */
    return TRUE;
}

static BOOL
TUIDisplayStatusMessage(
    IN PGINA_CONTEXT pgContext,
    IN HDESK hDesktop,
    IN DWORD dwOptions,
    IN PWSTR pTitle,
    IN PWSTR pMessage)
{
    static LPCWSTR newLine = L"\n";
    DWORD result;

    TRACE("TUIDisplayStatusMessage(%ws)\n", pMessage);

    UNREFERENCED_PARAMETER(pgContext);
    UNREFERENCED_PARAMETER(hDesktop);
    UNREFERENCED_PARAMETER(dwOptions);
    // UNREFERENCED_PARAMETER(pTitle);

/*******/
    if (pTitle)
    {
        WriteConsoleW(
            GetStdHandle(STD_OUTPUT_HANDLE),
            pTitle,
            wcslen(pTitle),
            &result,
            NULL) &&
        WriteConsoleW(
            GetStdHandle(STD_OUTPUT_HANDLE),
            newLine,
            wcslen(newLine),
            &result,
            NULL);
    }
/*******/

    return
        WriteConsoleW(
            GetStdHandle(STD_OUTPUT_HANDLE),
            pMessage,
            wcslen(pMessage),
            &result,
            NULL) &&
        WriteConsoleW(
            GetStdHandle(STD_OUTPUT_HANDLE),
            newLine,
            wcslen(newLine),
            &result,
            NULL);
}

static BOOL
TUIRemoveStatusMessage(
    IN PGINA_CONTEXT pgContext)
{
    UNREFERENCED_PARAMETER(pgContext);

    /* Nothing to do */
    return TRUE;
}

static BOOL
DisplayResourceText(
    IN UINT uIdResourceText,
    IN BOOL AddNewLine)
{
    static LPCWSTR newLine = L"\n";
    DWORD count;
    WCHAR Prompt[256];

    if (LoadStringW(hDllInstance, uIdResourceText, Prompt, _countof(Prompt)) == 0)
        return FALSE;

    if (!WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE),
                       Prompt, wcslen(Prompt),
                       &count, NULL))
    {
        return FALSE;
    }
    if (AddNewLine)
    {
        if (!WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE),
                           newLine, wcslen(newLine),
                           &count, NULL))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static VOID
TUIDisplaySASNotice(
    IN OUT PGINA_CONTEXT pgContext)
{
    TRACE("TUIDisplaySASNotice()\n");

    UNREFERENCED_PARAMETER(pgContext);

__debugbreak();
    DisplayResourceText(IDS_LOGGEDOUTSAS, TRUE);
    DisplayResourceText(IDS_PRESSCTRLALTDELETE, TRUE);
}

static INT
TUILoggedOnSAS(
    IN OUT PGINA_CONTEXT pgContext,
    IN DWORD dwSasType)
{
    TRACE("TUILoggedOnSAS()\n");

    UNREFERENCED_PARAMETER(pgContext);

    if (dwSasType != WLX_SAS_TYPE_CTRL_ALT_DEL)
    {
        /* Nothing to do for WLX_SAS_TYPE_TIMEOUT */
        return WLX_SAS_ACTION_NONE;
    }

    FIXME("FIXME: TUILoggedOnSAS(): Let's suppose the user wants to log off...\n");
    return WLX_SAS_ACTION_LOGOFF;
}


static NTSTATUS
GetSystemVersionStrings(
    OUT PZZWSTR pwszzVersion,
    IN SIZE_T cchDest,
    IN BOOLEAN InSafeMode,
    IN BOOLEAN AppendNtSystemRoot)
{
    NTSTATUS Status;

    RTL_OSVERSIONINFOEXW VerInfo;
    UNICODE_STRING BuildLabString;
    UNICODE_STRING CSDVersionString;
    RTL_QUERY_REGISTRY_TABLE VersionConfigurationTable[] =
    {
        {
            NULL,
            RTL_QUERY_REGISTRY_DIRECT,
            L"BuildLab",
            &BuildLabString,
            REG_NONE, NULL, 0
        },
        {
            NULL,
            RTL_QUERY_REGISTRY_DIRECT,
            L"CSDVersion",
            &CSDVersionString,
            REG_NONE, NULL, 0
        },

        {0}
    };

    WCHAR BuildLabBuffer[256];
    WCHAR VersionBuffer[256];
    PWCHAR EndBuffer;

    VerInfo.dwOSVersionInfoSize = sizeof(VerInfo);

    /*
     * This call is uniquely used to retrieve the current CSD numbers.
     * All the rest (major, minor, ...) is either retrieved from the
     * SharedUserData structure, or from the registry.
     */
    RtlGetVersion((PRTL_OSVERSIONINFOW)&VerInfo);

    /*
     * - Retrieve the BuildLab string from the registry (set by the kernel).
     * - In kernel-mode, szCSDVersion is not initialized. Initialize it
     *   and query its value from the registry.
     */
    RtlZeroMemory(BuildLabBuffer, sizeof(BuildLabBuffer));
    RtlInitEmptyUnicodeString(&BuildLabString,
                              BuildLabBuffer,
                              sizeof(BuildLabBuffer));
    RtlZeroMemory(VerInfo.szCSDVersion, sizeof(VerInfo.szCSDVersion));
    RtlInitEmptyUnicodeString(&CSDVersionString,
                              VerInfo.szCSDVersion,
                              sizeof(VerInfo.szCSDVersion));
    Status = RtlQueryRegistryValues(RTL_REGISTRY_WINDOWS_NT,
                                    L"",
                                    VersionConfigurationTable,
                                    NULL,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        /* Indicate nothing is there */
        BuildLabString.Length = 0;
        CSDVersionString.Length = 0;
    }
    /* NULL-terminate the strings */
    BuildLabString.Buffer[BuildLabString.Length / sizeof(WCHAR)] = UNICODE_NULL;
    CSDVersionString.Buffer[CSDVersionString.Length / sizeof(WCHAR)] = UNICODE_NULL;

    EndBuffer = VersionBuffer;
    if ( /* VerInfo.wServicePackMajor != 0 && */ CSDVersionString.Length)
    {
        /* Print the version string */
        Status = RtlStringCbPrintfExW(VersionBuffer,
                                      sizeof(VersionBuffer),
                                      &EndBuffer,
                                      NULL,
                                      0,
                                      L": %wZ",
                                      &CSDVersionString);
        if (!NT_SUCCESS(Status))
        {
            /* No version, NULL-terminate the string */
            *EndBuffer = UNICODE_NULL;
        }
    }
    else
    {
        /* No version, NULL-terminate the string */
        *EndBuffer = UNICODE_NULL;
    }

    if (InSafeMode)
    {
        /* String for Safe Mode */
        Status = RtlStringCchPrintfW(pwszzVersion,
                                     cchDest,
                                     L"ReactOS Version %S %wZ\n"
                                     L"(NT %u.%u Build %u%s)\n",
                                     KERNEL_VERSION_STR,
                                     &BuildLabString,
                                     SharedUserData->NtMajorVersion,
                                     SharedUserData->NtMinorVersion,
                                     (VerInfo.dwBuildNumber & 0xFFFF),
                                     VersionBuffer);

        if (AppendNtSystemRoot && NT_SUCCESS(Status))
        {
            Status = RtlStringCbPrintfW(VersionBuffer,
                                        sizeof(VersionBuffer),
                                        L" - %s\n",
                                        SharedUserData->NtSystemRoot);
            if (NT_SUCCESS(Status))
            {
                /* Replace the last newline by a NULL, before concatenating */
                EndBuffer = wcsrchr(pwszzVersion, L'\n');
                if (EndBuffer) *EndBuffer = UNICODE_NULL;

                /* The concatenated string has a terminating newline */
                Status = RtlStringCchCatW(pwszzVersion,
                                          cchDest,
                                          VersionBuffer);
                if (!NT_SUCCESS(Status))
                {
                    /* Concatenation failed, put back the newline */
                    if (EndBuffer) *EndBuffer = L'\n';
                }
            }

            /* Override any failures as the NtSystemRoot string is optional */
            Status = STATUS_SUCCESS;
        }
    }
    else
    {
        /* Multi-string for Normal Mode */
        Status = RtlStringCchPrintfW(pwszzVersion,
                                     cchDest,
                                     L"ReactOS Version %S\n"
                                     L"Build %wZ\n"
                                     L"Reporting NT %u.%u (Build %u%s)\n",
                                     KERNEL_VERSION_STR,
                                     &BuildLabString,
                                     SharedUserData->NtMajorVersion,
                                     SharedUserData->NtMinorVersion,
                                     (VerInfo.dwBuildNumber & 0xFFFF),
                                     VersionBuffer);

        if (AppendNtSystemRoot && NT_SUCCESS(Status))
        {
            Status = RtlStringCbPrintfW(VersionBuffer,
                                        sizeof(VersionBuffer),
                                        L"%s\n",
                                        SharedUserData->NtSystemRoot);
            if (NT_SUCCESS(Status))
            {
                Status = RtlStringCchCatW(pwszzVersion,
                                          cchDest,
                                          VersionBuffer);
            }

            /* Override any failures as the NtSystemRoot string is optional */
            Status = STATUS_SUCCESS;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        /* Fall-back string */
        Status = RtlStringCchPrintfW(pwszzVersion,
                                     cchDest,
                                     L"ReactOS Version %S %wZ\n",
                                     KERNEL_VERSION_STR,
                                     &BuildLabString);
        if (!NT_SUCCESS(Status))
        {
            /* General failure, NULL-terminate the string */
            pwszzVersion[0] = UNICODE_NULL;
        }
    }

#if 0
    /*
     * Convert the string separators (newlines) into NULLs
     * and NULL-terminate the multi-string.
     */
    while (*pwszzVersion)
    {
        EndBuffer = wcschr(pwszzVersion, L'\n');
        if (!EndBuffer) break;
        pwszzVersion = EndBuffer;

        *pwszzVersion++ = UNICODE_NULL;
    }
    *pwszzVersion = UNICODE_NULL;
#endif

    return Status;
}

static VOID
DisplayLogo(VOID)
{
    static const LPCWSTR pszLogo = L""
L"    ____                  __  ____  _____\n"
L"   / __ \\___  ____ ______/ /_/ __ \\/ ___/\n"
L"  / /_/ / _ \\/ __ `/ ___/ __/ / / /\\__ \\ \n"
L" / _, _/  __/ /_/ / /__/ /_/ /_/ /___/ / \n"
L"/_/ |_|\\___/\\__,_/\\___/\\__/\\____//____/  \n"
L"\n";

    static WCHAR wszzVersion[1024] = L"\0"; // Multi-string
    // We expect at most 4 strings (3 for version, 1 for optional NtSystemRoot)

    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    NTSTATUS Status;
    DWORD result;
    BOOLEAN InSafeMode = TRUE;

    /* Display the ASCII art logo */
    WriteConsoleW(hOutput,
                  pszLogo,
                  wcslen(pszLogo),
                  &result,
                  NULL);

    /* Display the system version information */
    if (!*wszzVersion)
    {
        Status = GetSystemVersionStrings(wszzVersion,
                                         ARRAYSIZE(wszzVersion),
                                         InSafeMode,
                                         TRUE /*g_AlwaysDisplayVersion*/);
    }
    else
    {
        Status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(Status) && *wszzVersion)
    {
        if (!InSafeMode)
        {
            /* Display the strings */
#if 0
            PWCHAR pstr = wszzVersion;
            while (*pstr)
            {
                WriteConsoleW(hOutput,
                              pstr,
                              wcslen(pstr),
                              &result,
                              NULL);
                pstr += (wcslen(pstr) + 1);
            }
#else
            WriteConsoleW(hOutput,
                          wszzVersion,
                          wcslen(wszzVersion),
                          &result,
                          NULL);
#endif
        }
        else
        {
            /* Safe Mode: single version information text in top center */
            WriteConsoleW(hOutput,
                          wszzVersion,
                          wcslen(wszzVersion),
                          &result,
                          NULL);
        }
        /* Extra newline */
        WriteConsoleW(hOutput,
                      L"\n",
                      1,
                      &result,
                      NULL);
    }
}

static BOOL
ReadString(
    IN UINT uIdResourcePrompt,
    IN OUT PWSTR Buffer,
    IN DWORD BufferLength,
    IN BOOL ShowString)
{
    BOOL Success = FALSE;
    HANDLE hInput  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwOldMode;
    BOOL bOldVisible;
    CONSOLE_CURSOR_INFO cci;
    DWORD count; // , i;
    // WCHAR charToDisplay = UNICODE_NULL;
    PWCHAR p;

    SecureZeroMemory(Buffer, BufferLength * sizeof(WCHAR));

    /* Retrieve the original console mode and set a line-editing mode */
    // TODO: Support ENABLE_PROCESSED_INPUT for Ctrl-C
    GetConsoleMode(hInput, &dwOldMode);
    if (!SetConsoleMode(hInput,
                        (dwOldMode | ENABLE_LINE_INPUT | (ShowString ? ENABLE_ECHO_INPUT : 0))
                            & ~(!ShowString ? ENABLE_ECHO_INPUT : 0) ))
    {
        return FALSE;
    }

    if (!DisplayResourceText(uIdResourcePrompt, FALSE))
        goto Quit1;

    /* Retrieve the original cursor visibility and show the cursor */
    GetConsoleCursorInfo(hOutput, &cci);
    bOldVisible = cci.bVisible;
    cci.bVisible = TRUE;
    SetConsoleCursorInfo(hOutput, &cci);

    /* Flush the input buffer */
    if (!FlushConsoleInputBuffer(hInput))
        goto Quit2;

#if 0
//
// That kind of code should be used ONLY if we want to display
// some kind of character fillings when entering the password.
//
    i = 0;
    for (;;)
    {
        WCHAR readChar;
        if (!ReadConsoleW(hInput, &readChar, 1, &count, NULL))
            goto Quit2;
        if (readChar == '\r' || readChar == '\n')
        {
            /* End of string */
            charToDisplay = L'\n';
            WriteConsoleW(hOutput,
                          &charToDisplay,
                          1,
                          &count,
                          NULL);
            break;
        }
        if (ShowString)
        {
            /* Display the character filling */
            charToDisplay = L'*';
            WriteConsoleW(hOutput,
                          &charToDisplay,
                          1,
                          &count,
                          NULL);
        }
        Buffer[i++] = readChar;
        /* FIXME: buffer overflow if the user writes too many chars! */
        UNREFERENCED_PARAMETER(BufferLength);
        /* FIXME: handle backspace */
    }
    Buffer[i] = UNICODE_NULL;
#else
    if (!ReadConsoleW(hInput,
                      Buffer,
                      BufferLength,
                      &count,
                      NULL))
    {
        goto Quit2;
    }

    /* Trim the CR-LF */
    for (p = Buffer; (p - Buffer < BufferLength) && *p; ++p)
    {
        if (*p == L'\r') // || (*p == L'\n')
        {
            *p = UNICODE_NULL;
            break;
        }
    }
#endif

    if (!ShowString)
    {
        /* Still display the \n */
        static LPCWSTR newLine = L"\n";
        WriteConsoleW(hOutput,
                      newLine,
                      wcslen(newLine),
                      &count,
                      NULL);
    }

    Success = TRUE;

Quit2:
    /* Restore the original cursor visibility */
    cci.bVisible = bOldVisible;
    SetConsoleCursorInfo(hOutput, &cci);
Quit1:
    /* Restore the original console mode */
    SetConsoleMode(hInput, dwOldMode);
    return Success;
}

static INT
TUILoggedOutSAS(
    IN OUT PGINA_CONTEXT pgContext)
{
    WCHAR UserName[256];
    WCHAR Domain[256];
    WCHAR Password[256];
    LPWSTR pDomain = NULL;
    NTSTATUS Status;
    NTSTATUS SubStatus = STATUS_SUCCESS;

    TRACE("TUILoggedOutSAS()\n");

    DisplayLogo();

    /* Ask the user for credentials */
    if (!ReadString(IDS_ASKFORUSER, UserName, _countof(UserName), TRUE))
        return WLX_SAS_ACTION_NONE;
    if (!ReadString(IDS_ASKFORDOMAIN, Domain, _countof(Domain), TRUE))
        return WLX_SAS_ACTION_NONE;
    if (*Domain)
        pDomain = Domain;
    if (!ReadString(IDS_ASKFORPASSWORD, Password, _countof(Password), FALSE))
        return WLX_SAS_ACTION_NONE;

    Status = DoLoginTasks(pgContext, UserName, pDomain, Password, &SubStatus);
    // TODO!
    // if (Status == STATUS_LOGON_FAILURE) ;
    // else if (Status == STATUS_ACCOUNT_RESTRICTION) ;
    // else
    if (!NT_SUCCESS(Status))
    {
        TRACE("DoLoginTasks failed! Status 0x%08lx\n", Status);
        return WLX_SAS_ACTION_NONE;
    }
    // FIXME!
    if (Status != STATUS_SUCCESS)
    {
        ERR("Unhandled DoLoginTasks Status 0x%08lx\n", Status);
        return WLX_SAS_ACTION_NONE;
    }

    if (!CreateProfile(pgContext, UserName, pDomain, Password))
    {
        ERR("Failed to create the profile!\n");
        return WLX_SAS_ACTION_NONE;
    }

    // TODO! Initialize contents of pgContext.

    return WLX_SAS_ACTION_LOGON;
}

static INT
TUILockedSAS(
    IN OUT PGINA_CONTEXT pgContext)
{
    HANDLE hToken;
    WCHAR UserName[256];
    WCHAR Password[256];
    NTSTATUS SubStatus;
    NTSTATUS Status;

    TRACE("TUILockedSAS()\n");

    UNREFERENCED_PARAMETER(pgContext);

    if (!DisplayResourceText(IDS_LOGGEDOUTSAS, TRUE))
        return WLX_SAS_ACTION_UNLOCK_WKSTA;

//
// FIXME! Use DoUnlock() instead!
//

    /* Ask the user for credentials */
    if (!ReadString(IDS_ASKFORUSER, UserName, _countof(UserName), TRUE))
        return WLX_SAS_ACTION_NONE;
    if (!ReadString(IDS_ASKFORPASSWORD, Password, _countof(Password), FALSE))
        return WLX_SAS_ACTION_NONE;

    Status = ConnectToLsa(pgContext);
    if (!NT_SUCCESS(Status))
    {
        WARN("ConnectToLsa() failed\n");
        return WLX_SAS_ACTION_NONE;
    }

// FIXME: No no no!!!
    Status = MyLogonUser(pgContext->LsaHandle,
                         pgContext->AuthenticationPackage,
                         UserName,
                         NULL,
                         Password,
                         &hToken,
                         &SubStatus);
    if (!NT_SUCCESS(Status))
    {
        WARN("MyLogonUser() failed\n");
        return WLX_SAS_ACTION_NONE;
    }

    CloseHandle(hToken);
    return WLX_SAS_ACTION_UNLOCK_WKSTA;
}

static VOID
TUIDisplayLockedNotice(
    IN OUT PGINA_CONTEXT pgContext)
{
}

GINA_UI GinaTextUI = {
    TUIInitialize,
    TUIDisplayStatusMessage,
    TUIRemoveStatusMessage,
    TUIDisplaySASNotice,
    TUILoggedOnSAS,
    TUILoggedOutSAS,
    TUILockedSAS,
    TUIDisplayLockedNotice,
};
