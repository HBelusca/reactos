/*
 * PROJECT:     ReactOS UI Headers Library
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Subclasses a ListBox into a Progress-List.
 * COPYRIGHT:   Copyright 2021 He Yang <1160386205@qq.com>
 *              Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

//
// For examples, check
// https://raw.githubusercontent.com/HBelusca/reactos/msgina_banner/sdk/include/reactos/ui/branding.h
//

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


// Add new item to the listbox:
// wParam: displayed string
// lParam: associated data (optional)
#define WM_ADDITEM  (WM_USER + 1)

// wParam is item index (0 based)
#define WM_SETINDEX (WM_USER + 2)


#define ITEM_VPADDING       3
#define ITEM_LEFTPADDING    22


/**
 * @brief
 * Subclasses and initializes an existing ListBox window,
 * for using it as a progress list.
 **/
BOOL
ProgressList_Subclass(
    _In_ HWND hWnd);

/**
 * @brief
 * Removes the subclassing of an existing ListBox window
 * used as a progress list.
 **/
VOID
ProgressList_Unsubclass(
    _In_ HWND hWnd);


/**
 * Library implementation.
 **/
#if defined(PROGLIST_LIB_IMPL) || !defined(PROGLIST_LIB)


/* See windowsx.h */
/**
 * @brief   SendMessage macro.
 **/
#ifndef SNDMSG
#ifdef __cplusplus
#define SNDMSG  ::SendMessage
#else
#define SNDMSG  SendMessage
#endif
#endif /* !SNDMSG */


/* Subclassing support helpers */
#include "uisubclass.h"

#define PROGLIST_SUBCLASS_ID    0x734C6750  // 'sLgP'

typedef struct _PROGLIST_DATA
{
    HWND m_hListBox;
    HFONT m_hBoldFont;
    HBITMAP m_hArrowBmp;
    BITMAP m_ArrowBmp;
    INT m_PointedItem;
    INT m_TotalHeight;
} PROGLIST_DATA, *PPROGLIST_DATA;


static HFONT
CreateBoldFont(
    _In_ HFONT hOrigFont)
{
    LOGFONTW lFont = { 0 };
    GetObjectW(hOrigFont, sizeof(lFont), &lFont);
    lFont.lfWeight = FW_BOLD;

    return CreateFontIndirectW(&lFont);
}

static void
ProgressList_CalcTextRect(
    _In_ PPROGLIST_DATA pData,
    _In_ LPCWSTR lpText,
    _Inout_ PRECT pRect)
{
    HDC hdc;
    HFONT OldFont;

    hdc = GetDC(pData->m_hListBox);
    GetClientRect(pData->m_hListBox, pRect);

    pRect->bottom = pRect->top;
    pRect->left += ITEM_LEFTPADDING;

    OldFont = SelectFont(hdc, SNDMSG(pData->m_hListBox, WM_GETFONT, 0, 0));
    DrawTextW(hdc, lpText, -1, pRect, DT_CALCRECT | DT_WORDBREAK);
    SelectFont(hdc, OldFont);

    ReleaseDC(pData->m_hListBox, hdc);

    pRect->bottom -= pRect->top;
    pRect->bottom += ITEM_VPADDING * 2;
    pRect->top = 0;
    pRect->right -= pRect->left;
    pRect->left = 0;
}


/*
 * Reflected Window Message IDs, see olectl.h
 */
#ifndef OCM__BASE // _OLECTL_H_

#define OCM__BASE               (WM_USER + 0x1c00) // == 0x2000 (WM_REFLECT)
#define OCM_COMMAND             (OCM__BASE + WM_COMMAND)

#define OCM_CTLCOLORBTN         (OCM__BASE + WM_CTLCOLORBTN)
#define OCM_CTLCOLOREDIT        (OCM__BASE + WM_CTLCOLOREDIT)
#define OCM_CTLCOLORDLG         (OCM__BASE + WM_CTLCOLORDLG)
#define OCM_CTLCOLORLISTBOX     (OCM__BASE + WM_CTLCOLORLISTBOX)
#define OCM_CTLCOLORMSGBOX      (OCM__BASE + WM_CTLCOLORMSGBOX)
#define OCM_CTLCOLORSCROLLBAR   (OCM__BASE + WM_CTLCOLORSCROLLBAR)
#define OCM_CTLCOLORSTATIC      (OCM__BASE + WM_CTLCOLORSTATIC)

#define OCM_DRAWITEM            (OCM__BASE + WM_DRAWITEM)
#define OCM_MEASUREITEM         (OCM__BASE + WM_MEASUREITEM)
#define OCM_DELETEITEM          (OCM__BASE + WM_DELETEITEM)
#define OCM_VKEYTOITEM          (OCM__BASE + WM_VKEYTOITEM)
#define OCM_CHARTOITEM          (OCM__BASE + WM_CHARTOITEM)
#define OCM_COMPAREITEM         (OCM__BASE + WM_COMPAREITEM)
#define OCM_HSCROLL             (OCM__BASE + WM_HSCROLL)
#define OCM_VSCROLL             (OCM__BASE + WM_VSCROLL)
#define OCM_PARENTNOTIFY        (OCM__BASE + WM_PARENTNOTIFY)
#define OCM_NOTIFY              (OCM__BASE + WM_NOTIFY)

#endif

/**
 * @brief
 * Parent window subclass procedure, for messages that require reflection.
 * For more details, see
 * https://www.codeproject.com/Articles/744603/Custom-Controls-in-Win-API-Encapsulation-of-Cust
 **/
static BOOL
CALLBACK
_ProgressList_ParentWndProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _In_ UINT_PTR uIdSubclass,
    _In_ DWORD_PTR dwRefData,
    _Out_ LRESULT* plResult)
{
    HWND hChildWnd = (HWND)dwRefData;
    UNREFERENCED_PARAMETER(uIdSubclass);

    switch (uMsg)
    {
        /* Messages where lParam == control window handle */
        case WM_COMMAND:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORMSGBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
        {
            /* Check that child window is the listbox */
            if (lParam == (LPARAM)hChildWnd)
            {
                *plResult = SNDMSG((HWND)lParam, (OCM__BASE + uMsg), wParam, lParam);
                return TRUE;
            }
            break;
        }

        /* Messages where wParam == control ID */
        case WM_DRAWITEM:    // lParam == PDRAWITEMSTRUCT
        case WM_MEASUREITEM: // lParam == PMEASUREITEMSTRUCT
        case WM_DELETEITEM:  // lParam == PDELETEITEMSTRUCT
        case WM_COMPAREITEM: // lParam == PCOMPAREITEMSTRUCT
        {
            if (wParam)
            {
                HWND hWndCtl = GetDlgItem(hWnd, wParam);
                /* Check that child window is the listbox */
                if (hWndCtl == hChildWnd)
                {
                    *plResult = SNDMSG(hWndCtl, (OCM__BASE + uMsg), wParam, lParam);
                    return TRUE;
                }
            }
            break;
        }

// WM_VKEYTOITEM:
// WM_CHARTOITEM:
    // lParam == handle to LB control
// WM_HSCROLL:
// WM_VSCROLL:
    // lParam == handle to control
// WM_PARENTNOTIFY:
    // Depends...

        case WM_NOTIFY:
        {
            HWND hWndCtl = ((NMHDR*)lParam)->hwndFrom;
            /* Check that child window is the listbox */
            if (hWndCtl == hChildWnd)
            {
                *plResult = SNDMSG(hWndCtl, (OCM__BASE + uMsg), wParam, lParam);
                return TRUE;
            }
        }

        default:
            break;
    }

    return FALSE;
}

/**
 * @brief
 * ProgressList window subclass procedure.
 **/
static BOOL
CALLBACK
_ProgressList_WndProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _In_ UINT_PTR uIdSubclass,
    _In_ DWORD_PTR dwRefData,
    _Out_ LRESULT* plResult)
{
    PPROGLIST_DATA pData = (PPROGLIST_DATA)dwRefData;
    UNREFERENCED_PARAMETER(uIdSubclass);

    *plResult = 0;
    switch (uMsg)
    {
        case WM_NCCREATE:
        case WM_CREATE:
            break;

        // case WM_NCDESTROY:
        case WM_DESTROY:
        {
            DeleteObject(pData->m_hArrowBmp);
            DeleteFont(pData->m_hBoldFont);
            break;
        }

        case WM_ADDITEM:
        {
            RECT lbRect;

            /*
             * Add a new item to the listbox, given by:
             * - its displayed string (in wParam)
             * - and its associated data (in lParam)
             */
            INT Index = ListBox_AddString(pData->m_hListBox, (LPARAM)wParam);
            // if (Index != LB_ERR && Index != LB_ERRSPACE)
            pData->m_TotalHeight += ListBox_GetItemHeight(pData->m_hListBox, Index);
            ListBox_SetItemData(pData->m_hListBox, Index, (LPARAM)lParam);

            // ResizeListBoxAndDialog(m_TotalHeight);
            GetWindowRect(pData->m_hListBox, &lbRect);
            SetWindowPos(pData->m_hListBox, NULL, 0, 0,
                         lbRect.right - lbRect.left, pData->m_TotalHeight,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);

            return TRUE;
        }

        case WM_SETINDEX:
        {
            pData->m_PointedItem = wParam;
            InvalidateRect(pData->m_hListBox, NULL, TRUE);
            return TRUE;
        }


        /*
         * Reflected messages from parent window.
         */
        case OCM_MEASUREITEM:
        {
            PMEASUREITEMSTRUCT pMeasureItem = (PMEASUREITEMSTRUCT)lParam;
            RECT TextRect = { 0 };

            CStringW ItemText;
            ListBox_GetText(pData->m_hListBox, pMeasureItem->itemID,
                            ItemText.GetBuffer(
                                ListBox_GetTextLen(pData->m_hListBox,
                                    pMeasureItem->itemID) + 1));

            ProgressList_CalcTextRect(pData, ItemText, &TextRect);

            ItemText.ReleaseBuffer();

            pMeasureItem->itemHeight = TextRect.bottom - TextRect.top;
            pMeasureItem->itemWidth  = TextRect.right - TextRect.left;

            return TRUE;
        }

        case OCM_DRAWITEM:
        {
            PDRAWITEMSTRUCT pDrawItem = (PDRAWITEMSTRUCT)lParam;

            CStringW ItemText;
            ListBox_GetText(pData->m_hListBox, pDrawItem->itemID,
                            ItemText.GetBuffer(
                                ListBox_GetTextLen(pData->m_hListBox,
                                    pDrawItem->itemID) + 1));

            SetBkMode(pDrawItem->hDC, TRANSPARENT);

            HFONT hOldFont = NULL;
            if (pData->m_PointedItem == (INT)pDrawItem->itemData)
            {
                HDC hCompDC = CreateCompatibleDC(pDrawItem->hDC);

                SelectBitmap(hCompDC, pData->m_hArrowBmp);

                int IconLeftPadding = (ITEM_LEFTPADDING - pData->m_ArrowBmp.bmWidth) / 2;
                int IconTopPadding = (pDrawItem->rcItem.bottom - pDrawItem->rcItem.top - pData->m_ArrowBmp.bmHeight) / 2;

                BitBlt(pDrawItem->hDC,
                       IconLeftPadding,
                       pDrawItem->rcItem.top + IconTopPadding,
                       pData->m_ArrowBmp.bmWidth,
                       pData->m_ArrowBmp.bmHeight,
                       hCompDC, 0, 0, SRCAND);

                DeleteDC(hCompDC);

                hOldFont = SelectFont(pDrawItem->hDC, pData->m_hBoldFont);
            }

            pDrawItem->rcItem.left += ITEM_LEFTPADDING;
            pDrawItem->rcItem.top += ITEM_VPADDING;
            DrawTextW(pDrawItem->hDC, ItemText, -1,
                      &(pDrawItem->rcItem), DT_WORDBREAK);

            if (hOldFont)
                SelectFont(pDrawItem->hDC, hOldFont);

            ItemText.ReleaseBuffer();

            return TRUE;
        }

        case OCM_CTLCOLORLISTBOX:
            *plResult = (LRESULT)GetStockBrush(NULL_BRUSH);
            return TRUE;
    }

    return FALSE;
}


BOOL
ProgressList_Subclass(
    _In_ HWND hWnd)
{
    TCHAR szClass[64];
    PPROGLIST_DATA pData;
    DWORD dwStyle, dwExStyle;

    /* Verify the window is a listbox; if not, fail */
    if (!GetClassName(hWnd, szClass, _countof(szClass)))
        return FALSE;
    if (_tcscmp(szClass, WC_LISTBOX) != 0)
        return FALSE;

    /* Allocate private data */
    pData = LocalAlloc(LMEM_ZEROINIT, sizeof(*pData));
    if (!pData)
        return FALSE;

    /* Initialize it */
    pData->m_hListBox = hWnd;
    pData->m_hBoldFont = CreateBoldFont(SNDMSG(hWnd, WM_GETFONT, 0, 0));

    pData->m_hArrowBmp = LoadBitmapW(NULL, MAKEINTRESOURCEW(OBM_MNARROW));
    GetObjectW(pData->m_hArrowBmp, sizeof(pData->m_ArrowBmp), &pData->m_ArrowBmp);

    pData->m_PointedItem = 0;
    pData->m_TotalHeight = 0;
    // ResizeListBoxAndDialog(m_TotalHeight);

    /* Ensure these styles are set */
    dwStyle = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE);
    dwStyle |= LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS |
               LBS_NOINTEGRALHEIGHT | WS_DISABLED | WS_VSCROLL;
    SetWindowLongPtr(hWnd, GWL_STYLE, dwStyle);

    /* Remove the sunken-edged border from the listbox */
    dwExStyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE);
    SetWindowLongPtr(hWnd, GWL_EXSTYLE, dwExStyle & ~WS_EX_CLIENTEDGE);

    /* Do the subclassing */
    if (!__UiSubclassWindow(hWnd,
                            _ProgressList_WndProc,
                            PROGLIST_SUBCLASS_ID,
                            (DWORD_PTR)pData))
    {
        /* We failed the subclassing, cleanup and bail out */
        LocalFree(pData);
        return FALSE;
    }

    /* In order to proceed with message reflection,
     * we need to subclass the parent window too! */
    if (!__UiSubclassWindow(GetParent(hWnd),
                            _ProgressList_ParentWndProc,
                            PROGLIST_SUBCLASS_ID,
                            (DWORD_PTR)hWnd))
    {
        /* We failed the subclassing, cleanup and bail out */
        __UiUnsubclassWindow(hWnd,
                             _ProgressList_WndProc,
                             PROGLIST_SUBCLASS_ID);
        LocalFree(pData);
        return FALSE;
    }

    return TRUE;
}

VOID
ProgressList_Unsubclass(
    _In_ HWND hWnd)
{
    PPROGLIST_DATA pData;

    /* Unsubclass the parent window */
    __UiUnsubclassWindow(GetParent(hWnd),
                         _ProgressList_ParentWndProc,
                         PROGLIST_SUBCLASS_ID);

    /* And now the listbox window */
    pData = (PPROGLIST_DATA)__UiUnsubclassWindow(hWnd,
                                                 _ProgressList_WndProc,
                                                 PROGLIST_SUBCLASS_ID);
    if (pData)
        LocalFree(pData);
}


#endif // defined(PROGLIST_LIB_IMPL) || !defined(PROGLIST_LIB)


#ifdef __cplusplus
}
#endif

/* EOF */
