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

static VOID
TuiDrawTimeoutString(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ PCHAR LineText,
    _In_ ULONG Length)
{
    /**
     * How to pad/fill:
     *
     *  Center  Box     What to do:
     *  0       0 or 1  Pad on the right with blanks.
     *  1       0       Pad on the left with blanks.
     *  1       1       Pad on the left with blanks + box bottom border.
     **/

    if (UiCenterMenu)
    {
        /* In boxed menu mode, pad on the left with blanks and box border;
         * otherwise, pad over all the box length until its right edge */
        TuiFillArea(0,
                    MenuInfo->Bottom,
                    UiMenuBox
                        ? MenuInfo->Left - 1 /* Left side of the box bottom */
                        : MenuInfo->Right,   /* Left side + all box length  */
                    MenuInfo->Bottom,
                    UiBackdropFillStyle,
                    ATTR(UiBackdropFgColor, UiBackdropBgColor));

        if (UiMenuBox)
        {
            /* Fill with box bottom border */
            TuiDrawBoxBottomLine(MenuInfo->Left,
                                 MenuInfo->Bottom,
                                 MenuInfo->Right,
                                 D_VERT,
                                 D_HORZ,
                                 ATTR(UiMenuFgColor, UiMenuBgColor));

            /* In centered boxed menu mode, the timeout string
             * does not go past the right border, in principle... */
        }

        if (Length > 0)
        {
            /* Display the timeout at the bottom-right part of the menu */
            UiDrawText(MenuInfo->Right - Length - 1,
                       MenuInfo->Bottom,
                       LineText,
                       ATTR(UiMenuFgColor, UiMenuBgColor));
        }
    }
    else
    {
        if (Length > 0)
        {
            /* Display the timeout under the menu directly */
            UiDrawText(0,
                       MenuInfo->Bottom + 4,
                       LineText,
                       ATTR(UiMenuFgColor, UiMenuBgColor));
        }

        /* Pad on the right with blanks, to erase
         * characters when the string length decreases */
        TuiFillArea(Length,
                    MenuInfo->Bottom + 4,
                    Length ? (Length + 1) : (UiScreenWidth - 1),
                    MenuInfo->Bottom + 4,
                    UiBackdropFillStyle,
                    ATTR(UiBackdropFgColor, UiBackdropBgColor));
    }
}

VOID
UiDrawTimeout(
    _In_ PUI_SCREEN_INFO ScreenInfo)
{
    ULONG Length;
    CHAR LineText[80];

    // FIXME: Since this function currently bases itself on the
    // existence of a menu in the screen to show the timeout,
    // just bail out if we don't have one.
    //
    // TODO: Make this independent on the existence of a menu in the future.
    if (!ScreenInfo->Menu)
        return;

    /* If there is a timeout, draw the time remaining */
    if (ScreenInfo->TimeOut >= 0)
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
                               ScreenInfo->TimeOut,
                               ptr + 2);
        }
        else
        {
            /* Copy the time text string, append a separating blank,
             * and add the remaining time. */
            RtlStringCbPrintfA(LineText, sizeof(LineText),
                               "%s %d",
                               UiTimeText,
                               ScreenInfo->TimeOut);
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
    TuiDrawTimeoutString(ScreenInfo->Menu, LineText, Length);
}

BOOLEAN
UiProcessMenuKeyboardEvent(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ ULONG KeyEvent);

/*static*/ VOID
TuiCalcMenuBoxSize(
    _Inout_ PUI_MENU_INFO MenuInfo);

ULONG_PTR
UiDisplayScreen(
    _In_opt_ PCSTR Header,
    _In_opt_ PCSTR Footer,
    _In_ BOOLEAN ShowBootOptions,
    _In_ PCSTR ItemList[],
    _In_ ULONG ItemCount,
    _In_ ULONG DefaultItem,
    _In_ LONG TimeOut,
    _Out_ PULONG SelectedItem,
    _In_ BOOLEAN CanEscape,
    _In_opt_ UiKeyPressFilterCallback KeyPressFilter,
    _In_opt_ PVOID Context)
{
    UI_SCREEN_INFO ScreenInfo;
    UI_MENU_INFO MenuInfo;
    ULONG_PTR Result = TRUE;
    ULONG KeyPress;
    ULONG LastClockSecond;
    ULONG CurrentClockSecond;

    /*
     * Before taking any default action if there is no timeout,
     * check whether the supplied key filter callback function
     * may handle a specific user keypress.
     */
    if (!TimeOut && KeyPressFilter && MachConsKbHit())
    {
        /* Get the key */
        KeyPress = MachConsGetCh();
        if (KeyPress == KEY_EXTENDED)
            KeyPress = MachConsGetCh();

        /* Call the supplied key filter callback function */
        KeyPressFilter(KeyPress, &Result/*, DefaultItem, Context*/);
        /* Fall back to the case below to either perform the
         * default action, or to return the handled command */
    }

    /* Check if there is no timeout */
    if (!TimeOut)
    {
        /* Return the default selected item */
        if (SelectedItem)
            *SelectedItem = DefaultItem;
        return Result;
    }

    /* Setup the UI_MENU_INFO structure */
    MenuInfo.ItemList = ItemList;
    MenuInfo.ItemCount = ItemCount;
    MenuInfo.SelectedItem = DefaultItem;
    // MenuInfo.Context = Context;

    /* Setup the UI_SCREEN_INFO structure */
    ScreenInfo.Header = Header;
    ScreenInfo.Footer = Footer;
    ScreenInfo.ShowBootOptions = ShowBootOptions;
    ScreenInfo.Menu = &MenuInfo;
    ScreenInfo.TimeOut = TimeOut;
    ScreenInfo.Context = Context;

    // FIXME: Theme-specific
    /* Calculate the size of the menu box */
    TuiCalcMenuBoxSize(&MenuInfo); // HACK: Investigate...
    // (void)UiDisplayMenuEx(&MenuInfo/*, SelectedItem, CanEscape*/);
    // UiDrawTimeout(&ScreenInfo);
    UiVtbl.DrawScreen(&ScreenInfo);

    VideoCopyOffScreenBufferToVRAM();

    /* Get the current second of time */
    LastClockSecond = ArcGetTime()->Second;

    /* Process keys */
    while (TRUE)
    {
        /* Check for a keypress */
        if (MachConsKbHit())
        {
            /* Get the key */
            KeyPress = MachConsGetCh();
            if (KeyPress == KEY_EXTENDED)
                KeyPress = MachConsGetCh();

            /* Check if the timeout is not already complete */
            if (ScreenInfo.TimeOut != -1)
            {
                /* Cancel and remove it */
                ScreenInfo.TimeOut = -1;
                UiDrawTimeout(&ScreenInfo);
            }

            /* Call the supplied key filter callback function
             * to see if it is going to handle this keypress */
            if (KeyPressFilter &&
                KeyPressFilter(KeyPress, &Result/*, MenuInfo.SelectedItem, ScreenInfo.Context*/))
            {
                /* It processed the key, so exit the loop and return
                 * both the selected item and the handled command */
                break;
            }

            /* Check for ENTER or ESC */
            if (KeyPress == KEY_ENTER) break;
            if (CanEscape && (KeyPress == KEY_ESC)) return FALSE;

            /* Process key presses for menu */
            if (UiProcessMenuKeyboardEvent(&MenuInfo, KeyPress))
            {
                /* Key handled, update the video buffer */
                VideoCopyOffScreenBufferToVRAM();
            }
        }

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
            if (ScreenInfo.TimeOut > 0)
            {
                ScreenInfo.TimeOut--;
                UiDrawTimeout(&ScreenInfo);
            }
            else if (ScreenInfo.TimeOut == 0)
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
    return Result;
}

#if 0
VOID
UiDrawScreen(
    _In_ PUI_SCREEN_INFO ScreenInfo)
{
}
#endif

/* EOF */
