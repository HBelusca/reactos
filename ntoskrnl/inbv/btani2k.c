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
#define VID_SCROLL_AREA_BOTTOM      400 // (480 - 65 - BOOTCHAR_HEIGHT - 2) // (400 + 13)

#define VID_PROGRESS_BAR_LEFT       165
#define VID_PROGRESS_BAR_TOP        446
#define VID_PROGRESS_BAR_WIDTH      366
#define VID_PROGRESS_BAR_HEIGHT     19

/* 16px space between shutdown logo and message */
#define VID_SHUTDOWN_LOGO_LEFT    225
#define VID_SHUTDOWN_LOGO_TOP     114
#define VID_SHUTDOWN_MSG_LEFT     213
#define VID_SHUTDOWN_MSG_TOP      354

#define VID_FOOTER_BG_TOP        (SCREEN_HEIGHT - 59)


/* GLOBALS *******************************************************************/

/*
 * ReactOS uses the same boot screen for all the products.
 *
 * Enable this define when ReactOS will have different SKUs
 * (Workstation, Server, Storage Server, Cluster Server, etc...).
 */
// #define REACTOS_SKUS

/*
 * Enable this define when Inbv will support rotating progress bar.
 */
#define BOOT_ROTBAR_IMPLEMENTED

#define BOOTANIM_BY_DPC
#include "btanihlp.c"

extern ULONG ProgressBarLeft, ProgressBarTop;
extern BOOLEAN ShowProgressBar;

#ifdef BOOT_ROTBAR_IMPLEMENTED
/*
 * Values for PltRotBarStatus:
 * - PltRotBarStatus == 1, do palette fading-in (done elsewhere in ReactOS);
 * - PltRotBarStatus == 2, do rotation bar animation;
 * - PltRotBarStatus == 3, stop the animation.
 * - Any other value is ignored and the animation continues to run.
 */
typedef enum _ROT_BAR_STATUS
{
    RBS_FADEIN = 1,
    RBS_ANIMATE,
    RBS_STOP_ANIMATE,
    RBS_STATUS_MAX
} ROT_BAR_STATUS;

typedef enum _ROT_BAR_TYPE
{
    RB_UNSPECIFIED,
    RB_SCREEN,
    RB_TEXT
} ROT_BAR_TYPE;

static BOOLEAN AnimationActive = FALSE;
static ROT_BAR_TYPE RotBarSelection = RB_UNSPECIFIED;
static ROT_BAR_STATUS PltRotBarStatus = 0;
static UCHAR RotLineBuffer[SCREEN_WIDTH * 6];
#endif

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
}

#ifdef BOOT_ROTBAR_IMPLEMENTED
VOID // BOOLEAN
NTAPI
BootAnimUpdate(
    _In_ PBOOT_ANIM_CTX BootAnimCtx)
{
    // Boot animation context variables
    static ULONG Y, Index = 0;

    UNREFERENCED_PARAMETER(BootAnimCtx);

    /* Unknown unexpected command */
    ASSERT(PltRotBarStatus < RBS_STATUS_MAX);

    if (PltRotBarStatus == RBS_STOP_ANIMATE)
    {
        /* Stop the animation */
        return; // return FALSE;
    }

    if (RotBarSelection != RB_UNSPECIFIED)
    {
        Index %= SCREEN_WIDTH;

        if (RotBarSelection == RB_SCREEN)
            Y = SCREEN_HEIGHT-6;
        else // if (RotBarSelection == RB_TEXT)
            Y = 79-6 /* for header v2 */; // 122-6 /*for header v1*/; // SCREEN_HEIGHT-65; // SCREEN_HEIGHT-6;

        /* Right part */
        VidBufferToScreenBlt(RotLineBuffer, Index, Y, SCREEN_WIDTH - Index, 6, SCREEN_WIDTH);
        if (Index > 0)
        {
            /* Left part */
            VidBufferToScreenBlt(RotLineBuffer + (SCREEN_WIDTH - Index) / 2, 0, Y, Index - 2, 6, SCREEN_WIDTH);
        }
        Index += 4;
    }

    return; // TRUE;
}

CODE_SEG("INIT")
static
VOID
InbvRotBarInit(VOID)
{
    PltRotBarStatus = RBS_FADEIN;
    /* Perform other initialization if needed */
}
#endif

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode)
{
DPRINT1("DisplayBootBitmap\n");

#ifdef BOOT_ROTBAR_IMPLEMENTED
    /* Check if the animation has already been created */
    if (AnimationActive)
    {
        /* Yes, just reset the progress bar but keep the animation alive */
        InbvAcquireLock();
        RotBarSelection = RB_UNSPECIFIED;
        InbvReleaseLock();
    }
#endif

    ShowProgressBar = FALSE;

    /* Check if this is text mode */
    if (TextMode)
    {
        PVOID Header, Footer;

        /*
         * Make the kernel resource section temporarily writable,
         * as we are going to change the bitmaps' palette in place.
         */
        InbvMakeKernelResourceSectionWritable();

        /* Check the type of the OS: workstation or server */
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvSolidColorFill(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_BLACK);
        InbvSolidColorFill(0, VID_FOOTER_BG_TOP, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_LIGHT_GRAY);

#if 0
        if (SharedUserData->NtProductType == NtProductWinNt)
        {
            /* Workstation - Get resources */
            Header = InbvGetResourceAddress(IDB_WKSTA_HEADER);
            Footer = InbvGetResourceAddress(IDB_WKSTA_FOOTER);
        }
        else
        {
            /* Server - Get resources */
            Header = InbvGetResourceAddress(IDB_SERVER_HEADER);
            Footer = InbvGetResourceAddress(IDB_SERVER_FOOTER);
        }
#else
        Header = InbvGetResourceAddress(IDB_WKSTA_HEADER);
        Footer = InbvGetResourceAddress(IDB_WKSTA_FOOTER);
#endif

        /* Set the scrolling region */
        InbvSetScrollRegion(VID_SCROLL_AREA_LEFT, VID_SCROLL_AREA_TOP,
                            VID_SCROLL_AREA_RIGHT, VID_SCROLL_AREA_BOTTOM);

        /* Make sure we have resources */
        if (Header && Footer)
        {
            /* BitBlt them on the screen */
            BitBltAligned(Footer,
                          TRUE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_BOTTOM,
                          0, 0, 0, 0 /*65*/);
            BitBltAligned(Header,
                          FALSE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_TOP,
                          0, 0, 0, 0);
        }

        // TODO: Improve: Don't show the progress bar if we do the transition
        // full-screen to text mode and we are at the end of boot.
        /* Set progress bar coordinates and display it */
        InbvSetProgressBarCoordinates(VID_PROGRESS_BAR_LEFT,
                                      VID_PROGRESS_BAR_TOP);

#ifdef BOOT_ROTBAR_IMPLEMENTED
        /* Store the line region to rotate in global buffer */
        InbvScreenToBufferBlt(RotLineBuffer, 0,
                              79-6 /* for header v2 */, // 122-6 /*for header v1*/, // SCREEN_HEIGHT-6,
                              SCREEN_WIDTH, 6, SCREEN_WIDTH);

        // {
            // /* Hide the simple progress bar if not used */
            // ShowProgressBar = FALSE;
        // }
#endif

        /* Restore the kernel resource section protection to be read-only */
        InbvMakeKernelResourceSectionReadOnly();
    }
    else
    {
        PVOID Screen;

        /* Is the boot driver installed? */
        if (!InbvBootDriverInstalled) return;

        /*
         * Make the kernel resource section temporarily writable,
         * as we are going to change the bitmaps' palette in place.
         */
        InbvMakeKernelResourceSectionWritable();

        /* Load the boot screen */
        Screen = InbvGetResourceAddress(IDB_BOOT_SCREEN);

        /* Make sure we have a logo */
        if (Screen)
        {
            /* Save the main image palette for implementing the fade-in effect */
            BootLogoCachePalette(Screen);

            /* Draw the screen */
DPRINT1("Starting DRAWING SCREEN\n");
            BitBltPalette(Screen, /*TRUE*/ FALSE, 0, 0);
DPRINT1("DRAWING SCREEN finished\n");

            /* Set progress bar coordinates and display it */
            InbvSetProgressBarCoordinates(VID_PROGRESS_BAR_LEFT,
                                          VID_PROGRESS_BAR_TOP);
        }

#ifdef BOOT_ROTBAR_IMPLEMENTED
        /* Store the line region to rotate in global buffer */
        InbvScreenToBufferBlt(RotLineBuffer, 0, SCREEN_HEIGHT-6, SCREEN_WIDTH, 6, SCREEN_WIDTH);

        // {
            // /* Hide the simple progress bar if not used */
            // ShowProgressBar = FALSE;
        // }
#endif

        /* Restore the kernel resource section protection to be read-only */
        InbvMakeKernelResourceSectionReadOnly();

        /* Display the boot logo and fade it in */
DPRINT1("Starting FADE-IN\n");
        BootLogoFadeIn();
DPRINT1("FADE-IN finished\n");

        /* Set filter which will draw text display if needed */
        InbvInstallDefaultDisplayStringFilter();
    }

#ifdef BOOT_ROTBAR_IMPLEMENTED
    if (!AnimationActive)
        AnimationActive = InbvAnimationInit(/*BootAnimUpdate,*/ 50);

    /* Do we have the animation running? */
    if (AnimationActive)
    {
        /* We do, initialize the progress bar */
        InbvAcquireLock();
        RotBarSelection = (TextMode ? RB_TEXT : RB_SCREEN);
        InbvRotBarInit();
        InbvReleaseLock();
    }
#endif
}

// CODE_SEG("INIT")
VOID
BootThemeCleanup(VOID)
{
    /* Check the display state */
    // if (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
    // {
        // /* Clear the screen */
        // VidSolidColorFill(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_BLACK);
    // }

DPRINT1("FinalizeBootLogo\n");

    /* Reset progress bar */
#ifdef BOOT_ROTBAR_IMPLEMENTED
    PltRotBarStatus = RBS_STOP_ANIMATE;
    AnimationActive = FALSE;
#endif
}

BOOLEAN
BootThemeDisplayShutdownMessage(
    _In_ BOOLEAN TextMode,
    _In_opt_ PCSTR Message)
{
    if (TextMode)
    {
        /* Center the message on screen. This code expects the message
         * to be 34 characters long, on a 80x25-character screen. */
        ULONG i;
        for (i = 0; i < 25; ++i) InbvDisplayString("\r\n");
        InbvDisplayString("                       ");
        return TRUE; // Caller should display the message
    }
    else
    {
        PUCHAR Logo1, Logo2;

        /* Yes we do, cleanup for shutdown screen */
        InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
        InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

        /* Display shutdown logo and message */
        Logo1 = InbvGetResourceAddress(IDB_SHUTDOWN_MSG);
        Logo2 = InbvGetResourceAddress(IDB_LOGO_DEFAULT);
        if (Logo1 && Logo2)
        {
            InbvBitBlt(Logo1, VID_SHUTDOWN_MSG_LEFT, VID_SHUTDOWN_MSG_TOP);
            InbvBitBlt(Logo2, VID_SHUTDOWN_LOGO_LEFT, VID_SHUTDOWN_LOGO_TOP);
        }
    }
    return FALSE;
}
