/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Console/terminal screen management.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

/**
 * @file    screen.c
 * @ingroup ConUtils
 *
 * @brief   Console/terminal screen management.
 **/

/* FIXME: Temporary HACK before we cleanly support UNICODE functions */
#define UNICODE
#define _UNICODE

#include <windef.h>
#include <winbase.h>
#include <wincon.h> // Console APIs (only if kernel32 support included)

#include "conutils.h"
#include "stream.h"
#include "screen.h"

// Temporary HACK
#define CON_STREAM_WRITE    ConStreamWrite


#if 0

VOID
ConClearLine(IN PCON_STREAM Stream)
{
    HANDLE hOutput = ConStreamGetOSHandle(Stream);

    /*
     * Erase the full line where the cursor is, and move
     * the cursor back to the beginning of the line.
     */

    if (IsConsoleHandle(hOutput))
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        DWORD dwWritten;

        GetConsoleScreenBufferInfo(hOutput, &csbi);

        csbi.dwCursorPosition.X = 0;
        // csbi.dwCursorPosition.Y;

        FillConsoleOutputCharacterW(hOutput, L' ',
                                    csbi.dwSize.X,
                                    csbi.dwCursorPosition,
                                    &dwWritten);
        SetConsoleCursorPosition(hOutput, csbi.dwCursorPosition);
    }
    else if (IsTTYHandle(hOutput))
    {
        ConPuts(Stream, L"\x1B[2K\x1B[1G"); // FIXME: Just use WriteFile
    }
    // else, do nothing for files
}

#endif


BOOL
ConGetScreenInfo(
    IN PCON_SCREEN Screen,
    OUT PCONSOLE_SCREEN_BUFFER_INFO pcsbi)
{
    BOOL Success;
    HANDLE hOutput;

    /* Parameters validation */
    if (!Screen || !pcsbi)
        return FALSE;

    hOutput = ConStreamGetOSHandle(Screen->Stream);

    /* Screen handle must be of TTY type (console or TTY) */
    if (!IsTTYHandle(hOutput))
        return FALSE;

    /* Update cached screen information */
    if (IsConsoleHandle(hOutput))
    {
        Success = GetConsoleScreenBufferInfo(hOutput, &Screen->csbi);
    }
    else
    {
#if 0
        /* TODO: Do something adequate for TTYs */
        // FIXME: At the moment we return hardcoded info.
        Screen->csbi.dwSize.X = 80;
        Screen->csbi.dwSize.Y = 25;

        // Screen->csbi.dwCursorPosition;
        // Screen->csbi.wAttributes;
        // Screen->csbi.srWindow;
        Screen->csbi.dwMaximumWindowSize = Screen->csbi.dwSize;
#else
        hOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL);

        Success = IsConsoleHandle(hOutput) &&
                  GetConsoleScreenBufferInfo(hOutput, &Screen->csbi);

        CloseHandle(hOutput);
#endif
    }

    if (Success)
    {
        /* Return it to the caller */
        *pcsbi = Screen->csbi;
    }

    return Success;
}

BOOL
ConGetCursorInfo(
    IN PCON_SCREEN Screen,
    OUT PCONSOLE_CURSOR_INFO pcci)
{
    BOOL Success;
    HANDLE hOutput;

    /* Parameters validation */
    if (!Screen || !pcci)
        return FALSE;

    hOutput = ConStreamGetOSHandle(Screen->Stream);

    /* Screen handle must be of TTY type (console or TTY) */
    if (!IsTTYHandle(hOutput))
        return FALSE;

    /* Update cached screen information */
    if (IsConsoleHandle(hOutput))
    {
        Success = GetConsoleCursorInfo(hOutput, &Screen->cci);
    }
    else
    {
#if 0
        /* TODO: Do something adequate for TTYs */
        // FIXME: At the moment we return hardcoded info.
        Screen->cci.dwSize = 25;
        Screen->cci.bVisible = TRUE;
#else
        hOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL);

        Success = IsConsoleHandle(hOutput) &&
                  GetConsoleCursorInfo(hOutput, &Screen->cci);

        CloseHandle(hOutput);
#endif
    }

    if (Success)
    {
        /* Return it to the caller */
        *pcci = Screen->cci;
    }

    return Success;
}

BOOL
ConSetCursorInfo(
    IN PCON_SCREEN Screen,
    IN PCONSOLE_CURSOR_INFO pcci)
{
    BOOL Success;
    HANDLE hOutput;

    /* Parameters validation */
    if (!Screen || !pcci)
        return FALSE;

    hOutput = ConStreamGetOSHandle(Screen->Stream);

    /* Screen handle must be of TTY type (console or TTY) */
    if (!IsTTYHandle(hOutput))
        return FALSE;

    /* Set the cursor information */
    if (IsConsoleHandle(hOutput))
    {
        Success = SetConsoleCursorInfo(hOutput, pcci);
    }
    else // if (IsTTYHandle(hOutput))
    {
        ConPrintf(Screen->Stream,
                  L"\x1B[%hu q"  // Mode style
                  L"\x1B[?25%c", // Visible (h) or hidden (l)
                  (pcci->dwSize <= 15) ? 3 : 1, // Blinking underline (3) or blinking block (1)
                  pcci->bVisible ? 'h' : 'l');
        /*
         * Might as well support the following SCO Terminal command:
         * ESC[= s ; e C
         *   Sets cursor parameters (where s is the starting and e is
         *   the ending scanlines of the cursor).
         */
        Success = TRUE;
    }
#if 0
    else
    {
        hOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL);

        Success = IsConsoleHandle(hOutput) &&
                  SetConsoleCursorInfo(hOutput, pcci);

        CloseHandle(hOutput);
    }
#endif

    if (Success)
    {
        /* Update cached screen information */
        Screen->cci = *pcci;
    }

    return Success;
}

BOOL
ConSetCursorPos(
    IN PCON_SCREEN Screen,
    IN COORD dwCursorPosition)
{
    BOOL Success;
    HANDLE hOutput;

    /* Parameters validation */
    if (!Screen)
        return FALSE;

    hOutput = ConStreamGetOSHandle(Screen->Stream);

    /* Screen handle must be of TTY type (console or TTY) */
    if (!IsTTYHandle(hOutput))
        return FALSE;

    /* Set the cursor position */
    if (IsConsoleHandle(hOutput))
    {
        Success = SetConsoleCursorPosition(hOutput, dwCursorPosition);
    }
    else // if (IsTTYHandle(hOutput))
    {
        ConPrintf(Screen->Stream, L"\x1B[%d;%dH",
                  1 + dwCursorPosition.Y,
                  1 + dwCursorPosition.X);
        Success = TRUE;
    }
#if 0
    else
    {
        hOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL);

        Success = IsConsoleHandle(hOutput) &&
                  SetConsoleCursorPosition(hOutput, dwCursorPosition);

        CloseHandle(hOutput);
    }
#endif

    if (Success)
    {
        /* Update cached screen information */
        Screen->csbi.dwCursorPosition = dwCursorPosition;
    }

    return Success;
}

// For real consoles, erase everything, otherwise (TTY) erase just the "screen".
// FIXME: Or we can add a BOOL flag?
VOID
ConClearScreen(IN PCON_SCREEN Screen)
{
    HANDLE hOutput;

    /* Parameters validation */
    if (!Screen) return;

#if 0
    /* Get the size of the visual screen */
    if (!ConGetScreenInfo(Screen, &csbi))
    {
        /* We assume it's a file handle */
        return;
    }
#endif

    hOutput = ConStreamGetOSHandle(Screen->Stream);

    if (IsConsoleHandle(hOutput))
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        COORD coPos;
        DWORD dwWritten;

        GetConsoleScreenBufferInfo(hOutput, &csbi);

        coPos.X = 0;
        coPos.Y = 0;
        FillConsoleOutputAttribute(hOutput, csbi.wAttributes,
                                   csbi.dwSize.X * csbi.dwSize.Y,
                                   coPos, &dwWritten);
        FillConsoleOutputCharacterW(hOutput, L' ',
                                    csbi.dwSize.X * csbi.dwSize.Y,
                                    coPos, &dwWritten);
        SetConsoleCursorPosition(hOutput, coPos);
    }
    else if (IsTTYHandle(hOutput))
    {
        /* Clear the full screen and move the cursor to (0,0) */
        ConPuts(Screen->Stream, L"\x1B[2J\x1B[1;1H");
    }
    else
    {
        /* Issue a Form-Feed control */
        WCHAR ch = L'\f';
        CON_STREAM_WRITE(Screen->Stream, &ch, 1);
    }
}

/* EOF */
