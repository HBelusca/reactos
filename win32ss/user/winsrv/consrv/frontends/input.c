/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/frontends/input.c
 * PURPOSE:         Common Front-Ends Input functions
 * PROGRAMMERS:     Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include "consrv.h"
#include "include/term.h"
#include "coninput.h"

#define NDEBUG
#include <debug.h>


/* PRIVATE FUNCTIONS **********************************************************/

static DWORD
ConioGetShiftState(PBYTE KeyState, LPARAM lParam)
{
    DWORD ssOut = 0;

    if (KeyState[VK_CAPITAL] & 0x01)
        ssOut |= CAPSLOCK_ON;

    if (KeyState[VK_NUMLOCK] & 0x01)
        ssOut |= NUMLOCK_ON;

    if (KeyState[VK_SCROLL] & 0x01)
        ssOut |= SCROLLLOCK_ON;

    if (KeyState[VK_SHIFT] & 0x80)
    // || (KeyState[VK_LSHIFT] & 0x80) || (KeyState[VK_RSHIFT] & 0x80)
        ssOut |= SHIFT_PRESSED;

    if (KeyState[VK_LCONTROL] & 0x80)
        ssOut |= LEFT_CTRL_PRESSED;
    if (KeyState[VK_RCONTROL] & 0x80)
        ssOut |= RIGHT_CTRL_PRESSED;
    // if (KeyState[VK_CONTROL] & 0x80) { ... }

    if (KeyState[VK_LMENU] & 0x80)
        ssOut |= LEFT_ALT_PRESSED;
    if (KeyState[VK_RMENU] & 0x80)
        ssOut |= RIGHT_ALT_PRESSED;
    // if (KeyState[VK_MENU] & 0x80) { ... }

    /* See WM_CHAR MSDN documentation for instance */
    if (HIWORD(lParam) & KF_EXTENDED)
        ssOut |= ENHANCED_KEY;

    /* Remember if the key was emitted as part of a Alt+Numpad sequence
     * and requires to be converted from input OEM code page to Unicode */
    ssOut |= (lParam & ALTNUMPAD_BIT);

    return ssOut;
}

#if !defined(KEY_TOGGLED) || !defined(KEY_PRESSED)
#define KEY_TOGGLED 0x0001
#define KEY_PRESSED 0x8000
#endif
static DWORD
TESTConioGetKeyState(LPARAM lParam)
{
    DWORD dwControlKeyState = 0;

    if (GetKeyState(VK_RMENU) & KEY_PRESSED)
        dwControlKeyState |= RIGHT_ALT_PRESSED;
    if (GetKeyState(VK_LMENU) & KEY_PRESSED)
        dwControlKeyState |= LEFT_ALT_PRESSED;
    if (GetKeyState(VK_RCONTROL) & KEY_PRESSED)
        dwControlKeyState |= RIGHT_CTRL_PRESSED;
    if (GetKeyState(VK_LCONTROL) & KEY_PRESSED)
        dwControlKeyState |= LEFT_CTRL_PRESSED;
    if (GetKeyState(VK_SHIFT) & KEY_PRESSED)
        dwControlKeyState |= SHIFT_PRESSED;
    if (GetKeyState(VK_NUMLOCK) & KEY_TOGGLED)
        dwControlKeyState |= NUMLOCK_ON;
    if (GetKeyState(VK_SCROLL) & KEY_TOGGLED)
        dwControlKeyState |= SCROLLLOCK_ON;
    if (GetKeyState(VK_CAPITAL) & KEY_TOGGLED)
        dwControlKeyState |= CAPSLOCK_ON;
    /* See WM_CHAR MSDN documentation for instance */
    if (HIWORD(lParam) & KF_EXTENDED)
        dwControlKeyState |= ENHANCED_KEY;

    /* Remember if the key was emitted as part of a Alt+Numpad sequence
     * and requires to be converted from input OEM code page to Unicode */
    dwControlKeyState |= (lParam & ALTNUMPAD_BIT);

    return dwControlKeyState;
}

VOID NTAPI
ConioProcessKey(PCONSRV_CONSOLE Console, MSG* msg)
{
    static BYTE KeyState[256] = { 0 };
    /* MSDN mentions that you should use the last virtual key code received
     * when putting a virtual key identity to a WM_CHAR message since multiple
     * or translated keys may be involved. */
    static UINT LastVirtualKey = 0;
    DWORD ShiftState;
    WCHAR UnicodeChar;
    UINT VirtualKeyCode;
    UINT VirtualScanCode;
    BOOL KeyDown;
    BOOLEAN Fake;    // Synthesized, not a real event
    BOOLEAN IsCharMsg, NotChar; // Message should not be used to return a character
    INPUT_RECORD er;

    if (Console == NULL)
    {
        DPRINT1("No Active Console!\n");
        return;
    }

    VirtualScanCode = HIWORD(msg->lParam) & 0xFF;
    KeyDown = !(HIWORD(msg->lParam) & KF_UP);
    IsCharMsg = (msg->message == WM_CHAR || msg->message == WM_SYSCHAR);

    /* Sanity checks for Win32k info */
    if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)
    {
        ASSERT((msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) && KeyDown);
    }
    if (msg->message == WM_KEYUP || msg->message == WM_SYSKEYUP)
    {
        ASSERT((msg->message == WM_KEYUP || msg->message == WM_SYSKEYUP) && !KeyDown);
        // ASSERT((msg->message == WM_KEYUP || msg->message == WM_SYSKEYUP) && (HIWORD(msg->lParam) & KF_REPEAT));
        ASSERT((msg->message == WM_KEYUP || msg->message == WM_SYSKEYUP) && (LOWORD(msg->lParam) == 1));
    }
    if (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP)
    {
        ASSERT((msg->message == WM_KEYDOWN || msg->message == WM_KEYUP) && !(HIWORD(msg->lParam) & KF_ALTDOWN));
    }

//__debugbreak();
    GetKeyboardState(KeyState);
    ShiftState = ConioGetShiftState(KeyState, msg->lParam);
    {
    DWORD dwKeyState = TESTConioGetKeyState(msg->lParam);
    if (ShiftState != dwKeyState)
        DPRINT1("**** CONSRV!%s(): Discrepancy ShiftState: 0x%08lx, dwKeyState: 0x%08lx ****\n",
                ShiftState, dwKeyState);
    }

    if (IsCharMsg) // || (msg->message == WM_DEADCHAR || msg->message == WM_SYSDEADCHAR)
    {
        VirtualKeyCode = LastVirtualKey;
        UnicodeChar = msg->wParam;
    }
    else
    {
        //WCHAR Chars[2];
        //INT RetChars = 0;

        VirtualKeyCode = msg->wParam;
#if 0
        RetChars = ToUnicodeEx(VirtualKeyCode,
                               VirtualScanCode,
                               KeyState,
                               Chars,
                               ARRAYSIZE(Chars),
                               0,
                               NULL);
        UnicodeChar = (RetChars == 1 ? Chars[0] : 0);
#else
        UnicodeChar = 0;
#endif
    }

    Fake = UnicodeChar &&
            (msg->message != WM_CHAR && msg->message != WM_SYSCHAR &&
             msg->message != WM_KEYUP && msg->message != WM_SYSKEYUP);
    NotChar = !IsCharMsg;
    if (NotChar) LastVirtualKey = msg->wParam;

    DPRINT1("CONSRV: %08lx %s %s %s %s %02x %04x '%lc' %08x\n",
           msg->message,
           KeyDown ? "down" : "up  ",
           IsCharMsg ? "char" : "key ",
           Fake ? "fake" : "real",
           NotChar ? "notc" : "char",
           VirtualScanCode,
           VirtualKeyCode,
           (UnicodeChar >= L' ') ? UnicodeChar : L'.',
           ShiftState);

    if (Fake) return;

//
// FIXME: Scrolling via keyboard shortcuts must be done differently,
// without touching the internal VirtualY variable.
//
#if 0
    if ( (ShiftState & (RIGHT_ALT_PRESSED | LEFT_ALT_PRESSED)) != 0 &&
         (VirtualKeyCode == VK_UP || VirtualKeyCode == VK_DOWN) )
    {
        if (!KeyDown) return;

        /* Scroll up or down */
        if (VirtualKeyCode == VK_UP)
        {
            /* Only scroll up if there is room to scroll up into */
            if (Console->ActiveBuffer->CursorPosition.Y != Console->ActiveBuffer->ScreenBufferSize.Y - 1)
            {
                Console->ActiveBuffer->VirtualY = (Console->ActiveBuffer->VirtualY +
                                                   Console->ActiveBuffer->ScreenBufferSize.Y - 1) %
                                                   Console->ActiveBuffer->ScreenBufferSize.Y;
                Console->ActiveBuffer->CursorPosition.Y++;
            }
        }
        else
        {
            /* Only scroll down if there is room to scroll down into */
            if (Console->ActiveBuffer->CursorPosition.Y != 0)
            {
                Console->ActiveBuffer->VirtualY = (Console->ActiveBuffer->VirtualY + 1) %
                                                   Console->ActiveBuffer->ScreenBufferSize.Y;
                Console->ActiveBuffer->CursorPosition.Y--;
            }
        }

        ConioDrawConsole(Console);
        return;
    }
#endif

    /* Send the key press to the console driver */

#ifndef FAKE_KEYSTROKE // FIXME: <ndk/kbd.h>
#define FAKE_KEYSTROKE  0x02000000
#endif

    // TODO: Check with https://github.com/microsoft/terminal/pull/15753/files
    er.EventType                        = KEY_EVENT;
    er.Event.KeyEvent.bKeyDown          = KeyDown;
    er.Event.KeyEvent.wRepeatCount      = LOWORD(msg->lParam);
    er.Event.KeyEvent.wVirtualKeyCode   = VirtualKeyCode;
    er.Event.KeyEvent.wVirtualScanCode  = (msg->lParam & FAKE_KEYSTROKE) ? 0 : VirtualScanCode; // VirtualScanCode; // TODO: Zero if it's a fake character.
    er.Event.KeyEvent.uChar.UnicodeChar = NotChar ? 0 : UnicodeChar; // UnicodeChar; // TODO: No character if !WM_CHAR && !WM_SYSCHAR
    er.Event.KeyEvent.dwControlKeyState = ShiftState;

#if DBG
    {
    PKEY_EVENT_RECORD kr = &er.Event.KeyEvent;
    DbgPrint("CONSRV KEY_EVENT: KeyDown: %lu, RepeatCount: %u, CtrlKeyState: 0x%08lx, VirtKeyCode: 0x%04x, VirtScanCode: 0x%04x, UChar: 0x%04x (%u) L'%C', AChar: 0x%02x (%u) '%c'\n",
        kr->bKeyDown, kr->wRepeatCount, kr->dwControlKeyState, kr->wVirtualKeyCode, kr->wVirtualScanCode,
        (unsigned short)kr->uChar.UnicodeChar, (unsigned short)kr->uChar.UnicodeChar, (kr->uChar.UnicodeChar > 0) ? kr->uChar.UnicodeChar : L'.',
        (unsigned char)kr->uChar.AsciiChar, (unsigned char)kr->uChar.AsciiChar, (kr->uChar.AsciiChar > 0) ? kr->uChar.AsciiChar : '.');
    }
#endif
    ConioProcessInputEvent(Console, &er);
}

DWORD
ConioEffectiveCursorSize(PCONSRV_CONSOLE Console, DWORD Scale)
{
    DWORD Size = (Console->ActiveBuffer->CursorInfo.dwSize * Scale + 99) / 100;
    /* If line input in progress, perhaps adjust for insert toggle */
    if (Console->LineBuffer && !Console->LineComplete && (Console->InsertMode ? !Console->LineInsertToggle : Console->LineInsertToggle))
        return (Size * 2 <= Scale) ? (Size * 2) : (Size / 2);
    return Size;
}

/* EOF */
