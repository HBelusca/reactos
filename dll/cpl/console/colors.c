/*
 * PROJECT:         ReactOS Console Configuration DLL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            dll/cpl/console/colors.c
 * PURPOSE:         Colors dialog
 * PROGRAMMERS:     Johannes Anderwald (johannes.anderwald@reactos.org)
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#include "console.h"

#define NDEBUG
#include <debug.h>

// Color page context
typedef struct _COLORPAGE_CTX
{
    PCONSOLE_PROPS_CTX pConProps;
    DWORD ActiveStaticControl;
} COLORPAGE_CTX, *PCOLORPAGE_CTX;

static VOID
PaintStaticControls(
    IN LPDRAWITEMSTRUCT drawItem,
    IN PCOLORPAGE_CTX pColorPage)
{
    PCONSOLE_STATE_INFO pConInfo = pColorPage->pConProps->ConInfo;
    HBRUSH hBrush;
    DWORD index;

    index = min(drawItem->CtlID - IDC_STATIC_COLOR1,
                ARRAYSIZE(pConInfo->ColorTable) - 1);

    hBrush = CreateSolidBrush(pConInfo->ColorTable[index]);
    FillRect(drawItem->hDC, &drawItem->rcItem, hBrush ? hBrush : GetStockObject(BLACK_BRUSH));
    if (hBrush) DeleteObject(hBrush);

    if (pColorPage->ActiveStaticControl == index)
        DrawFocusRect(drawItem->hDC, &drawItem->rcItem);
}

INT_PTR CALLBACK
ColorsProc(HWND hDlg,
           UINT uMsg,
           WPARAM wParam,
           LPARAM lParam)
{
    PCOLORPAGE_CTX pColorPage = (PCOLORPAGE_CTX)GetWindowLongPtrW(hDlg, DWLP_USER);
    PCONSOLE_PROPS_CTX pConProps;
    PCONSOLE_STATE_INFO ConInfo;

    DWORD colorIndex;
    COLORREF color;

    if (uMsg == WM_INITDIALOG)
    {
        pColorPage = (PCOLORPAGE_CTX)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pColorPage));
        if (!pColorPage)
        {
            EndDialog(hDlg, 0);
            return (INT_PTR)TRUE;
        }
        pColorPage->pConProps = (PCONSOLE_PROPS_CTX)((LPPROPSHEETPAGE)lParam)->lParam;
        ASSERT(pColorPage->pConProps);
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)pColorPage);
    }
    pConProps = (pColorPage ? pColorPage->pConProps : NULL);
    ConInfo = (pConProps ? pConProps->ConInfo : NULL);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            /* Set the valid range of the colour indicators */
            SendDlgItemMessageW(hDlg, IDC_UPDOWN_COLOR_RED  , UDM_SETRANGE, 0, (LPARAM)MAKELONG(255, 0));
            SendDlgItemMessageW(hDlg, IDC_UPDOWN_COLOR_GREEN, UDM_SETRANGE, 0, (LPARAM)MAKELONG(255, 0));
            SendDlgItemMessageW(hDlg, IDC_UPDOWN_COLOR_BLUE , UDM_SETRANGE, 0, (LPARAM)MAKELONG(255, 0));

            /* Select by default the screen background option */
            CheckRadioButton(hDlg, IDC_RADIO_SCREEN_TEXT, IDC_RADIO_POPUP_BACKGROUND, IDC_RADIO_SCREEN_BACKGROUND);
            SendMessageW(hDlg, WM_COMMAND, IDC_RADIO_SCREEN_BACKGROUND, 0);

            return TRUE;
        }

        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT drawItem = (LPDRAWITEMSTRUCT)lParam;

            if (IDC_STATIC_COLOR1 <= drawItem->CtlID && drawItem->CtlID <= IDC_STATIC_COLOR16)
                PaintStaticControls(drawItem, pColorPage);
            else if (drawItem->CtlID == IDC_STATIC_SCREEN_COLOR)
                PaintText(drawItem, pConProps, Screen);
            else if (drawItem->CtlID == IDC_STATIC_POPUP_COLOR)
                PaintText(drawItem, pConProps, Popup);

            return TRUE;
        }

        case WM_NOTIFY:
        {
            switch (((LPNMHDR)lParam)->code)
            {
                case PSN_APPLY:
                {
                    /* PSHNOTIFY's lParam is TRUE if 'OK' or 'Close'
                     * is pressed, and is FALSE if 'Apply' is pressed. */
                    ApplyConsoleInfo(hDlg, pConProps, !((LPPSHNOTIFY)lParam)->lParam);
                    return TRUE;
                }

                case UDN_DELTAPOS:
                {
                    LPNMUPDOWN lpnmud = (LPNMUPDOWN)lParam;

                    /* Get the current color */
                    colorIndex = pColorPage->ActiveStaticControl;
                    color = ConInfo->ColorTable[colorIndex];

                    if (lpnmud->hdr.idFrom == IDC_UPDOWN_COLOR_RED)
                    {
                        lpnmud->iPos = min(max(lpnmud->iPos + lpnmud->iDelta, 0), 255);
                        color = RGB(lpnmud->iPos, GetGValue(color), GetBValue(color));
                    }
                    else if (lpnmud->hdr.idFrom == IDC_UPDOWN_COLOR_GREEN)
                    {
                        lpnmud->iPos = min(max(lpnmud->iPos + lpnmud->iDelta, 0), 255);
                        color = RGB(GetRValue(color), lpnmud->iPos, GetBValue(color));
                    }
                    else if (lpnmud->hdr.idFrom == IDC_UPDOWN_COLOR_BLUE)
                    {
                        lpnmud->iPos = min(max(lpnmud->iPos + lpnmud->iDelta, 0), 255);
                        color = RGB(GetRValue(color), GetGValue(color), lpnmud->iPos);
                    }
                    else
                    {
                        break;
                    }

                    ConInfo->ColorTable[colorIndex] = color;
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_COLOR1 + colorIndex), NULL, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_SCREEN_COLOR), NULL, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_POPUP_COLOR) , NULL, TRUE);

                    PropSheet_Changed(GetParent(hDlg), hDlg);
                    break;
                }
            }

            break;
        }

        case WM_COMMAND:
        {
            /* NOTE: both BN_CLICKED and STN_CLICKED == 0 */
            if (HIWORD(wParam) == BN_CLICKED /* || HIWORD(wParam) == STN_CLICKED */)
            {
                WORD ctrlIndex = LOWORD(wParam);

                if (ctrlIndex == IDC_RADIO_SCREEN_TEXT       ||
                    ctrlIndex == IDC_RADIO_SCREEN_BACKGROUND ||
                    ctrlIndex == IDC_RADIO_POPUP_TEXT        ||
                    ctrlIndex == IDC_RADIO_POPUP_BACKGROUND)
                {
                    switch (ctrlIndex)
                    {
                    case IDC_RADIO_SCREEN_TEXT:
                        /* Get the colour of the screen foreground */
                        colorIndex = TextAttribFromAttrib(ConInfo->ScreenAttributes);
                        break;

                    case IDC_RADIO_SCREEN_BACKGROUND:
                        /* Get the colour of the screen background */
                        colorIndex = BkgdAttribFromAttrib(ConInfo->ScreenAttributes);
                        break;

                    case IDC_RADIO_POPUP_TEXT:
                        /* Get the colour of the popup foreground */
                        colorIndex = TextAttribFromAttrib(ConInfo->PopupAttributes);
                        break;

                    case IDC_RADIO_POPUP_BACKGROUND:
                        /* Get the colour of the popup background */
                        colorIndex = BkgdAttribFromAttrib(ConInfo->PopupAttributes);
                        break;
                    }

                    color = ConInfo->ColorTable[colorIndex];

                    /* Set the values of the colour indicators */
                    SetDlgItemInt(hDlg, IDC_EDIT_COLOR_RED  , GetRValue(color), FALSE);
                    SetDlgItemInt(hDlg, IDC_EDIT_COLOR_GREEN, GetGValue(color), FALSE);
                    SetDlgItemInt(hDlg, IDC_EDIT_COLOR_BLUE , GetBValue(color), FALSE);

                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_COLOR1 + pColorPage->ActiveStaticControl), NULL, TRUE);
                    pColorPage->ActiveStaticControl = colorIndex;
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_COLOR1 + pColorPage->ActiveStaticControl), NULL, TRUE);
                    break;
                }
                else
                if (IDC_STATIC_COLOR1 <= ctrlIndex && ctrlIndex <= IDC_STATIC_COLOR16)
                {
                    colorIndex = ctrlIndex - IDC_STATIC_COLOR1;

                    /* If the same static control was re-clicked, don't take it into account */
                    if (colorIndex == pColorPage->ActiveStaticControl)
                        break;

                    color = ConInfo->ColorTable[colorIndex];

                    /* Set the values of the colour indicators */
                    SetDlgItemInt(hDlg, IDC_EDIT_COLOR_RED  , GetRValue(color), FALSE);
                    SetDlgItemInt(hDlg, IDC_EDIT_COLOR_GREEN, GetGValue(color), FALSE);
                    SetDlgItemInt(hDlg, IDC_EDIT_COLOR_BLUE , GetBValue(color), FALSE);

                    if (IsDlgButtonChecked(hDlg, IDC_RADIO_SCREEN_TEXT))
                    {
                        ConInfo->ScreenAttributes = MakeAttrib(colorIndex, BkgdAttribFromAttrib(ConInfo->ScreenAttributes));
                    }
                    else if (IsDlgButtonChecked(hDlg, IDC_RADIO_SCREEN_BACKGROUND))
                    {
                        ConInfo->ScreenAttributes = MakeAttrib(TextAttribFromAttrib(ConInfo->ScreenAttributes), colorIndex);
                    }
                    else if (IsDlgButtonChecked(hDlg, IDC_RADIO_POPUP_TEXT))
                    {
                        ConInfo->PopupAttributes = MakeAttrib(colorIndex, BkgdAttribFromAttrib(ConInfo->PopupAttributes));
                    }
                    else if (IsDlgButtonChecked(hDlg, IDC_RADIO_POPUP_BACKGROUND))
                    {
                        ConInfo->PopupAttributes = MakeAttrib(TextAttribFromAttrib(ConInfo->PopupAttributes), colorIndex);
                    }

                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_COLOR1 + pColorPage->ActiveStaticControl), NULL, TRUE);
                    pColorPage->ActiveStaticControl = colorIndex;
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_COLOR1 + pColorPage->ActiveStaticControl), NULL, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_SCREEN_COLOR), NULL, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_POPUP_COLOR) , NULL, TRUE);

                    PropSheet_Changed(GetParent(hDlg), hDlg);
                    break;
                }
            }
            else if (HIWORD(wParam) == EN_KILLFOCUS)
            {
                WORD ctrlIndex = LOWORD(wParam);

                if (ctrlIndex == IDC_EDIT_COLOR_RED   ||
                    ctrlIndex == IDC_EDIT_COLOR_GREEN ||
                    ctrlIndex == IDC_EDIT_COLOR_BLUE)
                {
                    DWORD value;

                    /* Get the current colour */
                    colorIndex = pColorPage->ActiveStaticControl;
                    color = ConInfo->ColorTable[colorIndex];

                    /* Modify the colour component */
                    switch (ctrlIndex)
                    {
                    case IDC_EDIT_COLOR_RED:
                        value = GetDlgItemInt(hDlg, IDC_EDIT_COLOR_RED, NULL, FALSE);
                        value = min(max(value, 0), 255);
                        color = RGB(value, GetGValue(color), GetBValue(color));
                        break;

                    case IDC_EDIT_COLOR_GREEN:
                        value = GetDlgItemInt(hDlg, IDC_EDIT_COLOR_GREEN, NULL, FALSE);
                        value = min(max(value, 0), 255);
                        color = RGB(GetRValue(color), value, GetBValue(color));
                        break;

                    case IDC_EDIT_COLOR_BLUE:
                        value = GetDlgItemInt(hDlg, IDC_EDIT_COLOR_BLUE, NULL, FALSE);
                        value = min(max(value, 0), 255);
                        color = RGB(GetRValue(color), GetGValue(color), value);
                        break;
                    }

                    ConInfo->ColorTable[colorIndex] = color;
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_COLOR1 + colorIndex), NULL, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_SCREEN_COLOR), NULL, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_POPUP_COLOR) , NULL, TRUE);

                    PropSheet_Changed(GetParent(hDlg), hDlg);
                    break;
                }
            }

            break;
        }

        default:
            break;
    }

    return FALSE;
}
