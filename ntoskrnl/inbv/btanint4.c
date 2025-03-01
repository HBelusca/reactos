/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation - "ReactOS NT4"
 * COPYRIGHT:   Copyright 2024-2025 Hermès Bélusca-Maïto
 */

/* INCLUDES ******************************************************************/

#include <debug.h>

/* GLOBALS *******************************************************************/

extern BOOLEAN ShowProgressBar;

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
BootAnimInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(Count);

#if 0
    /* Quit if we're already installed */
    if (InbvBootDriverInstalled) return TRUE;

    /* Set the progress bar ranges */
    InbvSetProgressBarSubset(0, 100);
#endif

    /* Return install state */
    return TRUE;
}

static VOID
(NTAPI *pTickProgressBar)(
    _In_ ULONG SubPercentTimes100) = NULL;

/**
 * @brief
 * Ticks the progress bar. Used by InbvUpdateProgressBar() and related.
 *
 * @param[in]   SubPercentTimes100
 * The progress percentage, scaled up by 100.
 *
 * @return None.
 **/
VOID
// BootAnimTickProgressBar
BootThemeTickProgressBar(
    _In_ ULONG SubPercentTimes100)
{
    // static BOOLEAN FirstCall = TRUE;

    /* Make sure the progress bar is enabled, that we own and are installed */
    ASSERT(ShowProgressBar &&
           InbvBootDriverInstalled &&
           (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED));

    ASSERT(SubPercentTimes100 <= (100 * 100));

#if 0
    /* If this is the first progress bar call, install a filter to
     * detect progress bar interruption by strings being displayed */
    if (FirstCall)
    {
        InbvInstallDisplayStringFilter(ProgressDisplayFilter);
        FirstCall = FALSE;
    }
#endif

    /* Fill the progress bar */
    if (pTickProgressBar)
        pTickProgressBar(SubPercentTimes100);
}


/**
 * @brief
 * Detects whether a string gets displayed while the progress bar is enabled
 * and ticking. If so, disables the progress bar and makes some space, before
 * displaying the string.
 **/
static VOID
NTAPI
ProgressDisplayFilter(
    _Inout_ PCHAR* String)
{
    /* Stop showing the progress bar if neither "." nor "\r" are given */
    if (!**String || (**String != '.' && **String != '\r') || *(*String+1))
    {
        /* Remove the filter */
        InbvInstallDisplayStringFilter(NULL);

        /* Stop the progress bar and emit a new-line */
        ShowProgressBar = FALSE;
        InbvDisplayString("\r\n");
    }
}

static VOID
NTAPI
BootTickProgressBar(
    _In_ ULONG SubPercentTimes100)
{
    static const ULONG NumberOfCols = (SCREEN_WIDTH / 8 /*BOOTCHAR_WIDTH*/);
    static BOOLEAN FirstCall = TRUE;
    ULONG FillCount, i;

    /* If this is the first progress bar call, install a filter to
     * detect progress bar interruption by strings being displayed */
    if (FirstCall)
    {
        InbvInstallDisplayStringFilter(ProgressDisplayFilter);
        FirstCall = FALSE;
    }

    /* Compute fill count */
    FillCount = NumberOfCols * SubPercentTimes100 / (100 * 100);

    /* Fill the progress bar */
    // InbvDisplayString("\r");
    for (i = 0; i < FillCount; ++i) InbvDisplayString(".");
    InbvDisplayString("\r");
}

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode)
{
    /* NT4 style is always text mode */

    /*
     * Show progress only when TextMode is FALSE (usual case).
     *
     * TextMode is set to TRUE in SOS mode, where drivers loading progress
     * is displayed on screen. So we don't want to pollute the screen with
     * a progress bar.
     */
    pTickProgressBar = BootTickProgressBar;
    ShowProgressBar = !TextMode;

    /* Check if bootvid is installed */
    if (InbvBootDriverInstalled)
    {
        /* Acquire ownership and reset the display */
        // InbvAcquireDisplayOwnership();
        InbvResetDisplay(); // We need this for BOOTVID to set the correct palette.

        /* Display blue screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLUE);
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    /* No string filter needed, we display everything */
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE); // FIXME: Disabled to diagnose a VidDisplayString bug.
}

// CODE_SEG("INIT")
VOID
BootThemeCleanup(VOID)
{
    /* Remove the filter and stop the progress bar */
    InbvInstallDisplayStringFilter(NULL);
    ShowProgressBar = FALSE;

    /* If we own the display, begin the display */
    if (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
        /*Inbv*/VidDisplayString("\r\n");
}

//
// Idea for "TextMode":
// - If InbvIsBootDriverInstalled() == FALSE, this is "TextMode".
// - If InbvIsBootDriverInstalled() == TRUE, check whether we
//   previously owned the display. InbvCheckDisplayOwnership() is not
//   enough (because it returns TRUE when we are OWNED or DISABLED).
//   Instead, if InbvDisplayState == OWNED, suppose it's "TextMode".
//   If it's not (i.e. DISABLED or LOST), it's not "TextMode".
//   If it's LOST, we also need to re-acquire the ownership.
//

BOOLEAN
BootThemeDisplaySafeToPowerOffScreen(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    BOOLEAN FullScreen;

    /* NT4 style is always text mode */
    UNREFERENCED_PARAMETER(TextMode);
    UNREFERENCED_PARAMETER(Message);

    FullScreen = (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED);

    /* Check if bootvid is installed. If so, reset the display if we don't own it already. */
    if (InbvBootDriverInstalled /* && !TextMode */ && FullScreen)
    {
        /* Acquire ownership if needed, and reset the display */
        if (!InbvCheckDisplayOwnership()) // (InbvGetDisplayState() == INBV_DISPLAY_STATE_LOST)
            InbvAcquireDisplayOwnership();
        InbvResetDisplay();

        /* Display blue screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLUE);
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    /* No string filter needed, we display everything */
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);

    if (FullScreen)
    {
        /* Center the message on screen. This code expects the message
         * to be 34 characters long, on a 80x25-character screen. */
        ULONG i;
        for (i = 0; i < 25; ++i) InbvDisplayString("\r\n");
        InbvDisplayString("                       ");
    }
    else
    {
        /* Add some space after what may already be on the screen */
        InbvDisplayString("\r\n\r\n");
    }
    return TRUE; // Caller should display the message
}


// Like BootThemeTickProgressBar but for hibernation
static VOID
NTAPI
BootTickHiberProgress(
    _In_ ULONG SubPercentTimes100)
{
    static CHAR Buffer[128];

    // Message from beta builds: "PopSave: %lu%%\r"
    // RtlFindMessage(...)
    RtlStringCchPrintfA(Buffer, _countof(Buffer),
                        "Hibernating: %lu%%\r", SubPercentTimes100 / 100);
    // InbvDisplayString("\r");
    InbvDisplayString(Buffer);
}

BOOLEAN
BootThemeHiberProgressInit(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    BOOLEAN FullScreen;

    /* NT4 style is always text mode */
    UNREFERENCED_PARAMETER(TextMode);
    UNREFERENCED_PARAMETER(Message);

    /* Always show progress */
    pTickProgressBar = BootTickHiberProgress;
    ShowProgressBar = TRUE;
    // /* Set the progress bar ranges */
    // InbvSetProgressBarSubset(0, 100);

    FullScreen = (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED);

    /* Check if bootvid is installed. If so, reset the display if we don't own it already. */
    if (InbvBootDriverInstalled /* && !TextMode */ && FullScreen)
    {
        /* Acquire ownership and reset the display */
        InbvAcquireDisplayOwnership();
        InbvResetDisplay();

        /* Display blue screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLUE);
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    /* No string filter needed, we display everything */
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE); // FIXME: Disabled to diagnose a VidDisplayString bug.
    return FALSE;
}

BOOLEAN
BootThemeHiberProgressDone(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    /* NT4 style is always text mode */
    UNREFERENCED_PARAMETER(TextMode);
    UNREFERENCED_PARAMETER(Message);

    /* Add some space after the hibernation progress */
    InbvDisplayString("\r\n");
    return TRUE; // Caller should display the message
}
