/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     UI Video helpers for special effects.
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 */

#pragma once

#include <pshpack1.h>
typedef struct _PALETTE_ENTRY
{
    UCHAR Red;
    UCHAR Green;
    UCHAR Blue;
} PALETTE_ENTRY, *PPALETTE_ENTRY;
#include <poppack.h>


/* Temporary console abstraction used by the UI code */
typedef struct _UI_CONSOLE
{
    BOOLEAN (*KbHit)(VOID);
    INT (*GetCh)(VOID); // Read

    // VOID (*Reset)(VOID);
    INT (*Write)(_In_ PCHAR Buffer, _In_ SIZE_T Length);
    // VOID (*WriteRect)(_In_ PVOID Buffer, _Inout_ PSMALL_RECT Rect); // Kind of Blt for textmode.
    // VOID (*ClearScreen)(VOID);

    PVOID Context;

/** Read-only data **/
    ULONG ScreenWidth;  // Screen Width
    ULONG ScreenHeight; // Screen Height
    ULONG Depth; // FIXME: Useful only for GUIs.

/** Read/Write data **/ // TODO: Should be changed with VideoSetTextCursorPosition()...
    ULONG CursorX;
    ULONG CursorY;
    ULONG CurrAttr; // Current text attributes
} UI_CONSOLE, *PUI_CONSOLE;


BOOLEAN
InitVideoConsole(
    /* IN OUT PUI_CONSOLE Console, */
    IN BOOLEAN CachedOutput);

VOID VideoFreeOffScreenBuffer(VOID);
VOID VideoCopyOffScreenBufferToVRAM(VOID);

VOID VideoSavePaletteState(PPALETTE_ENTRY Palette, ULONG ColorCount);
VOID VideoRestorePaletteState(PPALETTE_ENTRY Palette, ULONG ColorCount);

VOID VideoSetAllColorsToBlack(ULONG ColorCount);
VOID VideoFadeIn(PPALETTE_ENTRY Palette, ULONG ColorCount);
VOID VideoFadeOut(ULONG ColorCount);
