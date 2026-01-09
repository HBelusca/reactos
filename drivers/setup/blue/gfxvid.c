/*
 * PROJECT:     ReactOS Console Text-Mode Device Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Text-mode emulation via Inbv/BootVid calls.
 * COPYRIGHT:   Copyright 2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include "blue.h"
#include <ndk/inbvfuncs.h>

/* Native definitions from BOOTVID (Boot Video Driver) */
#include <bootvid/bootvid.h> // drivers/bootvid...

#define NDEBUG
#include <debug.h>


/* FUNCTIONS *****************************************************************/

/* Copied from drivers/base/bootvid/precomp.h */
#define BOOTCHAR_HEIGHT 13
#define BOOTCHAR_WIDTH  8 // Each character line is encoded in a UCHAR.

/**
 * @brief   Convert CGA-like indexed color to BootVid ANSI-like color index.
 * @note    The Red and Blue channels are inverted between both representations.
 **/
#define CGA_TO_BOOTVID_COLOR(Color) \
    (((Color) & 0x0A) | (((Color) & 4) >> 2) | (((Color) & 1) << 2))


VOID
FASTCALL
InbvScrAcquireOwnership(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    if (InbvIsBootDriverInstalled() /* &&
        !InbvCheckDisplayOwnership() */)
    {
        /* Acquire ownership and reset the display */
        InbvAcquireDisplayOwnership();
        InbvResetDisplay();
        // InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
        // InbvSetTextColor(BV_COLOR_WHITE);
        InbvInstallDisplayStringFilter(NULL);
        InbvEnableDisplayString(TRUE); // Or set to FALSE to not interfere with kernel Inbv usage?
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    /* Read screen information */
    DeviceExtension->Columns = SCREEN_WIDTH / BOOTCHAR_WIDTH;
    DeviceExtension->Rows = SCREEN_HEIGHT;
    DeviceExtension->ScanLines = (BOOTCHAR_HEIGHT + 1);

    /* Calculate number of text rows */
    DeviceExtension->Rows = DeviceExtension->Rows / DeviceExtension->ScanLines;

    // /* Retrieve the current output cursor position */
    // /* Show blinking cursor */
    // // FIXME: cursor block? Call ScrSetCursorShape() instead?

    /* Set the cursor position */
    DeviceExtension->CursorX = 0;
    DeviceExtension->CursorY = 0;
}

VOID
FASTCALL
InbvScrSetCursor(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    // ULONG Offset;

    if (!DeviceExtension->VideoMemory)
        return;

    // TODO
    // Offset = (DeviceExtension->CursorY * DeviceExtension->Columns) + DeviceExtension->CursorX;
}

VOID
FASTCALL
InbvScrSetCursorShape(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    ULONG size, height;

    if (!DeviceExtension->VideoMemory)
        return;

    height = DeviceExtension->ScanLines;
    // data = (DeviceExtension->CursorVisible) ? 0x00 : 0x20;

    size = (DeviceExtension->CursorSize * height) / 100;
    if (size < 1)
        size = 1;

    DBG_UNREFERENCED_LOCAL_VARIABLE(size);
    DBG_UNREFERENCED_LOCAL_VARIABLE(height);
    // TODO
}

VOID
InbvScrWriteCharactersAttributes(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PCHAR Buffer,
    _In_ ULONG Length,
    _In_ USHORT StartPosX,
    _In_ USHORT StartPosY,
    _Out_opt_ PULONG WrittenLength)
{
    // FIXME!
    UNIMPLEMENTED;
    if (WrittenLength)
        *WrittenLength = 0;
}

VOID
InbvScrWriteCharacter(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ CHAR Character,
    _In_ USHORT CharAttribute,
    _In_ USHORT PosX,
    _In_ USHORT PosY)
{
    /*
     * BOOTVID doesn't offer a nice interface to place one (or a group of)
     * character(s) at a given place on the screen with specific foreground
     * and background colors: VidDisplayStringXY() only takes a "transparency"
     * flag, and otherwise hardcodes the colors to be used:
     * VidDisplayStringXY(String, Left, Top, Transparent);
     *
     * So instead, we need to play some tricks.
     */
    CHAR Buffer[2];
    UCHAR Color;

    /* Invoke VidSetScrollRegion() so that VidDisplayString()
     * places the character at the correct place */
    ULONG Left = PosX * BOOTCHAR_WIDTH;
    ULONG Top  = PosY * (BOOTCHAR_HEIGHT + 1);
    VidSetScrollRegion(Left, Top,
                       Left + 2 * BOOTCHAR_WIDTH, // * 2 so that VidDisplayString() doesn't scroll.
                       Top + (BOOTCHAR_HEIGHT + 1));

    /* Set the foreground text color */
    Color = CharAttribute /*& 0x0F*/;
    VidSetTextColor(CGA_TO_BOOTVID_COLOR(Color));
    /* Set the background text color */
    if (CharAttribute & 0xFF00 == 0)
    {
        Color = (CharAttribute >> 4) /*& 0x0F*/;
        VidSolidColorFill(Left, Top,
                          Left + BOOTCHAR_WIDTH,
                          Top + (BOOTCHAR_HEIGHT + 1),
                          CGA_TO_BOOTVID_COLOR(Color));
    }

    /* Display the single character */
    Buffer[0] = Character;
    Buffer[1] = ANSI_NULL;
    VidDisplayString(Buffer);
}

VOID
InbvScrScrollUp(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ CHAR FillCharacter,
    _In_ USHORT FillAttribute)
{
    ULONG i;

    /* Redefine the real region the console covers */
    // ULONG Left = PosX * BOOTCHAR_WIDTH;
    // ULONG Top  = PosY * (BOOTCHAR_HEIGHT + 1);
    VidSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    /* Trigger a scroll up by VidDisplayString()'ing as many newlines as needed */
    for (i = 0; i < SCREEN_HEIGHT / (BOOTCHAR_HEIGHT + 1) + 1; ++i)
        VidDisplayString("\n");
}

VOID
InbvScrSetPalette(
    _In_reads_(Count) const RGBQUAD* Table,
    _In_ ULONG Count)
{
    // const RGBQUAD* Entry = Table;
    // ULONG i;

    UCHAR PaletteBitmapBuffer[sizeof(BITMAPINFOHEADER) + sizeof(MainPalette)];
    PBITMAPINFOHEADER PaletteBitmap = (PBITMAPINFOHEADER)PaletteBitmapBuffer;
    PRGBQUAD Palette = (PRGBQUAD)(PaletteBitmapBuffer + sizeof(BITMAPINFOHEADER));
    // ULONG Index;

    // /* Check if we are installed and we own the display */
    // if (!InbvIsBootDriverInstalled() || !InbvCheckDisplayOwnership())
    //     return;

    /* Build a 0x0 bitmap containing the palette to be set using VidBitBlt() */
    RtlZeroMemory(PaletteBitmap, sizeof(BITMAPINFOHEADER));
    PaletteBitmap->biSize = sizeof(BITMAPINFOHEADER);
    PaletteBitmap->biBitCount = 4;
    PaletteBitmap->biCompression = BI_RGB;
    PaletteBitmap->biClrUsed = Count;

#if 0
    for (Index = 0; Index < Count; Index++) // for (i = 0; i < Count; i++, Entry++)
    {
        Palette[Index].rgbRed = Table[Index].rgbRed;
        Palette[Index].rgbGreen = Table[Index].rgbGreen;
        Palette[Index].rgbBlue = Table[Index].rgbBlue;
    }
#else
    RtlCopyMemory(Palette, Table, Count * sizeof(*Palette));
#endif

    /* Set the palette */
    VidBitBlt(PaletteBitmapBuffer, 0, 0);
}

VOID
InbvScrSetFont(
    _In_ PUCHAR FontBitfield)
{
    // TODO: Redraw the whole screenbuffer with the new font.
}

/* EOF */
