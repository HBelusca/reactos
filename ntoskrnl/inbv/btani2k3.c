/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation - "ReactOS 2003"
 * COPYRIGHT:   Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 *              Copyright 2007 Hervé Poussineau (hpoussin@reactos.org)
 *              Copyright 2012-2022 Hermès Bélusca-Maïto
 *              Copyright 2017-2018 Stanislav Motylkov
 *              Copyright 2019-2020 Yaroslav Kibysh
 */

/* INCLUDES ******************************************************************/

//
// Positions of areas and images
//

#define VID_SCROLL_AREA_LEFT       32
#define VID_SCROLL_AREA_TOP        80
#define VID_SCROLL_AREA_RIGHT     631
#define VID_SCROLL_AREA_BOTTOM    400

#define VID_PROGRESS_BAR_LEFT     259
#define VID_PROGRESS_BAR_TOP      352
#define VID_PROGRESS_BAR_WIDTH    121
#define VID_PROGRESS_BAR_HEIGHT   12

/* 16px space between shutdown logo and message */
#define VID_SHUTDOWN_LOGO_LEFT    225
#define VID_SHUTDOWN_LOGO_TOP     114
#define VID_SHUTDOWN_MSG_LEFT     213
#define VID_SHUTDOWN_MSG_TOP      354

#define VID_SKU_AREA_LEFT         418
#define VID_SKU_AREA_TOP          230
#define VID_SKU_AREA_RIGHT        454
#define VID_SKU_AREA_BOTTOM       256

#define VID_SKU_SAVE_AREA_LEFT    413
#define VID_SKU_SAVE_AREA_TOP     237

#define VID_SKU_TEXT_LEFT         180
#define VID_SKU_TEXT_TOP          121

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
 * Enable this define for having fancy features
 * in the boot and shutdown screens.
 */
// #define REACTOS_FANCY_BOOT

/*
 * Enable this define when Inbv will support rotating progress bar.
 */
#define INBV_ROTBAR_IMPLEMENTED

extern ULONG ProgressBarLeft, ProgressBarTop;
extern BOOLEAN ShowProgressBar;

#ifdef INBV_ROTBAR_IMPLEMENTED
/*
 * Change this to modify progress bar behaviour
 */
#define ROT_BAR_DEFAULT_MODE    RB_PROGRESS_BAR

/*
 * Values for PltRotBarStatus:
 * - PltRotBarStatus == 1, do palette fading-in (done elsewhere in ReactOS);
 * - PltRotBarStatus == 2, do rotation bar animation;
 * - PltRotBarStatus == 3, stop the animation thread.
 * - Any other value is ignored and the animation thread continues to run.
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
    RB_SQUARE_CELLS,
    RB_PROGRESS_BAR
} ROT_BAR_TYPE;

static BOOLEAN RotBarThreadActive = FALSE;
static ROT_BAR_TYPE RotBarSelection = RB_UNSPECIFIED;
static ROT_BAR_STATUS PltRotBarStatus = 0;
static UCHAR RotBarBuffer[24 * 9];
static UCHAR RotLineBuffer[SCREEN_WIDTH * 6];
#endif // INBV_ROTBAR_IMPLEMENTED


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
NTAPI
BootAnimTickProgressBar(
    _In_ ULONG SubPercentTimes100)
{
    ULONG FillCount;

    /* Make sure the progress bar is enabled, that we own and are installed */
    ASSERT(ShowProgressBar &&
           InbvBootDriverInstalled &&
           (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED));

    /* Compute fill count */
    FillCount = VID_PROGRESS_BAR_WIDTH * SubPercentTimes100 / (100 * 100);

    /* Acquire the lock */
    InbvAcquireLock();

    /* Fill the progress bar */
    VidSolidColorFill(ProgressBarLeft,
                      ProgressBarTop,
                      ProgressBarLeft + FillCount,
                      ProgressBarTop + VID_PROGRESS_BAR_HEIGHT,
                      BV_COLOR_WHITE);

    /* Release the lock */
    InbvReleaseLock();
}

#ifdef INBV_ROTBAR_IMPLEMENTED
static
VOID
NTAPI
InbvRotationThread(
    _In_ PVOID Context)
{
    ULONG X, Y, Index, Total;
    LARGE_INTEGER Delay = {{0}};

    UNREFERENCED_PARAMETER(Context);

    InbvAcquireLock();
    if (RotBarSelection == RB_SQUARE_CELLS)
    {
        Index = 0;
    }
    else
    {
        Index = 32;
    }
    X = ProgressBarLeft + 2;
    Y = ProgressBarTop + 2;
    InbvReleaseLock();

    while (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
    {
        /* Wait for a bit */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        InbvAcquireLock();

        /* Unknown unexpected command */
        ASSERT(PltRotBarStatus < RBS_STATUS_MAX);

        if (PltRotBarStatus == RBS_STOP_ANIMATE)
        {
            /* Stop the thread */
            InbvReleaseLock();
            break;
        }

        if (RotBarSelection == RB_SQUARE_CELLS)
        {
            Delay.QuadPart = -800000LL; // 80 ms
            Total = 18;
            Index %= Total;

            if (Index >= 3)
            {
                /* Fill previous bar position */
                VidSolidColorFill(X + ((Index - 3) * 8), Y, (X + ((Index - 3) * 8)) + 8 - 1, Y + 9 - 1, BV_COLOR_BLACK);
            }
            if (Index < Total - 1)
            {
                /* Draw the progress bar bit */
                if (Index < 2)
                {
                    /* Appearing from the left */
                    VidBufferToScreenBlt(RotBarBuffer + 8 * (2 - Index) / 2, X, Y, 22 - 8 * (2 - Index), 9, 24);
                }
                else if (Index >= Total - 3)
                {
                    /* Hiding to the right */
                    VidBufferToScreenBlt(RotBarBuffer, X + ((Index - 2) * 8), Y, 22 - 8 * (4 - (Total - Index)), 9, 24);
                }
                else
                {
                    VidBufferToScreenBlt(RotBarBuffer, X + ((Index - 2) * 8), Y, 22, 9, 24);
                }
            }
            Index++;
        }
        else if (RotBarSelection == RB_PROGRESS_BAR)
        {
            Delay.QuadPart = -600000LL; // 60 ms
            Total = SCREEN_WIDTH;
            Index %= Total;

            /* Right part */
            VidBufferToScreenBlt(RotLineBuffer, Index, SCREEN_HEIGHT-6, SCREEN_WIDTH - Index, 6, SCREEN_WIDTH);
            if (Index > 0)
            {
                /* Left part */
                VidBufferToScreenBlt(RotLineBuffer + (SCREEN_WIDTH - Index) / 2, 0, SCREEN_HEIGHT-6, Index - 2, 6, SCREEN_WIDTH);
            }
            Index += 32;
        }

        InbvReleaseLock();
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

CODE_SEG("INIT")
VOID
NTAPI
InbvRotBarInit(VOID)
{
    PltRotBarStatus = RBS_FADEIN;
    /* Perform other initialization if needed */
}
#endif // INBV_ROTBAR_IMPLEMENTED

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode)
{
    PVOID BootCopy = NULL, BootProgress = NULL, BootLogo = NULL, Header = NULL, Footer = NULL;

#ifdef INBV_ROTBAR_IMPLEMENTED
    UCHAR Buffer[RTL_NUMBER_OF(RotBarBuffer)];
    PVOID Bar = NULL, LineBmp = NULL;
    ROT_BAR_TYPE TempRotBarSelection = RB_UNSPECIFIED;
    NTSTATUS Status;
    HANDLE ThreadHandle = NULL;
#endif

#ifdef REACTOS_SKUS
    PVOID Text = NULL;
#endif

#ifdef INBV_ROTBAR_IMPLEMENTED
    /* Check if the animation thread has already been created */
    if (RotBarThreadActive)
    {
        /* Yes, just reset the progress bar but keep the thread alive */
        InbvAcquireLock();
        RotBarSelection = RB_UNSPECIFIED;
        InbvReleaseLock();
    }
#endif

    ShowProgressBar = FALSE;

    /* Check if this is text mode */
    if (TextMode)
    {
        /*
         * Make the kernel resource section temporarily writable,
         * as we are going to change the bitmaps' palette in place.
         */
        InbvMakeKernelResourceSectionWritable();

        /* Check the type of the OS: workstation or server */
        if (SharedUserData->NtProductType == NtProductWinNt)
        {
            /* Workstation; set colors */
            InbvSetTextColor(BV_COLOR_WHITE);
            InbvSolidColorFill(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_DARK_GRAY);
            InbvSolidColorFill(0, VID_FOOTER_BG_TOP, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_RED);

            /* Get resources */
            Header = InbvGetResourceAddress(IDB_WKSTA_HEADER);
            Footer = InbvGetResourceAddress(IDB_WKSTA_FOOTER);
        }
        else
        {
            /* Server; set colors */
            InbvSetTextColor(BV_COLOR_LIGHT_CYAN);
            InbvSolidColorFill(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_CYAN);
            InbvSolidColorFill(0, VID_FOOTER_BG_TOP, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_RED);

            /* Get resources */
            Header = InbvGetResourceAddress(IDB_SERVER_HEADER);
            Footer = InbvGetResourceAddress(IDB_SERVER_FOOTER);
        }

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
                          0, 0, 0, 59);
            BitBltAligned(Header,
                          FALSE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_TOP,
                          0, 0, 0, 0);
        }

        /* Restore the kernel resource section protection to be read-only */
        InbvMakeKernelResourceSectionReadOnly();
    }
    else
    {
#ifdef REACTOS_FANCY_BOOT
        /* Decide whether this is a good time to change our logo ;^) */
        BOOLEAN IsXmas = IsXmasTime();
#endif

        /* Is the boot driver installed? */
        if (!InbvBootDriverInstalled) return;

        /*
         * Make the kernel resource section temporarily writable,
         * as we are going to change the bitmaps' palette in place.
         */
        InbvMakeKernelResourceSectionWritable();

        /* Load boot screen logo */
        BootLogo = InbvGetResourceAddress(
            SELECT_LOGO_ID(IDB_LOGO_DEFAULT, IsXmas, IDB_LOGO_XMAS));

#ifdef REACTOS_SKUS
        Text = NULL;
        if (SharedUserData->NtProductType == NtProductWinNt)
        {
#ifdef INBV_ROTBAR_IMPLEMENTED
            /* Workstation product, use appropriate status bar color */
            Bar = InbvGetResourceAddress(IDB_BAR_WKSTA);
#endif
        }
        else
        {
            /* Display correct branding based on server suite */
            if (ExVerifySuite(StorageServer))
            {
                /* Storage Server Edition */
                Text = InbvGetResourceAddress(IDB_STORAGE_SERVER);
            }
            else if (ExVerifySuite(ComputeServer))
            {
                /* Compute Cluster Edition */
                Text = InbvGetResourceAddress(IDB_CLUSTER_SERVER);
            }
            else
            {
                /* Normal edition */
                Text = InbvGetResourceAddress(
                    SELECT_LOGO_ID(IDB_SERVER_LOGO, IsXmas, IDB_LOGO_XMAS));
            }

#ifdef INBV_ROTBAR_IMPLEMENTED
            /* Server product, use appropriate status bar color */
            Bar = InbvGetResourceAddress(IDB_BAR_DEFAULT);
#endif
        }
#else // REACTOS_SKUS
#ifdef INBV_ROTBAR_IMPLEMENTED
        /* Use default status bar */
        Bar = InbvGetResourceAddress(IDB_BAR_WKSTA);
#endif
#endif // REACTOS_SKUS

        /* Make sure we have a logo */
        if (BootLogo)
        {
            /* Save the main image palette for implementing the fade-in effect */
            BootLogoCachePalette(BootLogo);

            /* Draw the logo at the center of the screen */
            BitBltAligned(BootLogo,
                          TRUE,
                          AL_HORIZONTAL_CENTER,
                          AL_VERTICAL_CENTER,
                          0, 0, 0, 34);

#ifdef INBV_ROTBAR_IMPLEMENTED
            /* Choose progress bar */
            TempRotBarSelection = ROT_BAR_DEFAULT_MODE;
#endif

            /* Set progress bar coordinates and display it */
            InbvSetProgressBarCoordinates(VID_PROGRESS_BAR_LEFT,
                                          VID_PROGRESS_BAR_TOP);

#ifdef REACTOS_SKUS
            /* Check for non-workstation products */
            if (SharedUserData->NtProductType != NtProductWinNt)
            {
                /* Overwrite part of the logo for a server product */
                InbvScreenToBufferBlt(Buffer, VID_SKU_SAVE_AREA_LEFT,
                                      VID_SKU_SAVE_AREA_TOP, 7, 7, 8);
                InbvSolidColorFill(VID_SKU_AREA_LEFT, VID_SKU_AREA_TOP,
                                   VID_SKU_AREA_RIGHT, VID_SKU_AREA_BOTTOM, BV_COLOR_BLACK);
                InbvBufferToScreenBlt(Buffer, VID_SKU_SAVE_AREA_LEFT,
                                      VID_SKU_SAVE_AREA_TOP, 7, 7, 8);

                /* In setup mode, you haven't selected a SKU yet */
                if (ExpInTextModeSetup) Text = NULL;
            }
#endif // REACTOS_SKUS
        }

        /* Load and draw progress bar bitmap */
        BootProgress = InbvGetResourceAddress(IDB_PROGRESS_BAR);
        BitBltAligned(BootProgress,
                      TRUE,
                      AL_HORIZONTAL_CENTER,
                      AL_VERTICAL_CENTER,
                      0, 118, 0, 0);

        /* Load and draw copyright text bitmap */
        BootCopy = InbvGetResourceAddress(IDB_COPYRIGHT);
        BitBltAligned(BootCopy,
                      TRUE,
                      AL_HORIZONTAL_LEFT,
                      AL_VERTICAL_BOTTOM,
                      22, 0, 0, 20);

#ifdef REACTOS_SKUS
        /* Draw the SKU text if it exits */
        if (Text)
            BitBltPalette(Text, TRUE, VID_SKU_TEXT_LEFT, VID_SKU_TEXT_TOP);
#endif

#ifdef INBV_ROTBAR_IMPLEMENTED
        if ((TempRotBarSelection == RB_SQUARE_CELLS) && Bar)
        {
            /* Save previous screen pixels to buffer */
            InbvScreenToBufferBlt(Buffer, 0, 0, 22, 9, 24);
            /* Draw the progress bar bit */
            BitBltPalette(Bar, TRUE, 0, 0);
            /* Store it in global buffer */
            InbvScreenToBufferBlt(RotBarBuffer, 0, 0, 22, 9, 24);
            /* Restore screen pixels */
            InbvBufferToScreenBlt(Buffer, 0, 0, 22, 9, 24);
        }

        /*
         * Add a rotating bottom horizontal bar when using a progress bar,
         * to show that ReactOS can be still alive when the bar does not
         * appear to progress.
         */
        if (TempRotBarSelection == RB_PROGRESS_BAR)
        {
            LineBmp = InbvGetResourceAddress(IDB_ROTATING_LINE);
            if (LineBmp)
            {
                /* Draw the line and store it in global buffer */
                BitBltPalette(LineBmp, TRUE, 0, SCREEN_HEIGHT-6);
                InbvScreenToBufferBlt(RotLineBuffer, 0, SCREEN_HEIGHT-6, SCREEN_WIDTH, 6, SCREEN_WIDTH);
            }
        }
        else
        {
            /* Hide the simple progress bar if not used */
            ShowProgressBar = FALSE;
        }
#endif // INBV_ROTBAR_IMPLEMENTED

        /* Restore the kernel resource section protection to be read-only */
        InbvMakeKernelResourceSectionReadOnly();

        /* Display the boot logo and fade it in */
        BootLogoFadeIn();

#ifdef INBV_ROTBAR_IMPLEMENTED
        if (!RotBarThreadActive && TempRotBarSelection != RB_UNSPECIFIED)
        {
            /* Start the animation thread */
            Status = PsCreateSystemThread(&ThreadHandle,
                                          0,
                                          NULL,
                                          NULL,
                                          NULL,
                                          InbvRotationThread,
                                          NULL);
            if (NT_SUCCESS(Status))
            {
                /* The thread has started, close the handle as we don't need it */
                RotBarThreadActive = TRUE;
                ObCloseHandle(ThreadHandle, KernelMode);
            }
        }
#endif // INBV_ROTBAR_IMPLEMENTED

        /* Set filter which will draw text display if needed */
        InbvInstallDisplayStringFilter(DisplayFilter);
    }

#ifdef INBV_ROTBAR_IMPLEMENTED
    /* Do we have the animation thread? */
    if (RotBarThreadActive)
    {
        /* We do, initialize the progress bar */
        InbvAcquireLock();
        RotBarSelection = TempRotBarSelection;
        InbvRotBarInit();
        InbvReleaseLock();
    }
#endif
}

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID)
{
    /* Acquire lock and check the display state */
    InbvAcquireLock();
    if (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
    {
        /* Clear the screen */
        VidSolidColorFill(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BV_COLOR_BLACK);
    }

    /* Reset progress bar and lock */
#ifdef INBV_ROTBAR_IMPLEMENTED
    PltRotBarStatus = RBS_STOP_ANIMATE;
    RotBarThreadActive = FALSE;
#endif
    InbvReleaseLock();
}

VOID
NTAPI
DisplayShutdownBitmap(VOID)
{
    PUCHAR Logo1, Logo2;
#ifdef REACTOS_FANCY_BOOT
    /* Decide whether this is a good time to change our logo ;^) */
    BOOLEAN IsXmas = IsXmasTime();
#endif

#if 0
    /* Is the boot driver installed? */
    if (!InbvBootDriverInstalled)
        return;
#endif

    /* Yes we do, cleanup for shutdown screen */
    // InbvResetDisplay();
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
    InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    /* Display shutdown logo and message */
    Logo1 = InbvGetResourceAddress(IDB_SHUTDOWN_MSG);
    Logo2 = InbvGetResourceAddress(
        SELECT_LOGO_ID(IDB_LOGO_DEFAULT, IsXmas, IDB_LOGO_XMAS));

    if (Logo1 && Logo2)
    {
        InbvBitBlt(Logo1, VID_SHUTDOWN_MSG_LEFT, VID_SHUTDOWN_MSG_TOP);
#ifndef REACTOS_FANCY_BOOT
        InbvBitBlt(Logo2, VID_SHUTDOWN_LOGO_LEFT, VID_SHUTDOWN_LOGO_TOP);
#else
        /* Draw the logo at the center of the screen */
        BitBltAligned(Logo2,
                      FALSE,
                      AL_HORIZONTAL_CENTER,
                      AL_VERTICAL_BOTTOM,
                      0, 0, 0, SCREEN_HEIGHT - VID_SHUTDOWN_MSG_TOP + 16);

        /* We've got a logo shown, change the scroll region to get
         * the rest of the text down below the shutdown message */
        InbvSetScrollRegion(0,
                            VID_SHUTDOWN_MSG_TOP + ((PBITMAPINFOHEADER)Logo1)->biHeight + 32,
                            SCREEN_WIDTH - 1,
                            SCREEN_HEIGHT - 1);
#endif
    }

#ifdef REACTOS_FANCY_BOOT
    InbvDisplayString("\r\"");
    InbvDisplayString(GetFamousQuote());
    InbvDisplayString("\"");
#endif
}

VOID
NTAPI
DisplayShutdownText(VOID)
{
    ULONG i;

    for (i = 0; i < 25; ++i) InbvDisplayString("\r\n");
    InbvDisplayString("                       ");
    InbvDisplayString("The system may be powered off now.\r\n");

#ifdef REACTOS_FANCY_BOOT
    for (i = 0; i < 3; ++i) InbvDisplayString("\r\n");
    InbvDisplayString("\r\"");
    InbvDisplayString(GetFamousQuote());
    InbvDisplayString("\"");
#endif
}
