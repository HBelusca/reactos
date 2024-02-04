/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation - "ReactOS NT4"
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto
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
    static const ULONG NumberOfCols = (SCREEN_WIDTH / 8 /*BOOTCHAR_WIDTH*/); // 80;
    ULONG FillCount, i;

    /* Make sure the progress bar is enabled, that we own and are installed */
    ASSERT(ShowProgressBar &&
           InbvBootDriverInstalled &&
           (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED));

    ASSERT(SubPercentTimes100 <= (100 * 100));

    /* Compute fill count */
    FillCount = NumberOfCols * SubPercentTimes100 / (100 * 100);

    /* Fill the progress bar */
    // /*Inbv*/VidDisplayString("\r");
    for (i = 0; i < FillCount; ++i) /*Inbv*/VidDisplayString(".");
    /*Inbv*/VidDisplayString("\r");
}

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode)
{
    /* NT4 style is always text mode */
    UNREFERENCED_PARAMETER(TextMode);

    /* Always show progress */
    // NOTE: For the time being, TextMode is set to TRUE in SOS mode,
    // where other stuff gets displayed on screen, so we don't want to
    // pollute the screen with a "progress" bar. We therefore enable it
    // only when TextMode is FALSE, which is the usual case.
    ShowProgressBar = !TextMode;

    // /* Is the boot driver installed? */
    // if (!InbvBootDriverInstalled) return;

    // Same blue-screen style as the BSoD, see bug.c!KiDisplayBlueScreen()
    // /* Check if bootvid is installed */
    // if (InbvIsBootDriverInstalled())
    {
        // /* Acquire ownership and reset the display */
        // InbvAcquireDisplayOwnership();
        InbvResetDisplay(); // We need this for BOOTVID to set the correct palette.

        /* Display blue screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLUE);
        InbvSetTextColor(BV_COLOR_WHITE);
        // InbvInstallDisplayStringFilter(NULL);
        // InbvEnableDisplayString(TRUE);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    // /* Set filter which will draw text display if needed */
    // InbvInstallDefaultDisplayStringFilter();
}

// CODE_SEG("INIT")
VOID
BootThemeCleanup(VOID)
{
DPRINT1("FinalizeBootLogo\n");
}

BOOLEAN
BootThemeDisplayShutdownMessage(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    /* NT4 style is always text mode */
    UNREFERENCED_PARAMETER(TextMode);

    // /* Display blue screen */
    // InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLUE);
    // InbvSetTextColor(BV_COLOR_WHITE);
    // InbvInstallDisplayStringFilter(NULL);
    // InbvEnableDisplayString(TRUE);
    // InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    /* Caller should display the message */
    return TRUE;

    UNREFERENCED_PARAMETER(Message);
    // InbvDisplayString(Message);
    // return FALSE;
}
