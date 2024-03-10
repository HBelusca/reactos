/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video output - Wrapper functions
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>

// UCHAR MachDefaultTextColor = COLOR_GRAY;

/* Helper for console */
VOID
Win32VideoScrollUp(VOID)
{
    Win32ConVideoScrollUp();
}

VOID
Win32VideoClearScreen(
    _In_ UCHAR Attr)
{
    Win32ConVideoClearScreen(Attr);
}

VOID
Win32VideoPutChar(
    _In_ int Ch,
    _In_ UCHAR Attr,
    _In_ unsigned X,
    _In_ unsigned Y)
{
    Win32ConVideoPutChar(Ch, Attr, X, Y);
}

BOOLEAN
Win32VideoInit(VOID)
{
    return Win32ConVideoInit();
}

VIDEODISPLAYMODE
Win32VideoSetDisplayMode(
    _In_ PCSTR DisplayMode,
    _In_ BOOLEAN Init)
{
    return Win32ConVideoSetDisplayMode(DisplayMode, Init);
}

VOID
Win32VideoGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    Win32ConVideoGetDisplaySize(Width, Height, Depth);
}

ULONG
Win32VideoGetBufferSize(VOID)
{
    return Win32ConVideoGetBufferSize();
}

VOID
Win32VideoGetFontsFromFirmware(
    _Out_ PULONG RomFontPointers)
{
    Win32ConVideoGetFontsFromFirmware(RomFontPointers);
}

VOID
Win32VideoSetTextCursorPosition(
    _In_ UCHAR X,
    _In_ UCHAR Y)
{
    Win32ConVideoSetTextCursorPosition(X, Y);
}

VOID
Win32VideoHideShowTextCursor(
    _In_ BOOLEAN Show)
{
    Win32ConVideoHideShowTextCursor(Show);
}

VOID
Win32VideoCopyOffScreenBufferToVRAM(
    _In_ PVOID Buffer)
{
    Win32ConVideoCopyOffScreenBufferToVRAM(Buffer);
}

BOOLEAN
Win32VideoIsPaletteFixed(VOID)
{
    return Win32ConVideoIsPaletteFixed();
}

VOID
Win32VideoSetPaletteColor(
    _In_ UCHAR Color,
    _In_ UCHAR Red,
    _In_ UCHAR Green,
    _In_ UCHAR Blue)
{
    Win32ConVideoSetPaletteColor(Color, Red, Green, Blue);
}

VOID
Win32VideoGetPaletteColor(
    _In_ UCHAR Color,
    _Out_ PUCHAR Red,
    _Out_ PUCHAR Green,
    _Out_ PUCHAR Blue)
{
    Win32ConVideoGetPaletteColor(Color, Red, Green, Blue);
}

VOID
Win32VideoSync(VOID)
{
    Win32ConVideoSync();
}

/* EOF */
