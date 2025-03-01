/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation - "ReactOS 2000"
 * COPYRIGHT:   Copyright 2022-2024 Hermès Bélusca-Maïto
 */

/* INCLUDES ******************************************************************/

#include <debug.h>

//
// Positions of areas and images
//

#define VID_SCROLL_AREA_LEFT        8   // BOOTCHAR_WIDTH
#define VID_SCROLL_AREA_TOP         80  /* for header v2 */ // 125 for header v1
#define VID_SCROLL_AREA_RIGHT       (SCREEN_WIDTH - 1 - 8)
#define VID_SCROLL_AREA_BOTTOM      (400 + 13/*9*/) // (480 - 65 - BOOTCHAR_HEIGHT - 2) // 400

#define VID_ROTBAR_TOP              79 /* for header v2 */ // 122 /*for header v1*/ // (SCREEN_HEIGHT-65) // (SCREEN_HEIGHT-6)

#define VID_PROGRESS_BAR_LEFT       165
#define VID_PROGRESS_BAR_TOP        446
#define VID_PROGRESS_BAR_WIDTH      366
#define VID_PROGRESS_BAR_HEIGHT     19

#define VID_HIBER_PRGBAR_LEFT       210
#define VID_HIBER_PRGBAR_TOP        270

/* 16px space between shutdown logo and message */
#define VID_SHUTDOWN_LOGO_LEFT      225
#define VID_SHUTDOWN_LOGO_TOP       114
// #define VID_SHUTDOWN_MSG_LEFT       213
// #define VID_SHUTDOWN_MSG_TOP        354

#define VID_FOOTER_BG_TOP        (SCREEN_HEIGHT - 66)


/* GLOBALS *******************************************************************/

/*
 * ReactOS uses the same boot screen for all the products.
 *
 * Enable this define when ReactOS will have different SKUs
 * (Workstation, Server, Storage Server, Cluster Server, etc...).
 */
// #define REACTOS_SKUS

#define BOOTANIM_BY_DPC
#include "btanihlp.c"

extern ULONG ProgressBarLeft, ProgressBarTop;
extern BOOLEAN ShowProgressBar;

/*
 * Values for PltRotBarStatus:
 * - PltRotBarStatus == 1, do palette fading-in (done elsewhere in ReactOS);
 * - PltRotBarStatus == 2, do rotation bar animation;
 * - PltRotBarStatus == 3, stop the animation.
 * - Any other value is ignored and the animation continues to run.
 */
typedef enum _ROT_BAR_STATUS
{
    // RBS_PAUSE = 0,
    RBS_FADEIN = 1,
    RBS_ANIMATE,
    RBS_STOP_ANIMATE,
    RBS_STATUS_MAX
} ROT_BAR_STATUS;

static ULONG /*RotBarLeft = 0,*/ RotBarTop = 0;

static BOOLEAN AnimationActive = FALSE;
static ROT_BAR_STATUS PltRotBarStatus = 0;
static UCHAR RotLineBuffer[SCREEN_WIDTH * 6];


/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
BootAnimInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count)
{
#if 0
    ULONG i;

    /* Quit if we're already installed */
    if (InbvBootDriverInstalled) return TRUE;

    /* Find bitmap resources in the kernel */
    ResourceCount = min(Count, RTL_NUMBER_OF(ResourceList) - 1);
    for (i = 1; i <= ResourceCount; i++)
    {
        /* Do the lookup */
        ResourceList[i] = FindBitmapResource(LoaderBlock, i);
    }

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
    ULONG FillCount;

    /* Make sure the progress bar is enabled, that we own and are installed */
    ASSERT(ShowProgressBar &&
           InbvBootDriverInstalled &&
           (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED));

    ASSERT(SubPercentTimes100 <= (100 * 100));

    /* Compute fill count */
    FillCount = VID_PROGRESS_BAR_WIDTH * SubPercentTimes100 / (100 * 100);

    InbvAcquireLock();

    /* If 0%, draw also the left round corner */
    if (SubPercentTimes100 <= 10*100)
    {
        VidSolidColorFill(ProgressBarLeft - 6,
                          ProgressBarTop + 6,
                          ProgressBarLeft - 6,
                          ProgressBarTop - 6 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft - 5,
                          ProgressBarTop + 4,
                          ProgressBarLeft - 5,
                          ProgressBarTop - 4 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft - 4,
                          ProgressBarTop + 3,
                          ProgressBarLeft - 4,
                          ProgressBarTop - 3 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft - 3,
                          ProgressBarTop + 2 ,
                          ProgressBarLeft - 3,
                          ProgressBarTop - 2 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft - 2,
                          ProgressBarTop + 1,
                          ProgressBarLeft - 2,
                          ProgressBarTop - 1 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft - 1,
                          ProgressBarTop + 1,
                          ProgressBarLeft - 1,
                          ProgressBarTop - 1 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);
    }

    /* Fill the progress bar */
    VidSolidColorFill(ProgressBarLeft,
                      ProgressBarTop,
                      ProgressBarLeft + FillCount,
                      ProgressBarTop + VID_PROGRESS_BAR_HEIGHT,
                      BV_COLOR_WHITE);

    /* If 100%, draw also the right round corner */
    if (SubPercentTimes100 >= 100*100)
    {
        VidSolidColorFill(ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 1,
                          ProgressBarTop + 1,
                          ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 1,
                          ProgressBarTop - 1 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 2,
                          ProgressBarTop + 1,
                          ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 2,
                          ProgressBarTop - 1 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 3,
                          ProgressBarTop + 2 ,
                          ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 3,
                          ProgressBarTop - 2 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 4,
                          ProgressBarTop + 3,
                          ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 4,
                          ProgressBarTop - 3 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 5,
                          ProgressBarTop + 4,
                          ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 5,
                          ProgressBarTop - 4 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);

        VidSolidColorFill(ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 6,
                          ProgressBarTop + 6,
                          ProgressBarLeft + VID_PROGRESS_BAR_WIDTH + 6,
                          ProgressBarTop - 6 + VID_PROGRESS_BAR_HEIGHT,
                          BV_COLOR_WHITE);
    }

    InbvReleaseLock();
}

VOID
NTAPI
BootAnimUpdate(
    _In_ PBOOT_ANIM_CTX BootAnimCtx)
{
    // Boot animation context variables
    static ULONG Index = 0;

    UNREFERENCED_PARAMETER(BootAnimCtx);

    /* Unknown unexpected command */
    ASSERT(PltRotBarStatus < RBS_STATUS_MAX);

    if (PltRotBarStatus == RBS_STOP_ANIMATE)
    {
        /* Stop the animation */
        // BootAnimCtx->Terminate = TRUE;
        return;
    }

    if (PltRotBarStatus == RBS_FADEIN)
    {
        /* We are starting the animation, perform first initialization */
        Index = 0;
        PltRotBarStatus = RBS_ANIMATE;
    }
    else if (PltRotBarStatus == RBS_ANIMATE) // (PltRotBarStatus != 0)
    {
        Index %= SCREEN_WIDTH;

        /* Right part */
        VidBufferToScreenBlt(RotLineBuffer, Index,
                             RotBarTop, SCREEN_WIDTH - Index, 6, SCREEN_WIDTH);
        if (Index > 0)
        {
            /* Left part */
            VidBufferToScreenBlt(RotLineBuffer + (SCREEN_WIDTH - Index) / 2, 0,
                                 RotBarTop, Index - 2, 6, SCREEN_WIDTH);
        }
        Index += 4;
    }
}

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode)
{
    static BOOLEAN FirstCall = TRUE;

DPRINT1("DisplayBootBitmap\n");

    /* If the animation has already been created,
     * pause the animation but keep it alive */
    if (AnimationActive)
    {
        InbvAcquireLock();
        /// PltRotBarStatus = 0;
#if 1
        /* Stop the animation -- In DPC mode the animation
         * can be killed in the string display filter */
        PltRotBarStatus = RBS_STOP_ANIMATE;
        AnimationActive = FALSE;
#endif
        InbvReleaseLock();
    }

    ShowProgressBar = FALSE;

    /* Check if this is text mode */
    if (TextMode)
    {
        PVOID Header, Footer;

        InbvSolidColorFill(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_BLACK);
        InbvSetTextColor(BV_COLOR_WHITE);

        /*
         * Make the kernel resource section temporarily writable,
         * as we are going to change the bitmaps' palette in place.
         */
        InbvMakeKernelResourceSectionWritable();

#if 0
        /* Check the type of the OS: workstation or server */
        if (SharedUserData->NtProductType == NtProductWinNt)
        {
            /* Workstation; get resources */
            Header = InbvGetResourceAddress(IDB_WKSTA_HEADER);
            Footer = InbvGetResourceAddress(IDB_WKSTA_FOOTER);
        }
        else
        {
            /* Server; get resources */
            Header = InbvGetResourceAddress(IDB_SERVER_HEADER);
            Footer = InbvGetResourceAddress(IDB_SERVER_FOOTER);
        }
#else
        Header = InbvGetResourceAddress(IDB_WKSTA_HEADER);
        Footer = InbvGetResourceAddress(IDB_WKSTA_FOOTER);
#endif

        /* If we have the resources, BitBlt them on the screen,
         * first the footer and then the header, such that it is
         * the header bitmap that dictates which palette to use. */
        if (FirstCall && Footer)
        {
            BitBltAligned(Footer,
                          FALSE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_BOTTOM,
                          0, 0, 0, 0 /*65*/);
        }
        else
        {
            InbvSolidColorFill(0, VID_FOOTER_BG_TOP,
                               SCREEN_WIDTH-1, SCREEN_HEIGHT-1,
                               BV_COLOR_LIGHT_GRAY);
        }
        if (Header)
        {
            BitBltAligned(Header,
                          FALSE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_TOP,
                          0, 0, 0, 0);
        }

        /* Restore the kernel resource section protection to be read-only */
        InbvMakeKernelResourceSectionReadOnly();

        /* Set the scrolling region */
        InbvSetScrollRegion(VID_SCROLL_AREA_LEFT, VID_SCROLL_AREA_TOP,
                            VID_SCROLL_AREA_RIGHT, VID_SCROLL_AREA_BOTTOM);

        /* Set the progress bar coordinates and display it,
         * only if we directly are in text mode. If we do
         * the transition full-screen to text mode and we are
         * at the end of boot, don't show the progress bar. */
        if (FirstCall)
        {
            InbvSetProgressBarCoordinates(VID_PROGRESS_BAR_LEFT,
                                          VID_PROGRESS_BAR_TOP);
        }

        /* Set the rotating bar coordinates */
        RotBarTop = VID_ROTBAR_TOP-6;
    }
    else
    {
        PVOID Screen;

        /* Is the boot driver installed? */
        if (!InbvBootDriverInstalled)
            return;

        /*
         * Make the kernel resource section temporarily writable,
         * as we are going to change the bitmaps' palette in place.
         */
        InbvMakeKernelResourceSectionWritable();

        /* Load the boot screen */
        Screen = InbvGetResourceAddress(IDB_BOOT_SCREEN);
        if (Screen)
        {
            /* Save the main image palette for implementing the fade-in effect */
            BootLogoCachePalette(Screen);

            /* Draw the screen */
            BitBltPalette(Screen, FALSE /*TRUE*/, 0, 0);

            /* Set the progress bar coordinates and display it */
            InbvSetProgressBarCoordinates(VID_PROGRESS_BAR_LEFT,
                                          VID_PROGRESS_BAR_TOP);
        }

        /* Restore the kernel resource section protection to be read-only */
        InbvMakeKernelResourceSectionReadOnly();

        /* Set the rotating bar coordinates */
        RotBarTop = SCREEN_HEIGHT-6;

        /// /* Display the boot logo */
        /// BootLogoFadeIn();

        /* Set filter which will draw text display if needed */
        InbvInstallDefaultDisplayStringFilter();
    }

    /* Store the line region to rotate in the global buffer */
    InbvScreenToBufferBlt(RotLineBuffer, 0, RotBarTop,
                          SCREEN_WIDTH, 6, SCREEN_WIDTH);

    /* Start the rotating bar animation if needed */
    if (!AnimationActive)
        AnimationActive = InbvAnimationInit(/*BootAnimUpdate,*/ 50);
    if (AnimationActive)
    {
        /* The animation is running, initialize the progress bar */
        InbvAcquireLock();
        PltRotBarStatus = RBS_FADEIN;
        InbvReleaseLock();
    }

    FirstCall = FALSE;
}

// CODE_SEG("INIT")
VOID
BootThemeCleanup(VOID)
{
#if 0
    /* Stop the animation */
    PltRotBarStatus = RBS_STOP_ANIMATE;
    AnimationActive = FALSE;
#endif
}


/**
 * @brief
 * Helper used to show the shutdown message for both
 * regular shutdown and shutdown after hibernation.
 **/
static BOOLEAN
BootThemeDisplayShutdownMessage(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    if (TextMode)
    {
        /* Center the message on screen. This code expects the message
         * to be 34 characters long, on a 80x25-character screen. */
        InbvDisplayString("                       ");
        return TRUE; // Caller should display the message
    }
    else
    {
        /* Display the shutdown message */
        PUCHAR Logo = InbvGetResourceAddress(IDB_SHUTDOWN_MSG);
        if (Logo)
        {
            // InbvBitBlt(Logo, VID_SHUTDOWN_LOGO_LEFT, VID_SHUTDOWN_LOGO_TOP);
            BitBltAligned(Logo,
                          FALSE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_CENTER,
                          0, 0, 0, 0);
        }
    }
    return FALSE;
}

BOOLEAN
BootThemeDisplaySafeToPowerOffScreen(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    if (TextMode)
    {
        /* Center the message on screen. This code expects the message
         * to be 34 characters long, on a 80x25-character screen. */
        ULONG i;
        for (i = 0; i < 25; ++i) InbvDisplayString("\r\n");
        // InbvDisplayString("                       ");
    }
    else
    {
        /* Acquire ownership if needed, and reset the display */
        if (!InbvCheckDisplayOwnership()) // (InbvGetDisplayState() == INBV_DISPLAY_STATE_LOST)
            InbvAcquireDisplayOwnership();
        InbvResetDisplay();

        /* No string filter needed, we display everything */
        InbvInstallDisplayStringFilter(NULL);
        InbvEnableDisplayString(TRUE);

        /* Cleanup for shutdown screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }
    return BootThemeDisplayShutdownMessage(TextMode, Message);
}

BOOLEAN
BootThemeHiberProgressInit(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    if (TextMode)
    {
        // InbvResetDisplay();

        /* Center the message on screen. This code expects the message
         * to be 34 characters long, on a 80x25-character screen. */
        ULONG i;
        for (i = 0; i < 25; ++i) InbvDisplayString("\r\n");
        InbvDisplayString("                       ");

        /* Always show progress */
        ShowProgressBar = TRUE;

        return TRUE; // Caller should display the message
    }
    else // if (InbvBootDriverInstalled)
    {
        PUCHAR Logo;

        /* Acquire ownership and reset the display */
        InbvAcquireDisplayOwnership();
        InbvResetDisplay();

        /* No string filter needed, we display everything */
        InbvInstallDisplayStringFilter(NULL);
        InbvEnableDisplayString(TRUE);

        /* Cleanup for hibernation screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

        /* Display hibernation logo and message */
        Logo = InbvGetResourceAddress(IDB_HIBERNATE_BAR);
        if (Logo)
        {
            // InbvBitBlt(Logo, VID_HIBER_MSG_LEFT, VID_HIBER_MSG_TOP);
            BitBltAligned(Logo,
                          FALSE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_CENTER,
                          0, 0, 0, 0);
        }

        /* Set the progress bar range and coordinates, and display it */
        InbvSetProgressBarSubset(0, 100);
        InbvSetProgressBarCoordinates(VID_HIBER_PRGBAR_LEFT,
                                      VID_HIBER_PRGBAR_TOP);
    }
    return FALSE;
}

BOOLEAN
BootThemeHiberProgressDone(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    if (TextMode)
    {
        /* Add some space after the hibernation progress */
        InbvDisplayString("\r\n");
    }
    return BootThemeDisplayShutdownMessage(TextMode, Message);
}
