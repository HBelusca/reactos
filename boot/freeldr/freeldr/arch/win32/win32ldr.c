/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     Dual-licensed:
 *              GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Entry point
 * COPYRIGHT:   Copyright 2024-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>
#include <wincon.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* GLOBALS ********************************************************************/

/* FUNCTIONS ******************************************************************/

int main(void) // (int argc, char** argv)
{
    static const PCSTR Banner = "Starting Win32 FreeLoader...\n";
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),
                  Banner, (DWORD)strlen(Banner),
                  NULL, NULL);

    // GlobalImageHandle = ImageHandle;
    // GlobalSystemTable = SystemTable;

    /* Invoke the main function */
    BootMain(GetCommandLineA());
    UNREACHABLE;
    return 0;
}

VOID __cdecl Reboot(VOID)
{
    WARN("Stopping FreeLoader\n");
    ExitProcess(42);
    for (;;)
    {
        YieldProcessor();
    }
}

/* EOF */
