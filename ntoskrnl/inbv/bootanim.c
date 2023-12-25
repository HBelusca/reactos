/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "inbv/logo.h"

/* See also mm/ARM3/miarm.h */
#define MM_READONLY     1   // PAGE_READONLY
#define MM_READWRITE    4   // PAGE_WRITECOPY

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

// extern ULONG ProgressBarLeft, ProgressBarTop;
// extern BOOLEAN ShowProgressBar;


/* FUNCTIONS *****************************************************************/

#include "btanihlp.c"

/**
 * The functions the different boot themes must define and export are:
 *
 * CODE_SEG("INIT")
 * BOOLEAN
 * NTAPI
 * BootAnimInitialize(
 *     _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
 *     _In_ ULONG Count);
 *
 * VOID
 * NTAPI
 * BootAnimTickProgressBar(
 *     _In_ ULONG SubPercentTimes100);
 *
 * CODE_SEG("INIT")
 * VOID
 * NTAPI
 * DisplayBootBitmap(
 *     _In_ BOOLEAN TextMode);
 *
 * CODE_SEG("INIT")
 * VOID
 * NTAPI
 * FinalizeBootLogo(VOID);
 *
 * VOID
 * NTAPI
 * DisplayShutdownBitmap(VOID);
 *
 * VOID
 * NTAPI
 * DisplayShutdownText(VOID);
 *
 **/

#include "btani2k3.c"


/* FUNCTIONS *****************************************************************/

