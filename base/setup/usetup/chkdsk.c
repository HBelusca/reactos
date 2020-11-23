/*
 *  ReactOS kernel
 *  Copyright (C) 2006 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS text-mode setup
 * FILE:            base/setup/usetup/chkdsk.c
 * PURPOSE:         Filesystem chkdsk support functions
 * PROGRAMMER:      Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include "usetup.h"

#define NDEBUG
#include <debug.h>

static PPROGRESSBAR ChkdskProgressBar = NULL;

/* FUNCTIONS ****************************************************************/

static
BOOLEAN
NTAPI
ChkdskCallback(
    IN CALLBACKCOMMAND Command,
    IN ULONG Modifier,
    IN PVOID Argument)
{
    switch (Command)
    {
        default:
            DPRINT("Unknown callback %lu\n", (ULONG)Command);
            break;
    }

    return TRUE;
}

// VOID
// PrepareCheck(
    // IN OUT PCHECK_PARTITION_INFO PartInfo)
// {
// }

VOID
StartCheck(
    IN OUT PCHECK_PARTITION_INFO PartInfo)
{
    // TODO: Think about which values could be defaulted...
    // PartInfo->FileSystemName = PartInfo->PartEntry->FileSystem;
    PartInfo->FixErrors = TRUE;
    PartInfo->Verbose = FALSE;
    PartInfo->CheckOnlyIfDirty = TRUE;
    PartInfo->ScanDrive = FALSE;
    PartInfo->Callback = ChkdskCallback;

    ChkdskProgressBar = CreateProgressBar(6,
                                          yScreen - 14,
                                          xScreen - 7,
                                          yScreen - 10,
                                          10,
                                          24,
                                          TRUE,
                                          MUIGetString(STRING_CHECKINGDISK));

    ProgressSetStepCount(ChkdskProgressBar, 100);
}

VOID
EndCheck(
    IN NTSTATUS Status)
{
    DestroyProgressBar(ChkdskProgressBar);
    ChkdskProgressBar = NULL;

    DPRINT("ChkdskPartition() finished with status 0x%08lx\n", Status);
}

/* EOF */
