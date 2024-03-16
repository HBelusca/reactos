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

VOID
TuiDrawTimeoutString(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ PCHAR LineText,
    _In_ ULONG Length);

VOID
UiDrawMenuTimeout(
    _In_ PUI_MENU_INFO MenuInfo)
{
    ULONG Length;
    CHAR LineText[80];

    /* If there is a timeout, draw the time remaining */
    if (MenuInfo->TimeOut >= 0)
    {
        /* Find whether the time text string is escaped
         * with %d for specific countdown insertion. */
        PCHAR ptr = UiTimeText;
        while ((ptr = strchr(ptr, '%')) && (ptr[1] != 'd'))
        {
            /* Ignore any following character (including a following
             * '%' that would be escaped), thus skip two characters.
             * If this is the last character, ignore it and stop. */
            if (*++ptr)
                ++ptr;
        }
        ASSERT(!ptr || (ptr[0] == '%' && ptr[1] == 'd'));

        if (ptr)
        {
            /* Copy the time text string up to the '%d' insertion point and
             * skip it, add the remaining time and the rest of the string. */
            RtlStringCbPrintfA(LineText, sizeof(LineText),
                               "%.*s%d%s",
                               ptr - UiTimeText, UiTimeText,
                               MenuInfo->TimeOut,
                               ptr + 2);
        }
        else
        {
            /* Copy the time text string, append a separating blank,
             * and add the remaining time. */
            RtlStringCbPrintfA(LineText, sizeof(LineText),
                               "%s %d",
                               UiTimeText,
                               MenuInfo->TimeOut);
        }

        Length = (ULONG)strlen(LineText);
    }
    else
    {
        /* Erase the timeout with blanks */
        Length = 0;
    }

    // FIXME: Theme-specific
    /* Now do the UI-specific drawing */
    TuiDrawTimeoutString(MenuInfo, LineText, Length);
}

static ULONG
UiProcessMenuKeyboardEvent(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ UiMenuKeyPressFilterCallback KeyPressFilter);

BOOLEAN
UiDisplayMenu(
    IN PCSTR Header,
    IN PCSTR Footer OPTIONAL,
    IN BOOLEAN ShowBootOptions,
    IN PCSTR ItemList[],
    IN ULONG ItemCount,
    IN ULONG DefaultItem,
    IN LONG TimeOut,
    OUT PULONG SelectedItem,
    IN BOOLEAN CanEscape,
    IN UiMenuKeyPressFilterCallback KeyPressFilter OPTIONAL,
    IN PVOID Context OPTIONAL)
{
    UI_MENU_INFO MenuInfo;
    ULONG LastClockSecond;
    ULONG CurrentClockSecond;
    ULONG KeyPress;

    /*
     * Before taking any default action if there is no timeout,
     * check whether the supplied key filter callback function
     * may handle a specific user keypress. If it does, the
     * timeout is cancelled.
     */
    if (!TimeOut && KeyPressFilter && MachConsKbHit())
    {
        /* Get the key (get the extended key if needed) */
        KeyPress = MachConsGetCh();
        if (KeyPress == KEY_EXTENDED)
            KeyPress = MachConsGetCh();

        /*
         * Call the supplied key filter callback function to see
         * if it is going to handle this keypress.
         */
        if (KeyPressFilter(KeyPress, DefaultItem, Context))
        {
            /* It processed the key character, cancel the timeout */
            TimeOut = -1;
        }
    }

    /* Check if there is no timeout */
    if (!TimeOut)
    {
        /* Return the default selected item */
        if (SelectedItem)
            *SelectedItem = DefaultItem;
        return TRUE;
    }

    /* Setup the MENU_INFO structure */
    MenuInfo.Header = Header;
    MenuInfo.Footer = Footer;
    MenuInfo.ShowBootOptions = ShowBootOptions;
    MenuInfo.ItemList = ItemList;
    MenuInfo.ItemCount = ItemCount;
    MenuInfo.TimeOut = TimeOut;
    MenuInfo.SelectedItem = DefaultItem;
    MenuInfo.Context = Context;

    // FIXME: Theme-specific
    /* Calculate the size of the menu box */
    TuiCalcMenuBoxSize(&MenuInfo);

    /* Draw the menu */
    UiVtbl.DrawMenu(&MenuInfo);

    /* Get the current second of time */
    LastClockSecond = ArcGetTime()->Second;

    /* Process keys */
    while (TRUE)
    {
        /* Process key presses */
        KeyPress = UiProcessMenuKeyboardEvent(&MenuInfo, KeyPressFilter);

        /* Check for ENTER or ESC */
        if (KeyPress == KEY_ENTER) break;
        if (CanEscape && KeyPress == KEY_ESC) return FALSE;

        /* Get the updated time, and check if more than a second has elapsed */
        CurrentClockSecond = ArcGetTime()->Second;
        if (CurrentClockSecond != LastClockSecond)
        {
            /* Update the time information */
            LastClockSecond = CurrentClockSecond;

            // FIXME: Theme-specific
            /* Update the date & time */
            TuiUpdateDateTime();

            /* If there is a countdown, update it */
            if (MenuInfo.TimeOut > 0)
            {
                MenuInfo.TimeOut--;
                UiDrawMenuTimeout(&MenuInfo);
            }
            else if (MenuInfo.TimeOut == 0)
            {
                /* A timeout occurred, exit this loop and return selection */
                VideoCopyOffScreenBufferToVRAM();
                break;
            }
            VideoCopyOffScreenBufferToVRAM();
        }

        MachHwIdle();
    }

    /* Return the selected item */
    if (SelectedItem)
        *SelectedItem = MenuInfo.SelectedItem;
    return TRUE;
}

// NOTE: Used as a helper by *TuiDrawMenu
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

static ULONG
UiProcessMenuKeyboardEvent(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ UiMenuKeyPressFilterCallback KeyPressFilter)
{
    ULONG KeyEvent = 0;
    ULONG Selected, Count;

    /* Check for a keypress */
    if (!MachConsKbHit())
        return 0; // None, bail out

    /* Check if the timeout is not already complete */
    if (MenuInfo->TimeOut != -1)
    {
        /* Cancel it and remove it */
        MenuInfo->TimeOut = -1;
        UiDrawMenuTimeout(MenuInfo);
    }

    /* Get the key (get the extended key if needed) */
    KeyEvent = MachConsGetCh();
    if (KeyEvent == KEY_EXTENDED)
        KeyEvent = MachConsGetCh();

    /*
     * Call the supplied key filter callback function to see
     * if it is going to handle this keypress.
     */
    if (KeyPressFilter &&
        KeyPressFilter(KeyEvent, MenuInfo->SelectedItem, MenuInfo->Context))
    {
        /* It processed the key character, so redraw and exit */
        UiVtbl.DrawMenu(MenuInfo);
        return 0;
    }

    /* Process the key */
    if ((KeyEvent == KEY_UP  ) || (KeyEvent == KEY_DOWN) ||
        (KeyEvent == KEY_HOME) || (KeyEvent == KEY_END ))
    {
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
            if ((Selected > 0) &&
                (MenuInfo->ItemList[Selected] == NULL))
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
            if ((Selected < Count) &&
                (MenuInfo->ItemList[Selected] == NULL))
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

        /* Select new item and update video buffer */
        TuiDrawMenuItem(MenuInfo, MenuInfo->SelectedItem);
        VideoCopyOffScreenBufferToVRAM();
    }

    /* Return the pressed key */
    return KeyEvent;
}

/* EOF */
