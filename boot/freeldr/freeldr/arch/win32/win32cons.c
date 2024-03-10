/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Console output
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>
#include <wincon.h>
#include <winuser.h> // For VK_*

static unsigned CurrentCursorX = 0;
static unsigned CurrentCursorY = 0;
static unsigned CurrentAttr = 0x0f;

HANDLE hConIn;

// int _putch(int c);
// int _putchar(int c);
VOID
Win32ConsPutChar(int c)
{
    ULONG Width, Height, Unused;
    BOOLEAN NeedScroll;

    Win32VideoGetDisplaySize(&Width, &Height, &Unused);

    NeedScroll = (CurrentCursorY >= Height);
    if (NeedScroll)
    {
        Win32VideoScrollUp();
        --CurrentCursorY;
    }

    if (c == '\r')
    {
        CurrentCursorX = 0;
    }
    else if (c == '\n')
    {
        CurrentCursorX = 0;

        if (!NeedScroll)
            ++CurrentCursorY;
    }
    else if (c == '\t')
    {
        CurrentCursorX = (CurrentCursorX + 8) & ~ 7;
    }
    else
    {
        Win32VideoPutChar(c, CurrentAttr, CurrentCursorX, CurrentCursorY);
        CurrentCursorX++;
    }

    if (CurrentCursorX >= Width)
    {
        CurrentCursorX = 0;
        CurrentCursorY++;
    }
}

// int _kbhit();
BOOLEAN
Win32ConsKbHit(VOID)
{
    INPUT_RECORD ir = { 0 };
    ULONG dwAvail;

    /* Check whether there are key events in the console input queue,
     * getting rid of the other ones... */
    while (TRUE)
    {
        if (!PeekConsoleInputA(hConIn, &ir, 1, &dwAvail))
            return FALSE;

        /* If no events, bail out */
        if (!dwAvail)
            return FALSE;

        /* Only check for the first key-down character */
        if ((ir.EventType == KEY_EVENT) && ir.Event.KeyEvent.bKeyDown &&
            /*ir.Event.KeyEvent.uChar.AsciiChar*/
            ir.Event.KeyEvent.wVirtualKeyCode) // wVirtualScanCode
        {
            /* Found at least one! */
            return TRUE;
        }
        /* Otherwise remove that event */
        ReadConsoleInputA(hConIn, &ir, 1, &dwAvail);
    }
}

static
UCHAR
ConvertToBiosExtValue(UCHAR KeyIn)
{
    switch (KeyIn)
    {
        case VK_UP:
            return KEY_UP;
        case VK_DOWN:
            return KEY_DOWN;
        case VK_RIGHT:
            return KEY_RIGHT;
        case VK_LEFT:
            return KEY_LEFT;
        case VK_HOME:
            return KEY_HOME;
        case VK_END:
            return KEY_END;

        // case VK_INSERT:
        //     break;

        case VK_DELETE:
            return KEY_DELETE;

        // case VK_PRIOR:
        // case VK_NEXT:
        //     break;

        case VK_F1:
            return KEY_F1;
        case VK_F2:
            return KEY_F2;
        case VK_F3:
            return KEY_F3;
        case VK_F4:
            return KEY_F4;
        case VK_F5:
            return KEY_F5;
        case VK_F6:
            return KEY_F6;
        case VK_F7:
            return KEY_F7;
        case VK_F8:
            return KEY_F8;
        case VK_F9:
            return KEY_F9;
        case VK_F10:
            return KEY_F10;
        case VK_ESCAPE:
            return KEY_ESC;
    }
    return 0;
}

// int _getch();
// int _getchar();
int
Win32ConsGetCh(VOID)
{
    static BOOLEAN ExtendedKey = FALSE;
    static UCHAR ExtendedScanCode = 0;
    INPUT_RECORD ir = { 0 };
    PKEY_EVENT_RECORD pker = &ir.Event.KeyEvent;
    ULONG dwAvail;

    /* If an extended key press was detected the last time
     * we were called, return the scan code of that key. */
    if (ExtendedKey)
    {
        ExtendedKey = FALSE;
        return ExtendedScanCode;
    }

    /* Wait for new key events in the console input queue,
     * getting rid of the other ones... */
    while (TRUE)
    {
        /* Always remove that event */
        if (!ReadConsoleInputA(hConIn, &ir, 1, &dwAvail))
            return 0;

        /* If no events, bail out */
        if (!dwAvail)
            return 0;

        /* Only check for the first key-down character */
        if ((ir.EventType == KEY_EVENT) && pker->bKeyDown)
        {
            /* Found at least one! */
            if (pker->uChar.AsciiChar)
            {
                return pker->uChar.AsciiChar;
            }
            else
            {
                ExtendedKey = TRUE;
                ExtendedScanCode = ConvertToBiosExtValue(pker->wVirtualKeyCode); // wVirtualScanCode
                return KEY_EXTENDED;
            }
        }
    }
}

/* EOF */
