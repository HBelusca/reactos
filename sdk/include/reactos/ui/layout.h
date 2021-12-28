/*
 * PROJECT:     ReactOS headers
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Layout engine for resizable Win32 dialog boxes & windows
 * COPYRIGHT:   Copyright 2020-2021 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 *              Copyright 2021 Hermes Belusca-Maito
 */

#pragma once
#include <assert.h>

#include "layout_core.h"

/* Use specific definitions if we are used in WINE code */
#if defined(_WINE) || defined(__WINE__) || defined(__WINESRC__)
#define LAYOUT_WINE
#endif


/**
 * @brief   Anchor Border flags
 **/
#ifndef BF_LEFT

#define BF_LEFT         0x0001
#define BF_TOP          0x0002
#define BF_RIGHT        0x0004
#define BF_BOTTOM       0x0008

#define BF_TOPLEFT      (BF_TOP | BF_LEFT)
#define BF_TOPRIGHT     (BF_TOP | BF_RIGHT)
#define BF_BOTTOMLEFT   (BF_BOTTOM | BF_LEFT)
#define BF_BOTTOMRIGHT  (BF_BOTTOM | BF_RIGHT)
#define BF_RECT         (BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM)

#endif


//
// NOTE: This code is inspired from the LAYOUT_INFO and co.
// from Wine's originating shell32/wine/brsfolder.c .
//

// Equivalent of RESIZE_DIALOG_CONTROL_INFO, but Wine-inspired.
typedef struct LAYOUT_INFO
{
    UINT m_nCtrlID;
    UINT m_uEdges; /* BF_* flags */ // More-or-less equivalent to SMALL_RECT adjustment specification.

    HWND m_hwndCtrl;

    SMALL_RECT rcPercents;

    SIZE m_margin1;
    SIZE m_margin2;
} LAYOUT_INFO, *PLAYOUT_INFO;

typedef struct LAYOUT_DATA
{
    HWND m_hwndParent;
    HWND m_hwndGrip;
    PLAYOUT_INFO m_pLayouts;
    UINT m_cLayouts;
} LAYOUT_DATA, *PLAYOUT_DATA;

static __inline void
_layout_ModifySystemMenu(PLAYOUT_DATA pData, BOOL bEnableResize)
{
    if (bEnableResize)
    {
        GetSystemMenu(pData->m_hwndParent, TRUE); /* revert */
    }
    else
    {
        HMENU hSysMenu = GetSystemMenu(pData->m_hwndParent, FALSE);
        RemoveMenu(hSysMenu, SC_MAXIMIZE, MF_BYCOMMAND);
        RemoveMenu(hSysMenu, SC_SIZE, MF_BYCOMMAND);
        RemoveMenu(hSysMenu, SC_RESTORE, MF_BYCOMMAND);
    }
}

/* GRIP handling functions ****************************************************/

// TODO: Compare with M.Smith

/**
 * @brief
 * Helper for moving the sizing grip of the window.
 *
 * @param[in]       pData
 * Pointer to a @b LAYOUT_DATA structure.
 *
 * @param[in,opt]   hdwp
 * A handle to a multiple-window–position structure, returned by
 * BeginDeferWindowPos() or by the most recent call to DeferWindowPos()
 * or LayoutWindowPos().
 *
 * @return
 * Handle to the updated multiple-window–position structure. It may differ
 * from the one originally passed to the function.
 **/
static __inline
_Ret_maybenull_
HDWP
_layout_MoveGrip(
    _In_ PLAYOUT_DATA pData,
    _In_opt_ HDWP hDwp)
{
    RECT rcClient;

    // if (!IsWindowVisible(pData->m_hwndGrip))
        // return hDwp;

    SIZE size = { GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CYHSCROLL) };
    GetClientRect(pData->m_hwndParent, &rcClient);

    hDwp = LayoutWindowPos(hDwp,
                           pData->m_hwndGrip,
                           NULL,
                           rcClient.right - size.cx,
                           rcClient.bottom - size.cy,
                           size.cx, size.cy,
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS);
    return hDwp;
}

/**
 * @brief
 * Helper for moving the sizing grip of a resizable window.
 *
 * @param[in]   pData
 * Pointer to a @b LAYOUT_DATA structure.
 *
 * @param[in]   bShow
 * @b TRUE to show the grip, @b FALSE for hiding it.
 *
 * @return  None.
 **/
static __inline void
LayoutShowGrip(PLAYOUT_DATA pData, BOOL bShow)
{
    UINT uSWP = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED;
    DWORD style = GetWindowLongPtrW(pData->m_hwndParent, GWL_STYLE);
    DWORD new_style = (bShow ? (style | WS_SIZEBOX) : (style & ~WS_SIZEBOX));
    if (style != new_style)
    {
        SetWindowLongPtrW(pData->m_hwndParent, GWL_STYLE, new_style); /* change style */
        SetWindowPos(pData->m_hwndParent, NULL, 0, 0, 0, 0, uSWP); /* frame changed */
    }

    if (!bShow)
    {
        ShowWindow(pData->m_hwndGrip, SW_HIDE);
        return;
    }

    if (pData->m_hwndGrip == NULL)
    {
        DWORD style = WS_CHILD | WS_CLIPSIBLINGS | SBS_SIZEGRIP;
        pData->m_hwndGrip = CreateWindowExW(0, L"SCROLLBAR", NULL, style,
                                            0, 0, 0, 0, pData->m_hwndParent,
                                            NULL, GetModuleHandleW(NULL), NULL);
    }
    _layout_MoveGrip(pData, NULL);
    ShowWindow(pData->m_hwndGrip, SW_SHOWNOACTIVATE);
}


// NOTE: This is adapted to the wine-style layout structure.
static __inline
HDWP
_layout_DoMoveItem(
    _In_ PLAYOUT_DATA pData,
    _In_opt_ HDWP hDwp,
    _In_ const LAYOUT_INFO* pLayout,
    _In_ LONG nWidth,
    _In_ LONG nHeight)
{
    RECT rcChild, NewRect;

    if (!GetWindowRect(pLayout->m_hwndCtrl, &rcChild))
        return hDwp;
    MapWindowPoints(NULL, pData->m_hwndParent, (LPPOINT)&rcChild, sizeof(RECT) / sizeof(POINT));

    NewRect.left = pLayout->m_margin1.cx + nWidth * pLayout->rcPercents.Left / 100;
    NewRect.top = pLayout->m_margin1.cy + nHeight * pLayout->rcPercents.Top / 100;
    NewRect.right = pLayout->m_margin2.cx + nWidth * pLayout->rcPercents.Right / 100;
    NewRect.bottom = pLayout->m_margin2.cy + nHeight * pLayout->rcPercents.Bottom / 100;

    // NewRect.left = rcChildOrg.left + (nWidth - cxInit) * pLayout->rcPercents.Left / 100;
    // NewRect.top = rcChildOrg.top + (nHeight - cyInit) * pLayout->rcPercents.Top / 100;
    // NewRect.right = rcChildOrg.right + (nWidth - cxInit) * pLayout->rcPercents.Right / 100;
    // NewRect.bottom = rcChildOrg.bottom + (nHeight - cyInit) * pLayout->rcPercents.Bottom / 100;

    // NewRect.right - NewRect.left
    // == (rcChildOrg.right - rcChildOrg.left) + (nWidth - cxInit) * (pLayout->rcPercents.Right - pLayout->rcPercents.Left) / 100;
    //
    // NewRect.bottom - NewRect.top
    // == (rcChildOrg.bottom - rcChildOrg.top) + (nHeight - cyInit) * (pLayout->rcPercents.Bottom - pLayout->rcPercents.Top) / 100;

    if (!EqualRect(&NewRect, &rcChild))
    {
        hDwp = LayoutWindowPos(hDwp,
                               pLayout->m_hwndCtrl,
                               NULL,
                               NewRect.left,
                               NewRect.top,
                               NewRect.right - NewRect.left,
                               NewRect.bottom - NewRect.top,
                               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS);
    }

    return hDwp;
}

static __inline void
_layout_ArrangeLayout(_In_ PLAYOUT_DATA pData)
{
    RECT rcClient;
    LONG nWidth, nHeight;
    UINT iItem;

    HDWP hDwp = BeginDeferWindowPos(pData->m_cLayouts + 1);
    if (hDwp == NULL)
        return;

    /* As we are currently resizing from parent's WM_SIZE,
     * we are retrieving the **new** parent's client rect. */
    // Therefore it's not necessary to call that, since we can get the new cx/cy directly from WM_SIZE.
    GetClientRect(pData->m_hwndParent, &rcClient);
    nWidth = rcClient.right - rcClient.left;  // == new cx
    nHeight = rcClient.bottom - rcClient.top; // == new cy

    for (iItem = 0; iItem < pData->m_cLayouts; ++iItem)
    {
        hDwp = _layout_DoMoveItem(pData, hDwp, &pData->m_pLayouts[iItem], nWidth, nHeight);
    }

    hDwp = _layout_MoveGrip(pData, hDwp);
    EndDeferWindowPos(hDwp);
}

/**
 * @brief   Helper for converting @b BF_* flags into percentages.
 **/
static __inline void
_layout_GetPercents(
    _In_ PSMALL_RECT prcPercents,
    _In_ UINT uEdges)
{
    prcPercents->Left   = (uEdges & BF_LEFT)   ? 0   : 100;
    prcPercents->Right  = (uEdges & BF_RIGHT)  ? 100 : 0;
    prcPercents->Top    = (uEdges & BF_TOP)    ? 0   : 100;
    prcPercents->Bottom = (uEdges & BF_BOTTOM) ? 100 : 0;
}

static __inline void
_layout_InitLayouts(PLAYOUT_DATA pData)
{
    RECT rcClient, rcChild;
    LONG nWidth, nHeight;
    UINT iItem;
    PLAYOUT_INFO pInfo;

    GetClientRect(pData->m_hwndParent, &rcClient);
    nWidth = rcClient.right - rcClient.left;    // == cxInit
    nHeight = rcClient.bottom - rcClient.top;   // == cyInit

    for (iItem = 0; iItem < pData->m_cLayouts; ++iItem)
    {
        pInfo = &pData->m_pLayouts[iItem];
        if (pInfo->m_hwndCtrl == NULL)
        {
            pInfo->m_hwndCtrl = GetDlgItem(pData->m_hwndParent, pInfo->m_nCtrlID);
            if (pInfo->m_hwndCtrl == NULL)
                continue;
        }

        GetWindowRect(pInfo->m_hwndCtrl, &rcChild);
        MapWindowPoints(NULL, pData->m_hwndParent, (LPPOINT)&rcChild, sizeof(RECT) / sizeof(POINT));

        pInfo->m_margin1.cx = rcChild.left - nWidth * pInfo->rcPercents.Left / 100;
        pInfo->m_margin1.cy = rcChild.top - nHeight * pInfo->rcPercents.Top / 100;
        pInfo->m_margin2.cx = rcChild.right - nWidth * pInfo->rcPercents.Right / 100;
        pInfo->m_margin2.cy = rcChild.bottom - nHeight * pInfo->rcPercents.Bottom / 100;
    }
}

/* NOTE: Please call LayoutUpdate on parent's WM_SIZE. */
static __inline void
#ifdef LAYOUT_WINE
LayoutUpdate(
    _In_ HWND ignored1,
    _In_ PLAYOUT_DATA pData,
    _In_ PCVOID ignored2,
    _In_ UINT ignored3)
#else
LayoutUpdate(
    _In_ PLAYOUT_DATA pData)
#endif
{
#ifdef LAYOUT_WINE
    UNREFERENCED_PARAMETER(ignored1);
    UNREFERENCED_PARAMETER(ignored2);
    UNREFERENCED_PARAMETER(ignored3);
#endif

    if (pData == NULL || !pData->m_hwndParent)
        return;
    assert(IsWindow(pData->m_hwndParent));
    _layout_ArrangeLayout(pData);
}

static __inline void
LayoutEnableResize(PLAYOUT_DATA pData, BOOL bEnable)
{
    LayoutShowGrip(pData, bEnable);
    _layout_ModifySystemMenu(pData, bEnable);
}

static __inline
PLAYOUT_DATA
LayoutInit(
    _In_ HWND hwndParent,
    _In_ const LAYOUT_INFO* pLayouts,
    _In_ INT cLayouts)
{
    BOOL bShowGrip;
    SIZE_T cb;
    PLAYOUT_DATA pData = (PLAYOUT_DATA)HeapAlloc(GetProcessHeap(), 0, sizeof(LAYOUT_DATA));
    if (pData == NULL)
    {
        assert(0);
        return NULL;
    }

    /* NOTE: If cLayouts is negative, then don't show size grip */
    if (cLayouts < 0)
    {
        cLayouts = -cLayouts;
        bShowGrip = FALSE;
    }
    else
    {
        bShowGrip = TRUE;
    }

    cb = cLayouts * sizeof(LAYOUT_INFO);
    pData->m_cLayouts = cLayouts;
    pData->m_pLayouts = (PLAYOUT_INFO)HeapAlloc(GetProcessHeap(), 0, cb);
    if (pData->m_pLayouts == NULL)
    {
        assert(0);
        HeapFree(GetProcessHeap(), 0, pData);
        return NULL;
    }
    CopyMemory(pData->m_pLayouts, pLayouts, cb);

    assert(IsWindow(hwndParent));

    pData->m_hwndParent = hwndParent;

    pData->m_hwndGrip = NULL;
    if (bShowGrip)
        // LayoutShowGrip(pData, bShowGrip);
        LayoutEnableResize(pData, /*TRUE*/ bShowGrip);

    _layout_InitLayouts(pData);
    return pData;
}

static __inline void
LayoutDestroy(PLAYOUT_DATA pData)
{
    if (!pData)
        return;
    HeapFree(GetProcessHeap(), 0, pData->m_pLayouts);
    HeapFree(GetProcessHeap(), 0, pData);
}
