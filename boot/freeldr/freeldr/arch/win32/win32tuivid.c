/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video output via Console
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>
#include <wincon.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(UI);

HANDLE hConOut;
static ULONG ScreenWidth;
static ULONG ScreenHeight;

UCHAR MachDefaultTextColor = COLOR_GRAY;

// #define VGA_CHAR_SIZE 2
// #define TEXT_CHAR_SIZE 2
// UCHAR TextCols;
// UCHAR TextLines;

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

#define TOP_BOTTOM_LINES 0

/* Helper for console */
VOID
Win32ConVideoScrollUp(VOID)
{
    /* Scroll everything up one line */
    CHAR_INFO ciFill = {{' '}, ATTR(MachDefaultTextColor, COLOR_BLACK)};
    COORD dwOrig = {0, 0};
    SMALL_RECT scrollRect = { 0, 1, ScreenWidth - 1, ScreenHeight - 1 };

    /* Verify screen resolution */
    ASSERT(ScreenWidth > 1);
    ASSERT(ScreenHeight > 1);

    ScrollConsoleScreenBuffer(hConOut,
                              &scrollRect,
                              NULL /*&clipRect*/,
                              dwOrig,
                              &ciFill);
}

VOID
Win32ConVideoClearScreen(
    _In_ UCHAR Attr)
{
    COORD dwOrig = {0, 0};
    DWORD dwChars;

    /* Reset the default text attribute */
    /**/SetConsoleTextAttribute(hConOut, Attr);/**/

    /* Blank the whole buffer */
    FillConsoleOutputCharacterW(hConOut, L' ',
                                ScreenWidth * ScreenHeight,
                                dwOrig, &dwChars);
    FillConsoleOutputAttribute(hConOut, Attr,
                               ScreenWidth * ScreenHeight,
                               dwOrig, &dwChars);
    SetConsoleCursorPosition(hConOut, dwOrig);
}

VOID
Win32ConVideoPutChar(
    _In_ int Ch,
    _In_ UCHAR Attr,
    _In_ unsigned X,
    _In_ unsigned Y)
{
    CHAR cCh = Ch;
    WORD wAttr = Attr;
    COORD coord = { (SHORT)X, (SHORT)Y };
    DWORD dwWritten;

    WriteConsoleOutputCharacterA(hConOut, &cCh, 1, coord, &dwWritten);
    WriteConsoleOutputAttribute(hConOut, &wAttr, 1, coord, &dwWritten);
}

BOOLEAN
Win32ConVideoInit(VOID)
{
    hConOut = GetStdHandle(STD_OUTPUT_HANDLE);

#if 0 // If we want to resize the console to e.g. 130x45 ...
    {
    COORD sbSize = { 130, 45 };
    SMALL_RECT sRect = { 0,0, sbSize.X - 1, sbSize.Y - 1 };
    SetConsoleWindowInfo(hConOut, TRUE, &sRect);
    SetConsoleScreenBufferSize(hConOut, sbSize);
    SetConsoleWindowInfo(hConOut, TRUE, &sRect);
    }
#endif

    CONSOLE_SCREEN_BUFFER_INFO csbi;

    ScreenWidth = 0;
    ScreenHeight = 0;

    if (GetConsoleScreenBufferInfo(hConOut, &csbi))
    {
#if 0
        ScreenWidth = csbi.dwSize.X;
        ScreenHeight = csbi.dwSize.Y;
#else
        ScreenWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        ScreenHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#endif

        /* Verify screen resolution */
        ASSERT(ScreenWidth > 1);
        ASSERT(ScreenHeight > 1);
    }
    else
    {
        ERR("GetConsoleScreenBufferInfo() failed\n");
        return FALSE;
    }

    if (ScreenWidth == 0 || ScreenHeight == 0)
    {
        ERR("Bogus ScreenWidth: %lu , ScreenHeight: %lu\n",
            ScreenWidth, ScreenHeight);
        return FALSE;
    }

    Win32ConVideoClearScreen(ATTR(MachDefaultTextColor, COLOR_BLACK));
    return TRUE;
}

VIDEODISPLAYMODE
Win32ConVideoSetDisplayMode(
    _In_ PCSTR DisplayMode,
    _In_ BOOLEAN Init)
{
    /* We only have one mode: text */
    UNREFERENCED_PARAMETER(DisplayMode);
    // TODO: Init?
    return VideoTextMode;
}

VOID
Win32ConVideoGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    *Width = ScreenWidth; // / CHAR_WIDTH;
    *Height = ScreenHeight; // / CHAR_HEIGHT;
    *Depth = 0;
}

ULONG
Win32ConVideoGetBufferSize(VOID)
{
    return (ScreenHeight * ScreenWidth * 2); // VGA_CHAR_SIZE
    // return ((ScreenHeight / CHAR_HEIGHT) * (ScreenWidth / CHAR_WIDTH) * 2);
}

VOID
Win32ConVideoGetFontsFromFirmware(
    _Out_ PULONG RomFontPointers)
{
    /* Not supported */
}

VOID
Win32ConVideoSetTextCursorPosition(
    _In_ UCHAR X,
    _In_ UCHAR Y)
{
    COORD coord = { (SHORT)X, (SHORT)Y };
    SetConsoleCursorPosition(hConOut, coord);
}

VOID
Win32ConVideoHideShowTextCursor(
    _In_ BOOLEAN Show)
{
    CONSOLE_CURSOR_INFO cci;
    if (GetConsoleCursorInfo(hConOut, &cci))
    {
        cci.bVisible = Show;
        SetConsoleCursorInfo(hConOut, &cci);
    }
}

VOID
Win32ConVideoCopyOffScreenBufferToVRAM(
    _In_ PVOID Buffer)
{
    PUCHAR OffScreenBuffer = (PUCHAR)Buffer;
    ULONG Col, Line; // USHORT X, Y;

    for (Line = 0; Line < ScreenHeight /* / CHAR_HEIGHT */; Line++)
    {
        for (Col = 0; Col < ScreenWidth /* / CHAR_WIDTH */; Col++)
        {
            Win32ConVideoPutChar(OffScreenBuffer[0], OffScreenBuffer[1], Col, Line);
            OffScreenBuffer += 2; // VGA_CHAR_SIZE;
        }
    }
}

BOOLEAN
Win32ConVideoIsPaletteFixed(VOID)
{
    return FALSE;
}

VOID
Win32ConVideoSetPaletteColor(
    _In_ UCHAR Color,
    _In_ UCHAR Red,
    _In_ UCHAR Green,
    _In_ UCHAR Blue)
{
    /* Not supported */
}

VOID
Win32ConVideoGetPaletteColor(
    _In_ UCHAR Color,
    _Out_ PUCHAR Red,
    _Out_ PUCHAR Green,
    _Out_ PUCHAR Blue)
{
    /* Not supported */
}

VOID
Win32ConVideoSync(VOID)
{
    /* Not supported */
}

/* EOF */
