/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UI Menu Functions
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2005 Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2012-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

/* FUNCTIONS *****************************************************************/

/*static*/ VOID
TuiCalcMenuBoxSize(
    _Inout_ PUI_MENU_INFO MenuInfo);

BOOLEAN
UiProcessMenuKeyboardEvent(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ ULONG KeyEvent);

// UI_EVENT_PROC
static ULONG_PTR
NTAPI
UiMenuProc(
    _In_ PVOID UiContext,
    /**/_In_opt_ PVOID UserContext,/**/
    _In_ UI_EVENT Event,
    _In_ ULONG_PTR EventParam)
{
    PUI_MENU_INFO MenuInfo = (PUI_MENU_INFO)UserContext;

    switch (Event)
    {
    case UI_Initialize:
    {
        // FIXME: Theme-specific
        /* Calculate the size of the menu box */
        TuiCalcMenuBoxSize(MenuInfo); // HACK: Investigate...

        // TODO: Investigate: See UI_Paint
        /* Redraw the menu */
        // (void)UiDisplayMenuEx(MenuInfo/*, SelectedItem, CanEscape*/);
        UiDrawMenu(MenuInfo);

        /* Send the UI_MenuSelect notification */
        UiSendMsg(UiContext, /**/UserContext,/**/
                  UI_MenuSelect, (ULONG_PTR)MenuInfo->SelectedItem);
        break;
    }

    case UI_KeyPress:
    {
        ULONG KeyPress = (ULONG)EventParam;
        // BOOLEAN Extended = !!(EventParam & 0x0100);

        /* Filter out the extended flag */
        KeyPress &= ~0x0100;

#if 0
        /* Check for ENTER or ESC */
        if (KeyPress == KEY_ENTER)
        {
            UiEndUi(UiContext, TRUE);
            return TRUE;
        }
        else if (MenuInfo->CanEscape && (KeyPress == KEY_ESC))
        {
            UiEndUi(UiContext, FALSE);
            return TRUE;
        }
        else
#endif
        /* Process the key */
        if (UiProcessMenuKeyboardEvent(MenuInfo, KeyPress))
        {
            /* Key handled, redraw */
            UiRedraw(UiContext);
        }

        return TRUE;
    }

    case UI_Paint:
    {
        // /* Redraw the menu */
        // UiDrawMenu(MenuInfo);
        break;
    }

    default:
        break;
    }

    /* Perform default action */
    return FALSE;
}

ULONG_PTR
UiDisplayMenuEx(
    _Inout_ PUI_MENU_INFO MenuInfo/*,
    _Out_ PULONG SelectedItem,
    _In_ BOOLEAN CanEscape*/)
{
    // // FIXME: Now done in UI_Initialize
    // // FIXME: Theme-specific
    // /* Calculate the size of the menu box */
    // TuiCalcMenuBoxSize(MenuInfo);

    // // FIXME: Now done in UI_Paint
    // /* Draw the menu */
    // UiDrawMenu(MenuInfo);

#if 0
    Result = UiDispatch(UiMenuProc, MenuInfo);
    if (!Result)
        return FALSE;

    /* Process keys */
    while (TRUE)
    {
        /* Check for a keypress */
        if (MachConsKbHit())
        {
            /* Get the key */
            ULONG KeyPress = MachConsGetCh();
            if (KeyPress == KEY_EXTENDED)
                KeyPress = MachConsGetCh();

            /* Check for ENTER or ESC */
            if (KeyPress == KEY_ENTER) break;
            if (CanEscape && KeyPress == KEY_ESC) return FALSE;

            /* Process key presses */
            if (UiProcessMenuKeyboardEvent(&MenuInfo, KeyPress))
            {
                /* Key handled, update the video buffer */
                VideoCopyOffScreenBufferToVRAM();
            }
        }

        MachHwIdle();
    }

    /* Return the selected item */
    if (SelectedItem)
        *SelectedItem = MenuInfo->SelectedItem;
#endif
    return TRUE;
}

ULONG_PTR
UiDisplayMenu(
    _In_ PCSTR ItemList[],
    _In_ ULONG ItemCount,
    _In_ ULONG DefaultItem/*,
    _Out_ PULONG SelectedItem,
    _In_ BOOLEAN CanEscape*/)
{
    UI_MENU_INFO MenuInfo;

    /* Setup the MENU_INFO structure */
    MenuInfo.ItemList = ItemList;
    MenuInfo.ItemCount = ItemCount;
    MenuInfo.SelectedItem = DefaultItem;

    return UiDisplayMenuEx(&MenuInfo/*, SelectedItem, CanEscape*/);
}

VOID
UiDrawMenu(
    _In_ PUI_MENU_INFO MenuInfo)
{
    ULONG i;

    /* Draw the menu box */
    TuiDrawMenuBox(MenuInfo);

    /* Draw each line of the menu */
    for (i = 0; i < MenuInfo->ItemCount; ++i)
    {
        TuiDrawMenuItem(MenuInfo, i);
    }
}

BOOLEAN
UiProcessMenuKeyboardEvent(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ ULONG KeyEvent)
{
    ULONG Selected, Count;

    /* Process the key */
    if ((KeyEvent != KEY_UP  ) && (KeyEvent != KEY_DOWN) &&
        (KeyEvent != KEY_HOME) && (KeyEvent != KEY_END ))
    {
        return FALSE;
    }

    /* Get the current selected item and count */
    Selected = MenuInfo->SelectedItem;
    Count = MenuInfo->ItemCount - 1;

    /* Check the key and change the selected menu item */
    if ((KeyEvent == KEY_UP) && (Selected > 0))
    {
        /* Deselect previous item and go up */
        MenuInfo->SelectedItem--;
        TuiDrawMenuItem(MenuInfo, Selected);
        Selected--;

        /* Skip past any separators */
        if ((Selected > 0) && (MenuInfo->ItemList[Selected] == NULL))
        {
            MenuInfo->SelectedItem--;
        }
    }
    else if ( ((KeyEvent == KEY_UP) && (Selected == 0)) ||
               (KeyEvent == KEY_END) )
    {
        /* Go to the end */
        MenuInfo->SelectedItem = Count;
        TuiDrawMenuItem(MenuInfo, Selected);
    }
    else if ((KeyEvent == KEY_DOWN) && (Selected < Count))
    {
        /* Deselect previous item and go down */
        MenuInfo->SelectedItem++;
        TuiDrawMenuItem(MenuInfo, Selected);
        Selected++;

        /* Skip past any separators */
        if ((Selected < Count) && (MenuInfo->ItemList[Selected] == NULL))
        {
            MenuInfo->SelectedItem++;
        }
    }
    else if ( ((KeyEvent == KEY_DOWN) && (Selected == Count)) ||
               (KeyEvent == KEY_HOME) )
    {
        /* Go to the beginning */
        MenuInfo->SelectedItem = 0;
        TuiDrawMenuItem(MenuInfo, Selected);
    }

    /* Select the new item */
    TuiDrawMenuItem(MenuInfo, MenuInfo->SelectedItem);
    return TRUE;
}

/* EOF */
