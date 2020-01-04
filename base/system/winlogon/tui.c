/*
 * PROJECT:         ReactOS msgina.dll
 * FILE:            dll/win32/msgina/tui.c
 * PURPOSE:         ReactOS Logon GINA DLL
 * PROGRAMMER:      Hervé Poussineau (hpoussin@reactos.org)
 */

#include "winlogon.h"

#include <wincon.h>

static BOOL
TUIInitialize(
    IN PWLSESSION Session)
{
    TRACE("TUIInitialize(%p)\n", Session);
    return AllocConsole();
}

static VOID
TUIProcessEvents(
    IN PWLSESSION Session)
{
    /* Message loop for the SAS window */
    while (TRUE) ;
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
