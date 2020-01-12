/*
 * PROJECT:         ReactOS msgina.dll
 * FILE:            dll/win32/msgina/tui.c
 * PURPOSE:         ReactOS Logon GINA DLL
 * PROGRAMMER:      Hervé Poussineau (hpoussin@reactos.org)
 */

#if 0
#include "winlogon.h"
#endif

#include <wincon.h>

/* Console API functions which are absent from wincon.h */
#define EXENAME_LENGTH (255 + 1)

DWORD
WINAPI
GetConsoleInputExeNameW(
    IN DWORD nBufferLength,
    OUT LPWSTR lpExeName);

VOID
WINAPI
ExpungeConsoleCommandHistoryW(
    IN LPCWSTR lpExeName);

BOOL
WINAPI
SetConsoleNumberOfCommandsW(
    IN DWORD dwNumCommands,
    IN LPCWSTR lpExeName);


static BOOL
TUIInitialize(
    IN PWLSESSION Session)
{
    BOOL Success;
    PRTL_USER_PROCESS_PARAMETERS Parameters;
    HANDLE hConIn, hConOut;
    UNICODE_STRING OrgDesktopInfo;
    CONSOLE_CURSOR_INFO cci;
#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
    CONSOLE_HISTORY_INFO chi;
#endif
    WCHAR ExeNameBuffer[EXENAME_LENGTH];

    TRACE("TUIInitialize(%p)\n", Session);

    /*
     * Consoles are created on the process' *STARTUP* desktop, which in our
     * case is none, and thus will default to "WinSta\Default". However we do
     * NOT want the console to be created there but instead we want it to be
     * created on the "WinSta\Winlogon" desktop.
     * The console creation code uses the desktop path specified in the process'
     * NtCurrentPeb()->ProcessParameters->DesktopInfo UNICODE_STRING.
     * In order to trick the creation code to create the console on the desktop
     * of our choice, we will modify the DesktopInfo string to point to our
     * desktop and then create the console. We will then restore the original
     * DesktopInfo afterwards.
     */

    /* Save the original DesktopInfo and replace it with Winlogon's desktop */
    RtlAcquirePebLock();
    Parameters = NtCurrentPeb()->ProcessParameters;
    OrgDesktopInfo = Parameters->DesktopInfo;
    RtlInitUnicodeString(&Parameters->DesktopInfo, L"WinSta0\\Winlogon");
    RtlReleasePebLock();

    /* Allocate the console */
    Success = AllocConsole();

    RtlAcquirePebLock();
    /* Retrieve the standard input and output handles */
    hConIn  = Parameters->StandardInput;
    hConOut = Parameters->StandardOutput;
    /* Restore the original DesktopInfo */
    Parameters->DesktopInfo = OrgDesktopInfo;
    RtlReleasePebLock();

    if (!Success)
        return FALSE;

    /*
     * Setup the console:
     * - No history: cleared and of size equals to zero;
     * - Hide the cursor by default.
     */
__debugbreak();
    /* The console application name is NULL-terminated */
    *ExeNameBuffer = UNICODE_NULL; // It also has to be initialized to NULL before.
    GetConsoleInputExeNameW(ARRAYSIZE(ExeNameBuffer), ExeNameBuffer);

    SetConsoleNumberOfCommandsW(0, ExeNameBuffer);
    ExpungeConsoleCommandHistoryW(ExeNameBuffer);

#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
    chi.cbSize = sizeof(chi);
    chi.HistoryBufferSize = 0;
    chi.NumberOfHistoryBuffers = 0;
    chi.dwFlags = HISTORY_NO_DUP_FLAG;
    SetConsoleHistoryInfo(&chi);
#endif

#if 0 // Alternative for getting a valid hConOut...
    hConOut = OpenConsoleW(L"CONOUT$",
                           GENERIC_READ | GENERIC_WRITE,
                           FALSE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE);

    if (hConOut && (hConOut != INVALID_HANDLE_VALUE))
    {
        // TODO: Do the thing...
        /* And close the handle */
        CloseHandle(hConOut);
    }
#endif

    GetConsoleCursorInfo(hConOut, &cci);
    cci.bVisible = FALSE;
    SetConsoleCursorInfo(hConOut, &cci);

    /* Flush the input buffer */
    FlushConsoleInputBuffer(hConIn);

    return Success;
}

static VOID
TUIProcessEvents(
    IN PWLSESSION Session)
{
    // /* Message loop for the SAS window */
    // while (TRUE) ;

    MSG Msg;

    /* Message loop for the SAS window */
    while (GetMessageW(&Msg, Session->SASWindow, 0, 0))
    {
        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }
}

static LRESULT
TUIPostMessage(
    IN PWLSESSION Session,
    IN UINT   Msg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    return 0;
}

static LRESULT
TUISendMessage(
    IN PWLSESSION Session,
    IN UINT   Msg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    return 0;
}


#if 0

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
    UNREFERENCED_PARAMETER(pTitle);

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

#endif

LOGON_UI LogonTextUI = {
    TUIInitialize,
    TUIProcessEvents,
    TUIPostMessage,
    TUISendMessage,
};
