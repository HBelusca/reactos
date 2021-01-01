/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Keyboard functions
 * FILE:             win32ss/user/ntuser/keyboard.c
 * PROGRAMERS:       Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                   Rafal Harabien (rafalh@reactos.org)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserKbd);

BYTE gafAsyncKeyState[256 * 2 / 8]; // 2 bits per key
static BYTE gafAsyncKeyStateRecentDown[256 / 8]; // 1 bit per key
static PKEYBOARD_INDICATOR_TRANSLATION gpKeyboardIndicatorTrans = NULL;
static KEYBOARD_INDICATOR_PARAMETERS gIndicators = {0, 0};
KEYBOARD_ATTRIBUTES gKeyboardInfo;
int gLanguageToggleKeyState = 0;
DWORD gdwLanguageToggleKey = 0;

/* FUNCTIONS *****************************************************************/

/*
 * InitKeyboardImpl
 *
 * Initialization -- Right now, just zero the key state
 */
INIT_FUNCTION
NTSTATUS
NTAPI
InitKeyboardImpl(VOID)
{
    RtlZeroMemory(&gafAsyncKeyState, sizeof(gafAsyncKeyState));
    RtlZeroMemory(&gafAsyncKeyStateRecentDown, sizeof(gafAsyncKeyStateRecentDown));
    // Clear and set default information.
    RtlZeroMemory(&gKeyboardInfo, sizeof(gKeyboardInfo));
    gKeyboardInfo.KeyboardIdentifier.Type = 4; /* AT-101 */
    gKeyboardInfo.NumberOfFunctionKeys = 12; /* We're doing an 101 for now, so return 12 F-keys */
    return STATUS_SUCCESS;
}

/*
 * IntKeyboardGetIndicatorTrans
 *
 * Asks the keyboard driver to send a small table that shows which
 * lights should connect with which scancodes
 */
static
NTSTATUS
IntKeyboardGetIndicatorTrans(HANDLE hKeyboardDevice,
                             PKEYBOARD_INDICATOR_TRANSLATION *ppIndicatorTrans)
{
    NTSTATUS Status;
    DWORD dwSize = 0;
    IO_STATUS_BLOCK Block;
    PKEYBOARD_INDICATOR_TRANSLATION pRet;

    dwSize = sizeof(KEYBOARD_INDICATOR_TRANSLATION);

    pRet = ExAllocatePoolWithTag(PagedPool,
                                 dwSize,
                                 USERTAG_KBDTABLE);

    while (pRet)
    {
        Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Block,
                                       IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION,
                                       NULL, 0,
                                       pRet, dwSize);

        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;

        ExFreePoolWithTag(pRet, USERTAG_KBDTABLE);

        dwSize += sizeof(KEYBOARD_INDICATOR_TRANSLATION);

        pRet = ExAllocatePoolWithTag(PagedPool,
                                     dwSize,
                                     USERTAG_KBDTABLE);
    }

    if (!pRet)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(pRet, USERTAG_KBDTABLE);
        return Status;
    }

    *ppIndicatorTrans = pRet;
    return Status;
}

/*
 * IntKeyboardUpdateLeds
 *
 * Sends the keyboard commands to turn on/off the lights
 */
static
NTSTATUS
IntKeyboardUpdateLeds(HANDLE hKeyboardDevice,
                      WORD wVk,
                      WORD wScanCode)
{
    NTSTATUS Status;
    UINT i;
    USHORT LedFlag = 0;
    IO_STATUS_BLOCK Block;

    if (!gpKeyboardIndicatorTrans)
        return STATUS_NOT_SUPPORTED;

    switch (wVk)
    {
        case VK_CAPITAL: LedFlag = KEYBOARD_CAPS_LOCK_ON;   break;
        case VK_NUMLOCK: LedFlag = KEYBOARD_NUM_LOCK_ON;    break;
        case VK_SCROLL:  LedFlag = KEYBOARD_SCROLL_LOCK_ON; break;
        default:
            for (i = 0; i < gpKeyboardIndicatorTrans->NumberOfIndicatorKeys; i++)
            {
                if (gpKeyboardIndicatorTrans->IndicatorList[i].MakeCode == wScanCode)
                {
                    LedFlag = gpKeyboardIndicatorTrans->IndicatorList[i].IndicatorFlags;
                    break;
                }
            }
    }

    if (LedFlag)
    {
        gIndicators.LedFlags ^= LedFlag;

        /* Update the lights on the hardware */
        Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Block,
                                       IOCTL_KEYBOARD_SET_INDICATORS,
                                       &gIndicators, sizeof(gIndicators),
                                       NULL, 0);

        return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * UserInitKeyboard
 *
 * Initializes keyboard indicators translation and their state
 */
VOID NTAPI
UserInitKeyboard(HANDLE hKeyboardDevice)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK Block;

    IntKeyboardGetIndicatorTrans(hKeyboardDevice, &gpKeyboardIndicatorTrans);

    Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Block,
                                   IOCTL_KEYBOARD_QUERY_INDICATORS,
                                   NULL, 0,
                                   &gIndicators,
                                   sizeof(gIndicators));

    if (!NT_SUCCESS(Status))
    {
        WARN("NtDeviceIoControlFile() failed, ignored\n");
        gIndicators.LedFlags = 0;
        gIndicators.UnitId = 0;
    }

    SET_KEY_LOCKED(gafAsyncKeyState, VK_CAPITAL,
                   gIndicators.LedFlags & KEYBOARD_CAPS_LOCK_ON);
    SET_KEY_LOCKED(gafAsyncKeyState, VK_NUMLOCK,
                   gIndicators.LedFlags & KEYBOARD_NUM_LOCK_ON);
    SET_KEY_LOCKED(gafAsyncKeyState, VK_SCROLL,
                   gIndicators.LedFlags & KEYBOARD_SCROLL_LOCK_ON);

    // FIXME: Need device driver to work! HID support more than one!!!!
    Status = ZwDeviceIoControlFile(hKeyboardDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Block,
                                   IOCTL_KEYBOARD_QUERY_ATTRIBUTES,
                                   NULL, 0,
                                   &gKeyboardInfo, sizeof(gKeyboardInfo));

    if (!NT_SUCCESS(Status))
    {
        ERR("NtDeviceIoControlFile() failed, ignored\n");
    }
    TRACE("Keyboard type %u, subtype %u and number of func keys %u\n",
             gKeyboardInfo.KeyboardIdentifier.Type,
             gKeyboardInfo.KeyboardIdentifier.Subtype,
             gKeyboardInfo.NumberOfFunctionKeys);
}

/*
 * NtUserGetAsyncKeyState
 *
 * Gets key state from global bitmap
 */
SHORT
APIENTRY
NtUserGetAsyncKeyState(INT Key)
{
    WORD wRet = 0;

    TRACE("Enter NtUserGetAsyncKeyState\n");

    if (Key >= 0x100)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        ERR("Invalid parameter Key\n");
        return 0;
    }

    UserEnterExclusive();

    if (IS_KEY_DOWN(gafAsyncKeyState, Key))
        wRet |= 0x8000; // If down, windows returns 0x8000.
    if (gafAsyncKeyStateRecentDown[Key / 8] & (1 << (Key % 8)))
        wRet |= 0x1;
    gafAsyncKeyStateRecentDown[Key / 8] &= ~(1 << (Key % 8));

    UserLeave();

    TRACE("Leave NtUserGetAsyncKeyState, ret=%u\n", wRet);
    return wRet;
}

/*
 * UpdateAsyncKeyState
 *
 * Updates gafAsyncKeyState array
 */
static inline
VOID
UpdateAsyncKeyState(WORD wVk, BOOL bIsDown)
{
    IntUpdateKeyState(gafAsyncKeyState, wVk, bIsDown);
    if (bIsDown)
        gafAsyncKeyStateRecentDown[((BYTE)wVk) / 8] |= (1 << (((BYTE)wVk) % 8));
}

/*
 * co_CallLowLevelKeyboardHook
 *
 * Calls WH_KEYBOARD_LL hook
 */
static LRESULT
co_CallLowLevelKeyboardHook(WORD wVk, WORD wScanCode, DWORD dwFlags, BOOL bInjected, DWORD dwTime, DWORD dwExtraInfo)
{
    KBDLLHOOKSTRUCT KbdHookData;
    UINT uMsg;

    KbdHookData.vkCode = wVk;
    KbdHookData.scanCode = wScanCode;
    KbdHookData.flags = 0;
    if (dwFlags & KEYEVENTF_EXTENDEDKEY)
        KbdHookData.flags |= LLKHF_EXTENDED;
    if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU))
        KbdHookData.flags |= LLKHF_ALTDOWN;
    if (dwFlags & KEYEVENTF_KEYUP)
        KbdHookData.flags |= LLKHF_UP;
    if (bInjected)
        KbdHookData.flags |= LLKHF_INJECTED;
    KbdHookData.time = dwTime;
    KbdHookData.dwExtraInfo = dwExtraInfo;

    /* Note: it doesnt support WM_SYSKEYUP */
    if (dwFlags & KEYEVENTF_KEYUP)
        uMsg = WM_KEYUP;
    else if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) && !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
        uMsg = WM_SYSKEYDOWN;
    else
        uMsg = WM_KEYDOWN;

    return co_HOOK_CallHooks(WH_KEYBOARD_LL, HC_ACTION, uMsg, (LPARAM)&KbdHookData);
}

/*
 * SnapWindow
 *
 * Saves snapshot of specified window or whole screen in the clipboard
 */
static VOID
SnapWindow(HWND hWnd)
{
    HBITMAP hbm = NULL, hbmOld;
    HDC hdc = NULL, hdcMem;
    SETCLIPBDATA scd;
    INT cx, cy;
    PWND pWnd = NULL;

    TRACE("SnapWindow(%p)\n", hWnd);

    /* If no windows is given, make snapshot of desktop window */
    if (!hWnd)
        hWnd = IntGetDesktopWindow();

    pWnd = UserGetWindowObject(hWnd);
    if (!pWnd)
    {
        ERR("Invalid window\n");
        goto cleanup;
    }

    hdc = UserGetDCEx(pWnd, NULL, DCX_USESTYLE | DCX_WINDOW);
    if (!hdc)
    {
        ERR("UserGetDCEx failed!\n");
        goto cleanup;
    }

    cx = pWnd->rcWindow.right - pWnd->rcWindow.left;
    cy = pWnd->rcWindow.bottom - pWnd->rcWindow.top;

    hbm = NtGdiCreateCompatibleBitmap(hdc, cx, cy);
    if (!hbm)
    {
        ERR("NtGdiCreateCompatibleBitmap failed!\n");
        goto cleanup;
    }

    hdcMem = NtGdiCreateCompatibleDC(hdc);
    if (!hdcMem)
    {
        ERR("NtGdiCreateCompatibleDC failed!\n");
        goto cleanup;
    }

    hbmOld = NtGdiSelectBitmap(hdcMem, hbm);
    NtGdiBitBlt(hdcMem, 0, 0, cx, cy, hdc, 0, 0, SRCCOPY, 0, 0);
    NtGdiSelectBitmap(hdcMem, hbmOld);
    IntGdiDeleteDC(hdcMem, FALSE);

    /* Save snapshot in clipboard */
    if (UserOpenClipboard(NULL))
    {
        UserEmptyClipboard();
        scd.fIncSerialNumber = TRUE;
        scd.fGlobalHandle = FALSE;
        if (UserSetClipboardData(CF_BITMAP, hbm, &scd))
        {
            /* Bitmap is managed by system now */
            hbm = NULL;
        }
        UserCloseClipboard();
    }

cleanup:
    if (hbm)
        GreDeleteObject(hbm);
    if (hdc)
        UserReleaseDC(pWnd, hdc, FALSE);
}

/*
 * UserSendKeyboardInput
 *
 * Process keyboard input from input devices and SendInput API
 */
BOOL NTAPI
ProcessKeyEvent(PKBDTABLES pKbdTbl, WORD wVk, WORD wScanCode, DWORD dwFlags, BOOL bInjected, DWORD dwTime, DWORD dwExtraInfo)
{
    WORD wSimpleVk, wFixedVk, wVk2;
    PUSER_MESSAGE_QUEUE pFocusQueue;
    PTHREADINFO pti;
    BOOL bExt = (dwFlags & KEYEVENTF_EXTENDEDKEY) ? TRUE : FALSE;
    BOOL bIsDown = (dwFlags & KEYEVENTF_KEYUP) ? FALSE : TRUE;
    BOOL bPacket = (dwFlags & KEYEVENTF_UNICODE) ? TRUE : FALSE;
    BOOL bWasSimpleDown = FALSE, bPostMsg = TRUE, bIsSimpleDown;
    MSG Msg;
    static BOOL bMenuDownRecently = FALSE;

//
// TODO: We should actually update a global "RAW" keyboard state somewhere here.
//

    /* Support Numpad */
    // TODO: Improve!
    if ((wVk & KBDNUMPAD) && IS_KEY_LOCKED(gafAsyncKeyState, VK_NUMLOCK))
    {
        wVk = IntTranslateNumpadKey(wVk & 0xFF);
    }
    wVk &= 0xFF;

    /* AltGr handling */
    // TODO: Improve!
    if (wVk == VK_RMENU && (pKbdTbl->fLocaleFlags & KLLF_ALTGR))
    {
        /* For AltGr keyboards, RALT generates CTRL events */
        ProcessKeyEvent(pKbdTbl, VK_LCONTROL, SCANCODE_CTRL, dwFlags & KEYEVENTF_KEYUP, bInjected, dwTime, 0);
    }

    // TODO: Shift-Lock handling if KLLF_SHIFTLOCK !
    // TODO: Extra handling for OEM (e.g. '00'...), FarEast NLS...

    /* Get virtual key without shifts (VK_(L|R)* -> VK_*) */
    wSimpleVk = IntSimplifyVk(wVk);
    bWasSimpleDown = IS_KEY_DOWN(gafAsyncKeyState, wSimpleVk);

//
// FIXME: Actually the "async" keyboard state should be per-queue.
//
    /* Update key without shifts */
    wVk2 = IntFixVk(wSimpleVk, !bExt);
    bIsSimpleDown = bIsDown || IS_KEY_DOWN(gafAsyncKeyState, wVk2);
    UpdateAsyncKeyState(wSimpleVk, bIsSimpleDown);

//
// TODO: Verify SAS keys.
//

    /* Call WH_KEYBOARD_LL hook */
    if (co_CallLowLevelKeyboardHook(wVk, wScanCode, dwFlags, bInjected, dwTime, dwExtraInfo))
    {
        /* The key has been dropped, we are done */
        // FIXME: Clearly SAS keys must not be discarded.
        ERR("Kbd msg dropped by WH_KEYBOARD_LL hook\n");
        // bPostMsg = FALSE;
        return TRUE;
    }

//
// TODO: Support ALT+#### from numpad.
//

    if (bIsDown)
    {
        /* Update keyboard LEDs */
        IntKeyboardUpdateLeds(ghKeyboardDevice,
                              wSimpleVk,
                              wScanCode);
    }

    // THINK: Why not just setting wFixedVk = wVk?
    wFixedVk = IntFixVk(wSimpleVk, bExt); /* LSHIFT + EXT = RSHIFT */
    /* Now clear up the extended bit for Right-Shift, that is not really
     * extended, but was just used to indicate its right-handedness. */
    if (wSimpleVk == VK_SHIFT)
        bExt = FALSE;

    /* Check if this is a hotkey */
    if (co_UserProcessHotKeys(wSimpleVk, bIsDown)) //// Check if this is correct, refer to hotkey sequence message tests.
    {
        /* It has been processed, we are done */
        TRACE("HotKey Processed\n");
        // bPostMsg = FALSE;
        return TRUE;
    }

    /* If we have a focus queue, post a keyboard message */
    pFocusQueue = IntGetFocusMessageQueue();
    TRACE("ProcessKeyEvent Q 0x%p Active pWnd 0x%p Focus pWnd 0x%p\n",
           pFocusQueue,
           (pFocusQueue ?  pFocusQueue->spwndActive : 0),
           (pFocusQueue ?  pFocusQueue->spwndFocus : 0));

    /* If it is F10 or ALT is down and CTRL is up, it's a system key */
    if ( wVk == VK_F10 ||
        (wSimpleVk == VK_MENU && bMenuDownRecently) ||
        (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
        !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL)) ||
         // See MSDN WM_SYSKEYDOWN/UP fixes last wine Win test_keyboard_input.
        (pFocusQueue && !pFocusQueue->spwndFocus) )
    {
        bMenuDownRecently = FALSE; // reset
        if (bIsDown)
        {
            Msg.message = WM_SYSKEYDOWN;
            if (wSimpleVk == VK_MENU)
            {
                // Note: If only LALT is pressed WM_SYSKEYUP is generated instead of WM_KEYUP
                bMenuDownRecently = TRUE;
            }
        }
        else
            Msg.message = WM_SYSKEYUP;
    }
    else
    {
        if (bIsDown)
            Msg.message = WM_KEYDOWN;
        else
            Msg.message = WM_KEYUP;
    }

    /* Update async state of not simplified vk here.
       See user32_apitest:GetKeyState */
    UpdateAsyncKeyState(wFixedVk, bIsDown);

    /* Alt-Tab/Esc Check. Use FocusQueue or RIT Queue */
    if (bIsSimpleDown && !bWasSimpleDown &&
        IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
        !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL) &&
        (wVk == VK_ESCAPE || wVk == VK_TAB))
    {
        TRACE("Alt-Tab/Esc Pressed wParam %x\n", wVk);
    }

    if (bIsDown && wVk == VK_SNAPSHOT)
    {
        if (pFocusQueue &&
            IS_KEY_DOWN(gafAsyncKeyState, VK_MENU) &&
            !IS_KEY_DOWN(gafAsyncKeyState, VK_CONTROL))
        {
            // Snap from Active Window, Focus can be null.
            SnapWindow(pFocusQueue->spwndActive ? UserHMGetHandle(pFocusQueue->spwndActive) : 0);
        }
        else
        {
            // Snap Desktop.
            SnapWindow(NULL);
        }
    }
    else if (pFocusQueue && bPostMsg)
    {
        PWND Wnd = pFocusQueue->spwndFocus; // SysInit.....

        pti = pFocusQueue->ptiKeyboard;

        if (!Wnd && pFocusQueue->spwndActive) // SysInit.....
        {
            // Going with Active. WM_SYSKEYXXX last wine Win test_keyboard_input.
            Wnd = pFocusQueue->spwndActive;
        }
        if (Wnd) pti = Wnd->head.pti;

        /* Init message */
        Msg.hwnd = Wnd ? UserHMGetHandle(Wnd) : NULL;
        Msg.wParam = wFixedVk & 0xFF; /* Note: It's simplified by msg queue */
        Msg.lParam = MAKELPARAM(1, wScanCode);
        Msg.time = dwTime;
        Msg.pt = gpsi->ptCursor;

        if ( Msg.message == WM_KEYDOWN || Msg.message == WM_SYSKEYDOWN )
        {
            if ( (Msg.wParam == VK_SHIFT ||
                  Msg.wParam == VK_CONTROL ||
                  Msg.wParam == VK_MENU ) &&
                !IS_KEY_DOWN(gafAsyncKeyState, Msg.wParam))
            {
                ERR("Set last input\n");
                //ptiLastInput = pti;
            }
        }

        /* If it is VK_PACKET, high word of wParam is used for wchar */
        if (!bPacket)
        {
            if (bExt)
                Msg.lParam |= KF_EXTENDED << 16;
            if (IS_KEY_DOWN(gafAsyncKeyState, VK_MENU))
                Msg.lParam |= KF_ALTDOWN << 16;
            if (bWasSimpleDown)
                Msg.lParam |= KF_REPEAT << 16;
            if (!bIsDown)
                Msg.lParam |= KF_UP << 16;
            /* FIXME: Set KF_DLGMODE and KF_MENUMODE when needed */
            if (pFocusQueue->QF_flags & QF_DIALOGACTIVE)
                Msg.lParam |= KF_DLGMODE << 16;
            if (pFocusQueue->MenuOwner) // pti->pMenuState->fMenuStarted
                Msg.lParam |= KF_MENUMODE << 16;
        }

        // Post mouse move before posting key buttons, to keep it synced.
        if (pFocusQueue->QF_flags & QF_MOUSEMOVED)
        {
            IntCoalesceMouseMove(pti);
        }

        /* Post a keyboard message */
        TRACE("Posting keyboard msg %u wParam 0x%x lParam 0x%x\n", Msg.message, Msg.wParam, Msg.lParam);
        if (!Wnd) {ERR("Window is NULL\n");}
        MsqPostMessage(pti, &Msg, TRUE, QS_KEY, 0, dwExtraInfo);
    }
    return TRUE;
}

BOOL NTAPI
UserSendKeyboardInput(KEYBDINPUT *pKbdInput, BOOL bInjected)
{
    WORD wScanCode, wVk;
    PKL pKl = NULL;
    PKBDTABLES pKbdTbl;
    PUSER_MESSAGE_QUEUE pFocusQueue;
    DWORD dwTime;

    gppiInputProvider = ((PTHREADINFO)PsGetCurrentThreadWin32Thread())->ppi;

    /* Find the target thread whose locale is in effect */
    pFocusQueue = IntGetFocusMessageQueue();

    if (pFocusQueue && pFocusQueue->ptiKeyboard)
    {
        pKl = pFocusQueue->ptiKeyboard->KeyboardLayout;
    }

    if (!pKl)
        pKl = W32kGetDefaultKeyLayout();
    if (!pKl)
    {
        ERR("No keyboard layout!\n");
        return FALSE;
    }

    pKbdTbl = pKl->spkf->pKbdTbl;

    ASSERT(pKbdTbl);

    /* Note: wScan field is always used */
    wScanCode = pKbdInput->wScan;

    if (pKbdInput->dwFlags & KEYEVENTF_UNICODE)
    {
        /* Generate WM_KEYDOWN msg with wParam == VK_PACKET and
           high order word of lParam == pKbdInput->wScan */
        wVk = VK_PACKET;
    }
    else
    {
        wScanCode &= 0x7F;
        if (pKbdInput->dwFlags & KEYEVENTF_SCANCODE)
        {
            BOOL bExt = (pKbdInput->dwFlags & KEYEVENTF_EXTENDEDKEY) ? TRUE : FALSE;

            /* Don't ignore invalid scan codes */
            wVk = IntVscToVk(wScanCode | (bExt ? 0xE000 : 0), pKbdTbl);
            if (!wVk) /* use 0xFF if vsc is invalid */
                wVk = 0xFF;
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
        }
        else
        {
            wVk = pKbdInput->wVk & 0xFF;
        }
    }

    /* If time is given, use it */
    if (pKbdInput->time)
        dwTime = pKbdInput->time;
    else
        dwTime = EngGetTickCount32();

    /* Finally process this key */
    return ProcessKeyEvent(pKbdTbl, wVk, wScanCode, pKbdInput->dwFlags, bInjected,
                           dwTime, pKbdInput->dwExtraInfo);
}

/*
 * UserProcessKeyboardInput
 *
 * Process raw keyboard input data
 */
VOID NTAPI
UserProcessKeyboardInput(
    PKEYBOARD_INPUT_DATA pKbdInputData)
{
    WORD wScanCode, wVk;
    PKL pKl = NULL;
    PKBDTABLES pKbdTbl;
    PUSER_MESSAGE_QUEUE pFocusQueue;
    KEYBDINPUT KbdInput;

    /* Find the target thread whose locale is in effect */
    pFocusQueue = IntGetFocusMessageQueue();

    if (pFocusQueue && pFocusQueue->ptiKeyboard)
    {
        pKl = pFocusQueue->ptiKeyboard->KeyboardLayout;
    }

    if (!pKl)
        pKl = W32kGetDefaultKeyLayout();
    if (!pKl)
    {
        ERR("No keyboard layout!\n");
        return;
    }

    pKbdTbl = pKl->spkf->pKbdTbl;

    ASSERT(pKbdTbl);

    /* Calculate scan code with prefix */
    wScanCode = pKbdInputData->MakeCode & 0x7F;
    if (pKbdInputData->Flags & KEY_E0)
        wScanCode |= 0xE000;
    if (pKbdInputData->Flags & KEY_E1)
        wScanCode |= 0xE100;

    /* Convert scan code to virtual key.
       Note: We could call UserSendKeyboardInput using scan code,
             but it wouldn't interpret E1 key(s) properly */
    wVk = IntVscToVk(wScanCode, pKbdTbl);
    TRACE("UserProcessKeyboardInput: %x (break: %u) -> %x\n",
          wScanCode, (pKbdInputData->Flags & KEY_BREAK) ? 1u : 0, wVk);

    /* Do nothing more if == 0 */
    if (wVk == 0)
        return;

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


    /* Send keyboard input */
    KbdInput.wVk = wVk /*& 0xFF*/;
    KbdInput.wScan = wScanCode & 0x7F;
    KbdInput.dwFlags = 0;
    if (pKbdInputData->Flags & KEY_BREAK)
        KbdInput.dwFlags |= KEYEVENTF_KEYUP;

    if (wVk & KBDEXT)
        KbdInput.dwFlags |= KEYEVENTF_EXTENDEDKEY;
#if 1
/** HACK needed because our keyboard layouts are buggy... **/
    //
    // Based on wine input:test_Input_blackbox this is okay. It seems the
    // bit did not get set and more research is needed. Now the right
    // shift works.
    //
    if (wVk == VK_RSHIFT)
        KbdInput.dwFlags |= KEYEVENTF_EXTENDEDKEY;
#endif

    KbdInput.time = EngGetTickCount32();
    KbdInput.dwExtraInfo = pKbdInputData->ExtraInformation;

    /* Finally process this key */
    ProcessKeyEvent(pKbdTbl, KbdInput.wVk, KbdInput.wScan,
                    KbdInput.dwFlags, FALSE,
                    KbdInput.time, KbdInput.dwExtraInfo);

    /* E1 keys don't have break code */
    if (pKbdInputData->Flags & KEY_E1)
    {
        /* Send key up event */
        KbdInput.dwFlags |= KEYEVENTF_KEYUP;
        // KbdInput.time = EngGetTickCount32();
        ProcessKeyEvent(pKbdTbl, KbdInput.wVk, KbdInput.wScan,
                        KbdInput.dwFlags, FALSE,
                        KbdInput.time, KbdInput.dwExtraInfo);
    }
}

/*
 * IntTranslateKbdMessage
 *
 * Adds WM_(SYS)CHAR messages to message queue if message
 * describes key which produce character.
 */
BOOL FASTCALL
IntTranslateKbdMessage(LPMSG lpMsg,
                       UINT flags)
{
    PTHREADINFO pti;
    INT cch = 0, i;
    WCHAR wch[3] = { 0 };
    MSG NewMsg = { 0 };
    PKBDTABLES pKbdTbl;
    BOOL bResult = FALSE;

    switch(lpMsg->message)
    {
       case WM_KEYDOWN:
       case WM_KEYUP:
       case WM_SYSKEYDOWN:
       case WM_SYSKEYUP:
          break;
       default:
          return FALSE;
    }

    pti = PsGetCurrentThreadWin32Thread();

    if (!pti->KeyboardLayout)
    {
       pti->KeyboardLayout = W32kGetDefaultKeyLayout();
       pti->pClientInfo->hKL = pti->KeyboardLayout ? pti->KeyboardLayout->hkl : NULL;
       pKbdTbl = pti->KeyboardLayout ? pti->KeyboardLayout->spkf->pKbdTbl : NULL;
    }
    else
    {
       pKbdTbl = pti->KeyboardLayout->spkf->pKbdTbl;
    }
    if (!pKbdTbl)
        return FALSE;

    if (lpMsg->message != WM_KEYDOWN && lpMsg->message != WM_SYSKEYDOWN)
        return FALSE;

    /* Init pt, hwnd and time msg fields */
    NewMsg.pt = gpsi->ptCursor;
    NewMsg.hwnd = lpMsg->hwnd;
    NewMsg.time = EngGetTickCount32();

    TRACE("Enter IntTranslateKbdMessage msg %s, vk %x\n",
        lpMsg->message == WM_SYSKEYDOWN ? "WM_SYSKEYDOWN" : "WM_KEYDOWN", lpMsg->wParam);

    if (lpMsg->wParam == VK_PACKET)
    {
        NewMsg.message = (lpMsg->message == WM_KEYDOWN) ? WM_CHAR : WM_SYSCHAR;
        NewMsg.wParam = HIWORD(lpMsg->lParam);
        NewMsg.lParam = LOWORD(lpMsg->lParam);
        MsqPostMessage(pti, &NewMsg, FALSE, QS_KEY, 0, 0);
        return TRUE;
    }

    cch = IntToUnicodeEx(lpMsg->wParam,
                         HIWORD(lpMsg->lParam) & 0xFF,
                         pti->MessageQueue->afKeyState,
                         wch,
                         sizeof(wch) / sizeof(wch[0]),
                         0,
                         pKbdTbl);
    if (cch)
    {
        if (cch > 0) /* Normal characters */
        {
            NewMsg.message = (lpMsg->message == WM_KEYDOWN) ? WM_CHAR : WM_SYSCHAR;
        }
        else /* Dead character */
        {
            cch = -cch;
            NewMsg.message =
                (lpMsg->message == WM_KEYDOWN) ? WM_DEADCHAR : WM_SYSDEADCHAR;
        }
        NewMsg.lParam = lpMsg->lParam;

        /* Send all characters */
        for (i = 0; i < cch; ++i)
        {
            TRACE("Msg: %x '%lc' (%04x) %08x\n", NewMsg.message, wch[i], wch[i], NewMsg.lParam);
            NewMsg.wParam = wch[i];
            MsqPostMessage(pti, &NewMsg, FALSE, QS_KEY, 0, 0);
        }
        bResult = TRUE;
    }

    TRACE("Leave IntTranslateKbdMessage ret %d, cch %d, msg %x, wch %x\n",
        bResult, cch, NewMsg.message, NewMsg.wParam);
    return bResult;
}

/*
 * NtUserMapVirtualKeyEx
 *
 * Map a virtual key code, or virtual scan code, to a scan code, key code,
 * or unshifted unicode character. See IntMapVirtualKeyEx.
 */
UINT
APIENTRY
NtUserMapVirtualKeyEx(
    IN UINT uCode,
    IN UINT uType,
    IN HKL dwhkl,
    IN BOOL bUsehKL)
{
    PKBDTABLES pKbdTbl;
    PKL pKl = NULL;
    UINT ret = 0;

    TRACE("Enter NtUserMapVirtualKeyEx\n");
    UserEnterShared();

    if (bUsehKL)
    {
        /* Use given keyboard layout */
        pKl = UserHklToKbl(dwhkl);
    }
    else
    {
        /* Use thread keyboard layout */
        pKl = ((PTHREADINFO)PsGetCurrentThreadWin32Thread())->KeyboardLayout;
    }

    if (pKl)
    {
        pKbdTbl = pKl->spkf->pKbdTbl;
        if (pKbdTbl)
        {
            /* Valid types are between 0 and 4 (MAPVK_VK_TO_VSC_EX) */
            if (uType <= MAPVK_VK_TO_VSC_EX)
            {
                ret = IntMapVirtualKeyEx(uCode, uType, pKbdTbl);
            }
            else
            {
                EngSetLastError(ERROR_INVALID_PARAMETER);
                ERR("Wrong type value: %u\n", uType);
                ret = 0;
            }
        }
    }

    UserLeave();
    TRACE("Leave NtUserMapVirtualKeyEx, ret=%u\n", ret);
    return ret;
}

/*
 * NtUserToUnicodeEx
 *
 * Translates virtual key to characters
 */
int
APIENTRY
NtUserToUnicodeEx(
    UINT wVirtKey,
    UINT wScanCode,
    PBYTE pKeyStateUnsafe,
    LPWSTR pwszBuffUnsafe,
    INT cchBuff,
    UINT wFlags,
    HKL dwhkl)
{
    PTHREADINFO pti;
    BYTE afKeyState[256 * 2 / 8] = {0};
    PWCHAR pwszBuff = NULL;
    INT i, iRet = 0;
    PKL pKl = NULL;

    TRACE("Enter NtUserSetKeyboardState\n");

    /* Return 0 if SC_KEY_UP bit is set */
    if (wScanCode & SC_KEY_UP || wVirtKey >= 0x100)
    {
        ERR("Invalid parameter\n");
        return 0;
    }

    _SEH2_TRY
    {
        /* Probe and copy key state to smaller bitmap */
        ProbeForRead(pKeyStateUnsafe, 256 * sizeof(BYTE), 1);
        for (i = 0; i < 256; ++i)
        {
            if (pKeyStateUnsafe[i] & KS_DOWN_BIT)
                SET_KEY_DOWN(afKeyState, i, TRUE);
            if (pKeyStateUnsafe[i] & KS_LOCK_BIT)
                SET_KEY_LOCKED(afKeyState, i, TRUE);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ERR("Cannot copy key state\n");
        SetLastNtError(_SEH2_GetExceptionCode());
        _SEH2_YIELD(return 0);
    }
    _SEH2_END;

    pwszBuff = ExAllocatePoolWithTag(NonPagedPool, sizeof(WCHAR) * cchBuff, TAG_STRING);
    if (!pwszBuff)
    {
        ERR("ExAllocatePoolWithTag(%u) failed\n", sizeof(WCHAR) * cchBuff);
        return 0;
    }
    RtlZeroMemory(pwszBuff, sizeof(WCHAR) * cchBuff);

    UserEnterExclusive(); // Note: We modify wchDead static variable

    if (dwhkl)
        pKl = UserHklToKbl(dwhkl);

    if (!pKl)
    {
        pti = PsGetCurrentThreadWin32Thread();
        pKl = pti->KeyboardLayout;
    }

    ASSERT(pKl && pKl->spkf->pKbdTbl);

    iRet = IntToUnicodeEx(wVirtKey,
                          wScanCode,
                          afKeyState,
                          pwszBuff,
                          cchBuff,
                          wFlags,
                          pKl->spkf->pKbdTbl);

    MmCopyToCaller(pwszBuffUnsafe, pwszBuff, cchBuff * sizeof(WCHAR));
    ExFreePoolWithTag(pwszBuff, TAG_STRING);

    UserLeave();
    TRACE("Leave NtUserSetKeyboardState, ret=%i\n", iRet);
    return iRet;
}

/*
 * NtUserGetKeyNameText
 *
 * Gets key name from keyboard layout
 */
DWORD
APIENTRY
NtUserGetKeyNameText(LONG lParam, LPWSTR lpString, int cchSize)
{
    PTHREADINFO pti;
    DWORD dwRet = 0;
    SIZE_T cchKeyName;
    WORD wScanCode = HIWORD(lParam) & 0xFF;
    BOOL bExtKey = (HIWORD(lParam) & KF_EXTENDED) ? TRUE : FALSE;
    PKBDTABLES pKbdTbl;
    PCWSTR pKeyName;
    WCHAR KeyNameBuf[2];

    TRACE("Enter NtUserGetKeyNameText\n");

    UserEnterShared();

    /* Get current keyboard layout */
    pti = PsGetCurrentThreadWin32Thread();
    pKbdTbl = pti ? pti->KeyboardLayout->spkf->pKbdTbl : NULL;

    if (!pKbdTbl || cchSize < 1)
    {
        ERR("Invalid parameter\n");
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto cleanup;
    }

    /* "Do not care" flag */
    if (lParam & LP_DO_NOT_CARE_BIT)
    {
        /* Note: We could do vsc -> vk -> vsc conversion, instead of using
                 hardcoded scan codes, but it's not what Windows does */
        if (wScanCode == SCANCODE_RSHIFT && !bExtKey)
            wScanCode = SCANCODE_LSHIFT;
        else if (wScanCode == SCANCODE_CTRL || wScanCode == SCANCODE_ALT)
            bExtKey = FALSE;
    }

    pKeyName = IntGetKeyName(wScanCode, bExtKey, pKbdTbl);
    if (!pKeyName)
    {
        UINT uRet = IntMapVirtualKeyEx(wScanCode, MAPVK_VSC_TO_VK_EX, pKbdTbl);
        if (uRet)
        {
            uRet = IntMapVirtualKeyEx(uRet, MAPVK_VK_TO_CHAR, pKbdTbl);

            /* Check for the dead char flag set by IntVkToChar(). If we
             * have got one, try to find its name in the dead keys list. */
            if (uRet & 0x80000000)
            {
                UINT i;

                if (pKbdTbl->pKeyNamesDead)
                {
                    for (i = 0; pKbdTbl->pKeyNamesDead[i]; ++i)
                    {
                        /* If the dead char string is prefixed with it, we
                         * have found it, and return the name that follows. */
                        if (pKbdTbl->pKeyNamesDead[i][0] == LOWORD(uRet))
                            pKeyName = &pKbdTbl->pKeyNamesDead[i][1];
                    }
                }
            }
            else
            {
                /* Single character name */
                KeyNameBuf[0] = uRet;
                KeyNameBuf[1] = 0;
                if (KeyNameBuf[0])
                    pKeyName = KeyNameBuf;
            }
        }
    }

    if (pKeyName)
    {
        _SEH2_TRY
        {
            cchKeyName = wcslen(pKeyName);
            if (cchKeyName > (cchSize - 1UL))
                cchKeyName = cchSize - 1UL; // Don't count '\0'

            ProbeForWrite(lpString, (cchKeyName + 1) * sizeof(WCHAR), 1);
            RtlCopyMemory(lpString, pKeyName, cchKeyName * sizeof(WCHAR));
            lpString[cchKeyName] = UNICODE_NULL;
            dwRet = cchKeyName;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SetLastNtError(_SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
    }

cleanup:
    UserLeave();
    TRACE("Leave NtUserGetKeyNameText, ret=%lu\n", dwRet);
    return dwRet;
}

/*
 * UserGetKeyboardType
 *
 * Returns some keyboard specific information
 */
DWORD FASTCALL
UserGetKeyboardType(
    DWORD dwTypeFlag)
{
    switch (dwTypeFlag)
    {
        case 0:        /* Keyboard type */
            return (DWORD)gKeyboardInfo.KeyboardIdentifier.Type;
        case 1:        /* Keyboard Subtype */
            return (DWORD)gKeyboardInfo.KeyboardIdentifier.Subtype;
        case 2:        /* Number of F-keys */
            return (DWORD)gKeyboardInfo.NumberOfFunctionKeys;
        default:
            ERR("Unknown type!\n");
            return 0;    /* Note: we don't have to set last error here */
    }
}

/*
 * NtUserVkKeyScanEx
 *
 * Based on IntTranslateChar, instead of processing VirtualKey match,
 * look for wChar match.
 */
DWORD
APIENTRY
NtUserVkKeyScanEx(
    IN WCHAR wch,
    IN HKL dwhkl,
    IN BOOL bUsehKL)
{
    PKBDTABLES pKbdTbl;
    PKL pKl = NULL;
    DWORD Ret = (DWORD)-1;

    TRACE("NtUserVkKeyScanEx() wch %u, KbdLayout 0x%p\n", wch, dwhkl);
    UserEnterShared();

    if (bUsehKL)
    {
        /* Use given keyboard layout */
        if (dwhkl)
            pKl = UserHklToKbl(dwhkl);
    }
    else
    {
        /* Use thread keyboard layout */
        pKl = ((PTHREADINFO)PsGetCurrentThreadWin32Thread())->KeyboardLayout;
    }

    if (pKl)
    {
        pKbdTbl = pKl->spkf->pKbdTbl;
        if (pKbdTbl)
            Ret = IntVkKeyScanEx(wch, pKbdTbl);
    }

    UserLeave();
    return Ret;
}

/* EOF */
