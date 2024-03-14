/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     "Full" Text UI interface
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2022-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

#define TAG_TUI_PALETTE 'PiuT'

/* FUNCTIONS *****************************************************************/

/*static*/ BOOLEAN
FullTuiInitialize(VOID)
{
    /* Initialize main TUI */
    if (!TuiInitialize())
        return FALSE;

    MachVideoHideShowTextCursor(FALSE);
    MachVideoSetTextCursorPosition(0, 0);
    MachVideoClearScreen(ATTR(COLOR_GRAY, COLOR_BLACK));

    /* Load default settings with "Full" TUI Theme */

    UiStatusBarFgColor    = COLOR_BLACK;
    UiStatusBarBgColor    = COLOR_CYAN;
    UiBackdropFgColor     = COLOR_WHITE;
    UiBackdropBgColor     = COLOR_BLUE;
    UiBackdropFillStyle   = MEDIUM_FILL;
    UiTitleBoxFgColor     = COLOR_WHITE;
    UiTitleBoxBgColor     = COLOR_RED;
    UiMessageBoxFgColor   = COLOR_WHITE;
    UiMessageBoxBgColor   = COLOR_BLUE;
    UiMenuFgColor         = COLOR_WHITE;
    UiMenuBgColor         = COLOR_BLUE;
    UiTextColor           = COLOR_YELLOW;
    UiSelectedTextColor   = COLOR_BLACK;
    UiSelectedTextBgColor = COLOR_GRAY;
    UiEditBoxTextColor    = COLOR_WHITE;
    UiEditBoxBgColor      = COLOR_BLACK;

    UiShowTime          = TRUE;
    UiMenuBox           = TRUE;
    UiCenterMenu        = TRUE;
    UiUseSpecialEffects = FALSE;

    // TODO: Have a boolean to show/hide title box?
    RtlStringCbCopyA(UiTitleBoxTitleText, sizeof(UiTitleBoxTitleText),
                     "Boot Menu");

    RtlStringCbCopyA(UiTimeText, sizeof(UiTimeText),
                     "[Time Remaining: %d]");

    return TRUE;
}

/*static*/ VOID
FullTuiUnInitialize(VOID)
{
    if (UiUseSpecialEffects)
        TuiFadeOut();
    else
        MachVideoSetDisplayMode(NULL, FALSE);

    MachVideoClearScreen(ATTR(COLOR_GRAY, COLOR_BLACK));
    MachVideoSetTextCursorPosition(0, 0);
    MachVideoHideShowTextCursor(TRUE);

    /* Terminate main TUI */
    TuiUnInitialize();
}

static VOID
FullTuiDrawBackdrop(VOID)
{
    /* Fill in the background (excluding title box & status bar) */
    TuiFillArea(0,
                TUI_TITLE_BOX_CHAR_HEIGHT,
                UiScreenWidth - 1,
                UiScreenHeight - 2,
                UiBackdropFillStyle,
                ATTR(UiBackdropFgColor, UiBackdropBgColor));

    /* Draw the title box */
    TuiDrawBox(0,
               0,
               UiScreenWidth - 1,
               TUI_TITLE_BOX_CHAR_HEIGHT - 1,
               D_VERT,
               D_HORZ,
               TRUE,
               FALSE,
               ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));

    /* Draw version text */
    TuiDrawText(2,
                1,
                FrLdrVersionString,
                ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));

    /* Draw copyright */
    TuiDrawText(2,
                2,
                BY_AUTHOR,
                ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));
    TuiDrawText(2,
                3,
                AUTHOR_EMAIL,
                ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));

    /* Draw help text */
    TuiDrawText(UiScreenWidth - 16, 3,
                /*"F1 for Help"*/ "F8 for Options",
                ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));

    /* Draw title text */
    TuiDrawText((UiScreenWidth - (ULONG)strlen(UiTitleBoxTitleText)) / 2,
                2,
                UiTitleBoxTitleText,
                ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));

    /* Update the date & time */
    TuiUpdateDateTime();
    VideoCopyOffScreenBufferToVRAM();
}

static VOID
FullTuiDrawStatusText(PCSTR StatusText)
{
    SIZE_T    i;

    TuiDrawText(0, UiScreenHeight-1, " ", ATTR(UiStatusBarFgColor, UiStatusBarBgColor));
    TuiDrawText(1, UiScreenHeight-1, StatusText, ATTR(UiStatusBarFgColor, UiStatusBarBgColor));

    for (i=strlen(StatusText)+1; i<UiScreenWidth; i++)
    {
        TuiDrawText((ULONG)i, UiScreenHeight-1, " ", ATTR(UiStatusBarFgColor, UiStatusBarBgColor));
    }

    VideoCopyOffScreenBufferToVRAM();
}

VOID TuiUpdateDateTime(VOID)
{
    TIMEINFO* TimeInfo;
    PCSTR   DayPostfix;
    BOOLEAN PMHour = FALSE;
    CHAR Buffer[40];

    /* Don't draw the time if this has been disabled */
    if (!UiShowTime) return;

    TimeInfo = ArcGetTime();
    if (TimeInfo->Year < 1 || 9999 < TimeInfo->Year ||
        TimeInfo->Month < 1 || 12 < TimeInfo->Month ||
        TimeInfo->Day < 1 || 31 < TimeInfo->Day ||
        23 < TimeInfo->Hour ||
        59 < TimeInfo->Minute ||
        59 < TimeInfo->Second)
    {
        /* This happens on QEmu sometimes. We just skip updating. */
        return;
    }

    /* Get the day postfix */
    if (1 == TimeInfo->Day || 21 == TimeInfo->Day || 31 == TimeInfo->Day)
        DayPostfix = "st";
    else if (2 == TimeInfo->Day || 22 == TimeInfo->Day)
        DayPostfix = "nd";
    else if (3 == TimeInfo->Day || 23 == TimeInfo->Day)
        DayPostfix = "rd";
    else
        DayPostfix = "th";

    /* Build the date string in format: "MMMM dx yyyy" */
    RtlStringCbPrintfA(Buffer, sizeof(Buffer),
                       "%s %d%s %d",
                       UiMonthNames[TimeInfo->Month - 1],
                       TimeInfo->Day,
                       DayPostfix,
                       TimeInfo->Year);

    /* Draw the date */
    TuiDrawText(UiScreenWidth - (ULONG)strlen(Buffer) - 2, 1,
                Buffer, ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));

    /* Get the hour and change from 24-hour mode to 12-hour */
    if (TimeInfo->Hour > 12)
    {
        TimeInfo->Hour -= 12;
        PMHour = TRUE;
    }
    if (TimeInfo->Hour == 0)
    {
        TimeInfo->Hour = 12;
    }

    /* Build the time string in format: "h:mm:ss tt" */
    RtlStringCbPrintfA(Buffer, sizeof(Buffer),
                       "    %d:%02d:%02d %s",
                       TimeInfo->Hour,
                       TimeInfo->Minute,
                       TimeInfo->Second,
                       PMHour ? "PM" : "AM");

    /* Draw the time */
    TuiDrawText(UiScreenWidth - (ULONG)strlen(Buffer) - 2, 2,
                Buffer, ATTR(UiTitleBoxFgColor, UiTitleBoxBgColor));
}

static VOID
FullTuiSetProgressBarText(
    _In_ PCSTR ProgressText)
{
    ULONG ProgressBarWidth;
    CHAR ProgressString[256];

    /* Make sure the progress bar is enabled */
    ASSERT(UiProgressBar.Show);

    /* Calculate the width of the bar proper */
    ProgressBarWidth = UiProgressBar.Right - UiProgressBar.Left + 1;

    /* First make sure the progress bar text fits */
    RtlStringCbCopyA(ProgressString, sizeof(ProgressString), ProgressText);
    TuiTruncateStringEllipsis(ProgressString, ProgressBarWidth);

    /* Clear the text area */
    TuiFillArea(UiProgressBar.Left, UiProgressBar.Top,
                UiProgressBar.Right, UiProgressBar.Bottom - 1,
                ' ', ATTR(UiTextColor, UiMenuBgColor));

    /* Draw the "Loading..." text */
    TuiDrawCenteredText(UiProgressBar.Left, UiProgressBar.Top,
                        UiProgressBar.Right, UiProgressBar.Bottom - 1,
                        ProgressString, ATTR(UiTextColor, UiMenuBgColor));
}

static VOID
TuiTickProgressBar(
    _In_ ULONG SubPercentTimes100)
{
    ULONG ProgressBarWidth;
    ULONG FillCount;

    /* Make sure the progress bar is enabled */
    ASSERT(UiProgressBar.Show);

    ASSERT(SubPercentTimes100 <= (100 * 100));

    /* Calculate the width of the bar proper */
    ProgressBarWidth = UiProgressBar.Right - UiProgressBar.Left + 1;

    /* Compute fill count */
    // FillCount = (ProgressBarWidth * Position) / Range;
    FillCount = ProgressBarWidth * SubPercentTimes100 / (100 * 100);

    /* Fill the progress bar */
    /* Draw the percent complete -- Use the fill character */
    if (FillCount > 0)
    {
        TuiFillArea(UiProgressBar.Left, UiProgressBar.Bottom,
                    UiProgressBar.Left + FillCount - 1, UiProgressBar.Bottom,
                    '\xDB', ATTR(UiTextColor, UiMenuBgColor));
    }
    /* Fill the remaining with shadow blanks */
    TuiFillArea(UiProgressBar.Left + FillCount, UiProgressBar.Bottom,
                UiProgressBar.Right, UiProgressBar.Bottom,
                '\xB2', ATTR(UiTextColor, UiMenuBgColor));

    TuiUpdateDateTime();
    VideoCopyOffScreenBufferToVRAM();
}

static VOID
TuiDrawProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText);

static VOID
TuiDrawProgressBarCenter(
    _In_ PCSTR ProgressText)
{
    ULONG Left, Top, Right, Bottom, Width, Height;

    /* Build the coordinates and sizes */
    Height = 2;
    Width  = 50; // Allow for 50 "bars"
    Left = (UiScreenWidth - Width) / 2;
    Top  = (UiScreenHeight - Height + 4) / 2;
    Right  = Left + Width - 1;
    Bottom = Top + Height - 1;

    /* Inflate to include the box margins */
    Left -= 2;
    Right += 2;
    Top -= 1;
    Bottom += 1;

    /* Draw the progress bar */
    TuiDrawProgressBar(Left, Top, Right, Bottom, ProgressText);
}

static VOID
TuiDrawProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText)
{
    /* Draw the box */
    TuiDrawBox(Left, Top, Right, Bottom,
               VERT, HORZ, TRUE, TRUE,
               ATTR(UiMenuFgColor, UiMenuBgColor));

    /* Exclude the box margins */
    Left += 2;
    Right -= 2;
    Top += 1;
    Bottom -= 1;

    UiInitProgressBar(Left, Top, Right, Bottom, ProgressText);
}

static VOID
FullTuiFadeInBackdrop(VOID)
{
    PPALETTE_ENTRY TuiFadePalette = NULL;

    if (UiUseSpecialEffects && ! MachVideoIsPaletteFixed())
    {
        TuiFadePalette = (PPALETTE_ENTRY)FrLdrTempAlloc(sizeof(PALETTE_ENTRY) * 64,
                                                        TAG_TUI_PALETTE);

        if (TuiFadePalette != NULL)
        {
            VideoSavePaletteState(TuiFadePalette, 64);
            VideoSetAllColorsToBlack(64);
        }
    }

    // Draw the backdrop and title box
    FullTuiDrawBackdrop();

    if (UiUseSpecialEffects && ! MachVideoIsPaletteFixed() && TuiFadePalette != NULL)
    {
        VideoFadeIn(TuiFadePalette, 64);
        FrLdrTempFree(TuiFadePalette, TAG_TUI_PALETTE);
    }
}

VOID TuiFadeOut(VOID)
{
    PPALETTE_ENTRY TuiFadePalette = NULL;

    if (UiUseSpecialEffects && ! MachVideoIsPaletteFixed())
    {
        TuiFadePalette = (PPALETTE_ENTRY)FrLdrTempAlloc(sizeof(PALETTE_ENTRY) * 64,
                                                        TAG_TUI_PALETTE);

        if (TuiFadePalette != NULL)
        {
            VideoSavePaletteState(TuiFadePalette, 64);
        }
    }

    if (UiUseSpecialEffects && ! MachVideoIsPaletteFixed() && TuiFadePalette != NULL)
    {
        VideoFadeOut(64);
    }

    MachVideoSetDisplayMode(NULL, FALSE);

    if (UiUseSpecialEffects && ! MachVideoIsPaletteFixed() && TuiFadePalette != NULL)
    {
        VideoRestorePaletteState(TuiFadePalette, 64);
        FrLdrTempFree(TuiFadePalette, TAG_TUI_PALETTE);
    }

}

static VOID
FullTuiDrawMenu(
    _In_ PUI_MENU_INFO MenuInfo)
{
    ULONG i;

    // FIXME: Theme-specific
    /* Draw the backdrop */
    UiDrawBackdrop();

    /* Draw the menu box */
    TuiDrawMenuBox(MenuInfo);

    /* Draw each line of the menu */
    for (i = 0; i < MenuInfo->MenuItemCount; ++i)
    {
        TuiDrawMenuItem(MenuInfo, i);
    }

    // FIXME: Theme-specific
    /* Update the status bar */
    UiVtbl.DrawStatusText("Use \x18 and \x19 to select, then press ENTER.");

    /* Display the boot options if needed */
    if (MenuInfo->ShowBootOptions)
    {
        DisplayBootTimeOptions();
    }

    VideoCopyOffScreenBufferToVRAM();
}

const UIVTBL TuiVtbl =
{
    FullTuiInitialize,
    FullTuiUnInitialize,
    FullTuiDrawBackdrop,
    TuiFillArea,
    TuiDrawShadow,
    TuiDrawBox,
    TuiDrawText,
    TuiDrawText2,
    TuiDrawCenteredText,
    FullTuiDrawStatusText,
    TuiUpdateDateTime,
    TuiMessageBox,
    TuiMessageBoxCritical,
    TuiDrawProgressBarCenter,
    TuiDrawProgressBar,
    FullTuiSetProgressBarText,
    TuiTickProgressBar,
    TuiEditBox,
    TuiTextToColor,
    TuiTextToFillStyle,
    FullTuiFadeInBackdrop,
    TuiFadeOut,
    TuiDisplayMenu,
    FullTuiDrawMenu,
};

/* EOF */
