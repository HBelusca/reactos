/*
 * PROJECT:     ReactOS UI Headers Library
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Windows subclassing support helpers.
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * Enable this define if you do not want to use safe-subclassing
 * (which requires comctl32.dll version 6+).
 **/
// #define SAFE_SUBCLASSING


/**
 * @brief   Unified UI subclassing.
 **/

typedef BOOL
(CALLBACK *UIWNDPROC)(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _In_ UINT_PTR uIdSubclass,
    _In_ DWORD_PTR dwRefData,
    _Out_ LRESULT* plResult);


/**
 * Library implementation.
 **/
#if defined(PROGLIST_LIB_IMPL) || !defined(PROGLIST_LIB)

#ifdef SAFE_SUBCLASSING

// SUBCLASSPROC
static LRESULT
CALLBACK
__UiSubclassProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _In_ UINT_PTR uIdSubclass,
    _In_ DWORD_PTR dwRefData)
{
    LRESULT lRes;
    UIWNDPROC uiWndProc = (UIWNDPROC)dwRefData;

    if (pInfo->uiWndProc(hWnd, uMsg, wParam, lParam, uIdSubclass, pInfo->pData, &lRes))
        return lRes; // Handled

    /* Not handled, call the original window procedure */
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

#else // !SAFE_SUBCLASSING

typedef struct _UISUBCLASSINFO
{
    WNDPROC oldWndProc; // fnWndProc;
    UIWNDPROC uiWndProc;
    UINT_PTR uIdSubclass;
    DWORD_PTR pData;
} UISUBCLASSINFO, *PUISUBCLASSINFO;

// WNDPROC
static LRESULT
CALLBACK
__UiSubclassProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    LRESULT lRes;
    // PUISUBCLASSINFO pInfo = GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    PUISUBCLASSINFO pInfo = GetProp(hWnd, TEXT("UiSubclassInfo"));
    if (!pInfo)
        return 0; // DefWindowProcW(...); but could be also dialog, so...

    if (pInfo->uiWndProc(hWnd, uMsg, wParam, lParam, pInfo->uIdSubclass, pInfo->pData, &lRes))
        return lRes; // Handled

    /* Not handled, call the original window procedure */
    return CallWindowProc(pInfo->oldWndProc, hWnd, uMsg, wParam, lParam);
}

#endif // SAFE_SUBCLASSING

/**
 * @brief
 * Tries to subclass a window.
 **/
BOOL
__UiSubclassWindow(
    _In_ HWND hWnd,
    _In_ UIWNDPROC pNewWndProc,
    _In_ UINT_PTR uIdSubclass,
    _In_opt_ DWORD_PTR pData)
{
#ifdef SAFE_SUBCLASSING
    if (!SetProp(hWnd, TEXT("UiSubclassInfo"), (HANDLE)pData))
        return FALSE;
    if (!SetWindowSubclass(hWnd, __UiSubclassProc, uIdSubclass, pNewWndProc))
    {
        RemoveProp(hWnd, TEXT("UiSubclassInfo"));
        return FALSE;
    }
    return TRUE;
#else
    WNDPROC oldProc;
    PUISUBCLASSINFO pInfo;

    /* Verify that we have not subclassed already (the current
     * window procedure is not ours); if not, fail */
    oldProc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
    if (oldProc == pNewWndProc)
        return TRUE; // or FALSE; ?

    /* Allocate private info */
    pInfo = LocalAlloc(LMEM_ZEROINIT, sizeof(*pInfo));
    if (!pInfo)
        return FALSE;

    pInfo->oldWndProc = oldProc;
    pInfo->uIdSubclass = uIdSubclass;
    pInfo->pData = pData;

    /* NOTE: Don't use GWLP_USERDATA: we may have no control over the window */
    // SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pInfo);
    if (!SetProp(hWnd, TEXT("UiSubclassInfo"), (HANDLE)pInfo))
    {
        /* We failed, cleanup and bail out */
        LocalFree(pInfo);
        return FALSE;
    }

    // SubclassWindow()
    SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pNewWndProc);
    return TRUE;
#endif
}

/**
 * @brief
 * Attempts to unsubclass a window, if its current
 * window procedure corresponds to the "expected" one.
 **/
DWORD_PTR
__UiUnsubclassWindow(
    _In_ HWND hWnd,
    _In_ UIWNDPROC pExpectedWndProc,
    _In_ UINT_PTR uIdSubclass)
{
    DWORD_PTR pData = NULL;

#ifdef SAFE_SUBCLASSING
    // GetWindowSubclass(hWnd, pExpectedWndProc, uIdSubclass, &pData);
    pData = RemoveProp(hWnd, TEXT("UiSubclassInfo"));
    GetWindowSubclass(hWnd, pExpectedWndProc, uIdSubclass, &pData);
    RemoveWindowSubclass(hWnd, pExpectedWndProc, uIdSubclass);
#else
    WNDPROC wndProc;
    PUISUBCLASSINFO pInfo;

    UNREFERENCED_PARAMETER(uIdSubclass);

    /* Verify that we have subclassed (the current
     * window procedure is ours); if not, fail */
    wndProc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
    if (wndProc != pExpectedWndProc)
        return NULL;

    // NOTE: We _could_ do a GetProp in order to get pInfo (but not remove it)
    // and check whether its uIdSubclass member is equal to the one passed
    // as parameter there. But we don't do that for the time being.

    /* NOTE: Don't use GWLP_USERDATA: we may have no control over the window */
    // pInfo = GetWindowLongPtr(hWnd, GWLP_USERDATA);
    pInfo = RemoveProp(hWnd, TEXT("UiSubclassInfo"));
    if (!pInfo)
    {
        /* We failed, bail out */
        return NULL;
    }

    /* Actually unsubclass it */
    SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pInfo->oldWndProc);
    pData = pInfo->pData;
    LocalFree(pInfo);
#endif

    return pData;
}

#endif // defined(PROGLIST_LIB_IMPL) || !defined(PROGLIST_LIB)


#ifdef __cplusplus
}
#endif

/* EOF */
