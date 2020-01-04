/*
 * PROJECT:         ReactOS msgina.dll
 * FILE:            dll/win32/msgina/gui.c
 * PURPOSE:         ReactOS Logon GINA DLL
 * PROGRAMMER:      Hervé Poussineau (hpoussin@reactos.org)
 */

#include "winlogon.h"

#include <wingdi.h>
// #include <winnls.h>
// #include <winreg.h>

static BOOL
GUIInitialize(
    IN PWLSESSION Session)
{
    TRACE("GUIInitialize(%p)\n", Session);
    return TRUE;
}

static VOID
GUIProcessEvents(
    IN PWLSESSION Session)
{
    MSG Msg;

    /* Message loop for the SAS window */
    while (GetMessageW(&Msg, Session->SASWindow, 0, 0))
    {
        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }
}

static LRESULT
GUIPostMessage(
    IN PWLSESSION Session,
    IN UINT   Msg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    return PostMessageW(Session->SASWindow, Msg, wParam, lParam);
}

static LRESULT
GUISendMessage(
    IN PWLSESSION Session,
    IN UINT   Msg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    return SendMessageW(Session->SASWindow, Msg, wParam, lParam);
}

LOGON_UI LogonGraphicalUI = {
    GUIInitialize,
    GUIProcessEvents,
    GUIPostMessage,
    GUISendMessage,
};
