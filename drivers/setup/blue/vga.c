/*
 * PROJECT:     ReactOS Console Text-Mode Device Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VGA-specific routines.
 * COPYRIGHT:   Copyright 1999 Boudewijn Dekker <ariadne@xs4all.nl>
 *              Copyright 1999-2019 Eric Kohl <eric.kohl@reactos.org>
 *              Copyright 2006 Filip Navara <navaraf@reactos.org>
 *              Copyright 2019-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include "blue.h"
#include "vga.h"

#define NDEBUG
#include <debug.h>

/* NOTES *********************************************************************/
/*
 *  [[character][attribute]][[character][attribute]]....
 */

/* TYPEDEFS ******************************************************************/

#define VGA_CHAR_SIZE 2

typedef struct _VGA_REGISTERS
{
    UCHAR CRT[24];
    UCHAR Attribute[21];
    UCHAR Graphics[9];
    UCHAR Sequencer[5];
    UCHAR Misc;
} VGA_REGISTERS, *PVGA_REGISTERS;

static const VGA_REGISTERS VidpMode3Regs =
{
    /* CRT Controller Registers */
    {0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x47, 0x1E, 0x00,
    0x00, 0x00, 0x05, 0xF0, 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3},
    /* Attribute Controller Registers */
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x0C, 0x00, 0x0F, 0x08, 0x00},
    /* Graphics Controller Registers */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF},
    /* Sequencer Registers */
    {0x03, 0x00, 0x03, 0x00, 0x02},
    /* Misc Output Register */
    0x67
};

/* FUNCTIONS *****************************************************************/

static VOID
FASTCALL
VgaScrSetRegisters(
    _In_ const VGA_REGISTERS *Registers)
{
    UINT32 i;

    /* Update misc output register */
    WRITE_PORT_UCHAR(MISC, Registers->Misc);

    /* Synchronous reset on */
    WRITE_PORT_UCHAR(SEQ, 0x00);
    WRITE_PORT_UCHAR(SEQDATA, 0x01);

    /* Write sequencer registers */
    for (i = 1; i < sizeof(Registers->Sequencer); i++)
    {
        WRITE_PORT_UCHAR(SEQ, i);
        WRITE_PORT_UCHAR(SEQDATA, Registers->Sequencer[i]);
    }

    /* Synchronous reset off */
    WRITE_PORT_UCHAR(SEQ, 0x00);
    WRITE_PORT_UCHAR(SEQDATA, 0x03);

    /* Deprotect CRT registers 0-7 */
    WRITE_PORT_UCHAR(CRTC, 0x11);
    WRITE_PORT_UCHAR(CRTCDATA, Registers->CRT[0x11] & 0x7f);

    /* Write CRT registers */
    for (i = 0; i < sizeof(Registers->CRT); i++)
    {
        WRITE_PORT_UCHAR(CRTC, i);
        WRITE_PORT_UCHAR(CRTCDATA, Registers->CRT[i]);
    }

    /* Write graphics controller registers */
    for (i = 0; i < sizeof(Registers->Graphics); i++)
    {
        WRITE_PORT_UCHAR(GRAPHICS, i);
        WRITE_PORT_UCHAR(GRAPHICSDATA, Registers->Graphics[i]);
    }

    /* Write attribute controller registers */
    for (i = 0; i < sizeof(Registers->Attribute); i++)
    {
        READ_PORT_UCHAR(STATUS);
        WRITE_PORT_UCHAR(ATTRIB, i);
        WRITE_PORT_UCHAR(ATTRIB, Registers->Attribute[i]);
    }

    /* Set the PEL mask */
    WRITE_PORT_UCHAR(PELMASK, 0xff);
}

VOID
FASTCALL
VgaScrAcquireOwnership(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    UCHAR data, value;
    ULONG offset;

    _disable();

    VgaScrSetRegisters(&VidpMode3Regs);

#if 0
    /* Disable screen and enable palette access */
    READ_PORT_UCHAR(STATUS);
    WRITE_PORT_UCHAR(ATTRIB, 0x00);

    // Setting a default palette...
#endif
    /* Enable screen and disable palette access */
    READ_PORT_UCHAR(STATUS);
    WRITE_PORT_UCHAR(ATTRIB, 0x20);

    /* Switch blinking characters off */
    READ_PORT_UCHAR(ATTRC_INPST1);
    value = READ_PORT_UCHAR(ATTRC_WRITEREG);
    WRITE_PORT_UCHAR(ATTRC_WRITEREG, 0x10);
    data  = READ_PORT_UCHAR(ATTRC_READREG);
    data  = data & ~0x08;
    WRITE_PORT_UCHAR(ATTRC_WRITEREG, data);
    WRITE_PORT_UCHAR(ATTRC_WRITEREG, value);
    READ_PORT_UCHAR(ATTRC_INPST1);

    /* Read screen information from CRT controller */
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_COLUMNS);
    DeviceExtension->Columns = READ_PORT_UCHAR(CRTC_DATA) + 1;
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_ROWS);
    DeviceExtension->Rows = READ_PORT_UCHAR(CRTC_DATA);
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_OVERFLOW);
    data = READ_PORT_UCHAR(CRTC_DATA);
    DeviceExtension->Rows |= (((data & 0x02) << 7) | ((data & 0x40) << 3));
    DeviceExtension->Rows++;
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_SCANLINES);
    DeviceExtension->ScanLines = (READ_PORT_UCHAR(CRTC_DATA) & 0x1F) + 1;

    /* Retrieve the current output cursor position */
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSORPOSLO);
    offset = READ_PORT_UCHAR(CRTC_DATA);
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSORPOSHI);
    offset += (READ_PORT_UCHAR(CRTC_DATA) << 8);

    /* Show blinking cursor */
    // FIXME: cursor block? Call ScrSetCursorShape() instead?
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSORSTART);
    WRITE_PORT_UCHAR(CRTC_DATA, (DeviceExtension->ScanLines - 1) & 0x1F);
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSOREND);
    data = READ_PORT_UCHAR(CRTC_DATA) & 0xE0;
    WRITE_PORT_UCHAR(CRTC_DATA,
                     data | ((DeviceExtension->ScanLines - 1) & 0x1F));

    _enable();

    /* Calculate number of text rows */
    DeviceExtension->Rows = DeviceExtension->Rows / DeviceExtension->ScanLines;

    /* Set the cursor position, clipping it to the screen */
    DeviceExtension->CursorX = (USHORT)(offset % DeviceExtension->Columns);
    DeviceExtension->CursorY = (USHORT)(offset / DeviceExtension->Columns);
    // DeviceExtension->CursorX = min(max(DeviceExtension->CursorX, 0), DeviceExtension->Columns - 1);
    DeviceExtension->CursorY = min(max(DeviceExtension->CursorY, 0), DeviceExtension->Rows - 1);
}

VOID
FASTCALL
VgaScrSetCursor(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    ULONG Offset;

    Offset = (DeviceExtension->CursorY * DeviceExtension->Columns) + DeviceExtension->CursorX;

    _disable();
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSORPOSLO);
    WRITE_PORT_UCHAR(CRTC_DATA, Offset);
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSORPOSHI);
    WRITE_PORT_UCHAR(CRTC_DATA, Offset >> 8);
    _enable();
}

VOID
FASTCALL
VgaScrSetCursorShape(
    _In_ PDEVICE_EXTENSION DeviceExtension)
{
    ULONG size, height;
    UCHAR data, value;

    height = DeviceExtension->ScanLines;
    data = (DeviceExtension->CursorVisible) ? 0x00 : 0x20;

    size = (DeviceExtension->CursorSize * height) / 100;
    if (size < 1)
        size = 1;

    data |= (UCHAR)(height - size);

    _disable();
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSORSTART);
    WRITE_PORT_UCHAR(CRTC_DATA, data);
    WRITE_PORT_UCHAR(CRTC_COMMAND, CRTC_CURSOREND);
    value = READ_PORT_UCHAR(CRTC_DATA) & 0xE0;
    WRITE_PORT_UCHAR(CRTC_DATA, value | (height - 1));
    _enable();
}

VOID
VgaScrWriteCharactersAttributes(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PCHAR Buffer,
    _In_ ULONG Length,
    _In_ USHORT StartPosX,
    _In_ USHORT StartPosY,
    _Out_opt_ PULONG WrittenLength)
{
    PUCHAR vidmem = DeviceExtension->VideoMemory;
    ULONG offset = StartPosX + StartPosY * DeviceExtension->Columns;

    // FIXME: Does the buffer only contains chars? or chars + attributes?
    // FIXME2: Fix buffer overflow.
    RtlCopyMemory(&vidmem[offset * VGA_CHAR_SIZE], Buffer, Length);

    if (WrittenLength)
        *WrittenLength = Length;
}

VOID
VgaScrWriteCharacter(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ CHAR Character,
    _In_ USHORT CharAttribute,
    _In_ USHORT PosX,
    _In_ USHORT PosY)
{
    PUCHAR vidmem = DeviceExtension->VideoMemory;
    ULONG offset = PosX + PosY * DeviceExtension->Columns;

    vidmem[offset * VGA_CHAR_SIZE] = Character;
    vidmem[offset * VGA_CHAR_SIZE + 1] = (char)CharAttribute;
}

VOID
VgaScrScrollUp(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ CHAR FillCharacter,
    _In_ USHORT FillAttribute)
{
    PUCHAR vidmem;
    PUSHORT LinePtr;
    ULONG i;
    ULONG j, offset;
    USHORT cursory;
    USHORT rows, columns;

    vidmem  = DeviceExtension->VideoMemory;
    rows    = DeviceExtension->Rows;
    columns = DeviceExtension->Columns;
    cursory = DeviceExtension->CursorY;

    RtlCopyMemory(vidmem,
                  &vidmem[columns * VGA_CHAR_SIZE],
                  columns * (rows - 1) * VGA_CHAR_SIZE);

    LinePtr = (PUSHORT)&vidmem[columns * (rows - 1) * VGA_CHAR_SIZE];

    for (j = 0; j < columns; j++)
    {
        LinePtr[j] = DeviceExtension->CharAttribute << 8;
    }
    cursory = rows - 1;
    // INVESTIGATE: The following was added in commit
    // https://github.com/reactos/reactos/commit/5716745a13f2467e3b9ba28a89bbf452ee798b0a
    for (j = 0; j < columns; j++)
    {
        offset = j + cursory * columns;
        vidmem[offset * VGA_CHAR_SIZE] = ' ';
        vidmem[offset * VGA_CHAR_SIZE + 1] = (char)DeviceExtension->CharAttribute;
    }
}

VOID
VgaScrSetPalette(
    _In_reads_(Count) const RGBQUAD* Table,
    _In_ ULONG Count)
{
    const RGBQUAD* Entry = Table;
    ULONG i;

    for (i = 0; i < Count; i++, Entry++)
    {
        // SetPaletteEntryRGB(i, *Entry);
        WRITE_PORT_UCHAR(PELINDEX, i);
        WRITE_PORT_UCHAR(PELDATA, Entry->rgbRed >> 2);
        WRITE_PORT_UCHAR(PELDATA, Entry->rgbGreen >> 2);
        WRITE_PORT_UCHAR(PELDATA, Entry->rgbBlue >> 2);
    }
}

/* In font.c */
// VOID
// VgaScrSetFont(
//     _In_ PUCHAR FontBitfield);

/* EOF */
