/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "inbv/logo.h"

/* GLOBALS *******************************************************************/

// extern ULONG ProgressBarLeft, ProgressBarTop;
// extern BOOLEAN ShowProgressBar;

/* FEATURES ******************************************************************/

/**
 * This file centralizes the inclusion of custom boot themes, that are
 * implemented in separate source files.
 *
 * It also contains the implementation of the entry points that the other
 * kernel subsystems invoke at boot-time and shutdown or hibernation.
 *
 * The functions that each boot theme must implement are documented below.
 **/

#if 0
CODE_SEG("INIT")
BOOLEAN
NTAPI
BootAnimInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count);
#endif

/**
 * @brief
 * Theme-specific callback invoked by InbvUpdateProgressBar().
 * Ticks the progress bar.
 *
 * @param[in]   SubPercentTimes100
 * The progress percentage, scaled up by 100.
 *
 * @return None.
 **/
VOID
// BootAnimTickProgressBar
BootThemeTickProgressBar(
    _In_ ULONG SubPercentTimes100);

#if 0
CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode);
#endif

/**
 * @brief
 * Theme-specific callback invoked by FinalizeBootLogo().
 **/
// CODE_SEG("INIT")
VOID
BootThemeCleanup(VOID);

/**
 * @brief
 * Theme-specific callback invoked by DisplayShutdownMessage(), from PopShutdownHandler().
 *
 * @param[in]   TextMode
 * TRUE when called in text-mode, or graphics-emulated text mode.
 * FALSE when called in graphics mode.
 *
 * @param[in]   Message
 * The optional shutdown message string to display.
 *
 * @return
 * TRUE if the caller instead should display the shutdown message, FALSE if not.
 **/
BOOLEAN
BootThemeDisplayShutdownMessage(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message);


/* BOOT THEMES ***************************************************************/

// #include "btani2k3.c"
#include "btani2k.c"
// #include "btanint4.c"


/* FUNCTIONS *****************************************************************/

// FIXME: For the time being, implemented separately by each boot theme.
#if 0
CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode)
{
}
#endif

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID)
{
    /* Cleanup boot theme under lock */
    InbvAcquireLock();
    BootThemeCleanup();
    InbvReleaseLock();
}

VOID
NTAPI
DisplayShutdownMessage( // or "Screen"
    _In_ BOOLEAN TextMode) // _In_ POWER_ACTION PowerAction
{
    /* Check if this is text mode */
    if (TextMode)
    {
        /* Retrieve the default message */
        // RtlFindMessage(...)
        PCSTR Message = "The system may be powered off now.\r\n";

        /* Display the message */
        if (!BootThemeDisplayShutdownMessage(TextMode/*TRUE*/, Message))
            return; // The boot theme does not want to show the message

        InbvDisplayString((PCHAR)Message);
    }
    else
    {
#if 0
        /* Is the boot driver installed? */
        if (!InbvBootDriverInstalled)
            return;
#endif
        BootThemeDisplayShutdownMessage(TextMode/*FALSE*/, NULL);
    }
}


//
// Check the following:
// https://betawiki.net/wiki/Windows_2000_build_1743
// https://betawiki.net/wiki/Windows_2000_build_1773
// https://betawiki.net/wiki/Windows_2000_build_1796
// https://betawiki.net/wiki/Windows_2000_build_1814
// https://betawiki.net/wiki/Windows_2000_build_1835
//
// as well as:
// https://github.com/reactos/reactos/commit/014d8e9588fe1366d34cfde74c7e0e55841037ef
// https://github.com/reactos/reactos/commit/819a0ed90a7c1a5dedff08aa3b3c5501dc58c632
// the io/iomgr/driver.c!IopDisplayLoadingMessage() function.

VOID
NTAPI
DisplayHibernateScreen(
    _In_ BOOLEAN TextMode)
{
    /* Check if this is text mode */
    if (TextMode)
    {
        // Init blue screen? Or done by the caller as it is for shutdown or boot?
        // InbvResetDisplay();

        // Load the hibernation-in-progress message
    }
    else
    {
        // Display hibernation-in-progress bitmap

        /* Set the progress bar ranges */
        InbvSetProgressBarSubset(0, 100);

        // /* Set progress bar coordinates and display it */
        // InbvSetProgressBarCoordinates(left, top);
    }
}

VOID
NTAPI
FinalizeHibernateScreen(
    _In_ BOOLEAN TextMode)
{
    /* Check if this is text mode */
    if (TextMode)
    {
        /* Display the default message */
        // RtlFindMessage(...)
        InbvDisplayString("State saved, power off the system.\r\n");
    }
    else
    {
        // Display hibernation-complete bitmap, typically the you-can-shutdown
    }
}
