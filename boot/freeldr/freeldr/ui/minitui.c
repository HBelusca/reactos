/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     "Mini" Text UI interface
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2007 Hervé Poussineau <hpoussin@reactos.org>
 *              Copyright 2022-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <freeldr.h>

/* NTLDR or Vista+ BOOTMGR progress-bar style */
// #define NTLDR_PROGRESSBAR
// #define BTMGR_PROGRESSBAR /* Default style */

static BOOLEAN
MiniTuiInitialize(VOID)
{
    /* Initialize full TUI */
    if (!FullTuiInitialize())
        return FALSE;

    /* Override default settings with "Mini" TUI Theme */

    UiTextColor = TuiTextToColor("Default");

    UiStatusBarFgColor    = UiTextColor;
    UiStatusBarBgColor    = COLOR_BLACK;
    UiBackdropFgColor     = UiTextColor;
    UiBackdropBgColor     = COLOR_BLACK;
    UiBackdropFillStyle   = ' '; // TuiTextToFillStyle("None");
    UiTitleBoxFgColor     = COLOR_WHITE;
    UiTitleBoxBgColor     = COLOR_BLACK;
    // UiMessageBoxFgColor   = COLOR_WHITE;
    // UiMessageBoxBgColor   = COLOR_BLUE;
    UiMenuFgColor         = UiTextColor;
    UiMenuBgColor         = COLOR_BLACK;
    UiSelectedTextColor   = COLOR_BLACK;
    UiSelectedTextBgColor = UiTextColor;
    // UiEditBoxTextColor    = COLOR_WHITE;
    // UiEditBoxBgColor      = COLOR_BLACK;

    UiShowTime          = FALSE;
    UiMenuBox           = FALSE;
    UiCenterMenu        = FALSE;
    UiUseSpecialEffects = FALSE;

    // TODO: Have a boolean to show/hide title box?
    UiTitleBoxTitleText[0] = ANSI_NULL;

    RtlStringCbCopyA(UiTimeText, sizeof(UiTimeText),
                     "Seconds until highlighted choice will be started automatically:");

    return TRUE;
}

static VOID
MiniTuiDrawBackdrop(VOID)
{
    /* Fill in a black background */
    TuiFillArea(0, 0, UiScreenWidth - 1, UiScreenHeight - 1,
                UiBackdropFillStyle,
                ATTR(UiBackdropFgColor, UiBackdropBgColor));
}

static VOID
MiniTuiDrawStatusText(PCSTR StatusText)
{
    /* Minimal UI doesn't have a status bar */
}

static VOID
MiniTuiSetProgressBarText(
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
                UiProgressBar.Right,
#ifdef NTLDR_PROGRESSBAR
                UiProgressBar.Bottom - 1,
#else // BTMGR_PROGRESSBAR
                UiProgressBar.Bottom - 2, // One empty line between text and bar.
#endif
                ' ', ATTR(UiTextColor, UiMenuBgColor));

    /* Draw the "Loading..." text */
    TuiDrawCenteredText(UiProgressBar.Left, UiProgressBar.Top,
                        UiProgressBar.Right,
#ifdef NTLDR_PROGRESSBAR
                        UiProgressBar.Bottom - 1,
#else // BTMGR_PROGRESSBAR
                        UiProgressBar.Bottom - 2, // One empty line between text and bar.
#endif
                        ProgressString, ATTR(UiTextColor, UiMenuBgColor));
}

static VOID
MiniTuiTickProgressBar(
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
    /* Fill the remaining with blanks */
    TuiFillArea(UiProgressBar.Left + FillCount, UiProgressBar.Bottom,
                UiProgressBar.Right, UiProgressBar.Bottom,
                ' ', ATTR(UiTextColor, UiMenuBgColor));

    TuiUpdateDateTime();
    VideoCopyOffScreenBufferToVRAM();
}

static VOID
MiniTuiDrawProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText);

static VOID
MiniTuiDrawProgressBarCenter(
    _In_ PCSTR ProgressText)
{
    ULONG Left, Top, Right, Bottom, Width, Height;

    /* Build the coordinates and sizes */
#ifdef NTLDR_PROGRESSBAR
    Height = 2;
    Width  = UiScreenWidth;
    Left = 0;
    Top  = UiScreenHeight - Height - 2;
#else // BTMGR_PROGRESSBAR
    Height = 3;
    Width  = UiScreenWidth - 4;
    Left = 2;
    Top  = UiScreenHeight - Height - 3;
#endif
    Right  = Left + Width - 1;
    Bottom = Top + Height - 1;

    /* Draw the progress bar */
    MiniTuiDrawProgressBar(Left, Top, Right, Bottom, ProgressText);
}

static VOID
MiniTuiDrawProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText)
{
    UiInitProgressBar(Left, Top, Right, Bottom, ProgressText);
}

#if 0
static VOID
MiniTuiDrawMenu(
    _In_ PUI_MENU_INFO MenuInfo)
{
}
#endif

static VOID
MiniTuiDrawScreen(
    _In_ PUI_SCREEN_INFO ScreenInfo)
{
    PUI_MENU_INFO MenuInfo = ScreenInfo->Menu;

    /* Draw the backdrop */
    UiDrawBackdrop();

    /* No GUI status bar text, just minimal text. Show the screen header. */
    if (ScreenInfo->Header)
    {
        UiVtbl.DrawText(0,
                        MenuInfo ? MenuInfo->Top - 2 : 2,
                        ScreenInfo->Header,
                        ATTR(UiMenuFgColor, UiMenuBgColor));
    }

    if (MenuInfo)
    {
        UiDrawMenu(MenuInfo);

        /* Now tell the user how to choose */
        UiVtbl.DrawText(0,
                        MenuInfo->Bottom + 1,
                        "Use \x18 and \x19 to move the highlight to your choice.",
                        ATTR(UiMenuFgColor, UiMenuBgColor));
        UiVtbl.DrawText(0,
                        MenuInfo->Bottom + 2,
                        "Press ENTER to choose.",
                        ATTR(UiMenuFgColor, UiMenuBgColor));
    }

    /* And show the screen footer */
    if (ScreenInfo->Footer)
    {
        UiVtbl.DrawText(0,
                        UiScreenHeight - 4,
                        ScreenInfo->Footer,
                        ATTR(UiMenuFgColor, UiMenuBgColor));
    }

    UiDrawTimeout(ScreenInfo);
    TuiUpdateDateTime();

    /* Display the boot options if needed */
    if (ScreenInfo->ShowBootOptions)
        DisplayBootTimeOptions();
}

const UIVTBL MiniTuiVtbl =
{
    MiniTuiInitialize,
    FullTuiUnInitialize,
    MiniTuiDrawBackdrop,
    TuiFillArea,
    TuiDrawShadow,
    TuiDrawBox,
    TuiDrawText,
    TuiDrawText2,
    TuiDrawCenteredText,
    MiniTuiDrawStatusText,
    TuiUpdateDateTime,
    TuiMessageBox,
    TuiMessageBoxCritical,
    MiniTuiDrawProgressBarCenter,
    MiniTuiDrawProgressBar,
    MiniTuiSetProgressBarText,
    MiniTuiTickProgressBar,
    TuiEditBox,
    TuiTextToColor,
    TuiTextToFillStyle,
    MiniTuiDrawBackdrop, /* no FadeIn */
    TuiFadeOut,
    // MiniTuiDrawMenu,
    MiniTuiDrawScreen
};

/* EOF */
