/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation header
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

#pragma once

//
// Boot Splash-Screen Functions
//

CODE_SEG("INIT")
BOOLEAN
NTAPI
BootAnimInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count);

VOID
NTAPI
BootAnimTickProgressBar(
    _In_ ULONG SubPercentTimes100);

CODE_SEG("INIT")
VOID
NTAPI
InbvRotBarInit(VOID);

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(
    _In_ BOOLEAN TextMode);

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID);

VOID
NTAPI
DisplayShutdownBitmap(VOID);

VOID
NTAPI
DisplayShutdownText(VOID);
