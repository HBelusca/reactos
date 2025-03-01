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
// InbvInitializeResources();
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
 * Theme-specific callback invoked by DisplaySafeToPowerOffScreen(), from PopShutdownHandler(),
 * when the system is about to be powered off.
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
BootThemeDisplaySafeToPowerOffScreen(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message);

/**
 * @brief
 * Theme-specific callback invoked when hibernation starts.
 *
 * @param[in]   TextMode
 * TRUE when called in text-mode, or graphics-emulated text mode.
 * FALSE when called in graphics mode.
 **/
BOOLEAN
BootThemeHiberProgressInit(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message);

/**
 * @brief
 * Theme-specific callback invoked when hibernation finishes and
 * the system is about to be powered off.
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
BootThemeHiberProgressDone(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message);


/* BOOT THEMES ***************************************************************/

#include "config.h"

#ifndef BOOT_THEME_SRC
#pragma message("\n"\
    "A boot theme source file has not been specified!\n"\
    "Please specify one using the BOOT_THEME_SRC macro in the config.h file.\n"\
    "Example:\n"\
    "  #define BOOT_THEME_SRC \"bttheme.c\"\n")
#error "Missing BOOT_THEME_SRC"
#endif

#include BOOT_THEME_SRC


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


// Ba, Bga, Bt, Bgt, BgTh, BgAn, BAni, AnTh, AnFw (Animation framework)

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

typedef BOOLEAN
(PBOOT_THEME_DISPLAY_MESSAGE)(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message);

/**
 * @brief
 * Common helper for DisplaySafeToPowerOffScreen() and FinalizeHibernateScreen().
 **/
static VOID
DisplayMessage(
    _In_ BOOLEAN TextMode,
    _In_ PCSTR Message, // In the future: ULONG MessageId
    _In_ PBOOT_THEME_DISPLAY_MESSAGE ThemeDisplayMessage)
{
    /* Retrieve the default message */
    // PCSTR Message;
    // RtlFindMessage(...)

#if 0
    /* Check if this is text mode. Is the boot driver installed? */
    if (!TextMode && !InbvBootDriverInstalled)
        return;
#endif

    /* Let the boot theme display the message */
    if (!ThemeDisplayMessage(TextMode, Message))
        return; // The boot theme does not want to show the message.

    /* Display the message if necessary */
    InbvDisplayString((PCHAR)Message);
}

VOID
NTAPI
DisplaySafeToPowerOffScreen(
    _In_ BOOLEAN TextMode)
{
    DisplayMessage(TextMode,
                   "The system may be powered off now.\r\n",
                   BootThemeDisplaySafeToPowerOffScreen);
}

VOID
NTAPI
DisplayHibernateScreen(
    _In_ BOOLEAN TextMode)
{
    DisplayMessage(TextMode,
                   "Hibernating...\r\n",
                   BootThemeHiberProgressInit);
}

VOID
NTAPI
FinalizeHibernateScreen(
    _In_ BOOLEAN TextMode)
{
    DisplayMessage(TextMode,
                   "State saved, power off the system.\r\n",
                   BootThemeHiberProgressDone);
}

#if 0
VOID
NTAPI
DisplayResumeScreen(
    _In_ BOOLEAN TextMode)
{
}

// FinalizeResumeScreen()
#endif
