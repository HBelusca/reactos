/*
 * PROJECT:     ReactOS headers
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Core layout routines for resizable Win32 dialog boxes & windows
 * COPYRIGHT:   Copyright 2020-2021 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 *              Copyright 2021 Hermes Belusca-Maito
 */

#pragma once

/**
 * @brief   Structure defining the list of resizable dialog controls.
 **/
typedef struct _RESIZE_DIALOG_CONTROL_INFO
{
    /*
     * Constant static data
     */
    INT  nIDDlgItem; // FIXME: UINT nCtrlID; ??
    SMALL_RECT rcAdjustment;    /**< (Left,Top) / (Right,Bottom): Percentage
                                 **  adjustment on (X,Y) position / (width,height).
                                 **/
    UINT uWndPosFlags;          /**< Flags for DeferWindowPos() or SetWindowPos() */

    /*
     * Dynamic initialization data
     */
    HWND hwndCtrl;              /**< Handle to control window */
    RECT rcInit;                /**< Initial control window rectangle */
} RESIZE_DIALOG_CONTROL_INFO, *PRESIZE_DIALOG_CONTROL_INFO;





/**
 * @brief
 * Wrapper around DeferWindowPos() that handles the case of failure
 * or absence of a HDWP (multiple-window-position) structure.
 *
 * @param[in,opt]   hdwp
 * A handle to a multiple-window–position structure, returned by
 * BeginDeferWindowPos() or by the most recent call to DeferWindowPos()
 * or LayoutWindowPos().
 *
 * @param[in]       hWnd
 * A window handle for which update information is stored in @ hdwp.
 *
 * @param[in,opt]   hWndInsertAfter
 * A handle to the window that precedes the positioned window in the Z order.
 * This parameter is ignored if the @b SWP_NOZORDER flag is set in the @b uFlags
 * parameter. See DeferWindowPos() for more details.
 *
 * @param[in]   x
 * The x-coordinate of the window's upper-left corner.
 *
 * @param[in]   y
 * The y-coordinate of the window's upper-left corner.
 *
 * @param[in]   cx
 * The window's new width, in pixels.
 *
 * @param[in]   cy
 * The window's new height, in pixels.
 *
 * @param[in]   uFlags
 * A combination of values that affect the size and position of the window.
 * See DeferWindowPos() for more details.
 *
 * @return
 * Handle to the updated multiple-window–position structure. It may differ
 * from the one originally passed to the function.
 *
 * @see BeginDeferWindowPos(), DeferWindowPos(), SetWindowPos(), EndDeferWindowPos().
 **/
_Ret_maybenull_
inline HDWP
LayoutWindowPos(
    _In_opt_ HDWP hdwp, // hWinPosInfo,
    _In_ HWND hWnd,
    _In_opt_ HWND hWndInsertAfter,
    _In_ int  x,
    _In_ int  y,
    _In_ int  cx,
    _In_ int  cy,
    _In_ UINT uFlags)
{
    if (!(uFlags & SWP_NOREDRAW)) // TODO: SWP_NOCOPYBITS ??
    {
        /*
         * We're going to be moving and resizing the window, so make sure
         * it repaints itself. This will also avoid an ugly "jitter" effect
         * on Windows 10.
         */
        InvalidateRect(hWnd, NULL, TRUE);
    }

    /*
     * Now position the window. If DeferWindowPos fails,
     * move and resize the window manually below.
     */
    if (hdwp != NULL)
    {
        hdwp = DeferWindowPos(hdwp,
                              hWnd,
                              hWndInsertAfter,
                              x, y,
                              cx, cy,
                              uFlags);
    }

    /*
     * If the DeferWindowPos structure was not set up, or if we just
     * tore it down above by adding more controls than it could deal
     * with, move and resize manually here.
     */
    if (hdwp == NULL)
    {
         SetWindowPos(hWnd,
                      hWndInsertAfter,
                      x, y,
                      cx, cy,
                      uFlags);
    }

    return hdwp;
}

/* EOF */
