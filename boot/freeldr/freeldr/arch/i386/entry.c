/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
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
 * PROJECT:     FreeLoader - StartUp Module
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Entry point code for the 32/64-bit portion of the StartUp Module.
 * COPYRIGHT:   Copyright 2019 Hermes Belusca-Maito
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

/* GLOBALS ********************************************************************/

/* Copy of the boot context */
PBOOT_CONTEXT PcBootContext;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
NtProcessStartup(
    IN PBOOT_CONTEXT BootContext)
{
    /* Check the validity of the BootContext structure */
    if (!IS_BOOT_CONTEXT_VALID(BootContext))
        return; // Not valid, bail out quickly.
    ASSERT(BootContext->ImageBase == &__ImageBase);

    PcBootContext = BootContext;

//
// TODO: In this platform-specific entry point, we need to cache or convert
// data passed by the BootContext into something that becomes less platform-
// specific before calling the main function of FreeLoader.
//
#if 0
    MachInit(BootContext->CommandLine);

    /* Check if the CPU is new enough */
    FrLdrCheckCpuCompatibility(); // FIXME: Should be done inside MachInit!

    /* Initialize memory manager */
    if (!MmInitializeMemoryManager())
    {
        UiMessageBoxCritical("Unable to initialize memory manager.");
        goto Quit;
    }
#endif

    /* Call FreeLoader's main function */
    FrLdrMain(BootContext->CommandLine);

    /* If we reach this point, something went wrong before, therefore reboot */
    Reboot();
}
