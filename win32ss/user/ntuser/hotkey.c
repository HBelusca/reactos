/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          HotKey support
 * FILE:             win32ss/user/ntuser/hotkey.c
 * PROGRAMER:        Eric Kohl
 */

/*
 * FIXME: Hotkey notifications are triggered by keyboard input (physical or programmatically)
 * and since only desktops on WinSta0 can receive input, it seems very wrong to allow
 * windows/threads on desktops not belonging to WinSta0 to set hotkeys (receive notifications).
 *     -- Gunnar
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserHotkey);

/* GLOBALS *******************************************************************/

/*
 * Hardcoded hotkeys. See http://ivanlef0u.fr/repo/windoz/VI20051005.html (DEAD_LINK)
 * or https://web.archive.org/web/20170826161432/http://repo.meh.or.id/Windows/VI20051005.html .
 *
 * NOTE: The (Shift-)F12 keys are used only for the "UserDebuggerHotKey" setting
 * which enables setting a key shortcut which, when pressed, establishes a
 * breakpoint in the code being debugged:
 * see https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2003/cc786263(v=ws.10)
 * and https://flylib.com/books/en/4.441.1.33/1/ for more details.
 * By default the key is VK-F12 on a 101-key keyboard, and is VK_SUBTRACT
 * (hyphen / subtract sign) on a 82-key keyboard.
 */
/*                       pti   pwnd  modifiers  vk      id             next */
// HOT_KEY hkF12 =      {NULL, 1,    0,         VK_F12, IDHK_F12,      NULL};
// HOT_KEY hkShiftF12 = {NULL, 1,    MOD_SHIFT, VK_F12, IDHK_SHIFTF12, &hkF12};
// HOT_KEY hkWinKey =   {NULL, 1,    MOD_WIN,   0,      IDHK_WINKEY,   &hkShiftF12};

PHOT_KEY gphkFirst = NULL;
UINT gfsModOnlyCandidate;

/* FUNCTIONS *****************************************************************/

#define IsWindowHotKey(pHK) ( (pHK)->pti == NULL && (pHK)->id == IDHK_WNDKEY )

VOID FASTCALL
SetDebugHotKeys(VOID)
{
    UINT vk = VK_F12;
    if (!ENHANCED_KEYBOARD(gKeyboardInfo.KeyboardIdentifier))
        vk = VK_SUBTRACT;

    UserUnregisterHotKey(PWND_BOTTOM, IDHK_F12);
    UserUnregisterHotKey(PWND_BOTTOM, IDHK_SHIFTF12);
    UserRegisterHotKey(PWND_BOTTOM, IDHK_SHIFTF12, MOD_SHIFT, vk);
    UserRegisterHotKey(PWND_BOTTOM, IDHK_F12, 0, vk);

    TRACE("Debugger hotkeys set up! If you see this you enabled Debug Prints. Congrats!\n");
}

/**
 * @brief
 * Returns a value that indicates which modifier keys are being pressed down.
 **/
static
UINT FASTCALL
IntGetModifiers(_In_ PBYTE pKeyState)
{
    UINT fsModifiers = 0;

    if (IS_KEY_DOWN(pKeyState, VK_SHIFT))
        fsModifiers |= MOD_SHIFT;

    if (IS_KEY_DOWN(pKeyState, VK_CONTROL))
        fsModifiers |= MOD_CONTROL;

    if (IS_KEY_DOWN(pKeyState, VK_MENU))
        fsModifiers |= MOD_ALT;

    if (IS_KEY_DOWN(pKeyState, VK_LWIN) || IS_KEY_DOWN(pKeyState, VK_RWIN))
        fsModifiers |= MOD_WIN;

    return fsModifiers;
}

/**
 * @brief
 * Maps to/from MOD_/HOTKEYF_ (swaps the SHIFT and ALT bits).
 **/
static inline
UCHAR
IntSwapModHKF(UINT Input)
{
    return (Input & 2) | ((Input & 1) << 2) | ((Input >> 2) & 1);
}

/**
 * @brief
 * Removes hotkeys registered by the specified window on its cleanup.
 *
 * @see UnregisterThreadHotKeys(), UserUnregisterHotKey().
 **/
VOID FASTCALL
UnregisterWindowHotKeys(PWND pWnd)
{
    PHOT_KEY pHotKey = gphkFirst, phkNext, *pLink = &gphkFirst;

    while (pHotKey)
    {
        /* Save next ptr for later use */
        phkNext = pHotKey->pNext;

        /* Should we delete this hotkey? */
        if (pHotKey->pWnd == pWnd)
        {
            /* Update next ptr for previous hotkey and free memory */
            *pLink = phkNext;
            ExFreePoolWithTag(pHotKey, USERTAG_HOTKEY);
        }
        else
        {
            /* This hotkey will stay, use its next ptr */
            pLink = &pHotKey->pNext;
        }

        /* Move to the next entry */
        pHotKey = phkNext;
    }
}

/**
 * @brief
 * Removes hotkeys registered by the specified thread on its cleanup.
 *
 * @see UnregisterWindowHotKeys(), UserUnregisterHotKey().
 **/
VOID FASTCALL
UnregisterThreadHotKeys(PTHREADINFO pti)
{
    PHOT_KEY pHotKey = gphkFirst, phkNext, *pLink = &gphkFirst;

    while (pHotKey)
    {
        /* Save next ptr for later use */
        phkNext = pHotKey->pNext;

        /* Should we delete this hotkey? */
        if (pHotKey->pti == pti)
        {
            /* Update next ptr for previous hotkey and free memory */
            *pLink = phkNext;
            ExFreePoolWithTag(pHotKey, USERTAG_HOTKEY);
        }
        else
        {
            /* This hotkey will stay, use its next ptr */
            pLink = &pHotKey->pNext;
        }

        /* Move to the next entry */
        pHotKey = phkNext;
    }
}

/*
 * IsHotKey
 *
 * Checks if given key and modificators have corresponding hotkey
 */
static PHOT_KEY FASTCALL
IsHotKey(UINT fsModifiers, WORD wVk)
{
    PHOT_KEY pHotKey = gphkFirst;

    while (pHotKey)
    {
        if (pHotKey->fsModifiers == fsModifiers &&
            pHotKey->vk == wVk)
        {
            /* We have found it */
            return pHotKey;
        }

        /* Move to the next entry */
        pHotKey = pHotKey->pNext;
    }

    return NULL;
}

/**
 * @brief
 * Sends a WM_HOTKEY message if the given keys are a registered hotkey.
 **/
BOOL FASTCALL
co_UserProcessHotKeys(_In_ WORD wVk, _In_ BOOL bIsDown)
{
    UINT fsModifiers;
    PHOT_KEY pHotKey;
    PWND pWnd;
    BOOL IsModifier = FALSE;

    if (wVk == VK_SHIFT || wVk == VK_CONTROL || wVk == VK_MENU ||
        wVk == VK_LWIN || wVk == VK_RWIN)
    {
        /* Remember that this was a modifier */
        IsModifier = TRUE;
    }

    fsModifiers = IntGetModifiers(gafAsyncKeyState);

    if (bIsDown)
    {
        if (IsModifier)
        {
            /* Modifier key down -- no hotkey trigger, but remember this */
            gfsModOnlyCandidate = fsModifiers;
            return FALSE;
        }
        else
        {
            /* Regular key down -- check for hotkey, and reset mod candidates */
            pHotKey = IsHotKey(fsModifiers, wVk);
            gfsModOnlyCandidate = 0;
        }
    }
    else
    {
        if (IsModifier)
        {
            /* Modifier key up -- modifier-only keys are triggered here */
            pHotKey = IsHotKey(gfsModOnlyCandidate, 0);
            gfsModOnlyCandidate = 0;
        }
        else
        {
            /* Regular key up -- no hotkey, but reset mod-only candidates */
            gfsModOnlyCandidate = 0;
            return FALSE;
        }
    }

    if (!pHotKey)
        return FALSE;

    TRACE("Hotkey pressed (pWnd %p, id %d)\n", pHotKey->pWnd, pHotKey->id);

    /* FIXME: See comment about "UserDebuggerHotKey" on top of this file. */
    if (pHotKey->id == IDHK_SHIFTF12 || pHotKey->id == IDHK_F12)
    {
        BOOL DoNotPostMsg = FALSE;
        if (bIsDown)
        {
            ERR("Hotkey pressed for Debug Activation! ShiftF12 = %d or F12 = %d\n",
                pHotKey->id == IDHK_SHIFTF12, pHotKey->id == IDHK_F12);
            //DoNotPostMsg = co_ActivateDebugger(); // FIXME
        }
        return DoNotPostMsg;
    }

    /* WIN and F12 keys are not hardcoded here. See comments on top of this file. */
    if (pHotKey->id == IDHK_WINKEY)
    {
        ASSERT(!bIsDown);
        pWnd = ValidateHwndNoErr(InputWindowStation->ShellWindow);
        if (pWnd)
        {
            TRACE("System Hotkey Id %d Key %u\n", pHotKey->id, wVk);
            UserPostMessage(UserHMGetHandle(pWnd), WM_SYSCOMMAND, SC_TASKLIST, 0);
            co_IntShellHookNotify(HSHELL_TASKMAN, 0, 0);
            return FALSE;
        }
    }

    /* Handle window snapping hotkeys */
    if (pHotKey->id == IDHK_SNAP_LEFT ||
        pHotKey->id == IDHK_SNAP_RIGHT ||
        pHotKey->id == IDHK_SNAP_UP ||
        pHotKey->id == IDHK_SNAP_DOWN)
    {
        HWND topWnd = UserGetForegroundWindow();
        if (topWnd)
            UserPostMessage(topWnd, WM_KEYDOWN, wVk, 0);
        return TRUE;
    }

    if (!pHotKey->pWnd)
    {
        TRACE("UPTM Hotkey Id %d Key %u\n", pHotKey->id, wVk);
        UserPostThreadMessage(pHotKey->pti, WM_HOTKEY, pHotKey->id, MAKELONG(fsModifiers, wVk));
        //ptiLastInput = pHotKey->pti;
        return TRUE; /* Don't send any message */
    }

    /* Retrieve the window where to send the hotkey */
    pWnd = pHotKey->pWnd;
    if (pWnd == PWND_BOTTOM)
    {
        if (!gpqForeground)
            return FALSE;
        pWnd = gpqForeground->spwndFocus;
    }
    if (!pWnd)
        return FALSE;

    //  pWnd->head.rpdesk->pDeskInfo->spwndShell needs testing.
    if (pWnd == ValidateHwndNoErr(InputWindowStation->ShellWindow) && pHotKey->id == SC_TASKLIST)
    {
        UserPostMessage(UserHMGetHandle(pWnd), WM_SYSCOMMAND, SC_TASKLIST, 0);
        co_IntShellHookNotify(HSHELL_TASKMAN, 0, 0);
    }
    else if (IsWindowHotKey(pHotKey))
    {
        /* WM_SETHOTKEY notifies with WM_SYSCOMMAND, not WM_HOTKEY */
        if (bIsDown)
        {
            if (gpqForeground && gpqForeground->spwndActive)
                pWnd = gpqForeground->spwndActive;
            UserPostMessage(UserHMGetHandle(pWnd), WM_SYSCOMMAND,
                            SC_HOTKEY, (LPARAM)UserHMGetHandle(pHotKey->pWnd));
        }
    }
    else
    {
        TRACE("UPM Hotkey Id %d Key %u\n", pHotKey->id, wVk);
        UserPostMessage(UserHMGetHandle(pWnd), WM_HOTKEY, pHotKey->id, MAKELONG(fsModifiers, wVk));
    }
    //ptiLastInput = pWnd->head.pti;
    return TRUE; /* Don't send any message */
}


/*
 * DefWndGetHotKey --- GetHotKey message support
 *
 * Win: DWP_GetHotKey
 */
UINT FASTCALL
DefWndGetHotKey(PWND pWnd)
{
    PHOT_KEY pHotKey = gphkFirst;
    while (pHotKey)
    {
        if (pHotKey->pWnd == pWnd && IsWindowHotKey(pHotKey))
        {
            /* We have found it */
            return MAKEWORD(pHotKey->vk, IntSwapModHKF(pHotKey->fsModifiers));
        }

        /* Move to the next entry */
        pHotKey = pHotKey->pNext;
    }

    return 0;
}

/*
 * DefWndSetHotKey --- SetHotKey message support
 *
 * Win: DWP_SetHotKey
 */
INT FASTCALL
DefWndSetHotKey(PWND pWnd, WPARAM wParam)
{
    const UINT fsModifiers = IntSwapModHKF(HIBYTE(wParam));
    const UINT vk = LOBYTE(wParam);
    PHOT_KEY pHotKey, *pLink;
    INT iRet = 1;

    // A hotkey cannot be associated with a child window.
    if (pWnd->style & WS_CHILD)
        return 0;

    // VK_ESCAPE, VK_SPACE, VK_TAB and VK_PACKET are invalid hotkeys.
    if (vk == VK_ESCAPE || vk == VK_SPACE || vk == VK_TAB || vk == VK_PACKET)
    {
        return -1;
    }

    if (wParam)
    {
        pHotKey = gphkFirst;
        while (pHotKey)
        {
            if (pHotKey->fsModifiers == fsModifiers &&
                pHotKey->vk == vk &&
                IsWindowHotKey(pHotKey))
            {
                if (pHotKey->pWnd != pWnd)
                    iRet = 2; // Another window already has the same hotkey.
                break;
            }

            /* Move to the next entry */
            pHotKey = pHotKey->pNext;
        }
    }

    pHotKey = gphkFirst;
    pLink = &gphkFirst;
    while (pHotKey)
    {
        if (pHotKey->pWnd == pWnd && IsWindowHotKey(pHotKey))
        {
            /* This window has already hotkey registered */
            break;
        }

        /* Move to the next entry */
        pLink = &pHotKey->pNext;
        pHotKey = pHotKey->pNext;
    }

    if (wParam)
    {
        if (!pHotKey)
        {
            /* Create new hotkey */
            pHotKey = ExAllocatePoolWithTag(PagedPool, sizeof(HOT_KEY), USERTAG_HOTKEY);
            if (pHotKey == NULL)
                return 0;

            pHotKey->pWnd = pWnd;
            pHotKey->id = IDHK_WNDKEY; // Don't care, these hotkeys are unrelated to the hotkeys set by RegisterHotKey
            pHotKey->pNext = gphkFirst;
            gphkFirst = pHotKey;
        }

        /* A window can only have one hotkey. If the window already has a
           hotkey associated with it, the new hotkey replaces the old one. */
        pHotKey->pti = NULL; /* IsWindowHotKey */
        pHotKey->fsModifiers = fsModifiers;
        pHotKey->vk = vk;
    }
    else if (pHotKey)
    {
        /* Remove hotkey */
        *pLink = pHotKey->pNext;
        ExFreePoolWithTag(pHotKey, USERTAG_HOTKEY);
    }

    return iRet;
}


BOOL FASTCALL
UserRegisterHotKey(PWND pWnd,
                   int id,
                   UINT fsModifiers,
                   UINT vk)
{
    PHOT_KEY pHotKey;
    PTHREADINFO pHotKeyThread;

    /* Find the hotkey thread */
    if (pWnd == NULL || pWnd == PWND_BOTTOM)
    {
        pHotKeyThread = PsGetCurrentThreadWin32Thread(); // gptiCurrent;
    }
    else
    {
        pHotKeyThread = pWnd->head.pti;
    }

    /* Ignore the VK_PACKET key since it is not a real keyboard input */
    if (vk == VK_PACKET)
        return FALSE;

    /* Check whether we modify an existing hotkey */
    if (IsHotKey(fsModifiers, vk))
    {
        EngSetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);
        WARN("Hotkey already exists\n");
        return FALSE;
    }

    /* Create a new hotkey */
    pHotKey = ExAllocatePoolWithTag(PagedPool, sizeof(HOT_KEY), USERTAG_HOTKEY);
    if (pHotKey == NULL)
    {
        EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    pHotKey->pti = pHotKeyThread;
    pHotKey->pWnd = pWnd;
    pHotKey->fsModifiers = fsModifiers;
    pHotKey->vk = vk;
    pHotKey->id = id;

    /* Insert the hotkey into the global list */
    pHotKey->pNext = gphkFirst;
    gphkFirst = pHotKey;

    return TRUE;
}

/**
 * @brief
 * Removes hotkeys previously registered by the user.
 *
 * @see NtUserUnregisterHotKey(), UserRegisterHotKey(),
 *      UnregisterWindowHotKeys(), UnregisterThreadHotKeys().
 **/
BOOL FASTCALL
UserUnregisterHotKey(PWND pWnd, int id)
{
    PHOT_KEY pHotKey = gphkFirst, phkNext, *pLink = &gphkFirst;
    BOOL bRet = FALSE;

    while (pHotKey)
    {
        /* Save next ptr for later use */
        phkNext = pHotKey->pNext;

        /* Should we delete this hotkey? */
        if (pHotKey->pWnd == pWnd && pHotKey->id == id)
        {
            /* Update next ptr for previous hotkey and free memory */
            *pLink = phkNext;
            ExFreePoolWithTag(pHotKey, USERTAG_HOTKEY);

            bRet = TRUE;
        }
        else
        {
            /* This hotkey will stay, use its next ptr */
            pLink = &pHotKey->pNext;
        }

        /* Move to the next entry */
        pHotKey = phkNext;
    }

    return bRet;
}


/* SYSCALLS *****************************************************************/

BOOL APIENTRY
NtUserRegisterHotKey(HWND hWnd,
                     int id,
                     UINT fsModifiers,
                     UINT vk)
{
    PWND pWnd = NULL;
    BOOL bRet = FALSE;

    TRACE("Enter NtUserRegisterHotKey\n");

    // FIXME: Does Win2k3 support MOD_NOREPEAT?
    if (fsModifiers & ~(MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN))
    {
        WARN("Invalid modifiers: %x\n", fsModifiers);
        EngSetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }

    UserEnterExclusive();

    /* Check the hotkey thread */
    if (hWnd == NULL)
    {
        pWnd = NULL;
    }
    else
    {
        pWnd = UserGetWindowObject(hWnd);
        if (!pWnd)
            goto cleanup;

        /* FIXME?? "Fix" wine msg "Window on another thread" test_hotkey */
        if (pWnd->head.pti != gptiCurrent)
        {
            EngSetLastError(ERROR_WINDOW_OF_OTHER_THREAD);
            WARN("Must be from the same Thread.\n");
            goto cleanup;
        }
    }

    bRet = UserRegisterHotKey(pWnd, id, fsModifiers, vk);

cleanup:
    TRACE("Leave NtUserRegisterHotKey, ret=%i\n", bRet);
    UserLeave();
    return bRet;
}

BOOL APIENTRY
NtUserUnregisterHotKey(HWND hWnd, int id)
{
    BOOL bRet = FALSE;
    PWND pWnd = NULL;

    TRACE("Enter NtUserUnregisterHotKey\n");
    UserEnterExclusive();

    /* Fail if the given window is invalid */
    if (hWnd && !(pWnd = UserGetWindowObject(hWnd)))
        goto cleanup;

    bRet = UserUnregisterHotKey(pWnd, id);

cleanup:
    TRACE("Leave NtUserUnregisterHotKey, ret=%i\n", bRet);
    UserLeave();
    return bRet;
}

/* EOF */
