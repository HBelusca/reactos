/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Text UI Menu Functions
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2005 Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2012-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

/* FUNCTIONS *****************************************************************/

/*static*/ VOID
TuiCalcMenuBoxSize(
    _Inout_ PUI_MENU_INFO MenuInfo)
{
    ULONG i;
    ULONG Width = 0;
    ULONG Height;
    ULONG Length;

    /* Height is the menu item count plus 2 (top border & bottom border) */
    Height = MenuInfo->ItemCount + 2;
    Height -= 1; // Height is zero-based

    /* Loop every item */
    for (i = 0; i < MenuInfo->ItemCount; ++i)
    {
        /* Get the string length and make it become the new width if necessary */
        if (MenuInfo->ItemList[i])
        {
            Length = (ULONG)strlen(MenuInfo->ItemList[i]);
            if (Length > Width) Width = Length;
        }
    }

    /* Allow room for left & right borders, plus 8 spaces on each side */
    Width += 18;

    /* Check if we're drawing a centered menu */
    if (UiCenterMenu)
    {
        /* Calculate the menu box area for a centered menu */
        MenuInfo->Left = (UiScreenWidth - Width) / 2;
        MenuInfo->Top = (((UiScreenHeight - TUI_TITLE_BOX_CHAR_HEIGHT) -
                          Height) / 2) + TUI_TITLE_BOX_CHAR_HEIGHT;
    }
    else
    {
        /* Put the menu in the default left-corner position */
        MenuInfo->Left = -1;
        MenuInfo->Top = 4;
    }

    /* The other margins are the same */
    MenuInfo->Right = MenuInfo->Left + Width;
    MenuInfo->Bottom = MenuInfo->Top + Height;
}

VOID
TuiDrawMenuBox(
    _In_ PUI_MENU_INFO MenuInfo)
{
    // FIXME: Theme-specific
    /* Draw the menu box if requested */
    if (UiMenuBox)
    {
        UiDrawBox(MenuInfo->Left,
                  MenuInfo->Top,
                  MenuInfo->Right,
                  MenuInfo->Bottom,
                  D_VERT,
                  D_HORZ,
                  FALSE,    // Filled
                  TRUE,     // Shadow
                  ATTR(UiMenuFgColor, UiMenuBgColor));
    }
}

VOID
TuiDrawMenuItem(
    _In_ PUI_MENU_INFO MenuInfo,
    _In_ ULONG ItemIndex)
{
    ULONG SpaceLeft;
    ULONG SpaceRight;
    UCHAR Attribute;
    CHAR MenuLineText[80];

    /* If this is a separator */
    if (MenuInfo->ItemList[ItemIndex] == NULL)
    {
        // FIXME: Theme-specific
        /* Draw its left box corner */
        if (UiMenuBox)
        {
            UiDrawText(MenuInfo->Left,
                       MenuInfo->Top + 1 + ItemIndex,
                       "\xC7",
                       ATTR(UiMenuFgColor, UiMenuBgColor));
        }

        /* Make it a separator line and use menu colors */
        RtlZeroMemory(MenuLineText, sizeof(MenuLineText));
        RtlFillMemory(MenuLineText,
                      min(sizeof(MenuLineText), (MenuInfo->Right - MenuInfo->Left - 1)),
                      0xC4);

        /* Draw the item */
        UiDrawText(MenuInfo->Left + 1,
                   MenuInfo->Top + 1 + ItemIndex,
                   MenuLineText,
                   ATTR(UiMenuFgColor, UiMenuBgColor));

        // FIXME: Theme-specific
        /* Draw its right box corner */
        if (UiMenuBox)
        {
            UiDrawText(MenuInfo->Right,
                       MenuInfo->Top + 1 + ItemIndex,
                       "\xB6",
                       ATTR(UiMenuFgColor, UiMenuBgColor));
        }

        /* We are done */
        return;
    }

    /* This is not a separator */
    ASSERT(MenuInfo->ItemList[ItemIndex]);

    /* Check if using centered menu */
    if (UiCenterMenu)
    {
        /*
         * We will want the string centered so calculate
         * how many spaces will be to the left and right.
         */
        ULONG SpaceTotal =
            (MenuInfo->Right - MenuInfo->Left - 2) -
            (ULONG)strlen(MenuInfo->ItemList[ItemIndex]);
        SpaceLeft  = (SpaceTotal / 2) + 1;
        SpaceRight = (SpaceTotal - SpaceLeft) + 1;
    }
    else
    {
        /* Simply left-align it */
        SpaceLeft  = 4;
        SpaceRight = 0;
    }

    /* Format the item text string */
    RtlStringCbPrintfA(MenuLineText, sizeof(MenuLineText),
                       "%*s%s%*s",
                       SpaceLeft, "",   // Left padding
                       MenuInfo->ItemList[ItemIndex],
                       SpaceRight, ""); // Right padding

    if (ItemIndex == MenuInfo->SelectedItem)
    {
        /* If this is the selected item, use the selected colors */
        Attribute = ATTR(UiSelectedTextColor, UiSelectedTextBgColor);
    }
    else
    {
        /* Normal item colors */
        Attribute = ATTR(UiTextColor, UiMenuBgColor);
    }

    /* Draw the item */
    UiDrawText(MenuInfo->Left + 1,
               MenuInfo->Top + 1 + ItemIndex,
               MenuLineText,
               Attribute);
}

/* EOF */
