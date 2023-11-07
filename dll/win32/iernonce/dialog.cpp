/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Classes for displaying progress dialog.
 * COPYRIGHT:   Copyright 2021 He Yang <1160386205@qq.com>
 */

#include "iernonce.h"
#include <progresslist.h>
#include <process.h>

ProgressDlg::ProgressDlg(_In_ RunOnceExInstance &RunOnceExInst) :
    m_hListBox(NULL),
    m_RunOnceExInst(RunOnceExInst)
{ ; }

BOOL ProgressDlg::RunDialogBox()
{
    // Show the dialog and run the items only when the list is not empty.
    if (m_RunOnceExInst.m_SectionList.GetSize() != 0)
    {
        return (DoModal() == 1);
    }
    return TRUE;
}

void ProgressDlg::ResizeListBoxAndDialog(_In_ int NewHeight)
{
    RECT ListBoxRect;
    RECT DlgRect;
    ::GetWindowRect(m_hListBox, &ListBoxRect);
    GetWindowRect(&DlgRect);

    int HeightDiff = NewHeight - (ListBoxRect.bottom - ListBoxRect.top);

    ::SetWindowPos(m_hListBox, NULL, 0, 0,
                   ListBoxRect.right - ListBoxRect.left, NewHeight,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);

    SetWindowPos(HWND_TOP, 0, 0,
                 DlgRect.right - DlgRect.left,
                 DlgRect.bottom - DlgRect.top + HeightDiff,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
}

unsigned int __stdcall
RunOnceExExecThread(_In_ void* Param)
{
    ProgressDlg *pProgressDlg = (ProgressDlg *)Param;

    pProgressDlg->m_RunOnceExInst.Exec(pProgressDlg->m_hWnd);
    return 0;
}

BOOL
ProgressDlg::ProcessWindowMessage(
    _In_ HWND hwnd,
    _In_ UINT message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _Out_ LRESULT& lResult,
    _In_ DWORD dwMsgMapID)
{
    lResult = 0;
    switch (message)
    {
        case WM_INITDIALOG:
        {
            if (!m_RunOnceExInst.m_Title.IsEmpty())
                SetWindowTextW(m_RunOnceExInst.m_Title);

            // Get the listbox and subclass it
            m_hListBox = GetDlgItem(IDC_LB_ITEMS);
            if (!ProgressList_Subclass(m_hListBox))
            {
                // Couldn't subclass, bail out
                EndDialog(0);
                return TRUE;
            }

            // m_hArrowBmp = LoadBitmapW(NULL, MAKEINTRESOURCE(OBM_MNARROW));

            // Add all sections with non-empty title into listbox
            int TotalHeight = 0;
            for (int i = 0; i < m_RunOnceExInst.m_SectionList.GetSize(); i++)
            {
                RunOnceExSection &Section = m_RunOnceExInst.m_SectionList[i];

                if (!Section.m_SectionTitle.IsEmpty())
                {
                    ::SendMessage(m_hListBox, WM_ADDITEM, Section.m_SectionTitle, i);
                    // TotalHeight
                }
            }

            ResizeListBoxAndDialog(TotalHeight);

            // Launch a thread to execute tasks.
            HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, RunOnceExExecThread, (void*)this, 0, NULL);
            if (hThread == INVALID_HANDLE_VALUE)
            {
                EndDialog(0);
                return TRUE;
            }
            CloseHandle(hThread);

            lResult = TRUE; // set keyboard focus to the dialog box control.
            break;
        }

        case WM_DESTROY:
        {
            // Unsubclass the listbox
            ProgressList_Unsubclass(m_hListBox);
            break;
        }

        case WM_SETINDEX:
        {
            if ((int)wParam == m_RunOnceExInst.m_SectionList.GetSize())
            {
                // All sections are handled, lParam is bSuccess.
                EndDialog(lParam);
            }
            ::SendMessage(m_hListBox, WM_SETINDEX, wParam, lParam);
            // InvalidateRect(NULL); // TODO: Recheck
            break;
        }
    }
    return TRUE;
}
