/*
 *  ReactOS kernel
 *  Copyright (C) 2002 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS text-mode setup
 * FILE:            base/setup/usetup/keytrans.c
 * PURPOSE:         Console support functions: keyboard translation
 * PROGRAMMER:      Tinus
 *
 * NB: Hardcoded to US keyboard
 */
#include <usetup.h>
#include "keytrans.h"

#include <ndk/umfuncs.h>

#define NDEBUG
#include <debug.h>


HANDLE HandleDll = NULL;
PKBDTABLES pKbdTbl = NULL; // KbdTablesFallback;

// gafRawKeyState;
BYTE afKeyState[256 * 2 / 8]; // 2 bits per key


/*
 * IntKeyboardUpdateLeds
 *
 * Sends the keyboard commands to turn on/off the lights
 */
static
NTSTATUS
IntKeyboardUpdateLeds(
    HANDLE hKeyboardDevice,
    // WORD wVk,
    // WORD wScanCode
    DWORD oldState,
    DWORD newState)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    KEYBOARD_INDICATOR_PARAMETERS kip;

    /*
     * Check if the state of the indicators has been changed.
     * TODO: For Japan (and CJK world), handle VK_KANA.
     */
    if ((oldState ^ newState) & (NUMLOCK_ON | CAPSLOCK_ON | SCROLLLOCK_ON))
    {
        kip.UnitId   = 0;
        kip.LedFlags = 0;

        if ((newState & NUMLOCK_ON))
            kip.LedFlags |= KEYBOARD_NUM_LOCK_ON;

        if ((newState & CAPSLOCK_ON))
            kip.LedFlags |= KEYBOARD_CAPS_LOCK_ON;

        if ((newState & SCROLLLOCK_ON))
            kip.LedFlags |= KEYBOARD_SCROLL_LOCK_ON;

        /* Update the state of the leds on primary keyboard */
        DPRINT("NtDeviceIoControlFile dwLeds=%x\n", kip.LedFlags);

        Status = NtDeviceIoControlFile(hKeyboardDevice,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &IoStatusBlock,
                                       IOCTL_KEYBOARD_SET_INDICATORS,
                                       &kip,
                                       sizeof(kip),
                                       NULL,
                                       0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtDeviceIoControlFile(IOCTL_KEYBOARD_SET_INDICATORS) failed (Status %lx)\n", Status);
        }
    }

    return Status;
}

static VOID
IntUpdateControlKeyState(
    IN OUT LPDWORD State, // LedState
    IN PBYTE afKeyState,
    IN BOOLEAN bIsDown)
{
    DWORD Value = 0;
    DWORD oldState, newState;

    oldState = newState = *State;

    /* Get the current state for the console KeyEvent */
    if (IS_KEY_LOCKED(afKeyState, VK_CAPITAL))
    {
        Value |= CAPSLOCK_ON;
        if (bIsDown)
            newState ^= CAPSLOCK_ON;
    }

    if (IS_KEY_LOCKED(afKeyState, VK_NUMLOCK))
    {
        Value |= NUMLOCK_ON;
        if (bIsDown)
            newState ^= NUMLOCK_ON;
    }

    if (IS_KEY_LOCKED(afKeyState, VK_SCROLL))
    {
        Value |= SCROLLLOCK_ON;
        if (bIsDown)
            newState ^= SCROLLLOCK_ON;
    }

    if (IS_KEY_DOWN(afKeyState, VK_SHIFT))
    // || (IS_KEY_DOWN(afKeyState, VK_LSHIFT)) || (IS_KEY_DOWN(afKeyState, VK_RSHIFT))
        Value |= SHIFT_PRESSED;

    if (IS_KEY_DOWN(afKeyState, VK_LCONTROL))
        Value |= LEFT_CTRL_PRESSED;
    if (IS_KEY_DOWN(afKeyState, VK_RCONTROL))
        Value |= RIGHT_CTRL_PRESSED;
    // if (IS_KEY_DOWN(afKeyState, VK_CONTROL)) { ... }

    if (IS_KEY_DOWN(afKeyState, VK_LMENU))
        Value |= LEFT_ALT_PRESSED;
    if (IS_KEY_DOWN(afKeyState, VK_RMENU))
        Value |= RIGHT_ALT_PRESSED;
    // if (IS_KEY_DOWN(afKeyState, VK_MENU)) { ... }

    // newState |= Value; or newState &= ~Value; depending on DOWN or UP.
    newState = Value;

    *State = newState;
}

#if 1

/*static*/ DWORD
IntVKFromKbdInput(
    IN USHORT wScanCode,
    IN PBYTE afKeyState,
    IN PKBDTABLES pKbdTbl)
{
    WORD wVk;

    /* Convert scan code to virtual key */
    // wVk = IntMapVirtualKeyEx(wScanCode, MAPVK_VSC_TO_VK_EX, pKbdTbl);
    wVk = IntVscToVk(wScanCode, pKbdTbl);

    /* Do nothing more if == 0 */
    if (wVk == 0)
        return wVk;

    /* Support numlock */
    if ((wVk & KBDNUMPAD) && IS_KEY_LOCKED(afKeyState, VK_NUMLOCK))
    {
        wVk = IntTranslateNumpadKey(wVk & 0xFF);
    }

    return wVk;
}

#endif

NTSTATUS
IntTranslateKey(HANDLE hConsoleInput, PKEYBOARD_INPUT_DATA InputData, KEY_EVENT_RECORD *Event)
{
    static DWORD dwControlKeyState = 0;
    DWORD dwOldCtlState;
    WORD wScanCode, wVk, wSimpleVk, wVk2;
    BOOLEAN bKeyDown;

    RtlZeroMemory(Event, sizeof(KEY_EVENT_RECORD));

    DPRINT("Translating: %x\n", InputData->MakeCode);

    /*
     * Low-level (hardware) translation.
     */

    /* Calculate scan code with prefix */
    wScanCode = InputData->MakeCode & 0x7F;
    if (InputData->Flags & KEY_E0)
        wScanCode |= 0xE000;
    if (InputData->Flags & KEY_E1)
        wScanCode |= 0xE100;

/*
 *  wVk = IntVKFromKbdInput(wScanCode, afKeyState, pKbdTbl);
 */
    /* Convert scan code to virtual key */
    // wVk = IntMapVirtualKeyEx(wScanCode, MAPVK_VSC_TO_VK_EX, pKbdTbl);
    wVk = IntVscToVk(wScanCode, pKbdTbl);

    /* Do nothing more if == 0 */
    if (wVk == 0)
        return STATUS_NO_MORE_ENTRIES; // Some "meaningful" status code...

//
// TODO: Proper handling of VK_PAUSE
//

//
// TODO: Proper handling of KBDMULTIVK, doing equivalent of IntGetModBits()
// but for low-level translation.
// Dependent on the underlying keyboard type: IBM PC/XT (83-key),
// Olivetti "ICO" (102-key), IBM PC/AT (84-key), IBM enhanced (101/102-key),
// etc...
//


    /*
     * High-level translation.
     */

//
// TODO: Update the raw key state now.
//

    bKeyDown = (!(InputData->Flags & KEY_BREAK));

    /***/
    wVk2 = wVk & 0xFF;
    wSimpleVk = IntSimplifyVk(wVk2);
    IntUpdateKeyState(afKeyState, wVk2, bKeyDown);
    if ((wSimpleVk == VK_SHIFT)   ||
        (wSimpleVk == VK_CONTROL) ||
        (wSimpleVk == VK_MENU))
    {
        IntUpdateKeyState(afKeyState, wSimpleVk, bKeyDown);
    }
    /***/


//
// TODO:
// On Windows it seems the numlock translation is done later:
// in the equivalent of IntTranslateChar() proper.
//
    /* Support numlock */
    if ((wVk & KBDNUMPAD) && IS_KEY_LOCKED(afKeyState, VK_NUMLOCK))
    {
        wVk = IntTranslateNumpadKey(wVk & 0xFF);
    }

    // TODO: AltGr handling if KLLF_ALTGR !
    // TODO: Shift-Lock handling if KLLF_SHIFTLOCK !
    // TODO: Extra handling for OEM (e.g. '00'...), FarEast NLS...

    wVk2 = wVk & 0xFF;
    wSimpleVk = IntSimplifyVk(wVk);

    /* Display the key event info */
    DbgPrint("WINDOWS: LLK: %s scancode=0x%04X virtual=0x%04X isExt=%s WM_MSG=0x%lX\n",
           bKeyDown ? "down" : "up  ",
           wScanCode & 0xFFF, // The "Exxx" part is definitively removed.
           (wVk & 0xFF),
           (wVk & KBDEXT /* or VK_RSHIFT */) ? "yes" : "no ",
           0);


    Event->wVirtualKeyCode  = wSimpleVk & 0xFF;
    Event->wVirtualScanCode = wScanCode & 0x7F;

    Event->bKeyDown = bKeyDown;

// TODO: FIXME: Correctly handle VK_PAUSE -- in Win32k as well!!
    dwOldCtlState = dwControlKeyState;
    if (!(InputData->Flags & KEY_E1)) /* Only the pause key has E1 */
        IntUpdateControlKeyState(&dwControlKeyState, afKeyState, bKeyDown);
    Event->dwControlKeyState = dwControlKeyState;

    /* Now clear up the extended bit for Right-Shift, that is not really
     * extended, but was just used to indicate its right-handedness. */
    if (wSimpleVk == VK_SHIFT)
        wVk &= ~KBDEXT;

    if (wVk & KBDEXT)
        Event->dwControlKeyState |= ENHANCED_KEY;

    /* Update the keyboard indicators if needed */
    if ((dwOldCtlState ^ dwControlKeyState) & (NUMLOCK_ON | CAPSLOCK_ON | SCROLLLOCK_ON))
        IntKeyboardUpdateLeds(hConsoleInput, dwOldCtlState, dwControlKeyState);

    Event->wRepeatCount = 1;

    {
#if 0
        WCHAR UniChars[2];
        INT RetChars = 0;
#else
        BOOL bDead, bLigature;
#endif

#if 0
        RetChars = IntToUnicodeEx(Event->wVirtualKeyCode,
                                  wScanCode, // Event->wVirtualScanCode,
                                  afKeyState,
                                  UniChars,
                                  RTL_NUMBER_OF(UniChars),
                                  0,
                                  pKbdTbl);
#else
        IntTranslateChar(Event->wVirtualKeyCode,
                         afKeyState,
                         &bDead, &bLigature,
                         &Event->uChar.UnicodeChar,
                         pKbdTbl);
#endif
    }

    /* Display the key event info */
    DbgPrint("CONSOLE: key: %s scancode=0x%04X virtual=0x%04X rptCnt=%d ctlState=0x%08lX ascii='%c' (0x%02X) unicode='%lc' (0x%04X)\n",
           Event->bKeyDown ? "down" : "up  ",
           Event->wVirtualScanCode,
           Event->wVirtualKeyCode,
           Event->wRepeatCount,
           Event->dwControlKeyState,
           (Event->uChar.AsciiChar >= ' ') ? Event->uChar.AsciiChar : '.',
                (unsigned char)Event->uChar.AsciiChar,
           (Event->uChar.UnicodeChar >= L' ') ? Event->uChar.UnicodeChar : L'.',
                Event->uChar.UnicodeChar);

    return STATUS_SUCCESS;
}



/*
 * KbdLoadKbdDll
 *
 * Loads keyboard layout DLL and gets address to KbdTables.
 */
NTSTATUS
KbdLoadKbdDll(
    IN PCWSTR pwszLayoutDllPath,
    OUT PHANDLE pHandleDll,
    OUT PKBDTABLES* pKbdTables)
{
    NTSTATUS Status;
    PFN_KBD_LAYER_DESCRIPTOR pfnKbdLayerDescriptor;
    UNICODE_STRING LayoutDllPath;
    HANDLE HandleDll;

    /* Load the keyboard layout DLL */
    DPRINT1("Loading Keyboard DLL %ws\n", pwszLayoutDllPath);

    RtlInitUnicodeString(&LayoutDllPath, pwszLayoutDllPath);
    Status = LdrLoadDll(NULL, /* Default search path */
                        NULL, /* No specific Dll characteristics */
                        &LayoutDllPath,
                        &HandleDll);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to load dll %ws (Status 0x%08lx)\n", pwszLayoutDllPath, Status);
        return Status;
    }
    ASSERT(HandleDll);

    /* Find KbdLayerDescriptor function and get layout tables */
    DPRINT1("Loaded %ws\n", pwszLayoutDllPath);
    Status = LdrGetProcedureAddress(HandleDll,
                                    NULL, /* ANSI export name; we will use ordinal instead */
                                    ORDINAL_KbdLayerDescriptor, // "KbdLayerDescriptor" export
                                    (PVOID*)&pfnKbdLayerDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to retrieve \"%s\" export (ordinal %d) (Status 0x%08lx)\n",
                "KbdLayerDescriptor", ORDINAL_KbdLayerDescriptor, Status);
        goto Quit;
    }
    ASSERT(pfnKbdLayerDescriptor);
    // DPRINT1("Error: %ws has no KbdLayerDescriptor()\n", pwszLayoutDllPath);

    /* Load the keyboard layout descriptor */
    _SEH2_TRY
    {
        *pKbdTables = pfnKbdLayerDescriptor();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *pKbdTables = NULL;
        Status = _SEH2_GetExceptionCode();
        DPRINT1("Exception 0x%lx while calling \"%s\" export (ordinal %d)\n",
                Status, "KbdLayerDescriptor", ORDINAL_KbdLayerDescriptor);
    }
    _SEH2_END;

Quit:
    if (!NT_SUCCESS(Status) || !pfnKbdLayerDescriptor || !*pKbdTables)
    {
        DPRINT1("Failed to load the keyboard layout.\n");
        if (HandleDll) LdrUnloadDll(HandleDll);
        return Status;
    }

    *pHandleDll = HandleDll;
    return STATUS_SUCCESS;
}

VOID // NTSTATUS
KbdUnloadKbdDll(
    IN HANDLE HandleDll)
{
    LdrUnloadDll(HandleDll);
}
