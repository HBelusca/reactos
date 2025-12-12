/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Emulated console implementation
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <freeldr.h>
#include "console.h"

/* GLOBALS ********************************************************************/

/* Current cursor X/Y positions on the screen */
static ULONG CurrentCursorX = 0;
static ULONG CurrentCursorY = 0;

/* Current attributes on the screen */
static UCHAR CurrentAttr = ATTR(COLOR_GRAY, COLOR_BLACK);

#define TAB_WIDTH   8

/* OUTPUT FUNCTIONS ***********************************************************/

/**
 * @brief   Prints formatted text to the console.
 **/
INT
printf(
    _In_ PCSTR Format, ...)
{
    va_list ap;
    INT Length;
    CHAR Buffer[512];

    va_start(ap, Format);
    Length = _vsnprintf(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    if (Length == -1)
        Length = (INT)sizeof(Buffer);

    ConsWriteString(Buffer, Length);
ConsGetCh();
    return Length;
}

/**
 * @brief
 * Initializes the console with the specified parameters.
 *
 * @return
 * TRUE if initialization is successful; FALSE if not.
 **/
BOOLEAN
ConsInitialize(
    _In_ ULONG InitialCursorX,
    _In_ ULONG InitialCursorY,
    _In_ UCHAR InitialAttributes)
{
    CurrentCursorX = InitialCursorX;
    CurrentCursorY = InitialCursorY;
    CurrentAttr = InitialAttributes;
    // MachVtbl.ConsInit();
    return TRUE;
}

/**
 * @brief
 * Clears the screen with the given text color attribute, setting the current
 * text attributes to the one specified, and resets the current cursor to the
 * top-left position.
 **/
VOID
ConsClearScreen(
    _In_ UCHAR Attr)
{
    CurrentCursorX = 0;
    CurrentCursorY = 0;
    CurrentAttr = Attr;
    MachVtbl.VideoClearScreen(Attr);
}

static VOID
ConsWriteCharWorker(
    _In_ UCHAR Char,
    _In_ ULONG ConsoleWidth,
    _In_ ULONG ConsoleHeight)
{
    BOOLEAN NeedScroll;

    NeedScroll = (CurrentCursorY >= ConsoleHeight);
    if (NeedScroll)
    {
        FbConsScrollUp(CurrentAttr); // FIXME!
        --CurrentCursorY;
    }

    if (Char == '\a')
    {
        MachBeep();
    }
    else if (Char == '\b')
    {
        if (CurrentCursorX > 0)
        {
            --CurrentCursorX;
        }
        else if (CurrentCursorX == 0 && CurrentCursorY > 0)
        {
            --CurrentCursorY;
            CurrentCursorX = ConsoleWidth - 1;
        }
        MachVideoPutChar(' ', CurrentAttr, CurrentCursorX, CurrentCursorY);
        return;
    }

    if (Char == '\r')
    {
        CurrentCursorX = 0;
    }
    else if (Char == '\n')
    {
        /* We display a CR '\n', do a LF also */
        CurrentCursorX = 0;
        if (!NeedScroll)
            ++CurrentCursorY;
    }
    else if (Char == '\t')
    {
        /* Display a TAB '\t' by issuing 8 spaces ' ' */
        // CurrentCursorX = (CurrentCursorX + TAB_WIDTH) & ~TAB_WIDTH;
        UINT8 i;
        for (i = 0; i < TAB_WIDTH; ++i)
            ConsWriteCharWorker(' ', ConsoleWidth, ConsoleHeight);
        return;
    }
    else
    {
        MachVideoPutChar(Char, CurrentAttr, CurrentCursorX, CurrentCursorY);
        ++CurrentCursorX;
    }

    if (CurrentCursorX >= ConsoleWidth)
    {
        CurrentCursorX = 0;
        ++CurrentCursorY;
    }
}

/**
 * @brief
 * Outputs the given character using the current attributes, to the console
 * at the current X/Y position.
 **/
// NOTE: Generic console function, doesn't depend on graphics vs. text
VOID
ConsWriteChar(
    _In_ UCHAR Char)
{
    ULONG Width, Height, Unused;

    if (MachVtbl.ConsPutChar)
    {
        MachVtbl.ConsPutChar(Char);
        return;
    }

    MachVideoGetDisplaySize(&Width, &Height, &Unused);
    ConsWriteCharWorker(Char, Width, Height);
}

// NOTE: Generic console function, doesn't depend on graphics vs. text
VOID
ConsWriteString(
    _In_ _When_(Count == -1, _Pre_z_) PCCH String,
    _In_ ULONG Count)
{
    ULONG Width, Height, Unused;

    if (MachVtbl.ConsPutChar)
    {
        if (Count == -1)
        {
            for (; *String != ANSI_NULL; ++String)
                MachVtbl.ConsPutChar(*String);
        }
        else
        {
            for (; Count--; ++String)
                MachVtbl.ConsPutChar(*String);
        }
        return;
    }

    MachVideoGetDisplaySize(&Width, &Height, &Unused);

    if (Count == -1)
    {
        for (; *String != ANSI_NULL; ++String)
            ConsWriteCharWorker(*String, Width, Height);
    }
    else
    {
        for (; Count--; ++String)
            ConsWriteCharWorker(*String, Width, Height);
    }
}

// TODO: Support scrolling a specific rectangle only.
VOID
ConsScrollUp(
    _In_ UCHAR Attr)
{
}

VOID
ConsSetCursorPosition(
    _In_ ULONG X,
    _In_ ULONG Y)
{
    CurrentCursorX = X;
    CurrentCursorY = Y;
    MachVtbl.VideoSetTextCursorPosition(X, Y);
}


/* INPUT FUNCTIONS ************************************************************/

BOOLEAN
ConsKbHit(VOID)
{
    return MachVtbl.ConsKbHit();
}

UINT
ConsGetCh(VOID)
{
    return MachVtbl.ConsGetCh();
}
