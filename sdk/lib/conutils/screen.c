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
// #include <winnls.h>
#include <wincon.h>  // Console APIs (only if kernel32 support included)
#include <strsafe.h>

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
        hOutput = CreateFileW(L"CONOUT$", GENERIC_READ|GENERIC_WRITE,
                             FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
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


/*
 * Headless terminal text colors -- Taken from ntoskrnl/inbv/inbv.c
 */

// Conversion table CGA to ANSI color index
const UCHAR CGA_TO_ANSI_COLOR_TABLE[16] =
{
    0,  // Black
    4,  // Blue
    2,  // Green
    6,  // Cyan
    1,  // Red
    5,  // Magenta
    3,  // Brown/Yellow
    7,  // Grey/White

    60, // Bright Black
    64, // Bright Blue
    62, // Bright Green
    66, // Bright Cyan
    61, // Bright Red
    65, // Bright Magenta
    63, // Bright Yellow
    67  // Bright Grey (White)
};

// #define CGA_TO_ANSI_COLOR(CgaColor) /
    // CGA_TO_ANSI_COLOR_TABLE[CgaColor & 0x0F]

BOOL
ConSetScreenColor(
    IN PCON_SCREEN Screen,
    IN WORD wColor,
    IN BOOL bFill)
{
    HANDLE hOutput;

    /* Parameters validation */
    if (!Screen) return FALSE;

    /* Foreground and Background colors cannot be the same */
    if ((wColor & 0x0F) == ((wColor & 0xF0) >> 4))
        return FALSE;

    hOutput = ConStreamGetOSHandle(Screen->Stream);

    /* Fill the whole background if needed */
    // FIXME: I am not aware of any ANSI sequence capable of doing that...
    if (bFill && IsConsoleHandle(hOutput))
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        COORD coPos;
        DWORD dwWritten;

        GetConsoleScreenBufferInfo(hOutput, &csbi);

        coPos.X = 0;
        coPos.Y = 0;
        FillConsoleOutputAttribute(hOutput,
                                   LOBYTE(wColor),
                                   csbi.dwSize.X * csbi.dwSize.Y,
                                   coPos,
                                   &dwWritten);
    }

    /* Set the text attribute */
    if (IsConsoleHandle(hOutput))
    {
        // NOTE: High byte contains interesting stuff
        SetConsoleTextAttribute(hOutput, LOBYTE(wColor));
    }
    else if (IsTTYHandle(hOutput))
    {
        /*
         * This is using aixterm (not standard) way, but it correctly works.
         * The "more" standard way, using the "bold"/"intensity" attribute,
         * does not always work as expected.
         */
        ConPrintf(Screen->Stream, L"\x1B[%d;%dm",
                  30 + CGA_TO_ANSI_COLOR(wColor & 0x0F),
                  40 + CGA_TO_ANSI_COLOR(((wColor & 0xF0) >> 4)));
    }

    return TRUE;
}

/* EOF */
